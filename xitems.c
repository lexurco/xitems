#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>
#include <X11/Xatom.h>

#define PROGNAME "xitems"
#define MAXKS 32

/* usage -- print usage information and die. */
static void
usage(void)
{
	printf(
"usage: " PROGNAME " [-font font] [-bg colour] [-fg colour]\n"
"    [-sbg colour] [-sfg colour] [-bw width]\n"
"    [-hp padding] [-vp padding] [-x x] [-y y]\n");
	exit(1);
}

/* die -- print formatted string, and exit with the status eval. */
static void
die(int eval, const char *fmt, ...)
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

/* warn -- print formatted string to stderr. */
static void
warn(const char *fmt, ...)
{
	fputs(PROGNAME ": ", stderr);
	if (fmt) {
		va_list argp;
		va_start(argp, fmt);
		vfprintf(stderr, fmt, argp);
		va_end(argp);
	}
	fputc('\n', stderr);
}

enum direction {
	DIR_UP,
	DIR_DOWN
};

/* doubly-linked cyclic list */
struct item {
	struct item *prev;
	struct item *next;
	char *s;
	KeySym ks[MAXKS]; /* array of associated keysyms */
	size_t len; /* length of string */
	size_t nks; /* number of associated keysyms */
	bool dirty; /* should be redrawn */
};

static struct item *first = NULL; /* first member of the list */
static struct item *selected = NULL;

/* command-line options and X resources */
static char *o_font = NULL;
static char *o_bg = NULL, *o_fg = NULL, *o_sbg = NULL, *o_sfg = NULL;
static int o_x = -1, o_y = -1;
static int o_bw = -1;
static int o_hp = -1, o_vp = -1;

/* X globals */
static Display *dpy = NULL;
static int screen;
static Window win;
static GC gc_sel, gc_norm;
static Pixmap pm_sel, pm_norm; /* background pixmaps */
static XFontStruct *font;
static int height, width; /* height and width of one item */

/* selpos -- mark the item at position y (from top) as selected. */
static void
selpos(int y)
{
	struct item *unselected = selected;
	int top = 1;

	selected = first;

	if (y <= 0)
		goto end;

	for (selected = first; selected != first->prev;
	    selected = selected->next) {
		if (y >= top && y < top+height)
			break;
		top += height;
	}

end:
	if (unselected != selected)
		unselected->dirty = selected->dirty = true;
}

/* redraw -- redraw the entire window */
static void
redraw(void)
{
	struct item *it = first;
	int y = 0;

	do {
		if (it->dirty) {
			GC gc;
			Pixmap pm;

			if (it == selected) {
				gc = gc_sel;
				pm = pm_sel;
			} else {
				gc = gc_norm;
				pm = pm_norm;
			}

			XCopyArea(dpy, pm, win, gc, 0, 0, width, height, 0, y);
			XDrawString(dpy, win, gc, o_hp, y + o_vp+font->ascent,
			    it->s, it->len);

			it->dirty = false;
		}

		y += height;
		it = it->next;
	} while (it != first);
}

/* succeed -- exit successfully. If print is true, also print selected item. */
static void
succeed(bool print)
{
	if (print && selected)
		puts(selected->s);
	exit(0);
}

/* scroll -- select the previous, or next item, depending on dir. */
static void
scroll(enum direction dir)
{
	if (selected) {
		selected->dirty = true;
		selected = (dir == DIR_UP) ? selected->prev : selected->next;
	} else
		selected = (dir == DIR_UP) ? first->prev : first;
	selected->dirty = true;
}

/*
 * keyselectd -- compare ks with keysyms stored in the items list, and mark
 * the first match as selected. Return true on match, and false otherwise.
 */
static bool
keyselect(KeySym ks)
{
	struct item *it = first;
	KeySym k, dummy;

	XConvertCase(ks, &k, &dummy);

	do {
		size_t i;
		for (i = 0; i < it->nks; ++i)
			if (it->ks[i] == k) {
				selected->dirty = true;
				selected = it;
				selected->dirty = true;
				return true;
			}
		it = it->next;
	} while (it != first);

	return false;
}

