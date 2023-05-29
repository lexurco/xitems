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
#include <X11/Xft/Xft.h>

#define PROGNAME "xitems"
#define MAXKS 32

/* usage -- print usage information and die. */
static void
usage(void)
{
	printf(
"usage: " PROGNAME " [-font font] [-bg colour] [-fg colour]\n"
"    [-sbg colour] [-sfg colour] [-bc colour] [-bw width]\n"
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
static char *o_bg = NULL, *o_fg = NULL, *o_sbg = NULL, *o_sfg = NULL,
    *o_bc = NULL;
static int o_x = -1, o_y = -1;
static int o_bw = -1;
static int o_hp = -1, o_vp = -1;

/* X globals */
enum {
	PIXEL_BG = 0,
	PIXEL_BC,
};
#define PIXEL_N 2

static Display *dpy = NULL;
static int screen;
static Window win;
static int height, width; /* height and width of one item */
static XftFont *font;
static XftColor c_fg, c_sfg, c_sbg;
static unsigned long pixels[PIXEL_N];

/*
 * freeitems -- recursively free the list of items. First call should be
 * freeitems(NULL).
 */
static void
freeitems(struct item *it)
{
	if (!it)
		it = first;
	else if (it == first)
		return;

	freeitems(it->next);
	free(it);
}

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

/* inbounds -- check if point y is within the bounds of line going from top. */
static bool
inbounds(int y, int top, int height)
{
	return y >= top && y <= top+height;
}

/* expose -- mark exposed items as dirty. */
static void
expose(XExposeEvent e)
{
	struct item *it = first;
	int y = 0;

	do {
		/*
		 * If either end of e's vertical part is within the bounds
		 * of the item, there is at least some collision. Conversely,
		 * if either of the item's vertical ends is within e's bounds,
		 * they must collide.
		 */
		int bot = y+height, e_bot = e.y+e.height;
		if (inbounds(e.y, y, bot) || inbounds(e_bot, y, bot) ||
		    inbounds(y, e.y, e_bot) || inbounds(bot, e.y, e_bot))
			it->dirty = true;

		y += height;
		it = it->next;
	} while (it != first);
}

/* redraw -- redraw the entire window. */
static void
redraw(void)
{
	struct item *it = first;
	int y = 0;

	static XftDraw *d = NULL;

	if (!d && !(d = XftDrawCreate(dpy, win, DefaultVisual(dpy, screen),
	    DefaultColormap(dpy, screen))))
		die(1, "couldn't create XftDraw");

	do {
		if (it->dirty) {
			XftColor *c;

			XClearArea(dpy, win, 0, y, width, height, False);

			if (it == selected) {
				c = &c_sfg;
				XftDrawRect(d, &c_sbg, 0, y, width, height);
			} else
				c = &c_fg;

			XftDrawStringUtf8(d, c, font,
			    o_hp, y + o_vp+font->ascent,
			    (FcChar8 *)it->s, it->len);

			it->dirty = false;
		}

		y += height;
		it = it->next;
	} while (it != first);
}

/* cleanup -- free allocated resources. */
static void
cleanup(void)
{
	Colormap cmap = DefaultColormap(dpy, screen);
	Visual *vis = DefaultVisual(dpy, screen);

	freeitems(NULL);

	XftFontClose(dpy, font);

	XFreeColors(dpy, cmap, pixels, PIXEL_N, 0);
	XftColorFree(dpy, vis, cmap, &c_fg);
	XftColorFree(dpy, vis, cmap, &c_sbg);
	XftColorFree(dpy, vis, cmap, &c_sfg);

	XUngrabKeyboard(dpy, CurrentTime);
	XUngrabPointer(dpy, CurrentTime);
	
	XCloseDisplay(dpy);
}

/* succeed -- exit successfully. If print is true, also print selected item. */
static void
succeed(bool print)
{
	if (print && selected)
		puts(selected->s);
	cleanup();
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
		expose(ev.xexpose);
		if (ev.xexpose.count)
			break;
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

/* alloccol_xft -- safely allocate new Xft colour c from string s. */
static void
alloccol_xft(char *s, XftColor *c)
{
	if (!(XftColorAllocName(dpy, DefaultVisual(dpy, screen),
	    DefaultColormap(dpy, screen), s, c)))
		die(1, "couldn't allocate Xft colour %s", s);
}

/* alloccol -- safely allocate new colour c from string s. */
static void
alloccol(char *s, XColor *c)
{
	XColor dummy;
	if (!(XAllocNamedColor(dpy, DefaultColormap(dpy, screen), s, c, &dummy)))
		die(1, "couldn't allocate colour %s", s);
}

/*
 * setupx -- create and map a window for n items; assign values to the X
 * globals.
 */
static void
setupx(int n)
{
	struct item *it;
	XColor col;
	XClassHint ch = {PROGNAME, PROGNAME};
	XSetWindowAttributes swa = {
		.override_redirect = True,
		.save_under = True,
		.event_mask = ExposureMask | StructureNotifyMask |
		    KeyPressMask | ButtonPressMask | ButtonReleaseMask |
		    PointerMotionMask | LeaveWindowMask | EnterWindowMask,
	};

	if (!(font = XftFontOpenName(dpy, screen, o_font)))
		die(1, "couldn't load font");
	height = font->height + o_vp;

	it = first;
	do {
		XGlyphInfo gi;
		XftTextExtentsUtf8(dpy, font, (FcChar8 *)it->s, it->len, &gi);
		if (gi.xOff + o_hp*2 > width)
			width = gi.xOff + o_hp*2;
		it = it->next;
	} while (it != first);

	if (o_x + width > DisplayWidth(dpy, screen))
		o_x = DisplayWidth(dpy, screen) - width;
	if (o_y + height*n > DisplayHeight(dpy, screen))
		o_y = DisplayHeight(dpy, screen) - height*n;

	alloccol(o_bg, &col);
	pixels[PIXEL_BG] = swa.background_pixel = col.pixel;
	alloccol(o_bc, &col);
	pixels[PIXEL_BC] = swa.border_pixel = col.pixel;

	win = XCreateWindow(dpy, RootWindow(dpy, screen), o_x, o_y,
	    width, n*height, o_bw, CopyFromParent, CopyFromParent,
	    CopyFromParent, CWOverrideRedirect | CWBackPixel | CWBorderPixel |
		CWEventMask | CWSaveUnder, &swa);
	XSetClassHint(dpy, win, &ch);

	alloccol_xft(o_fg, &c_fg);
	alloccol_xft(o_sfg, &c_sfg);
	alloccol_xft(o_sbg, &c_sbg);

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
			warn("no such keysym: %s", p);
		else if (new->nks >= MAXKS)
			warn("too many keysyms (%s)", p);
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
sdefault(const char *opt, const char *def)
{
	char *val = XGetDefault(dpy, PROGNAME, opt);
	return val ? val : (char *)def;
}

static int
idefault(const char *opt, int def)
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
				case 'c': /* -bc */
					o_bc = sarg(&argc, &argv);
					break;
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
		o_font = sdefault("font", "DejaVu Sans Mono-10");
	if (!o_sbg)
		o_sbg = sdefault("selectedBackground", "black");
	if (!o_sfg)
		o_sfg = sdefault("selectedForeground", "white");
	if (!o_bc)
		o_bc = sdefault("borderColour", "black");
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
