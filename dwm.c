/* See LICENSE file for copyright and license details.
 *
 * dynamic window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance. Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of dwm are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag. Clients are organized in a linked client
 * list on each monitor, the focus history is remembered through a stack list
 * on each monitor. Each client contains a bit array to indicate the tags of a
 * client.
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <dirent.h>
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <X11/extensions/Xinerama.h>
#include <X11/Xft/Xft.h>

#include "drw.h"
#include "util.h"

/* macros */
#define BUTTONMASK (ButtonPressMask | ButtonReleaseMask)
#define CLEANMASK(mask) \
    (mask & ~(numlockmask | LockMask) & (ShiftMask | ControlMask | Mod1Mask | Mod2Mask | Mod3Mask | Mod4Mask | Mod5Mask))
#define INTERSECT(x, y, w, h, m) \
    (MAX(0, MIN((x) + (w), (m)->mx + (m)->mw) - MAX((x), (m)->mx)) * MAX(0, MIN((y) + (h), (m)->my + (m)->mh) - MAX((y), (m)->my)))
#define ISVISIBLE(C) ((C->tags & C->mon->tagset[C->mon->seltags]))
#define LENGTH(X) (sizeof X / sizeof X[0])
#define MOUSEMASK (BUTTONMASK | PointerMotionMask)
#define WIDTH(X) ((X)->w + 2 * (X)->bw)
#define HEIGHT(X) ((X)->h + 2 * (X)->bw)
#define TAGMASK ((1 << LENGTH(tags)) - 1)
#define TEXTW(X) (drw_fontset_getwidth(drw, (X)) + lrpad)

/* enums */
enum { CurNormal, CurResize, CurMove, CurLast }; /* cursor */
enum {
    NetSupported,
    NetWMName,
    NetWMState,
    NetWMCheck,
    NetWMFullscreen,
    NetActiveWindow,
    NetWMWindowType,
    NetWMWindowTypeDialog,
    NetClientList,
    NetLast
};                                                                                              /* EWMH atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast };                                   /* default atoms */
enum { ClkTagBar, ClkStatusText, ClkWinTitle, ClkClientWin, ClkRootWin, ClkLast }; /* clicks */

typedef union {
    long i;
    unsigned long ui;
    float f;
    const void *v;
} Arg;

