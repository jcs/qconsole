/*
 * qconsole
 * Copyright (c) 2005, 2008, 2015 joshua stein <jcs@jcs.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <err.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xproto.h>

#define DIR_UP		1
#define DIR_DOWN	-1

#define MAX_SPEED	10
#define DEF_SPEED	7
#define DEF_HEIGHT	157

#define BORDER		4

struct xinfo {
	Display* dpy;
	Window win;
	Window xterm;
	int dpy_width, dpy_height;
	int screen;
	int speed;
	int width, height;
	int cur_direction;
} main_win;

char *win_name = "qconsole";

/* args to pass to xterm; requires a trailing blank -into option */
static char *xterm_args[6] = {
	"xterm",
	"-name", "qconsole",
	"-into", "",
	NULL
};

static int	debug = 0;
static pid_t	xterm_pid = 0;
static int	shutting_down = 0;
static int	respawning = 0;

extern char	*__progname;

void		draw_window(const char *);
void		scroll(int direction, int quick);
void		xterm_spawn(void);
void		child_handler(int sig);
void		exit_handler(int sig);
void		x_error_handler(Display * d, XErrorEvent * e);
void		usage(void);

int
main(int argc, char* argv[])
{
	char *display = NULL, *p;
	int ch;

	memset(&main_win, 0, sizeof(struct xinfo));
	main_win.height = DEF_HEIGHT;
	main_win.speed = DEF_SPEED;
	main_win.cur_direction = DIR_UP;

	while ((ch = getopt(argc, argv, "dh:s:")) != -1)
		switch (ch) {
		case 'd':
			debug = 1;
			break;

		case 'h':
			main_win.height = strtol(optarg, &p, 10);
			if (*p || main_win.height < 1)
				errx(1, "illegal height value -- %s", optarg);

			break;

		case 's':
			main_win.speed = strtol(optarg, &p, 10);
			if (*p || main_win.speed < 1 ||
			    main_win.speed > MAX_SPEED)
				errx(1, "speed must be between 1 and %d",
				    MAX_SPEED);

			break;

		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	/* die gracefully */
	signal(SIGINT, exit_handler);
	signal(SIGTERM, exit_handler);

	/* handle xterm exiting */
	signal(SIGCHLD, child_handler);

	/* fire up initial xterm */
	draw_window(display);
	xterm_spawn();

	/* wait for events */
        for (;;) {
		XEvent event;
		memset(&event, 0, sizeof(XEvent));
		XNextEvent(main_win.dpy, &event);

		switch (event.type) {
		case ReparentNotify: {
			if (debug)
				printf("xterm spawned and reparented to us\n");

			XReparentEvent *e = (XReparentEvent *) &event;
			main_win.xterm = e->window;

			/* move completely off-screen */
			XMoveWindow(main_win.dpy, main_win.xterm,
				-1, -1);
			XResizeWindow(main_win.dpy, main_win.xterm,
				main_win.width,
				main_win.height - BORDER);

			break;
		}

		case UnmapNotify:
			if (debug)
				printf("xterm unmapped, respawning\n");

			xterm_spawn();

			break;

		case KeyRelease:
			scroll(-(main_win.cur_direction), 0);
			break;

		case FocusOut:
			if (main_win.cur_direction == DIR_DOWN)
				XSetInputFocus(main_win.dpy, main_win.xterm,
					RevertToParent, CurrentTime);

			break;
		}
	}

	exit(1);
}

void
draw_window(const char *display)
{
	int rc;
	XSetWindowAttributes attributes;
	XTextProperty win_name_prop;

	if (!(main_win.dpy = XOpenDisplay(display)))
		errx(1, "Unable to open display %s", XDisplayName(display));

	XSetErrorHandler ((XErrorHandler) x_error_handler);

	main_win.screen = DefaultScreen(main_win.dpy);
	main_win.width = main_win.dpy_width = DisplayWidth(main_win.dpy,
		main_win.screen);
	main_win.dpy_height = DisplayHeight(main_win.dpy, main_win.screen);
	main_win.win = XCreateSimpleWindow(main_win.dpy,
		RootWindow(main_win.dpy, main_win.screen),
		0, -(main_win.height),
		main_win.width, main_win.height,
		0,
		BlackPixel(main_win.dpy, main_win.screen),
		BlackPixel(main_win.dpy, main_win.screen));

	if (!(rc = XStringListToTextProperty(&win_name, 1, &win_name_prop)))
		errx(1, "XStringListToTextProperty");

	XSetWMName(main_win.dpy, main_win.win, &win_name_prop);

	/* remove all window manager decorations and force our position/size */
	attributes.override_redirect = True;
	XChangeWindowAttributes(main_win.dpy, main_win.win,
		CWOverrideRedirect, &attributes);

	XMapRaised(main_win.dpy, main_win.win);

	XFlush(main_win.dpy);
	XSync(main_win.dpy, False);

	/* we need to know when the xterm gets reparented to us */
	XSelectInput(main_win.dpy, main_win.win, SubstructureNotifyMask |
		FocusChangeMask);

	/* bind to control+o */
	/* TODO: allow this key to be configurable */
	XGrabKey(main_win.dpy, XKeysymToKeycode(main_win.dpy, XK_o),
		ControlMask, DefaultRootWindow(main_win.dpy), False,
		GrabModeAsync, GrabModeAsync);
}

void
scroll(int direction, int quick)
{
	int cur_x, cur_y, inc, dest;
	unsigned width, height, bw, depth;
	Window root;

	if (direction == DIR_DOWN) {
		XSetInputFocus(main_win.dpy, main_win.xterm, RevertToParent,
			CurrentTime);
		dest = 0;
	} else {
		XSetInputFocus(main_win.dpy, PointerRoot, RevertToParent,
			CurrentTime);
		dest = -(main_win.height);
	}

	XGetGeometry(main_win.dpy, main_win.win, &root,
		&cur_x, &cur_y, &width, &height, &bw, &depth);

	if (debug)
		printf("scrolling from %d to %d%s\n", cur_y, dest,
		    (quick ? " quickly" : ""));

	if (direction == DIR_DOWN)
		XRaiseWindow(main_win.dpy, main_win.win);

	/* smoothly scroll to our destination */
	while (!quick && cur_y != dest) {
		inc = (abs(dest - cur_y) / MAX_SPEED) / (MAX_SPEED + 1 -
			main_win.speed);

		if (inc < 1)
			inc = 1;

		if ((direction == DIR_DOWN && (cur_y + inc >= dest)) ||
		    (direction == DIR_UP && (cur_y - inc < dest)))
			break;

		cur_y -= (inc * direction);

		XMoveWindow(main_win.dpy, main_win.win, 0, cur_y);
		XSync(main_win.dpy, False);
	}

	XMoveWindow(main_win.dpy, main_win.win, 0, dest);
	main_win.cur_direction = direction;

	if (direction == DIR_DOWN)
		XRaiseWindow(main_win.dpy, main_win.win);
	else
		XLowerWindow(main_win.dpy, main_win.win);
}

void
child_handler(int sig)
{
	if (!xterm_pid)
		return;

	if (debug && sig)
		printf("got SIGCHLD, cleaning up after xterm pid %d\n",
		    xterm_pid);

	waitpid(-1, NULL, WNOHANG);
	xterm_pid = 0;
}

void
xterm_spawn(void)
{
	pid_t pid;

	/* this is called in response to SIGCHLD, but signal handlers can't do
	 * any x11 operations */

	if (shutting_down) {
		if (debug)
			printf("shutting down, not respawning\n");

		return;
	}

	if (debug)
		printf("in xterm_spawn\n");

	/* clean up if previous xterm died */
	if (xterm_pid) {
		child_handler(0);

		scroll(DIR_UP, 1);
		XUnmapSubwindows(main_win.dpy, main_win.win);
	}

	if (!xterm_pid && !shutting_down) {
		if (debug)
			printf("forking new xterm into %d\n",
			    (int)main_win.win);

		switch (pid = fork()) {
		case -1:
			errx(1, "unable to fork");

		case 0:
			/* fork xterm and pass our window id to -into opt */
			asprintf(&xterm_args[4], "%d", (int)main_win.win);
			execvp("xterm", xterm_args);
			exit(0);

		default:
			xterm_pid = pid;
			/* we are now able to be killed */
			respawning = 1;
		}
	}
}

void
exit_handler(int sig)
{
	shutting_down = 1;

	if (debug)
		printf("in exit_handler with sig %d, shutting down\n", sig);

	if (xterm_pid)
		kill(xterm_pid, SIGKILL);

	exit(0);
}

void
x_error_handler(Display * d, XErrorEvent * e)
{
	if (e->error_code == BadAccess && e->request_code == X_GrabKey)
		errx(1, "could not bind key.  possibly another application "
			"bound to it?\n");
}

void
usage(void)
{
	fprintf(stderr, "usage: %s %s\n", __progname,
		"[-d] [-h <height>] [-s <speed 1-10>]");
	exit(1);
}
