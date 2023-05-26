#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <stdbool.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>
#include <X11/Xatom.h>

#define HPAD 2 /* horizontal padding */
#define VPAD 1 /* vertical padding */

#define LEN(A) (sizeof(A)/sizeof((A)[0]))

static void
errx(int eval, const char *fmt, ...)
{
	fputs("lockscreen: ", stderr);
	if (fmt) {
		va_list argp;
		va_start(argp, fmt);
		vfprintf(stderr, fmt, argp);
		va_end(argp);
	}
	fputc('\n', stderr);
	exit(eval);
}

/* doubly-linked cyclic list */
struct item {
	size_t len; /* length of string */
	char *s;
	KeySym *ks; /* NoSym-terminated array of keysyms */
	struct item *prev;
	struct item *next;
};

static Window win;
static Pixmap pm_sel, pm_norm;
static GC gc_sel, gc_norm;
static Display *dpy;
static struct item *selected = NULL;
static int screen;

struct item *
insitem(struct item *it, char *s, size_t len) {
	struct item *new;
	if (!(new = calloc(1, sizeof *new)))
		return NULL;
	if (it) {
		new->prev = it;
		new->next = it->next;
		it->next->prev = new;
		it->next = new;
	} else
		new->prev = new->next = new;
	new->s = strdup(s);
	new->len = len;
	new->ks = NULL; /* TODO */
	return new;
}

void
redraw(struct item *first, int height, int width, XFontStruct *font)
{
	struct item *it = first;
	int y_text = VPAD + font->ascent;
	int y_pm = 0;

	do {
		GC gc;
		Pixmap pm;

		if (it == selected) {
			gc = gc_sel;
			pm = pm_sel;
		} else {
			gc = gc_norm;
			pm = pm_norm;
		}

		XCopyArea(dpy, pm, win, gc, 0, 0, width, height, 0, y_pm);
		XDrawString(dpy, win, gc, HPAD, y_text, it->s, it->len);

		y_text += height;
		y_pm += height;
		it = it->next;
	} while (it != first);
}

int
main(int argc, char *argv[])
{
	(void)argc, (void)argv;

	struct item *first = NULL, *last = NULL;
	XFontStruct *font;
	size_t linesize = 0, nitems = 0;
	ssize_t linelen;
	char *line = NULL;
	int width = 0; /* XXX */
	int height;
	XGCValues values;

	/* XXX take keysyms into account */
	while ((linelen = getline(&line, &linesize, stdin)) != -1) {
		struct item *it;
		line[--linelen] = '\0'; /* get rid of '\n' */
		if (!(it = insitem(last, line, linelen)))
			exit(1);
		nitems++;
		last = it;
	}
	free(line);

	if (!last)
		exit(1);
	first = last->next;

	if (!(dpy = XOpenDisplay(NULL)))
		errx(1, "couldn't open display");
	screen = DefaultScreen(dpy);
	font = XLoadQueryFont(dpy, "fixed"); /* XXX */
	height = font->ascent + font->descent + VPAD;

	last = first;
	do {
		int w;
		if ((w = XTextWidth(font, last->s, last->len) + HPAD*2) > width)
			width = w;
		last = last->next;
	} while (last != first);

	win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0, width,
	    nitems*height + VPAD, 1, BlackPixel(dpy, screen),
	    WhitePixel(dpy, screen));

	gc_sel = XCreateGC(dpy, win, 0, &values);
	gc_norm = XCreateGC(dpy, win, 0, &values);
	XSetForeground(dpy, gc_sel, BlackPixel(dpy, screen));
	XSetForeground(dpy, gc_norm, WhitePixel(dpy, screen));

	pm_sel = XCreatePixmap(dpy, win, width, height,
	    DefaultDepth(dpy, screen));
	pm_norm = XCreatePixmap(dpy, win, width, height,
	    DefaultDepth(dpy, screen));
	XFillRectangle(dpy, pm_sel, gc_sel, 0, 0, width, height);
	XFillRectangle(dpy, pm_norm, gc_norm, 0, 0, width, height);

	/*
	 * Since the background pixmaps are already created, the GCs can be
	 * reused for text.
	 */
	XSetForeground(dpy, gc_sel, WhitePixel(dpy, screen));
	XSetForeground(dpy, gc_norm, BlackPixel(dpy, screen));

	XMapRaised(dpy, win);
	XSelectInput(dpy, win, ExposureMask | KeyPressMask |
	    StructureNotifyMask);

	for (;;) {
		XKeyEvent ke;
		KeySym ks;
		char *dummy = "";
		XEvent ev;

		XNextEvent(dpy, &ev);

		/* XXX try to avoid full redraws */
		switch (ev.type) {
		case Expose:
			redraw(first, height, width, font);
			break;
		case KeyPress:
			ke = ev.xkey;
			XLookupString(&ke, dummy, 0, &ks, NULL);

			switch (ks) {
			case XK_q:
			case XK_Q:
				exit(0);
				/* NOTREACHED */
			case XK_j:
			case XK_J:
				selected = selected ? selected->next : first;
				redraw(first, height, width, font);
				break;
			case XK_k:
			case XK_K:
				selected = selected ? selected->prev : first->prev;
				redraw(first, height, width, font);
				break;
			case XK_Return:
				puts(selected->s);
				exit(0);
				/* NOTREACHED */
			}
			break;
		}
	}
	/* NOTREACHED */
}