typedef struct {
    unsigned int click;
    unsigned int mask;
    unsigned int button;
    void (*func)(const Arg *arg);
    const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct Client Client;
struct Client {
    char name[256];
    float mina, maxa;
    int x, y, w, h;
    int oldx, oldy, oldw, oldh;
    int basew, baseh, incw, inch, maxw, maxh, minw, minh;
    int bw, oldbw;
    unsigned int tags;
    int isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen;
    Client *next;
    Client *snext;
    Monitor *mon;
    Window win;
};

typedef struct {
    unsigned int mod;
    KeySym keysym;
    void (*func)(const Arg *);
    const Arg arg;
} Key;

struct Monitor {
    float mfact;
    int nmaster;
    int num;
    int by, bh;         /* bar geometry */
    int mx, my, mw, mh; /* screen size */
    int wx, wy, ww, wh; /* window area  */
    int gappx;
    unsigned int seltags;
    unsigned int tagset[2];
    Client *clients;
    Client *sel;
    Client *stack;
    Monitor *next;
    Window barwin;
    Window traywin;
};

typedef struct {
    const char *class;
    const char *instance;
    const char *title;
    unsigned int tags;
    int isfloating;
    int monitor;
} Rule;

/* function declarations */
static void applyrules(Client *c);
static int applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact);
static void arrange(Monitor *m);
static void attach(Client *c);
static void attachstack(Client *c);
static void buttonpress(XEvent *e);
static void checkotherwm();
static void cleanup();
static void cleanupmon(Monitor *mon);
static void clientmessage(XEvent *e);
static void configure(Client *c);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static Monitor *createmon();
static void destroynotify(XEvent *e);
static void detach(Client *c);
static void detachstack(Client *c);
static Monitor *dirtomon(int dir);
static void enternotify(XEvent *e);
static void focus(Client *c);
static void focusin(XEvent *e);
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
static Atom getatomprop(Client *c, Atom prop);
static int getrootptr(int *x, int *y);
static long getstate(Window w);
static int gettextprop(Window w, Atom atom, char *text, unsigned int size);
static void grabbuttons(Client *c, int focused);
static void grabkeys();
static void incnmaster(const Arg *arg);
static void keypress(XEvent *e);
static void killclient(const Arg *arg);
static void manage(Window w, XWindowAttributes *wa);
static void managealtbar(Window win, XWindowAttributes *wa);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
static void motionnotify(XEvent *e);
static void movemouse(const Arg *arg);
static Client *nexttiled(Client *c);
static void pop(Client *);
static void propertynotify(XEvent *e);
static void quit(const Arg *arg);
static Monitor *recttomon(int x, int y, int w, int h);
static void resize(Client *c, int x, int y, int w, int h, int interact);
static void resizeclient(Client *c, int x, int y, int w, int h);
static void resizemouse(const Arg *arg);
static void restack(Monitor *m);
static void run();
static void runautostart();
static void scan();
static int sendevent(Client *c, Atom proto);
static void sendmon(Client *c, Monitor *m);
static void setclientstate(Client *c, long state);
static void setfocus(Client *c);
static void setfullscreen(Client *c, int fullscreen);
static void setmfact(const Arg *arg);
static void setup();
static void seturgent(Client *c, int urg);
static void showhide(Client *c);
static void sigchld(int unused);
static void spawn(const Arg *arg);
static void tagmon(const Arg *arg);
static void tile(Monitor *);
static void togglefloating(const Arg *arg);
static void togglefullscr(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void unfocus(Client *c, int setfocus);
static void unmanage(Client *c, int destroyed);
static void unmanagealtbar(Window w);
static void unmanagetray(Window w);
static void unmapnotify(XEvent *e);
static void updatebarpos(Monitor *m);
static void updateclientlist();
static int updategeom();
static void updatenumlockmask();
static void updatesizehints(Client *c);
static void updatetitle(Client *c);
static void updatewindowtype(Client *c);
static void updatewmhints(Client *c);
static void view(const Arg *arg);
static Client *wintoclient(Window w);
static Monitor *wintomon(Window w);
static int wmclasscontains(Window win, const char *class, const char *name);
static int xerror(Display *dpy, XErrorEvent *ee);
static int xerrordummy(Display *dpy, XErrorEvent *ee);
static int xerrorstart(Display *dpy, XErrorEvent *ee);
static void zoom(const Arg *arg);

static void keyrelease(XEvent *e);
static void combotag(const Arg *arg);
static void comboview(const Arg *arg);

/* variables */
static const char broken[] = "broken";
static char stext[256];
static int screen;
static int sw, sh;      /* X display screen geometry width, height */
static int bh; /* bar geometry */
static int lrpad;       /* sum of left and right padding for text */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int numlockmask = 0;
static void (*handler[LASTEvent])(XEvent *) = {[ButtonPress] = buttonpress,
                                               [ButtonRelease] = keyrelease,
                                               [ClientMessage] = clientmessage,
                                               [ConfigureRequest] = configurerequest,
                                               [ConfigureNotify] = configurenotify,
                                               [DestroyNotify] = destroynotify,
                                               [EnterNotify] = enternotify,
                                               [FocusIn] = focusin,
                                               [KeyRelease] = keyrelease,
                                               [KeyPress] = keypress,
                                               [MappingNotify] = mappingnotify,
                                               [MapRequest] = maprequest,
                                               [MotionNotify] = motionnotify,
                                               [PropertyNotify] = propertynotify,
                                               [UnmapNotify] = unmapnotify};
static Atom wmatom[WMLast], netatom[NetLast];
static int running = 1;
static Cur *cursor[CurLast];
static Display *dpy;
static Drw *drw;
static Monitor *mons, *selmon;
static Window root, wmcheckwin;

// --------------------------------- CONFIG START ------------------------

/* appearance */
static const unsigned int borderpx = 0; /* border pixel of windows */
static const unsigned int gappx = 10;
static const unsigned int snap = 32;        /* snap pixel */
static const char *altbarclass = "Polybar"; /* Alternate bar class name */
/* tagging */
static const char *tags[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};

static const Rule rules[] = {
        /* xprop(1): */
        /* 	WM_CLASS(STRING) = instance, class */
        /* 	WM_NAME(STRING) = title */
        /* class      instance    title       tags mask     isfloating   monitor */
        {"Gimp", NULL, NULL, 0, 1, -1},
        {"Firefox", NULL, NULL, 1 << 8, 0, -1},
};

static const float mfact = 0.55;  /* factor of master area size [0.05..0.95] */
static const int nmaster = 1;     /* number of clients in master area */
static const int resizehints = 1; /* 1 means respect size hints in tiled resizals */

/* key definitions */
#define MODKEY Mod4Mask
#define TAGKEYS(KEY, TAG)                                                                                  \
    {MODKEY, KEY, comboview, {.ui = 1 << TAG}}, {MODKEY | ControlMask, KEY, toggleview, {.ui = 1 << TAG}}, \
            {MODKEY | ShiftMask, KEY, combotag, {.ui = 1 << TAG}}, {MODKEY | ControlMask | ShiftMask, KEY, toggletag, {.ui = 1 << TAG}},

/* commands */
static const char *runnercmd[] = {"rofi", "-show", "run", NULL};
static const char *termcmd[] = {"alacritty", NULL};
static const char *browsercmd[] = {"firefox", NULL};
static const char *lockcmd[] = {"betterlockscreen", "-l", NULL};
static const char *zealcmd[] = {"zeal", NULL};

static Key keys[] = {
        /* modifier                     key        function        argument */
        {MODKEY, XK_d, spawn, {.v = runnercmd}},
        {MODKEY, XK_Return, spawn, {.v = termcmd}},
        {MODKEY, XK_b, spawn, {.v = browsercmd}},
        {MODKEY | ShiftMask, XK_p, spawn, {.v = lockcmd}},
        {MODKEY, XK_z, spawn, {.v = zealcmd}},
        {MODKEY, XK_j, focusstack, {.i = +1}},
        {MODKEY, XK_k, focusstack, {.i = -1}},
        {MODKEY, XK_u, incnmaster, {.i = +1}},
        {MODKEY, XK_i, incnmaster, {.i = -1}},
        {MODKEY, XK_y, setmfact, {.f = -0.05}},
        {MODKEY, XK_o, setmfact, {.f = +0.05}},
        {MODKEY, XK_f, zoom, {0}},
        {MODKEY | ShiftMask, XK_f, togglefullscr, {0}},
        {MODKEY | ShiftMask, XK_q, killclient, {0}},
        {MODKEY | ShiftMask, XK_space, togglefloating, {0}},
        {MODKEY, XK_0, comboview, {.ui = ~0}},
        {MODKEY | ShiftMask, XK_0, combotag, {.ui = ~0}},
        {MODKEY, XK_l, focusmon, {.i = -1}},
        {MODKEY, XK_h, focusmon, {.i = +1}},
        {MODKEY | ShiftMask, XK_l, tagmon, {.i = -1}},
        {MODKEY | ShiftMask, XK_h, tagmon, {.i = +1}},
        TAGKEYS(XK_1, 0) TAGKEYS(XK_2, 1) TAGKEYS(XK_3, 2) TAGKEYS(XK_4, 3) TAGKEYS(XK_5, 4) TAGKEYS(XK_6, 5) TAGKEYS(XK_7, 6)
                TAGKEYS(XK_8, 7) TAGKEYS(XK_9, 8){MODKEY | ShiftMask, XK_e, quit, {0}},
};

/* button definitions */
/* click can be ClkTagBar, ClkStatusText, ClkWinTitle,
 * ClkClientWin, or ClkRootWin */
static Button buttons[] = {
        {ClkClientWin, MODKEY, Button1, movemouse, {0}},
        {ClkClientWin, MODKEY, Button2, togglefloating, {0}},
        {ClkClientWin, MODKEY, Button3, resizemouse, {0}},
};
// --------------------------------- CONFIG END --------------------------

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags {
    char limitexceeded[LENGTH(tags) > 31 ? -1 : 1];
};

/* function implementations */
static int combo = 0;

void keyrelease(XEvent *e) { combo = 0; }

void combotag(const Arg *arg) {
    if (selmon->sel && arg->ui & TAGMASK) {
        if (combo) {
            selmon->sel->tags |= arg->ui & TAGMASK;
        } else {
            combo = 1;
            selmon->sel->tags = arg->ui & TAGMASK;
        }
        focus(NULL);
        arrange(selmon);
    }
}

void comboview(const Arg *arg) {
    unsigned newtags = arg->ui & TAGMASK;
    if (combo) {
        selmon->tagset[selmon->seltags] |= newtags;
    } else {
        selmon->seltags ^= 1; /*toggle tagset*/
        combo = 1;
        if (newtags) selmon->tagset[selmon->seltags] = newtags;
    }
    focus(NULL);
    arrange(selmon);
}

void applyrules(Client *c) {
    const char *class, *instance;
    unsigned int i;
    const Rule *r;
    Monitor *m;
    XClassHint ch = {NULL, NULL};

    /* rule matching */
    c->isfloating = 0;
    c->tags = 0;
    XGetClassHint(dpy, c->win, &ch);
    class = ch.res_class ? ch.res_class : broken;
    instance = ch.res_name ? ch.res_name : broken;

    for (i = 0; i < LENGTH(rules); i++) {
        r = &rules[i];
        if ((!r->title || strstr(c->name, r->title)) && (!r->class || strstr(class, r->class))
            && (!r->instance || strstr(instance, r->instance))) {
            c->isfloating = r->isfloating;
            c->tags |= r->tags;
            for (m = mons; m && m->num != r->monitor; m = m->next)
                ;
            if (m) c->mon = m;
        }
    }
    if (ch.res_class) XFree(ch.res_class);
    if (ch.res_name) XFree(ch.res_name);
    c->tags = c->tags & TAGMASK ? c->tags & TAGMASK : c->mon->tagset[c->mon->seltags];
}

int applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact) {
    int baseismin;
    Monitor *m = c->mon;

    /* set minimum possible */
    *w = MAX(1, *w);
    *h = MAX(1, *h);
    if (interact) {
        if (*x > sw) *x = sw - WIDTH(c);
        if (*y > sh) *y = sh - HEIGHT(c);
        if (*x + *w + 2 * c->bw < 0) *x = 0;
        if (*y + *h + 2 * c->bw < 0) *y = 0;
    } else {
        if (*x >= m->wx + m->ww) *x = m->wx + m->ww - WIDTH(c);
        if (*y >= m->wy + m->wh) *y = m->wy + m->wh - HEIGHT(c);
        if (*x + *w + 2 * c->bw <= m->wx) *x = m->wx;
        if (*y + *h + 2 * c->bw <= m->wy) *y = m->wy;
    }
    if (*h < bh) *h = bh;
    if (*w < bh) *w = bh;
    if (resizehints || c->isfloating) {
        /* see last two sentences in ICCCM 4.1.2.3 */
        baseismin = c->basew == c->minw && c->baseh == c->minh;
        if (!baseismin) { /* temporarily remove base dimensions */
            *w -= c->basew;
            *h -= c->baseh;
        }
        /* adjust for aspect limits */
        if (c->mina > 0 && c->maxa > 0) {
            if (c->maxa < (float)*w / *h)
                *w = *h * c->maxa + 0.5;
            else if (c->mina < (float)*h / *w)
                *h = *w * c->mina + 0.5;
        }
        if (baseismin) { /* increment calculation requires this */
            *w -= c->basew;
            *h -= c->baseh;
        }
        /* adjust for increment value */
        if (c->incw) *w -= *w % c->incw;
        if (c->inch) *h -= *h % c->inch;
        /* restore base dimensions */
        *w = MAX(*w + c->basew, c->minw);
        *h = MAX(*h + c->baseh, c->minh);
        if (c->maxw) *w = MIN(*w, c->maxw);
        if (c->maxh) *h = MIN(*h, c->maxh);
    }
    return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void arrange(Monitor *m) {
    if (m)
        showhide(m->stack);
    else
        for (m = mons; m; m = m->next) showhide(m->stack);
    if (m) {
        tile(m);
        restack(m);
    } else
        for (m = mons; m; m = m->next) tile(m);
}

void attach(Client *c) {
    c->next = c->mon->clients;
    c->mon->clients = c;
}

void attachstack(Client *c) {
    c->snext = c->mon->stack;
    c->mon->stack = c;
}

void buttonpress(XEvent *e) {
    unsigned int i, x, click;
    Arg arg = {0};
    Client *c;
    Monitor *m;
    XButtonPressedEvent *ev = &e->xbutton;

    click = ClkRootWin;
    /* focus monitor if necessary */
    if ((m = wintomon(ev->window)) && m != selmon) {
        unfocus(selmon->sel, 1);
        selmon = m;
        focus(NULL);
    }
    if (ev->window == selmon->barwin) {
        i = x = 0;
        do
            x += TEXTW(tags[i]);
        while (ev->x >= x && ++i < LENGTH(tags));
        if (i < LENGTH(tags)) {
            click = ClkTagBar;
            arg.ui = 1 << i;
        } else if (ev->x > selmon->ww - (int)TEXTW(stext)) {
            click = ClkStatusText;
        } else {
            click = ClkWinTitle;
        }
    } else if ((c = wintoclient(ev->window))) {
        focus(c);
        restack(selmon);
        XAllowEvents(dpy, ReplayPointer, CurrentTime);
        click = ClkClientWin;
    }
    for (i = 0; i < LENGTH(buttons); i++)
        if (click == buttons[i].click && buttons[i].func && buttons[i].button == ev->button
            && CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
            buttons[i].func(click == ClkTagBar && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);
}

void checkotherwm() {
    xerrorxlib = XSetErrorHandler(xerrorstart);
    /* this causes an error if some other window manager is running */
    XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
    XSync(dpy, False);
    XSetErrorHandler(xerror);
    XSync(dpy, False);
}

void cleanup() {
    Arg a = {.ui = ~0};
    Monitor *m;
    size_t i;

    view(&a);
    for (m = mons; m; m = m->next)
        while (m->stack) unmanage(m->stack, 0);
    XUngrabKey(dpy, AnyKey, AnyModifier, root);
    while (mons) cleanupmon(mons);
    for (i = 0; i < CurLast; i++) drw_cur_free(drw, cursor[i]);
    XDestroyWindow(dpy, wmcheckwin);
    drw_free(drw);
    XSync(dpy, False);
    XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}

void cleanupmon(Monitor *mon) {
    Monitor *m;

    if (mon == mons)
        mons = mons->next;
    else {
        for (m = mons; m && m->next != mon; m = m->next)
            ;
        m->next = mon->next;
    }
    free(mon);
}

void clientmessage(XEvent *e) {
    XClientMessageEvent *cme = &e->xclient;
    Client *c = wintoclient(cme->window);

    if (!c) return;
    if (cme->message_type == netatom[NetWMState]) {
        if (cme->data.l[1] == netatom[NetWMFullscreen] || cme->data.l[2] == netatom[NetWMFullscreen])
            setfullscreen(c,
                          (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
                           || (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ && !c->isfullscreen)));
    } else if (cme->message_type == netatom[NetActiveWindow]) {
        if (c != selmon->sel && !c->isurgent) seturgent(c, 1);
    }
}

void configure(Client *c) {
    XConfigureEvent ce;

    ce.type = ConfigureNotify;
    ce.display = dpy;
    ce.event = c->win;
    ce.window = c->win;
    ce.x = c->x;
    ce.y = c->y;
    ce.width = c->w;
    ce.height = c->h;
    ce.border_width = c->bw;
    ce.above = None;
    ce.override_redirect = False;
    XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

void configurenotify(XEvent *e) {
    Monitor *m;
    Client *c;
    XConfigureEvent *ev = &e->xconfigure;
    int dirty;

    /* TODO: updategeom handling sucks, needs to be simplified */
    if (ev->window == root) {
        dirty = (sw != ev->width || sh != ev->height);
        sw = ev->width;
        sh = ev->height;
        if (updategeom() || dirty) {
            drw_resize(drw, sw, bh);
            for (m = mons; m; m = m->next) {
                for (c = m->clients; c; c = c->next)
                    if (c->isfullscreen) resizeclient(c, m->mx, m->my, m->mw, m->mh);
                XMoveResizeWindow(dpy, m->barwin, m->wx, m->by, m->ww, m->bh);
            }
            focus(NULL);
            arrange(NULL);
        }
    }
}

void configurerequest(XEvent *e) {
    Client *c;
    Monitor *m;
    XConfigureRequestEvent *ev = &e->xconfigurerequest;
    XWindowChanges wc;

    if ((c = wintoclient(ev->window))) {
        if (ev->value_mask & CWBorderWidth)
            c->bw = ev->border_width;
        else if (c->isfloating) {
            m = c->mon;
            if (ev->value_mask & CWX) {
                c->oldx = c->x;
                c->x = m->mx + ev->x;
            }
            if (ev->value_mask & CWY) {
                c->oldy = c->y;
                c->y = m->my + ev->y;
            }
            if (ev->value_mask & CWWidth) {
                c->oldw = c->w;
                c->w = ev->width;
            }
            if (ev->value_mask & CWHeight) {
                c->oldh = c->h;
                c->h = ev->height;
            }
            if ((c->x + c->w) > m->mx + m->mw && c->isfloating) c->x = m->mx + (m->mw / 2 - WIDTH(c) / 2);  /* center in x direction */
            if ((c->y + c->h) > m->my + m->mh && c->isfloating) c->y = m->my + (m->mh / 2 - HEIGHT(c) / 2); /* center in y direction */
            if ((ev->value_mask & (CWX | CWY)) && !(ev->value_mask & (CWWidth | CWHeight))) configure(c);
            if (ISVISIBLE(c)) XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
        } else
            configure(c);
    } else {
        wc.x = ev->x;
        wc.y = ev->y;
        wc.width = ev->width;
        wc.height = ev->height;
        wc.border_width = ev->border_width;
        wc.sibling = ev->above;
        wc.stack_mode = ev->detail;
        XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
    }
    XSync(dpy, False);
}

Monitor *createmon() {
    Monitor *m;

    m = ecalloc(1, sizeof(Monitor));
    m->tagset[0] = m->tagset[1] = 1;
    m->mfact = mfact;
    m->nmaster = nmaster;
    m->bh = bh;
    m->gappx = gappx;
    return m;
}

void destroynotify(XEvent *e) {
    Client *c;
    Monitor *m;
    XDestroyWindowEvent *ev = &e->xdestroywindow;

    if ((c = wintoclient(ev->window)))
        unmanage(c, 1);
    else if ((m = wintomon(ev->window)) && m->barwin == ev->window)
        unmanagealtbar(ev->window);
    else if (m->traywin == ev->window)
        unmanagetray(ev->window);
}

void detach(Client *c) {
    Client **tc;

    for (tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next)
        ;
    *tc = c->next;
}

void detachstack(Client *c) {
    Client **tc, *t;

    for (tc = &c->mon->stack; *tc && *tc != c; tc = &(*tc)->snext)
        ;
    *tc = c->snext;

    if (c == c->mon->sel) {
        for (t = c->mon->stack; t && !ISVISIBLE(t); t = t->snext)
            ;
        c->mon->sel = t;
    }
}

Monitor *dirtomon(int dir) {
    Monitor *m = NULL;

    if (dir > 0) {
        if (!(m = selmon->next)) m = mons;
    } else if (selmon == mons)
        for (m = mons; m->next; m = m->next)
            ;
    else
        for (m = mons; m->next != selmon; m = m->next)
            ;
    return m;
}

void enternotify(XEvent *e) {
    Client *c;
    Monitor *m;
    XCrossingEvent *ev = &e->xcrossing;

    if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root) return;
    c = wintoclient(ev->window);
    m = c ? c->mon : wintomon(ev->window);
    if (m != selmon) {
        unfocus(selmon->sel, 1);
        selmon = m;
    } else if (!c || c == selmon->sel)
        return;
    focus(c);
}

void focus(Client *c) {
    if (!c || !ISVISIBLE(c))
        for (c = selmon->stack; c && !ISVISIBLE(c); c = c->snext)
            ;
    if (selmon->sel && selmon->sel != c) unfocus(selmon->sel, 0);
    if (c) {
        if (c->mon != selmon) selmon = c->mon;
        if (c->isurgent) seturgent(c, 0);
        detachstack(c);
        attachstack(c);
        grabbuttons(c, 1);
        setfocus(c);
    } else {
        XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
        XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
    }
    selmon->sel = c;
}

/* there are some broken focus acquiring clients needing extra handling */
void focusin(XEvent *e) {
    XFocusChangeEvent *ev = &e->xfocus;

    if (selmon->sel && ev->window != selmon->sel->win) setfocus(selmon->sel);
}

void focusmon(const Arg *arg) {
    Monitor *m;

    if (!mons->next) return;
    if ((m = dirtomon(arg->i)) == selmon) return;
    unfocus(selmon->sel, 0);
    selmon = m;
    focus(NULL);
}

void focusstack(const Arg *arg) {
    Client *c = NULL, *i;

    if (!selmon->sel) return;
    if (arg->i > 0) {
        for (c = selmon->sel->next; c && !ISVISIBLE(c); c = c->next)
            ;
        if (!c)
            for (c = selmon->clients; c && !ISVISIBLE(c); c = c->next)
                ;
    } else {
        for (i = selmon->clients; i != selmon->sel; i = i->next)
            if (ISVISIBLE(i)) c = i;
        if (!c)
            for (; i; i = i->next)
                if (ISVISIBLE(i)) c = i;
    }
    if (c) {
        focus(c);
        restack(selmon);
    }
}

Atom getatomprop(Client *c, Atom prop) {
    int di;
    unsigned long dl;
    unsigned char *p = NULL;
    Atom da, atom = None;

    if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, XA_ATOM, &da, &di, &dl, &dl, &p) == Success && p) {
        atom = *(Atom *)p;
        XFree(p);
    }
    return atom;
}