/* proc -- body of the main event-reading loop. */
static void
proc(void)
{
	XKeyEvent ke;
	KeySym ks;
	char *dummy = "";
	XEvent ev;

	static bool inwin = false;

	XNextEvent(dpy, &ev);

	switch (ev.type) {
	case EnterNotify:
		inwin = true;
		/* FALLTHRU */
	case MotionNotify:
		selpos(ev.xbutton.y);
		/* FALLTHRU */
	case Expose:
		redraw();
		break;
	case LeaveNotify:
		inwin = false;
		break;
	case ButtonPress:
		if (ev.xbutton.button == Button4) {
			scroll(DIR_UP);
			redraw();
		} else if (ev.xbutton.button == Button5) {
			scroll(DIR_DOWN);
			redraw();
		} else if (inwin)
			succeed(true);
		else
			succeed(false);
		break;
	case KeyPress:
		ke = ev.xkey;
		XLookupString(&ke, dummy, 0, &ks, NULL);

		if (ke.state & ControlMask) {
			switch (ks) {
			case XK_bracketleft:
			case XK_C:
			case XK_c:
				ks = XK_Escape;
				break;
			case XK_M:
			case XK_m:
			case XK_J:
			case XK_j:
				ks = XK_Return;
				break;
			case XK_N:
			case XK_n:
				ks = XK_j;
				return;
			case XK_P:
			case XK_p:
				ks = XK_k;
				return;
			}
		} else if (keyselect(ks))
			succeed(true);

		switch (ks) {
		case XK_j:
		case XK_J:
		case XK_Down:
			scroll(DIR_DOWN);
			redraw();
			break;
		case XK_k:
		case XK_K:
		case XK_Up:
			scroll(DIR_UP);
			redraw();
			break;
		case XK_Return:
			succeed(true);
			/* NOTREACHED */
		case XK_Escape:
			succeed(false);
			/* NOTREACHED */
		}

		break;
	}
}

/* grabptr -- try to grab pointer for a second */
static void
grabptr(void)
{
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };
	int i;

	for (i = 0; i < 1000; ++i) {
		if (XGrabPointer(dpy, RootWindow(dpy, screen), True,
		    ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None,
		    CurrentTime) == GrabSuccess)
			return;
		nanosleep(&ts, NULL);
	}
	die(1, "couldn't grab pointer");
}

/* grabkb -- try to grab keyboard for a second */
static void
grabkb(void)
{
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };
	int i;

	for (i = 0; i < 1000; ++i) {
		if (XGrabKeyboard(dpy, RootWindow(dpy, screen), True,
		    GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess)
			return;
		nanosleep(&ts, NULL);
	}
	die(1, "couldn't grab keyboard");
}

/* setfocus -- try setting focus to the menu for a second */
static void
setfocus(void)
{
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };
	Window focuswin;
	int i, dummy;

	for (i = 0; i < 1000; ++i) {
		XGetInputFocus(dpy, &focuswin, &dummy);
		if (focuswin == win)
			return;
		XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
		nanosleep(&ts, NULL);
	}
	die(1, "couldn't grab keyboard");
}

/*
 * setupx -- create and map a window for n items; assign values to the X
 * globals.
 */
