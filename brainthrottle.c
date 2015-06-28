/* brainthrottle.c **
 * 
 * Attempts to detect content skimming through excessive scrolling and dims 
 * the screen to slow the user down.
 *
 *
 * Design  **
 *
 * main() installs an EventTap. The EventTap callback (handleScroll) tracks 
 * the scroll displacement (recentScrollTotal). When scrolling exceeds 
 * scrollThreshold, each time the EventTap fires a timer is created (or 
 * restarted) and the screen dims. When the timer expires, the screen 
 * brightness is restored to its original value (prevBrightness).
 *
 *
 * Motvation **
 *
 * Sometimes we skim when we shouldn't. This is an experiment in fixing that.
 * 
 *
 * Compile and Run **
 *
 * Install OSX developer tools, then:
 *
 * $ clang -o brainthrottle brainthrottle.c -framework IOKit -framework ApplicationServices -Wl,-U,_CGDisplayModeGetPixelWidth -Wl,-U,_CGDisplayModeGetPixelHeight -mmacosx-version-min=10.6
 * $ ./brainthrottle
 *
 * Use Ctrl-C to exit.
 *
 *
 * Known issues **
 *
 * - Only works with the main display
 * - Main display brightness must be controllable via OSX
 * - OSX only
 * - Skim detection message should have a timestamp for analysis purposes
 * - Parameters should be command line arguments
 *
 *
 * Author **
 *
 * Jonathan Foote
 * jmfoote@loyola.edu
 * 28 June 2015
 *
 * Thanks to Matt Danger for blogging about OSX brightness control back in 
 * in 2008: http://mattdanger.net/
 */

#include <stdio.h>
#include <unistd.h>
#include <IOKit/graphics/IOGraphicsLib.h>
#include <ApplicationServices/ApplicationServices.h>
#include <sys/time.h>
#include <errno.h>
#include <signal.h>


/*
 * Constants: Use these to tune program behavior
 */
const int penaltyTimeoutSec = 5;      // Seconds penalty (screen dim) lasts
const int restoreTimeoutSec = 10;     // Seconds before resetting scroll count
const int64_t scrollThreshold = 1000; // Higher=more scrolling before timeout


/*
 * Global variables
 */
CFMachPortRef scrollEventTap;         // Pointer to EventTap function
int64_t recentScrollTotal = 0;        // Compared against scrollThreshold
time_t lastScrollTime = 0;            // Last time scrolling was detected
float prevBrightness = -1;            // Brightness before screen dim
bool penalized = false;               // True if screen is penalized (dimmed)


/*
 * Brightness constants and external function declarations
 */
const CFStringRef kDisplayBrightness = CFSTR(kIODisplayBrightnessKey);
const int kMaxDisplays = 16;
extern size_t CGDisplayModeGetPixelWidth(CGDisplayModeRef mode)
  __attribute__((weak_import));
extern size_t CGDisplayModeGetPixelHeight(CGDisplayModeRef mode)
  __attribute__((weak_import));

/*
 * Gets the main display service. Called by the get/setBrightness functions.
 */
io_service_t getDisplayService() {
    CGDisplayErr err;
    CGDirectDisplayID display[kMaxDisplays];
    CGDisplayCount numDisplays;

    err = CGGetOnlineDisplayList(kMaxDisplays, display, &numDisplays);
    if (err != CGDisplayNoErr) {
        fprintf(stderr, "cannot get list of displays (error %d)\n", err);
        return (io_service_t)0;
    }

    CGDirectDisplayID dspy = display[0];
    return CGDisplayIOServicePort(dspy);
}


/*
 * Gets the brightness level of the main display
 */
float getBrightness() {
    float brightness = -1;
    CGDisplayErr err;
    io_service_t service = getDisplayService();
    if (0 == service) {
        return -1;
    }

    err = IODisplayGetFloatParameter(service, kNilOptions, kDisplayBrightness, &brightness);
    if (err != kIOReturnSuccess) {
        fprintf(stderr, "failed to get brightness of display (error %d)\n", err);
    } else {
        printf("display brightness %f\n", brightness);
    }
    return brightness;
}


/* 
 * Sets the brightness level of the main display.
 */
void setBrightness(float brightness) { 
    CGDisplayErr err;
    io_service_t service = getDisplayService();
    if (0 == service) {
        return; 
    }

    err = IODisplaySetFloatParameter(service, kNilOptions, kDisplayBrightness, brightness);
    if (err != kIOReturnSuccess) {
        fprintf(stderr, "Failed to set brightness of display (error %d)\n", err);
    } 
}