int getrootptr(int *x, int *y) {
    int di;
    unsigned int dui;
    Window dummy;

    return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

long getstate(Window w) {
    int format;
    long result = -1;
    unsigned char *p = NULL;
    unsigned long n, extra;
    Atom real;

    if (XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False, wmatom[WMState], &real, &format, &n, &extra, (unsigned char **)&p)
        != Success)
        return -1;
    if (n != 0) result = *p;
    XFree(p);
    return result;
}

int gettextprop(Window w, Atom atom, char *text, unsigned int size) {
    char **list = NULL;
    int n;
    XTextProperty name;

    if (!text || size == 0) return 0;
    text[0] = '\0';
    if (!XGetTextProperty(dpy, w, &name, atom) || !name.nitems) return 0;
    if (name.encoding == XA_STRING)
        strncpy(text, (char *)name.value, size - 1);
    else {
        if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success && n > 0 && *list) {
            strncpy(text, *list, size - 1);
            XFreeStringList(list);
        }
    }
    text[size - 1] = '\0';
    XFree(name.value);
    return 1;
}

void grabbuttons(Client *c, int focused) {
    updatenumlockmask();
    {
        unsigned int i, j;
        unsigned int modifiers[] = {0, LockMask, numlockmask, numlockmask | LockMask};
        XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
        if (!focused) XGrabButton(dpy, AnyButton, AnyModifier, c->win, False, BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
        for (i = 0; i < LENGTH(buttons); i++)
            if (buttons[i].click == ClkClientWin)
                for (j = 0; j < LENGTH(modifiers); j++)
                    XGrabButton(dpy, buttons[i].button, buttons[i].mask | modifiers[j], c->win, False, BUTTONMASK, GrabModeAsync,
                                GrabModeSync, None, None);
    }
}

void grabkeys() {
    updatenumlockmask();
    {
        unsigned int i, j;
        unsigned int modifiers[] = {0, LockMask, numlockmask, numlockmask | LockMask};
        KeyCode code;

        XUngrabKey(dpy, AnyKey, AnyModifier, root);
        for (i = 0; i < LENGTH(keys); i++)
            if ((code = XKeysymToKeycode(dpy, keys[i].keysym)))
                for (j = 0; j < LENGTH(modifiers); j++)
                    XGrabKey(dpy, code, keys[i].mod | modifiers[j], root, True, GrabModeAsync, GrabModeAsync);
    }
}

void incnmaster(const Arg *arg) {
    selmon->nmaster = MAX(selmon->nmaster + arg->i, 0);
    arrange(selmon);
}

static int isuniquegeom(XineramaScreenInfo *unique, size_t n, XineramaScreenInfo *info) {
    while (n--)
        if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org && unique[n].width == info->width
            && unique[n].height == info->height)
            return 0;
    return 1;
}