static void
setupx(int n)
{
	struct item *it;
	XGCValues gcv;
	XColor col, dummy;
	XClassHint ch = {PROGNAME, PROGNAME};
	XSetWindowAttributes swa = {
		.override_redirect = True,
		.save_under = True,
		.event_mask = ExposureMask | StructureNotifyMask |
		    KeyPressMask | ButtonPressMask | ButtonReleaseMask |
		    PointerMotionMask | LeaveWindowMask | EnterWindowMask,
	};

	if (!(font = XLoadQueryFont(dpy, o_font)))
		die(1, "couldn't load font");
	height = font->ascent + font->descent + o_vp;

	it = first;
	do {
		int w;
		if ((w = XTextWidth(font, it->s, it->len) + o_hp*2) > width)
			width = w;
		it = it->next;
	} while (it != first);

	if (o_x + width > DisplayWidth(dpy, screen))
		o_x = DisplayWidth(dpy, screen) - width;
	if (o_y + height*n > DisplayHeight(dpy, screen))
		o_y = DisplayHeight(dpy, screen) - height*n;

	XAllocNamedColor(dpy, DefaultColormap(dpy, screen), o_bg, &col, &dummy);
	swa.background_pixel = col.pixel;
	win = XCreateWindow(dpy, RootWindow(dpy, screen), o_x, o_y,
	    width, n*height, o_bw, CopyFromParent, CopyFromParent,
	    CopyFromParent, CWOverrideRedirect | CWBackPixel | CWEventMask |
		CWSaveUnder, &swa);
	XSetClassHint(dpy, win, &ch);

	/*
	 * Foreground here means the colour with which to draw the background
	 * pixmap, i.e. the actual background colour.
	 */
	gcv.foreground = col.pixel;
	gc_norm = XCreateGC(dpy, win, GCForeground, &gcv);
	XAllocNamedColor(dpy, DefaultColormap(dpy, screen), o_sbg, &col,
	    &dummy);
	gcv.foreground = col.pixel;
	gc_sel = XCreateGC(dpy, win, GCForeground, &gcv);

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
	XAllocNamedColor(dpy, DefaultColormap(dpy, screen), o_fg, &col,
	    &dummy);
	XSetForeground(dpy, gc_norm, col.pixel);
	XAllocNamedColor(dpy, DefaultColormap(dpy, screen), o_sfg, &col,
	    &dummy);
	XSetForeground(dpy, gc_sel, col.pixel);

	grabkb();
	grabptr();

	XMapRaised(dpy, win);

	setfocus();
}

/*
 * Create a new struct item, and insert it a after the it item. The rest of the
 * arguments are values that the new item receives. On success return pointer
 * to the new item. Return NULL otherwise.
 */
static struct item *
insitem(struct item *it, char *s) {
	struct item *new;
	char *p, *end;

	if (!(new = calloc(1, sizeof *new)))
		return NULL;
	if (it) {
		new->prev = it;
		new->next = it->next;
		it->next->prev = new;
		it->next = new;
	} else
		new->prev = new->next = new;
	new->nks = 0;
	new->dirty = true;

	for (p = s; ; p = end) {
		size_t n;
		char c;
		KeySym ks;

		n = strspn(p, " ");
		p = p + n;
		if (!(n = strcspn(p, " \t"))) {
			p += strspn(p, "  \t");
			break;
		}

		end = p + n;
		c = *end;
		*end = '\0';

		if ((ks = XStringToKeysym(p)) == NoSymbol)
			warn("no such keysym: %s\n", p);
		else if (new->nks >= MAXKS)
			warn("too many keysyms (%s)\n", p);
		else {
			KeySym k, dummy;
			XConvertCase(ks, &k, &dummy);
			new->ks[new->nks++] = k;
		}

		*end = c;
	}

	new->len = strlen(p);
	new->s = strdup(p);

	return new;
}

/*
 * mkitems -- create a list of items from stdin, and return pointer to the
 * new list's first element. If np is not NULL, set its value to the number of
 * elements in the list. Return NULL on error.
 */
static struct item *
mkitems(int *np)
{
	struct item *last = NULL;
	size_t n = 0;
	char *line = NULL;
	size_t linesize = 0;
	ssize_t linelen;

	while ((linelen = getline(&line, &linesize, stdin)) != -1) {
		struct item *it;
		if (line[linelen-1] == '\n')
			line[--linelen] = '\0';
		if (!(it = insitem(last, line)))
			die(1, "couldn't insert new item");
		n++;
		last = it;
	}
	free(line);

	if (np)
		*np = n;

	return last ? last->next : NULL;
}

