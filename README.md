# brainthrottle

Attempts to detect when you are skimming text (through excessive scrolling) and progressively dims the screen to slow you down. 

<p align="center"><img src="http://foote.pub/images/brainthrottle-crappy.gif" alt="Crappy gif"><br><i>Crappy cell phone video demo</i></p>

Sometimes we skim when we shouldn't. This is a simple, for-fun experiment in fixing that. 


### How to use

#### Compile 

Install OSX developer tools, then:

```
$ clang -o brainthrottle brainthrottle.c -framework IOKit -framework ApplicationServices -Wl,-U,_CGDisplayModeGetPixelWidth -Wl,-U,_CGDisplayModeGetPixelHeight -mmacosx-version-min=10.6
```

#### Run

When you should be comprehending what you are reading and scrolling is a good proxy for skimming, run brainthrottle. Use Ctrl-C to exit.

```
$ ./brainthrottle
```

You can set how much scrolling triggers a screen dim, how long the screen is dimmed, etc. via constants at the top of `brainthrottle.c`. Command line options are on the `TODO` list.


### Design

`main` installs an EventTap. The EventTap callback (`handleScroll`) tracks the scroll displacement (`recentScrollTotal`). When scrolling exceeds `scrollThreshold`, each time the EventTap fires a timer is created (or restarted) and the screen dims. When the timer expires, the screen brightness is restored to its original value (`prevBrightness`).


### Known issues

- OSX only
- Only works with the main display
- Main display brightness must be controllable via OSX
- Skim detection message should have a timestamp for logging and analysis 
- Tuning parameters should be command line arguments
- Demo is crappy cellphone gif


### Author

```
Jonathan Foote
jmfoote@loyola.edu
28 June 2015
```

Thanks to Matt Danger for blogging about OSX brightness control back in in 2008: http://mattdanger.net/