void keypress(XEvent *e) {
    unsigned int i;
    KeySym keysym;
    XKeyEvent *ev;

    ev = &e->xkey;
    keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
    for (i = 0; i < LENGTH(keys); i++)
        if (keysym == keys[i].keysym && CLEANMASK(keys[i].mod) == CLEANMASK(ev->state) && keys[i].func) keys[i].func(&(keys[i].arg));
}

void killclient(const Arg *arg) {
    if (!selmon->sel) return;
    if (!sendevent(selmon->sel, wmatom[WMDelete])) {
        XGrabServer(dpy);
        XSetErrorHandler(xerrordummy);
        XSetCloseDownMode(dpy, DestroyAll);
        XKillClient(dpy, selmon->sel->win);
        XSync(dpy, False);
        XSetErrorHandler(xerror);
        XUngrabServer(dpy);
    }
}

void manage(Window w, XWindowAttributes *wa) {
    Client *c, *t = NULL;
    Window trans = None;
    XWindowChanges wc;

    c = ecalloc(1, sizeof(Client));
    c->win = w;
    /* geometry */
    c->x = c->oldx = wa->x;
    c->y = c->oldy = wa->y;
    c->w = c->oldw = wa->width;
    c->h = c->oldh = wa->height;
    c->oldbw = wa->border_width;

    updatetitle(c);
    if (XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans))) {
        c->mon = t->mon;
        c->tags = t->tags;
    } else {
        c->mon = selmon;
        applyrules(c);
    }

    if (c->x + WIDTH(c) > c->mon->mx + c->mon->mw) c->x = c->mon->mx + c->mon->mw - WIDTH(c);
    if (c->y + HEIGHT(c) > c->mon->my + c->mon->mh) c->y = c->mon->my + c->mon->mh - HEIGHT(c);
    c->x = MAX(c->x, c->mon->mx);
    /* only fix client y-offset, if the client center might cover the bar */
    c->y = MAX(c->y,
               ((c->mon->by == c->mon->my) && (c->x + (c->w / 2) >= c->mon->wx) && (c->x + (c->w / 2) < c->mon->wx + c->mon->ww))
                       ? bh
                       : c->mon->my);
    c->bw = borderpx;

    wc.border_width = c->bw;
    XConfigureWindow(dpy, w, CWBorderWidth, &wc);
    configure(c); /* propagates border_width, if size doesn't change */
    updatewindowtype(c);
    updatesizehints(c);
    updatewmhints(c);
    XSelectInput(dpy, w, EnterWindowMask | FocusChangeMask | PropertyChangeMask | StructureNotifyMask);
    grabbuttons(c, 0);
    if (!c->isfloating) c->isfloating = c->oldstate = trans != None || c->isfixed;
    if (c->isfloating) XRaiseWindow(dpy, c->win);
    attach(c);
    attachstack(c);
    XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend, (unsigned char *)&(c->win), 1);
    XMoveResizeWindow(dpy, c->win, c->x + 2 * sw, c->y, c->w, c->h); /* some windows require this */
    setclientstate(c, NormalState);
    if (c->mon == selmon) unfocus(selmon->sel, 0);
    c->mon->sel = c;
    arrange(c->mon);
    XMapWindow(dpy, c->win);
    focus(NULL);
}

