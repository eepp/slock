
/* See LICENSE file for license details. */
#define _XOPEN_SOURCE 500
#if HAVE_SHADOW_H
#include <shadow.h>
#endif

#include <math.h>
#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <X11/keysym.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#if HAVE_BSD_AUTH
#include <login_cap.h>
#include <bsd_auth.h>
#endif

typedef struct {
	int screen;
	Window root, win;
	Pixmap pmap;
	unsigned long colors[2];
} Lock;

struct rgb {
    double r;       // percent
    double g;       // percent
    double b;       // percent
};

struct hsv {
    double h;       // angle in degrees
    double s;       // percent
    double v;       // percent
};

static Lock **locks;
static int nscreens;
static Bool running = True;

static void
die(const char *errstr, ...) {
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

#ifndef HAVE_BSD_AUTH
static const char *
getpw(void) { /* only run as root */
	const char *rval;
	struct passwd *pw;

	pw = getpwuid(getuid());
	if(!pw)
		die("slock: cannot retrieve password entry (make sure to suid or sgid slock)");
	endpwent();
	rval =  pw->pw_passwd;

#if HAVE_SHADOW_H
	if (strlen(rval) >= 1) { /* kludge, assumes pw placeholder entry has len >= 1 */
		struct spwd *sp;
		sp = getspnam(getenv("USER"));
		if(!sp)
			die("slock: cannot retrieve shadow entry (make sure to suid or sgid slock)\n");
		endspent();
		rval = sp->sp_pwdp;
	}
#endif

	/* drop privileges */
	if(setgid(pw->pw_gid) < 0 || setuid(pw->pw_uid) < 0)
		die("slock: cannot drop privileges");
	return rval;
}
#endif

static void
#ifdef HAVE_BSD_AUTH
readpw(Display *dpy)
#else
readpw(Display *dpy, const char *pws)
#endif
{
	char buf[32], passwd[256];
	int num, screen;
	unsigned int len, llen;
	KeySym ksym;
	XEvent ev;

	len = llen = 0;
	running = True;

	/* As "slock" stands for "Simple X display locker", the DPMS settings
	 * had been removed and you can set it with "xset" or some other
	 * utility. This way the user can easily set a customized DPMS
	 * timeout. */
	while(running && !XNextEvent(dpy, &ev)) {
		if(ev.type == KeyPress) {
			buf[0] = 0;
			num = XLookupString(&ev.xkey, buf, sizeof buf, &ksym, 0);
			if(IsKeypadKey(ksym)) {
				if(ksym == XK_KP_Enter)
					ksym = XK_Return;
				else if(ksym >= XK_KP_0 && ksym <= XK_KP_9)
					ksym = (ksym - XK_KP_0) + XK_0;
			}
			if(IsFunctionKey(ksym) || IsKeypadKey(ksym)
					|| IsMiscFunctionKey(ksym) || IsPFKey(ksym)
					|| IsPrivateKeypadKey(ksym))
				continue;
			switch(ksym) {
			case XK_Return:
				passwd[len] = 0;
#ifdef HAVE_BSD_AUTH
				running = !auth_userokay(getlogin(), NULL, "auth-xlock", passwd);
#else
				running = strcmp(crypt(passwd, pws), pws);
#endif
				if(running != False)
					XBell(dpy, 100);
				len = 0;
				break;
			case XK_Escape:
				len = 0;
				break;
			case XK_BackSpace:
				if(len)
					--len;
				break;
			default:
				if(num && !iscntrl((int) buf[0]) && (len + num < sizeof passwd)) { 
					memcpy(passwd + len, buf, num);
					len += num;
				}
				break;
			}
			if(llen == 0 && len != 0) {
				for(screen = 0; screen < nscreens; screen++) {
					XSetWindowBackground(dpy, locks[screen]->win, locks[screen]->colors[1]);
					XClearWindow(dpy, locks[screen]->win);
				}
			} else if(llen != 0 && len == 0) {
				for(screen = 0; screen < nscreens; screen++) {
					XSetWindowBackground(dpy, locks[screen]->win, locks[screen]->colors[0]);
					XClearWindow(dpy, locks[screen]->win);
				}
			}
			llen = len;
		}
		else for(screen = 0; screen < nscreens; screen++)
			XRaiseWindow(dpy, locks[screen]->win);
	}
}

static void
unlockscreen(Display *dpy, Lock *lock) {
	if(dpy == NULL || lock == NULL)
		return;

	XUngrabPointer(dpy, CurrentTime);
	XFreeColors(dpy, DefaultColormap(dpy, lock->screen), lock->colors, 2, 0);
	XFreePixmap(dpy, lock->pmap);
	XDestroyWindow(dpy, lock->win);

	free(lock);
}

/* credit: <http://stackoverflow.com/questions/3018313/algorithm-to-convert-rgb-to-hsv-and-hsv-to-rgb> */
static void hsv2rgb(struct hsv* in, struct rgb* out) {
    double hh, p, q, t, ff;
    long i;

    if (in->s <= 0.0) {       // < is bogus, just shuts up warnings
        if (isnan(in->h)) {   // in.h == NAN
            out->r = in->v;
            out->g = in->v;
            out->b = in->v;
            return;
        }
        // error - should never happen
        out->r = 0.0;
        out->g = 0.0;
        out->b = 0.0;
        return;
    }
    hh = in->h;
    if (hh >= 360.0) hh = 0.0;
    hh /= 60.0;
    i = (long) hh;
    ff = hh - i;
    p = in->v * (1.0 - in->s);
    q = in->v * (1.0 - (in->s * ff));
    t = in->v * (1.0 - (in->s * (1.0 - ff)));

    switch(i) {
    case 0:
        out->r = in->v;
        out->g = t;
        out->b = p;
        break;
    case 1:
        out->r = q;
        out->g = in->v;
        out->b = p;
        break;
    case 2:
        out->r = p;
        out->g = in->v;
        out->b = t;
        break;

    case 3:
        out->r = p;
        out->g = q;
        out->b = in->v;
        break;
    case 4:
        out->r = t;
        out->g = p;
        out->b = in->v;
        break;
    case 5:
    default:
        out->r = in->v;
        out->g = p;
        out->b = q;
        break;
    }
}

static void gen_random_pastel(XColor* color, double hue) {
	struct hsv hsv;
	struct rgb rgb;
	hsv.h = hue;
	hsv.s = 0.6;
	hsv.v = 0.95;
	hsv2rgb(&hsv, &rgb);
	color->red = (unsigned short) (rgb.r * 65535.0);
	color->green = (unsigned short) (rgb.g * 65535.0);
	color->blue = (unsigned short) (rgb.b * 65535.0);
}

static Lock *
lockscreen(Display *dpy, int screen) {
	char curs[] = {0, 0, 0, 0, 0, 0, 0, 0};
	unsigned int len;
	Lock *lock;
	XColor color;
	XSetWindowAttributes wa;
	Cursor invisible;
	double hue1, hue2, tmp;

	if(dpy == NULL || screen < 0)
		return NULL;

	lock = malloc(sizeof(Lock));
	if(lock == NULL)
		return NULL;

	lock->screen = screen;
	lock->root = RootWindow(dpy, lock->screen);

	/* init */
	wa.override_redirect = 1;
	lock->win = XCreateWindow(dpy, lock->root, 0, 0, DisplayWidth(dpy, lock->screen), DisplayHeight(dpy, lock->screen),
			0, DefaultDepth(dpy, lock->screen), CopyFromParent,
			DefaultVisual(dpy, lock->screen), CWOverrideRedirect | CWBackPixel, &wa);
	
	/* locked color */
	hue1 = (double) (rand() % 360);
	gen_random_pastel(&color, hue1);
	XAllocColor(dpy, DefaultColormap(dpy, lock->screen), &color);
	lock->colors[0] = color.pixel;
	XSetWindowBackground(dpy, lock->win, lock->colors[0]);
	
	/* trying to unlock color */
	hue2 = hue1;
	while (hue2 == hue1) {
		hue2 = (double) (rand() % 360);
		tmp = hue2 - hue1;
		if (tmp < 0.0) {
			tmp += 360.0;
		}
		if (tmp < 100.0) {
			hue2 = hue1;
		}
	}
	gen_random_pastel(&color, hue2);
	XAllocColor(dpy, DefaultColormap(dpy, lock->screen), &color);
	lock->colors[1] = color.pixel;
	
	lock->pmap = XCreateBitmapFromData(dpy, lock->win, curs, 8, 8);
	invisible = XCreatePixmapCursor(dpy, lock->pmap, lock->pmap, &color, &color, 0, 0);
	XDefineCursor(dpy, lock->win, invisible);
	XMapRaised(dpy, lock->win);
	for(len = 1000; len; len--) {
		if(XGrabPointer(dpy, lock->root, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
			GrabModeAsync, GrabModeAsync, None, invisible, CurrentTime) == GrabSuccess)
			break;
		usleep(1000);
	}
	if(running && (len > 0)) {
		for(len = 1000; len; len--) {
			if(XGrabKeyboard(dpy, lock->root, True, GrabModeAsync, GrabModeAsync, CurrentTime)
				== GrabSuccess)
				break;
			usleep(1000);
		}
	}

	running &= (len > 0);
	if(!running) {
		unlockscreen(dpy, lock);
		lock = NULL;
	}
	else 
		XSelectInput(dpy, lock->root, SubstructureNotifyMask);

	return lock;
}

static void
usage(void) {
	fprintf(stderr, "usage: slock [-v]\n");
	exit(EXIT_FAILURE);
}

int
main(int argc, char **argv) {
#ifndef HAVE_BSD_AUTH
	const char *pws;
#endif
	Display *dpy;
	int screen;

	srand(time(NULL));

	if((argc == 2) && !strcmp("-v", argv[1]))
		die("slock-%s, © 2006-2012 Anselm R Garbe, 2013 Philippe Proulx\n", VERSION);
	else if(argc != 1)
		usage();

	if(!getpwuid(getuid()))
		die("slock: no passwd entry for you");

#ifndef HAVE_BSD_AUTH
	pws = getpw();
#endif

	if(!(dpy = XOpenDisplay(0)))
		die("slock: cannot open display");
	/* Get the number of screens in display "dpy" and blank them all. */
	nscreens = ScreenCount(dpy);
	locks = malloc(sizeof(Lock *) * nscreens);
	if(locks == NULL)
		die("slock: malloc: %s", strerror(errno));
	int nlocks = 0;
	for(screen = 0; screen < nscreens; screen++) {
		if ( (locks[screen] = lockscreen(dpy, screen)) != NULL)
			nlocks++;
	}
	XSync(dpy, False);

	/* Did we actually manage to lock something? */
	if (nlocks == 0) { // nothing to protect
		free(locks);
		XCloseDisplay(dpy);
		return 1;
	}

	/* Everything is now blank. Now wait for the correct password. */
#ifdef HAVE_BSD_AUTH
	readpw(dpy);
#else
	readpw(dpy, pws);
#endif

	/* Password ok, unlock everything and quit. */
	for(screen = 0; screen < nscreens; screen++)
		unlockscreen(dpy, locks[screen]);

	free(locks);
	XCloseDisplay(dpy);

	return 0;
}
