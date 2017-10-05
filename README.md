## qconsole

![https://i.imgur.com/IBAAN8j.png](https://i.imgur.com/IBAAN8j.png)

qconsole is a simple application that makes an xterm window behave like the
drop-down console on the game "Quake" (and probably many others).

When qconsole is started, it spawns an xterm with the "`-into`" option to
reparent it under qconsole, and then hides it offscreen.

To show the terminal, press `Control+O` and it will scroll onto the screen.
Press it again and it will scroll back off the screen.
If the xterm is killed, a new one will be spawned.

The xterm is launched with a name of `qconsole`.
To adjust its colors and font, add xterm X resources to your X resources file
(and maybe run xrdb) such as:

		.qconsole*background:           white
		.qconsole*foreground:           black
		.qconsole*internalBorder:       5
		.qconsole*utmpInhibit:          true
		.qconsole*rightScrollBar:       true

To adjust the window height from its default of 1/5 of the screen height,
use the `-h` option.
To adjust the speed that the console scrolls, use the `-s` option (1 to 10,
default of 5).