void managealtbar(Window win, XWindowAttributes *wa) {
    Monitor *m;
    if (!(m = recttomon(wa->x, wa->y, wa->width, wa->height))) return;

    m->barwin = win;
    m->by = wa->y;
    bh = m->bh = wa->height;
    updatebarpos(m);
    arrange(m);
    XSelectInput(dpy, win, EnterWindowMask | FocusChangeMask | PropertyChangeMask | StructureNotifyMask);
    XMoveResizeWindow(dpy, win, wa->x, wa->y, wa->width, wa->height);
    XMapWindow(dpy, win);
    XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend, (unsigned char *)&win, 1);
}

void mappingnotify(XEvent *e) {
    XMappingEvent *ev = &e->xmapping;

    XRefreshKeyboardMapping(ev);
    if (ev->request == MappingKeyboard) grabkeys();
}

void maprequest(XEvent *e) {
    static XWindowAttributes wa;
    XMapRequestEvent *ev = &e->xmaprequest;

    if (!XGetWindowAttributes(dpy, ev->window, &wa)) return;
    if (wa.override_redirect) return;
    if (wmclasscontains(ev->window, altbarclass, ""))
        managealtbar(ev->window, &wa);
    else if (!wintoclient(ev->window))
        manage(ev->window, &wa);
}

void motionnotify(XEvent *e) {
    static Monitor *mon = NULL;
    Monitor *m;
    XMotionEvent *ev = &e->xmotion;

    if (ev->window != root) return;
    if ((m = recttomon(ev->x_root, ev->y_root, 1, 1)) != mon && mon) {
        unfocus(selmon->sel, 1);
        selmon = m;
        focus(NULL);
    }
    mon = m;
}

void movemouse(const Arg *arg) {
    int x, y, ocx, ocy, nx, ny;
    Client *c;
    Monitor *m;
    XEvent ev;
    Time lasttime = 0;

    if (!(c = selmon->sel)) return;
    if (c->isfullscreen) /* no support moving fullscreen windows by mouse */
        return;
    restack(selmon);
    ocx = c->x;
    ocy = c->y;
    if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync, None, cursor[CurMove]->cursor, CurrentTime) != GrabSuccess)
        return;
    if (!getrootptr(&x, &y)) return;
    do {
        XMaskEvent(dpy, MOUSEMASK | ExposureMask | SubstructureRedirectMask, &ev);
        switch (ev.type) {
        case ConfigureRequest:
        case Expose:
        case MapRequest:
            handler[ev.type](&ev);
            break;
        case MotionNotify:
            if ((ev.xmotion.time - lasttime) <= (1000 / 60)) continue;
            lasttime = ev.xmotion.time;

            nx = ocx + (ev.xmotion.x - x);
            ny = ocy + (ev.xmotion.y - y);
            if (abs(selmon->wx - nx) < snap)
                nx = selmon->wx;
            else if (abs((selmon->wx + selmon->ww) - (nx + WIDTH(c))) < snap)
                nx = selmon->wx + selmon->ww - WIDTH(c);
            if (abs(selmon->wy - ny) < snap)
                ny = selmon->wy;
            else if (abs((selmon->wy + selmon->wh) - (ny + HEIGHT(c))) < snap)
                ny = selmon->wy + selmon->wh - HEIGHT(c);
            if (!c->isfloating && (abs(nx - c->x) > snap || abs(ny - c->y) > snap))
                togglefloating(NULL);
            if (c->isfloating) resize(c, nx, ny, c->w, c->h, 1);
            break;
        }
    } while (ev.type != ButtonRelease);
    XUngrabPointer(dpy, CurrentTime);
    if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
        sendmon(c, m);
        selmon = m;
        focus(NULL);
    }
}

Client *nexttiled(Client *c) {
    for (; c && (c->isfloating || !ISVISIBLE(c)); c = c->next)
        ;
    return c;
}

void pop(Client *c) {
    detach(c);
    attach(c);
    focus(c);
    arrange(c->mon);
}

void propertynotify(XEvent *e) {
    Client *c;
    Window trans;
    XPropertyEvent *ev = &e->xproperty;

    if (ev->state == PropertyDelete)
        return; /* ignore */
    else if ((c = wintoclient(ev->window))) {
        switch (ev->atom) {
        default:
            break;
        case XA_WM_TRANSIENT_FOR:
            if (!c->isfloating && (XGetTransientForHint(dpy, c->win, &trans)) && (c->isfloating = (wintoclient(trans)) != NULL))
                arrange(c->mon);
            break;
        case XA_WM_NORMAL_HINTS:
            updatesizehints(c);
            break;
        case XA_WM_HINTS:
            updatewmhints(c);
            break;
        }
        if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName]) {
            updatetitle(c);
        }
        if (ev->atom == netatom[NetWMWindowType]) updatewindowtype(c);
    }
}

void quit(const Arg *arg) { running = 0; }

Monitor *recttomon(int x, int y, int w, int h) {
    Monitor *m, *r = selmon;
    int a, area = 0;

    for (m = mons; m; m = m->next)
        if ((a = INTERSECT(x, y, w, h, m)) > area) {
            area = a;
            r = m;
        }
    return r;
}

void resize(Client *c, int x, int y, int w, int h, int interact) {
    if (applysizehints(c, &x, &y, &w, &h, interact)) resizeclient(c, x, y, w, h);
}

void resizeclient(Client *c, int x, int y, int w, int h) {
    XWindowChanges wc;

    c->oldx = c->x;
    c->x = wc.x = x;
    c->oldy = c->y;
    c->y = wc.y = y;
    c->oldw = c->w;
    c->w = wc.width = w;
    c->oldh = c->h;
    c->h = wc.height = h;
    wc.border_width = c->bw;
    XConfigureWindow(dpy, c->win, CWX | CWY | CWWidth | CWHeight | CWBorderWidth, &wc);
    configure(c);
    XSync(dpy, False);
}

