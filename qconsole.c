/* vim:ts=8
 * $Id: qconsole.c,v 1.1 2008/10/02 05:25:26 jcs Exp $
 *
 * qconsole
 *
 * Copyright (c) 2005, 2008 joshua stein <jcs@jcs.org>
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
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

/* so our window manager knows us */
char* win_name = "qconsole";

struct xinfo {
	Display* dpy;
	int dpy_width, dpy_height;
	int screen;
	Window win;
	int width;
	int height;
} x;

pid_t xterm_pid = 0;
int shutting_down = 0;

extern char *__progname;

void	xterm_handler(int sig);
void	exit_handler(int sig);
void	draw_window(void);
void	scroll_down(void);
void	usage(void);

int
main(int argc, char* argv[])
{
	XEvent event;

	signal(SIGCHLD, xterm_handler);

	signal(SIGINT, exit_handler);
	signal(SIGTERM, exit_handler);

	draw_window();

	/* all set, fire up xterm */
	xterm_handler(0);

        for (;;) {
		bzero(&event, sizeof(XEvent));
		XNextEvent (x.dpy, &event);

		if (event.type == ReparentNotify) {
			XReparentEvent *e = (XReparentEvent *) &event;

			printf("got notify signal, win is %d\n",
				e->window);

			XMoveWindow(x.dpy, e->window, 0, 0);
			XResizeWindow(x.dpy, e->window, x.width, x.height);
			XSetInputFocus(x.dpy, e->window, RevertToParent,
				CurrentTime);

			scroll_down();
		} else
			printf("unknown event type 0x%x for win %d\n",
				event.type, event.xany.window);
	}

	exit(1);
}

void
draw_window(void)
{
	int rc;
	char *display = NULL;
	XSetWindowAttributes attributes;
	XTextProperty win_name_prop;

	bzero(&x, sizeof(struct xinfo));

	if (!(x.dpy = XOpenDisplay(display)))
		errx(1, "Unable to open display %s", XDisplayName(display));
		/* NOTREACHED */

	x.screen = DefaultScreen(x.dpy);

	x.width = x.dpy_width = DisplayWidth(x.dpy, x.screen);
	x.dpy_height = DisplayHeight(x.dpy, x.screen);

	x.height = x.dpy_height / 5;

	x.win = XCreateSimpleWindow(x.dpy, RootWindow(x.dpy, x.screen),
			0, -(x.height),
			x.width, x.height,
			0,
			BlackPixel(x.dpy, x.screen),
			BlackPixel(x.dpy, x.screen));

	if (!(rc = XStringListToTextProperty(&win_name, 1, &win_name_prop)))
		errx(1, "XStringListToTextProperty");
		/* NOTREACHED */

	XSetWMName(x.dpy, x.win, &win_name_prop);

	/* remove all window manager decorations and force our position/size */
	/* XXX: apparently this is not very nice */
	attributes.override_redirect = True;
	XChangeWindowAttributes(x.dpy, x.win, CWOverrideRedirect, &attributes);

	XMapWindow(x.dpy, x.win);

	XFlush(x.dpy);
	XSync(x.dpy, False);

	/* we want to know when we're exposed */
	XSelectInput(x.dpy, x.win, SubstructureNotifyMask);
}

void
exit_handler(int sig)
{
	shutting_down = 1;

	if (xterm_pid)
		kill(xterm_pid, SIGKILL);

	XCloseDisplay(x.dpy);

	exit(0);
	/* NOTREACHED */
}
void
xterm_handler(int sig)
{
	char *pargv[8] = {
		"xterm", "-bg", "black", "-fg", "gray", "-into", "",
		NULL
	};
	pid_t pid;

	if (shutting_down)
		return;

	printf("in xterm_handler\n");

	if (xterm_pid) {
		int s;

		XUnmapSubwindows(x.dpy, x.win);

		printf("waiting\n");
		wait(&s);
		printf("done\n");
		xterm_pid = 0;
	}

	if (!xterm_pid) {
		switch (pid = fork()) {
		case -1:
			errx(1, "Unable to fork");
		case 0:
			asprintf(&pargv[6], "%d", x.win);
			execvp("xterm", pargv);
			exit(0);

		default:
			xterm_pid = pid;
		}
	}
}

void
scroll_down(void)
{
	int cur_x, cur_y, j;
	unsigned width, height, bw, depth;
	Window root;

	XGetGeometry(x.dpy, x.win, &root,
		&cur_x, &cur_y, &width, &height, &bw, &depth);

	printf("currently at %d/%d\n", cur_x, cur_y);

	for (j = cur_y; j <= 0;) {
		printf("moving to %d/%d\n", 0, j);
		XMoveWindow(x.dpy, x.win, 0, j);
		XFlush(x.dpy);
		XSync(x.dpy, False);
		j -= (cur_y / 20) - 1;
	}

	XMoveWindow(x.dpy, x.win, 0, 0);
}

void
usage(void)
{
	fprintf(stderr, "usage: %s %s\n", __progname,
		"[-display host:dpy] [-width <pixels>] [time time2 ...]");
	exit(1);
}