/*
 * Called when the EventTap fires. Updates the scrolling global variables and
 * controls screen dimming. 
 */
static CGEventRef handleScroll (
    CGEventTapProxy proxy,
    CGEventType type,
    CGEventRef event,
    void * refcon
) {
    int64_t scrollX, scrollY, scrollDiff;
    int key;



    // If the event tap has timed out, reinstall it
    // Also, if the event isn't a scroll, just return

    if (type == kCGEventTapDisabledByTimeout) {
        CGEventTapEnable(scrollEventTap, true);
        return event;
    } else if (type != kCGEventScrollWheel) {
        return event;
    }


    // Store scroll stats

    scrollX = CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis1);
    scrollY = CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis2);
    scrollDiff = 1 + llabs(scrollX) + llabs(scrollY);


    // If restoreTimeoutSec seconds have elapsed reset the scroll counter

    time_t now = time(NULL);
    if ((now - lastScrollTime) > restoreTimeoutSec) {
        printf("Resetting scroll counter\n");
        recentScrollTotal = scrollDiff;
    } else {
        recentScrollTotal += scrollDiff;
    }
    lastScrollTime = now;


    // If skimming not detected (yet), nothing to do, just return

    if (recentScrollTotal < scrollThreshold) {
        return event;
    }


    // Skimming detected: dim screen
    // If penalty timeout timer hasn't been started yet, store brightness and start timer
    
    struct itimerval timerValue;
    struct itimerval oldTimerValue;
    struct sigaction action;

    if (-1 == getitimer(ITIMER_REAL, &timerValue)) {
        fprintf(stderr, "Error getting timer\n");
        return event;
    }
    float brightness = getBrightness();
    if (0 == timerValue.it_value.tv_sec && 0 == timerValue.it_value.tv_usec) { 
        // Timer not set
        prevBrightness = brightness;
    }


    timerValue.it_value.tv_sec = penaltyTimeoutSec;
    timerValue.it_value.tv_usec = 0;
    if (-1 == setitimer(ITIMER_REAL, &timerValue, NULL)) {
        fprintf(stderr, "Error setting timer\n");
        return event;
    }

    if (!penalized) {
        printf("Skimming detected.\n"); 
        penalized = true;
    }

    // Decrease screen brightness

    float penalty = brightness - (brightness * (float)scrollDiff/100);
    if (penalty < 0.05) {
        penalty = 0.0;
    }
    setBrightness(penalty); 

    return event;
}


/*
 * Screen dim timeout handler. Resets screen brightness and disables timer.
 */
void handleTimeout(int signo)
{

    // Restore brightness if screen has been dimmed

    if (penalized) {
        setBrightness(prevBrightness);
        penalized = false;
    }
    lastScrollTime = 0; 


    // Disable timer

    struct itimerval timerValue;
    timerValue.it_value.tv_sec = 0;
    timerValue.it_value.tv_usec = 0;
    if (-1 == setitimer(ITIMER_REAL, NULL, NULL)) {
         fprintf(stderr, "Error setting timer in signal handler; bailing\n");
         exit(-1);
    }

    if (signo != SIGALRM) {
        printf("Exiting\n");
        exit(0);
    }
}


/*
 * Entry point for the program. Installs the scroll-handler EventTap and runs 
 * event loop
 */
int main (
    int argc,
    char ** argv
) {
    prevBrightness = getBrightness();


    // Add penalty timeout handler

    struct sigaction action;
    action.sa_handler = &handleTimeout;
    action.sa_flags = SA_NODEFER;
    sigaction(SIGALRM, &action, NULL);
    sigaction(SIGINT, &action, NULL);


    // Create scroll event handler
    
    CGEventMask emask;
    CFRunLoopSourceRef runLoopSource;
    emask = CGEventMaskBit(kCGEventKeyDown) |  
        CGEventMaskBit(kCGEventScrollWheel) |
        CGEventMaskBit(kCGEventRightMouseDown) | 
        CGEventMaskBit(kCGEventLeftMouseDown);

    scrollEventTap = CGEventTapCreate (
        kCGSessionEventTap, 
        kCGTailAppendEventTap, 
        kCGEventTapOptionListenOnly, 
        emask,
        &handleScroll,
        NULL 
    );
    runLoopSource = CFMachPortCreateRunLoopSource(
        kCFAllocatorDefault,
        scrollEventTap,
        0
    );
    CFRunLoopAddSource(
        CFRunLoopGetCurrent(),
        runLoopSource,
        kCFRunLoopDefaultMode
    );


    // Run event loop

    CFRunLoopRun();

    return 0;
}