void resizemouse(const Arg *arg) {
    int ocx, ocy, nw, nh;
    Client *c;
    Monitor *m;
    XEvent ev;
    Time lasttime = 0;

    if (!(c = selmon->sel)) return;
    if (c->isfullscreen) /* no support resizing fullscreen windows by mouse */
        return;
    restack(selmon);
    ocx = c->x;
    ocy = c->y;
    if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync, None, cursor[CurResize]->cursor, CurrentTime)
        != GrabSuccess)
        return;
    XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
    do {
        XMaskEvent(dpy, MOUSEMASK | ExposureMask | SubstructureRedirectMask, &ev);
        switch (ev.type) {
        case ConfigureRequest:
        case Expose:
        case MapRequest:
            handler[ev.type](&ev);
            break;
        case MotionNotify:
            if ((ev.xmotion.time - lasttime) <= (1000 / 60)) continue;
            lasttime = ev.xmotion.time;

            nw = MAX(ev.xmotion.x - ocx - 2 * c->bw + 1, 1);
            nh = MAX(ev.xmotion.y - ocy - 2 * c->bw + 1, 1);
            if (c->mon->wx + nw >= selmon->wx && c->mon->wx + nw <= selmon->wx + selmon->ww && c->mon->wy + nh >= selmon->wy
                && c->mon->wy + nh <= selmon->wy + selmon->wh) {
                if (!c->isfloating && (abs(nw - c->w) > snap || abs(nh - c->h) > snap))
                    togglefloating(NULL);
            }
            if (c->isfloating) resize(c, c->x, c->y, nw, nh, 1);
            break;
        }
    } while (ev.type != ButtonRelease);
    XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
    XUngrabPointer(dpy, CurrentTime);
    while (XCheckMaskEvent(dpy, EnterWindowMask, &ev))
        ;
    if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
        sendmon(c, m);
        selmon = m;
        focus(NULL);
    }
}

void restack(Monitor *m) {
    XEvent ev;

    if (!m->sel) return;
    if (m->sel->isfloating) XRaiseWindow(dpy, m->sel->win);
    XSync(dpy, False);
    while (XCheckMaskEvent(dpy, EnterWindowMask, &ev))
        ;
}

void run() {
    XEvent ev;

    XSync(dpy, False);
	while (running && !XNextEvent(dpy, &ev))
	    if (handler[ev.type])
			handler[ev.type](&ev); /* call handler */
}

void runautostart() {
    char const *system_config = "/etc/dwm/autostart.sh";

    if (access(system_config, F_OK) != -1) system(system_config);

    char *home = getenv("HOME");
    char const *user_config_suffix = "/.config/dwm";
    char *const user_config = calloc(strlen(home) + strlen(user_config_suffix) + 256, sizeof(char));
    sprintf(user_config, "%s%s", home, user_config_suffix);

    DIR *d;
    struct dirent *dir_file;

    d = opendir(user_config);

    if (d) {
        while ((dir_file = readdir(d)) != NULL) {
            if (dir_file->d_type == DT_REG) {
                sprintf(user_config, "%s%s/%s", home, user_config_suffix, dir_file->d_name);
                system(user_config);
            }
        }
        closedir(d);
    }

    free(user_config);
}

void scan() {
    unsigned int i, num;
    Window d1, d2, *wins = NULL;
    XWindowAttributes wa;

    if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
        for (i = 0; i < num; i++) {
            if (!XGetWindowAttributes(dpy, wins[i], &wa) || wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1)) continue;
            if (wmclasscontains(wins[i], altbarclass, ""))
                managealtbar(wins[i], &wa);
            else if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)
                manage(wins[i], &wa);
        }
        for (i = 0; i < num; i++) { /* now the transients */
            if (!XGetWindowAttributes(dpy, wins[i], &wa)) continue;
            if (XGetTransientForHint(dpy, wins[i], &d1) && (wa.map_state == IsViewable || getstate(wins[i]) == IconicState))
                manage(wins[i], &wa);
        }
        if (wins) XFree(wins);
    }
}

void sendmon(Client *c, Monitor *m) {
    if (c->mon == m) return;
    unfocus(c, 1);
    detach(c);
    detachstack(c);
    c->mon = m;
    c->tags = m->tagset[m->seltags]; /* assign tags of target monitor */
    attach(c);
    attachstack(c);
    focus(NULL);
    arrange(NULL);
}

void setclientstate(Client *c, long state) {
    long data[] = {state, None};

    XChangeProperty(dpy, c->win, wmatom[WMState], wmatom[WMState], 32, PropModeReplace, (unsigned char *)data, 2);
}

int sendevent(Client *c, Atom proto) {
    int n;
    Atom *protocols;
    int exists = 0;
    XEvent ev;

    if (XGetWMProtocols(dpy, c->win, &protocols, &n)) {
        while (!exists && n--) exists = protocols[n] == proto;
        XFree(protocols);
    }
    if (exists) {
        ev.type = ClientMessage;
        ev.xclient.window = c->win;
        ev.xclient.message_type = wmatom[WMProtocols];
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = proto;
        ev.xclient.data.l[1] = CurrentTime;
        XSendEvent(dpy, c->win, False, NoEventMask, &ev);
    }
    return exists;
}

void setfocus(Client *c) {
    if (!c->neverfocus) {
        XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
        XChangeProperty(dpy, root, netatom[NetActiveWindow], XA_WINDOW, 32, PropModeReplace, (unsigned char *)&(c->win), 1);
    }
    sendevent(c, wmatom[WMTakeFocus]);
}

void setfullscreen(Client *c, int fullscreen) {
    if (fullscreen && !c->isfullscreen) {
        XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32, PropModeReplace, (unsigned char *)&netatom[NetWMFullscreen], 1);
        c->isfullscreen = 1;
        c->oldstate = c->isfloating;
        c->oldbw = c->bw;
        c->bw = 0;
        c->isfloating = 1;
        resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
        XRaiseWindow(dpy, c->win);
    } else if (!fullscreen && c->isfullscreen) {
        XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32, PropModeReplace, (unsigned char *)0, 0);
        c->isfullscreen = 0;
        c->isfloating = c->oldstate;
        c->bw = c->oldbw;
        c->x = c->oldx;
        c->y = c->oldy;
        c->w = c->oldw;
        c->h = c->oldh;
        resizeclient(c, c->x, c->y, c->w, c->h);
        arrange(c->mon);
    }
}

/* arg > 1.0 will set mfact absolutely */
void setmfact(const Arg *arg) {
    float f;

    if (!arg) return;
    f = arg->f < 1.0 ? arg->f + selmon->mfact : arg->f - 1.0;
    if (f < 0.05 || f > 0.95) return;
    selmon->mfact = f;
    arrange(selmon);
}