/*
 * {s,i}default -- return the {char *,int} value of the X default, or def
 * otherwise.
 */
static char *
sdefault(char *opt, char *def)
{
	char *val = XGetDefault(dpy, PROGNAME, opt);
	return val ? val : def;
}

static int
idefault(char *opt, int def)
{
	char *val = XGetDefault(dpy, PROGNAME, opt);
	return val ? atoi(val) : def;
}

/* nextarg -- safely skip the current command-line option */
static void
nextarg(int *argcp, char **argvp[])
{
	if (--*argcp <= 0)
		usage();
	++*argvp;
}

/*
 * {s,i}arg -- return the {char *,int} argument of the current command-line
 * option.
 */
static char *
sarg(int *argcp, char **argvp[])
{
	nextarg(argcp, argvp);
	return **argvp;
}

static int
iarg(int *argcp, char **argvp[])
{
	nextarg(argcp, argvp);
	return atoi(**argvp);
}

/*
 * xitems -- pop-up menu for X, constructed from stdin, and printing user choice
 * to stdout.
 */
int
main(int argc, char *argv[])
{
	int n = 0;

	for (--argc, ++argv; argc > 0; --argc, ++argv)
		if (argv[0][0] == '-')
			switch (argv[0][1]) {
			case 'b':
				switch (argv[0][2]) {
				case 'g': /* -bg */
					o_bg = sarg(&argc, &argv);
					break;
				case 'w': /* -bw */
					o_bw = iarg(&argc, &argv);
					break;
				default:
					usage();
					/* NOTREACHED */
				}
				break;
			case 'f':
				switch (argv[0][2]) {
				case 'g': /* -fg */
					o_fg = sarg(&argc, &argv);
					break;
				case 'o': /* -font */
					o_font = sarg(&argc, &argv);
					break;
				default:
					usage();
					/* NOTREACHED */
				}
				break;
			case 'h': /* -hp */
				o_hp = iarg(&argc, &argv);
				break;
			case 's':
				switch (argv[0][2]) {
				case 'b': /* -sbg */
					o_sbg = sarg(&argc, &argv);
					break;
				case 'f': /* -sfg */
					o_sfg = sarg(&argc, &argv);
					break;
				default:
					usage();
					/* NOTREACHED */
				}
				break;
			case 'v': /* -vp */
				o_vp = iarg(&argc, &argv);
				break;
			case 'x': /* -x */
				o_x = iarg(&argc, &argv);
				break;
			case 'y': /* -y */
				o_y = iarg(&argc, &argv);
				break;
			default:
				usage();
				/* NOTREACHED */
			}

	if (!(dpy = XOpenDisplay(NULL)))
		die(1, "couldn't open display");
	screen = DefaultScreen(dpy);

	if (!o_bg)
		o_bg = sdefault("background", "white");
	if (!o_fg)
		o_fg = sdefault("foreground", "black");
	if (!o_font)
		o_font = sdefault("font", "fixed");
	if (!o_sbg)
		o_sbg = sdefault("selectedBackground", "black");
	if (!o_sfg)
		o_sfg = sdefault("selectedForeground", "white");
	if (o_bw == -1)
		o_bw = idefault("borderWidth", 1);
	if (o_hp == -1)
		o_hp = idefault("horizontalPadding", 2);
	if (o_vp == -1)
		o_vp = idefault("verticalPadding", 1);

	if (o_x == -1 || o_y == -1) {
		Window w;
		int i;
		unsigned int ui;
		int *xp, *yp;

		xp = (o_x == -1) ? &o_x : &i;
		yp = (o_y == -1) ? &o_y : &i;

		XQueryPointer(dpy, RootWindow(dpy, screen), &w, &w, xp, yp, &i,
		    &i, &ui);
	}

	if (!(first = selected = mkitems(&n))) {
		if (n)
			die(1, "coulnd't create items list");
		else
			exit(0);
	}
	setupx(n);

	for (;;)
		proc();
	/* NOTREACHED */
}
