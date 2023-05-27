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

#define PROGNAME "xitems"

#define LEN(A) (sizeof(A)/sizeof((A)[0]))

static void
usage(void)
{
	printf("too bad\n");
	exit(1);
}

static void
errx(int eval, const char *fmt, ...)
{
	fputs(PROGNAME ": ", stderr);
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

char *
sdefault(char *opt, char *def)
{
	char *val = XGetDefault(dpy, PROGNAME, opt);
	return val ? val : def;
}

int
idefault(char *opt, int def)
{
	char *val = XGetDefault(dpy, PROGNAME, opt);
	return val ? atoi(val) : def;
}

char *
sarg(int *argcp, char **argvp[])
{
	if (--*argcp <= 0)
		usage();
	++*argvp;
	return **argvp;
}

int
iarg(int *argcp, char **argvp[])
{
	if (--*argcp <= 0)
		usage();
	++*argvp;
	return atoi(**argvp);
}

int
main(int argc, char *argv[])
{
	struct item *first = NULL, *last = NULL;
	XFontStruct *font;
	size_t linesize = 0, nitems = 0;
	ssize_t linelen;
	char *line = NULL;
	int width = 0;
	int height;
	XGCValues values;
	XSetWindowAttributes swa = {
		.override_redirect = True,
		.save_under = True,
		.event_mask = ExposureMask | KeyPressMask | StructureNotifyMask,
	};
	XClassHint ch = {PROGNAME, PROGNAME};
	XColor col1, col2;

	char *fontname, *bg, *fg, *sbg, *sfg;
	int x, y, bw;
	fontname = bg = fg = sbg = sfg = NULL;
	x = y = bw = -1;

	for (--argc, ++argv; argc > 0; --argc, ++argv)
		if (argv[0][0] == '-')
			switch (argv[0][1]) {
			case 'b':
				switch (argv[0][2]) {
				case 'g': /* -bg */
					bg = sarg(&argc, &argv);
					break;
				case 'w': /* -bw */
					bw = iarg(&argc, &argv);
					break;
				default:
					usage();
					/* NOTREACHED */
				}
				break;

			case 'f':
				switch (argv[0][2]) {
				case 'g': /* -fg */
					fg = sarg(&argc, &argv);
					break;
				case 'o': /* -font */
					fontname = sarg(&argc, &argv);
					break;
				default:
					usage();
					/* NOTREACHED */
				}
				break;

			case 's':
				switch (argv[0][2]) {
				case 'b': /* -sbg */
					sbg = sarg(&argc, &argv);
					break;
				case 'f': /* -sfg */
					sfg = sarg(&argc, &argv);
					break;
				default:
					usage();
					/* NOTREACHED */
				}
				break;

			case 'x': /* -x */
				x = iarg(&argc, &argv);
				break;

			case 'y': /* -y */
				y = iarg(&argc, &argv);
				break;

			default:
				usage();
				/* NOTREACHED */
			}

	if (!(dpy = XOpenDisplay(NULL)))
		errx(1, "couldn't open display");
	screen = DefaultScreen(dpy);

	if (!bg)
		bg = sdefault("background", "white");
	if (!fg)
		fg = sdefault("foreground", "black");
	if (!fontname)
		fontname = sdefault("font", "fixed");
	if (!sbg)
		sbg = sdefault("selectedBackground", "black");
	if (!sfg)
		sfg = sdefault("selectedForeground", "white");
	if (bw == -1)
		bw = idefault("borderWidth", 1);

	if (x == -1 || y == -1) {
		Window w;
		int i;
		unsigned int ui;
		int *xp, *yp;

		xp = (x == -1) ? &x : &i;
		yp = (y == -1) ? &y : &i;

		XQueryPointer(dpy, RootWindow(dpy, screen), &w, &w, xp, yp, &i,
		    &i, &ui);
	}

	font = XLoadQueryFont(dpy, fontname);
	height = font->ascent + font->descent + VPAD;

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

	last = first;
	do {
		int w;
		if ((w = XTextWidth(font, last->s, last->len) + HPAD*2) > width)
			width = w;
		last = last->next;
	} while (last != first);

	XAllocNamedColor(dpy, DefaultColormap(dpy, screen), bg, &col1, &col2);
	swa.background_pixel = col1.pixel;
	win = XCreateWindow(dpy, RootWindow(dpy, screen), x, y,
	    width, nitems*height + VPAD, bw, CopyFromParent, CopyFromParent,
	    CopyFromParent, CWOverrideRedirect | CWBackPixel | CWEventMask,
	    &swa);
	XSetClassHint(dpy, win, &ch);

	/*
	 * Foreground here means the colour with which to draw the background
	 * pixmap, i.e. the actual background colour.
	 */
	values.foreground = col1.pixel;
	gc_norm = XCreateGC(dpy, win, GCForeground, &values);
	XAllocNamedColor(dpy, DefaultColormap(dpy, screen), sbg, &col1, &col2);
	values.foreground = col1.pixel;
	gc_sel = XCreateGC(dpy, win, GCForeground, &values);

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
	XAllocNamedColor(dpy, DefaultColormap(dpy, screen), fg, &col1, &col2);
	XSetForeground(dpy, gc_norm, col1.pixel);
	XAllocNamedColor(dpy, DefaultColormap(dpy, screen), sfg, &col1, &col2);
	XSetForeground(dpy, gc_sel, col1.pixel);

	if (XGrabKeyboard(dpy, RootWindow(dpy, screen), True, GrabModeAsync,
	    GrabModeAsync, CurrentTime) != GrabSuccess)
		errx(1, "cannot grab keyboard");

	XMapRaised(dpy, win);

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
				if (selected)
					puts(selected->s);
				exit(0);
				/* NOTREACHED */
			}
			break;
		}
	}
	/* NOTREACHED */
}