void setup() {
    XSetWindowAttributes wa;
    Atom utf8string;

    /* clean up any zombies immediately */
    sigchld(0);

    /* init screen */
    screen = DefaultScreen(dpy);
    sw = DisplayWidth(dpy, screen);
    sh = DisplayHeight(dpy, screen);
    root = RootWindow(dpy, screen);
    drw = drw_create(dpy, screen, root, sw, sh);
    updategeom();
    /* init atoms */
    utf8string = XInternAtom(dpy, "UTF8_STRING", False);
    wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
    wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
    wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
    netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
    netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
    netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
    netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
    netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
    /* init cursors */
    cursor[CurNormal] = drw_cur_create(drw, XC_left_ptr);
    cursor[CurResize] = drw_cur_create(drw, XC_sizing);
    cursor[CurMove] = drw_cur_create(drw, XC_fleur);
    /* supporting window for NetWMCheck */
    wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
    XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32, PropModeReplace, (unsigned char *)&wmcheckwin, 1);
    XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8, PropModeReplace, (unsigned char *)"dwm", 3);
    XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32, PropModeReplace, (unsigned char *)&wmcheckwin, 1);
    /* EWMH support per view */
    XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32, PropModeReplace, (unsigned char *)netatom, NetLast);
    XDeleteProperty(dpy, root, netatom[NetClientList]);
    /* select events */
    wa.cursor = cursor[CurNormal]->cursor;
    wa.event_mask = SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask | PointerMotionMask | EnterWindowMask
            | LeaveWindowMask | StructureNotifyMask | PropertyChangeMask;
    XChangeWindowAttributes(dpy, root, CWEventMask | CWCursor, &wa);
    XSelectInput(dpy, root, wa.event_mask);
    grabkeys();
    focus(NULL);
}

void seturgent(Client *c, int urg) {
    XWMHints *wmh;

    c->isurgent = urg;
    if (!(wmh = XGetWMHints(dpy, c->win))) return;
    wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
    XSetWMHints(dpy, c->win, wmh);
    XFree(wmh);
}

void showhide(Client *c) {
    if (!c) return;
    if (ISVISIBLE(c)) {
        /* show clients top down */
        XMoveWindow(dpy, c->win, c->x, c->y);
        if (c->isfloating && !c->isfullscreen) resize(c, c->x, c->y, c->w, c->h, 0);
        showhide(c->snext);
    } else {
        /* hide clients bottom up */
        showhide(c->snext);
        XMoveWindow(dpy, c->win, WIDTH(c) * -2, c->y);
    }
}

void sigchld(int unused) {
    if (signal(SIGCHLD, sigchld) == SIG_ERR) die("can't install SIGCHLD handler:");
    while (0 < waitpid(-1, NULL, WNOHANG))
        ;
}

void spawn(const Arg *arg) {
    if (fork() == 0) {
        if (dpy) close(ConnectionNumber(dpy));
        setsid();
        execvp(((char **)arg->v)[0], (char **)arg->v);
        fprintf(stderr, "dwm: execvp %s", ((char **)arg->v)[0]);
        perror(" failed");
        exit(EXIT_SUCCESS);
    }
}

void tag(const Arg *arg) {
    if (selmon->sel && arg->ui & TAGMASK) {
        selmon->sel->tags = arg->ui & TAGMASK;
        focus(NULL);
        arrange(selmon);
    }
}

void tagmon(const Arg *arg) {
    if (!selmon->sel || !mons->next) return;
    sendmon(selmon->sel, dirtomon(arg->i));
}

void tile(Monitor *m) {
    unsigned int i, n, h, mw, my, ty;
    Client *c;

    for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++)
        ;
    if (n == 0) return;

    if (n > m->nmaster)
        mw = m->nmaster ? m->ww * m->mfact : 0;
    else
        mw = m->ww - m->gappx;
    for (i = 0, my = ty = m->gappx, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++)
        if (i < m->nmaster) {
            h = (m->wh - my) / (MIN(n, m->nmaster) - i) - m->gappx;
            resize(c, m->wx + m->gappx, m->wy + my, mw - (2 * c->bw) - m->gappx, h - (2 * c->bw), 0);
            if (my + HEIGHT(c) < m->wh) my += HEIGHT(c) + m->gappx;
        } else {
            h = (m->wh - ty) / (n - i) - m->gappx;
            resize(c, m->wx + mw + m->gappx, m->wy + ty, m->ww - mw - (2 * c->bw) - 2 * m->gappx, h - (2 * c->bw), 0);
            if (ty + HEIGHT(c) < m->wh) ty += HEIGHT(c) + m->gappx;
        }
}

void togglefloating(const Arg *arg) {
    if (!selmon->sel) return;
    if (selmon->sel->isfullscreen) /* no support for fullscreen windows */
        return;
    selmon->sel->isfloating = !selmon->sel->isfloating || selmon->sel->isfixed;
    if (selmon->sel->isfloating) resize(selmon->sel, selmon->sel->x, selmon->sel->y, selmon->sel->w, selmon->sel->h, 0);
    arrange(selmon);
}

void togglefullscr(const Arg *arg) {
    if (selmon->sel) setfullscreen(selmon->sel, !selmon->sel->isfullscreen);
}

void toggletag(const Arg *arg) {
    unsigned int newtags;

    if (!selmon->sel) return;
    newtags = selmon->sel->tags ^ (arg->ui & TAGMASK);
    if (newtags) {
        selmon->sel->tags = newtags;
        focus(NULL);
        arrange(selmon);
    }
}

void toggleview(const Arg *arg) {
    unsigned int newtagset = selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);

    if (newtagset) {
        selmon->tagset[selmon->seltags] = newtagset;
        focus(NULL);
        arrange(selmon);
    }
}

void unfocus(Client *c, int setfocus) {
    if (!c) return;
    grabbuttons(c, 0);
    if (setfocus) {
        XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
        XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
    }
}

void unmanage(Client *c, int destroyed) {
    Monitor *m = c->mon;
    XWindowChanges wc;

    detach(c);
    detachstack(c);
    if (!destroyed) {
        wc.border_width = c->oldbw;
        XGrabServer(dpy); /* avoid race conditions */
        XSetErrorHandler(xerrordummy);
        XConfigureWindow(dpy, c->win, CWBorderWidth, &wc); /* restore border */
        XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
        setclientstate(c, WithdrawnState);
        XSync(dpy, False);
        XSetErrorHandler(xerror);
        XUngrabServer(dpy);
    }
    free(c);
    focus(NULL);
    updateclientlist();
    arrange(m);
}

void unmanagealtbar(Window w) {
    Monitor *m = wintomon(w);

    if (!m) return;

    m->barwin = 0;
    m->by = 0;
    m->bh = 0;
    updatebarpos(m);
    arrange(m);
}

void unmanagetray(Window w) {
    Monitor *m = wintomon(w);

    if (!m) return;

    m->traywin = 0;
    updatebarpos(m);
    arrange(m);
}

void unmapnotify(XEvent *e) {
    Client *c;
    Monitor *m;
    XUnmapEvent *ev = &e->xunmap;

    if ((c = wintoclient(ev->window))) {
        if (ev->send_event)
            setclientstate(c, WithdrawnState);
        else
            unmanage(c, 0);
    } else if ((m = wintomon(ev->window)) && m->barwin == ev->window)
        unmanagealtbar(ev->window);
    else if (m->traywin == ev->window)
        unmanagetray(ev->window);
}

void updatebarpos(Monitor *m) {
    m->wy = m->my;
    m->wh = m->mh;
    m->wh -= m->bh;
    m->by = m->wy;
    m->wy = m->wy + m->bh;
}

void updateclientlist() {
    Client *c;
    Monitor *m;

    XDeleteProperty(dpy, root, netatom[NetClientList]);
    for (m = mons; m; m = m->next)
        for (c = m->clients; c; c = c->next)
            XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend, (unsigned char *)&(c->win), 1);
}

int updategeom() {
    int dirty = 0;

    if (XineramaIsActive(dpy)) {
        int i, j, n, nn;
        Client *c;
        Monitor *m;
        XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nn);
        XineramaScreenInfo *unique = NULL;

        for (n = 0, m = mons; m; m = m->next, n++)
            ;
        /* only consider unique geometries as separate screens */
        unique = ecalloc(nn, sizeof(XineramaScreenInfo));
        for (i = 0, j = 0; i < nn; i++)
            if (isuniquegeom(unique, j, &info[i])) memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
        XFree(info);
        nn = j;
        if (n <= nn) { /* new monitors available */
            for (i = 0; i < (nn - n); i++) {
                for (m = mons; m && m->next; m = m->next)
                    ;
                if (m)
                    m->next = createmon();
                else
                    mons = createmon();
            }
            for (i = 0, m = mons; i < nn && m; m = m->next, i++)
                if (i >= n || unique[i].x_org != m->mx || unique[i].y_org != m->my || unique[i].width != m->mw
                    || unique[i].height != m->mh) {
                    dirty = 1;
                    m->num = i;
                    m->mx = m->wx = unique[i].x_org;
                    m->my = m->wy = unique[i].y_org;
                    m->mw = m->ww = unique[i].width;
                    m->mh = m->wh = unique[i].height;
                    updatebarpos(m);
                }
        } else { /* less monitors available nn < n */
            for (i = nn; i < n; i++) {
                for (m = mons; m && m->next; m = m->next)
                    ;
                while ((c = m->clients)) {
                    dirty = 1;
                    m->clients = c->next;
                    detachstack(c);
                    c->mon = mons;
                    attach(c);
                    attachstack(c);
                }
                if (m == selmon) selmon = mons;
                cleanupmon(m);
            }
        }
        free(unique);
    } else {  /* default monitor setup */
        if (!mons) mons = createmon();
        if (mons->mw != sw || mons->mh != sh) {
            dirty = 1;
            mons->mw = mons->ww = sw;
            mons->mh = mons->wh = sh;
            updatebarpos(mons);
        }
    }
    if (dirty) {
        selmon = mons;
        selmon = wintomon(root);
    }
    return dirty;
}

void updatenumlockmask() {
    unsigned int i, j;
    XModifierKeymap *modmap;

    numlockmask = 0;
    modmap = XGetModifierMapping(dpy);
    for (i = 0; i < 8; i++)
        for (j = 0; j < modmap->max_keypermod; j++)
            if (modmap->modifiermap[i * modmap->max_keypermod + j] == XKeysymToKeycode(dpy, XK_Num_Lock)) numlockmask = (1 << i);
    XFreeModifiermap(modmap);
}

void updatesizehints(Client *c) {
    long msize;
    XSizeHints size;

    if (!XGetWMNormalHints(dpy, c->win, &size, &msize)) /* size is uninitialized, ensure that size.flags aren't used */
        size.flags = PSize;
    if (size.flags & PBaseSize) {
        c->basew = size.base_width;
        c->baseh = size.base_height;
    } else if (size.flags & PMinSize) {
        c->basew = size.min_width;
        c->baseh = size.min_height;
    } else
        c->basew = c->baseh = 0;
    if (size.flags & PResizeInc) {
        c->incw = size.width_inc;
        c->inch = size.height_inc;
    } else
        c->incw = c->inch = 0;
    if (size.flags & PMaxSize) {
        c->maxw = size.max_width;
        c->maxh = size.max_height;
    } else
        c->maxw = c->maxh = 0;
    if (size.flags & PMinSize) {
        c->minw = size.min_width;
        c->minh = size.min_height;
    } else if (size.flags & PBaseSize) {
        c->minw = size.base_width;
        c->minh = size.base_height;
    } else
        c->minw = c->minh = 0;
    if (size.flags & PAspect) {
        c->mina = (float)size.min_aspect.y / size.min_aspect.x;
        c->maxa = (float)size.max_aspect.x / size.max_aspect.y;
    } else
        c->maxa = c->mina = 0.0;
    c->isfixed = (c->maxw && c->maxh && c->maxw == c->minw && c->maxh == c->minh);
}

void updatetitle(Client *c) {
    if (!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name)) gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
    if (c->name[0] == '\0') /* hack to mark broken clients */
        strcpy(c->name, broken);
}

void updatewindowtype(Client *c) {
    Atom state = getatomprop(c, netatom[NetWMState]);
    Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

    if (state == netatom[NetWMFullscreen]) setfullscreen(c, 1);
    if (wtype == netatom[NetWMWindowTypeDialog]) c->isfloating = 1;
}

void updatewmhints(Client *c) {
    XWMHints *wmh;

    if ((wmh = XGetWMHints(dpy, c->win))) {
        if (c == selmon->sel && wmh->flags & XUrgencyHint) {
            wmh->flags &= ~XUrgencyHint;
            XSetWMHints(dpy, c->win, wmh);
        } else
            c->isurgent = (wmh->flags & XUrgencyHint) ? 1 : 0;
        if (wmh->flags & InputHint)
            c->neverfocus = !wmh->input;
        else
            c->neverfocus = 0;
        XFree(wmh);
    }
}

void view(const Arg *arg) {
    if ((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags]) return;
    selmon->seltags ^= 1; /* toggle sel tagset */
    if (arg->ui & TAGMASK) selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;
    focus(NULL);
    arrange(selmon);
}

Client *wintoclient(Window w) {
    Client *c;
    Monitor *m;

    for (m = mons; m; m = m->next)
        for (c = m->clients; c; c = c->next)
            if (c->win == w) return c;
    return NULL;
}

Monitor *wintomon(Window w) {
    int x, y;
    Client *c;
    Monitor *m;

    if (w == root && getrootptr(&x, &y)) return recttomon(x, y, 1, 1);
    for (m = mons; m; m = m->next)
        if (w == m->barwin || w == m->traywin) return m;
    if ((c = wintoclient(w))) return c->mon;
    return selmon;
}

int wmclasscontains(Window win, const char *class, const char *name) {
    XClassHint ch = {NULL, NULL};
    int res = 1;

    if (XGetClassHint(dpy, win, &ch)) {
        if (ch.res_name && strstr(ch.res_name, name) == NULL) res = 0;
        if (ch.res_class && strstr(ch.res_class, class) == NULL) res = 0;
    } else
        res = 0;

    if (ch.res_class) XFree(ch.res_class);
    if (ch.res_name) XFree(ch.res_name);

    return res;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
int xerror(Display *dpy, XErrorEvent *ee) {
    if (ee->error_code == BadWindow || (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
        || (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
        || (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
        || (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
        || (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
        || (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
        || (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
        || (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
        return 0;
    fprintf(stderr, "dwm: fatal error: request code=%d, error code=%d\n", ee->request_code, ee->error_code);
    return xerrorxlib(dpy, ee); /* may call exit */
}

int xerrordummy(Display *dpy, XErrorEvent *ee) { return 0; }

/* Startup Error handler to check if another window manager
 * is already running. */
int xerrorstart(Display *dpy, XErrorEvent *ee) {
    die("dwm: another window manager is already running");
    return -1;
}

void zoom(const Arg *arg) {
    Client *c = selmon->sel;

    if (selmon->sel && selmon->sel->isfloating) return;
    if (c == nexttiled(selmon->clients))
        if (!c || !(c = nexttiled(c->next))) return;
    pop(c);
}

int main(int argc, char *argv[]) {
    if (argc == 2 && !strcmp("-v", argv[1]))
        die("dwm-" VERSION);
    else if (argc != 1)
        die("usage: dwm [-v]");
    if (!setlocale(LC_CTYPE, "") || !XSupportsLocale()) fputs("warning: no locale support\n", stderr);
    if (!(dpy = XOpenDisplay(NULL))) die("dwm: cannot open display");
    checkotherwm();
    setup();
    scan();
    runautostart();
    run();
    cleanup();
    XCloseDisplay(dpy);
    return EXIT_SUCCESS;
}
