/*
 * Copyright © 2005 Novell, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of
 * Novell, Inc. not be used in advertising or publicity pertaining to
 * distribution of the software without specific, written prior permission.
 * Novell, Inc. makes no representations about the suitability of this
 * software for any purpose. It is provided "as is" without express or
 * implied warranty.
 *
 * NOVELL, INC. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
 * NO EVENT SHALL NOVELL, INC. BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author: David Reveman <davidr@novell.com>
 */

#include <X11/Xatom.h>

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <compiz.h>

#define MIN_SPEED_DEFAULT   1.5f
#define MIN_SPEED_MIN       0.1f
#define MIN_SPEED_MAX       50.0f
#define MIN_SPEED_PRECISION 0.1f

#define MIN_TIMESTEP_DEFAULT   0.5f
#define MIN_TIMESTEP_MIN       0.1f
#define MIN_TIMESTEP_MAX       50.0f
#define MIN_TIMESTEP_PRECISION 0.1f

static char *winType[] = {
    "Toolbar",
    "Utility",
    "Dialog",
    "Normal"
};
#define N_WIN_TYPE (sizeof (winType) / sizeof (winType[0]))

static int displayPrivateIndex;

typedef struct _MinDisplay {
    int		    screenPrivateIndex;
    HandleEventProc handleEvent;
    Atom	    winChangeStateAtom;
    Atom	    winIconGeometryAtom;
} MinDisplay;

#define MIN_SCREEN_OPTION_SPEED       0
#define MIN_SCREEN_OPTION_TIMESTEP    1
#define MIN_SCREEN_OPTION_WINDOW_TYPE 2
#define MIN_SCREEN_OPTION_NUM         3

typedef struct _MinScreen {
    int	windowPrivateIndex;

    CompOption opt[MIN_SCREEN_OPTION_NUM];

    PreparePaintScreenProc preparePaintScreen;
    DonePaintScreenProc    donePaintScreen;
    PaintScreenProc        paintScreen;
    PaintWindowProc        paintWindow;
    DamageWindowRectProc   damageWindowRect;
    FocusWindowProc	   focusWindow;

    float speed;
    float timestep;

    unsigned int wMask;

    int moreAdjust;
} MinScreen;

typedef struct _MinWindow {
    GLfloat xVelocity, yVelocity, xScaleVelocity, yScaleVelocity;
    GLfloat xScale, yScale;
    GLfloat tx, ty;

    Bool adjust;

    XRectangle icon;

    int state, newState;

    int unmapCnt;
} MinWindow;

#define GET_MIN_DISPLAY(d)				    \
    ((MinDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define MIN_DISPLAY(d)			 \
    MinDisplay *md = GET_MIN_DISPLAY (d)

#define GET_MIN_SCREEN(s, md)					\
    ((MinScreen *) (s)->privates[(md)->screenPrivateIndex].ptr)

#define MIN_SCREEN(s)						     \
    MinScreen *ms = GET_MIN_SCREEN (s, GET_MIN_DISPLAY (s->display))

#define GET_MIN_WINDOW(w, ms)				        \
    ((MinWindow *) (w)->privates[(ms)->windowPrivateIndex].ptr)

#define MIN_WINDOW(w)					   \
    MinWindow *mw = GET_MIN_WINDOW  (w,			   \
		    GET_MIN_SCREEN  (w->screen,		   \
		    GET_MIN_DISPLAY (w->screen->display)))

#define NUM_OPTIONS(s) (sizeof ((s)->opt) / sizeof (CompOption))

static CompOption *
minGetScreenOptions (CompScreen *screen,
		     int	*count)
{
    MIN_SCREEN (screen);

    *count = NUM_OPTIONS (ms);
    return ms->opt;
}

static Bool
minSetScreenOption (CompScreen      *screen,
		    char	    *name,
		    CompOptionValue *value)
{
    CompOption *o;
    int	       index;

    MIN_SCREEN (screen);

    o = compFindOption (ms->opt, NUM_OPTIONS (ms), name, &index);
    if (!o)
	return FALSE;

    switch (index) {
    case MIN_SCREEN_OPTION_SPEED:
	if (compSetFloatOption (o, value))
	{
	    ms->speed = o->value.f;
	    return TRUE;
	}
	break;
    case MIN_SCREEN_OPTION_TIMESTEP:
	if (compSetFloatOption (o, value))
	{
	    ms->timestep = o->value.f;
	    return TRUE;
	}
	break;
    case MIN_SCREEN_OPTION_WINDOW_TYPE:
	if (compSetOptionList (o, value))
	{
	    ms->wMask = compWindowTypeMaskFromStringList (&o->value);
	    return TRUE;
	}
    default:
	break;
    }

    return FALSE;
}

static void
minScreenInitOptions (MinScreen *ms)
{
    CompOption *o;
    int	       i;

    o = &ms->opt[MIN_SCREEN_OPTION_SPEED];
    o->name		= "speed";
    o->shortDesc	= "Speed";
    o->longDesc		= "Minimize speed";
    o->type		= CompOptionTypeFloat;
    o->value.f		= MIN_SPEED_DEFAULT;
    o->rest.f.min	= MIN_SPEED_MIN;
    o->rest.f.max	= MIN_SPEED_MAX;
    o->rest.f.precision = MIN_SPEED_PRECISION;

    o = &ms->opt[MIN_SCREEN_OPTION_TIMESTEP];
    o->name		= "timestep";
    o->shortDesc	= "Timestep";
    o->longDesc		= "Minimize timestep";
    o->type		= CompOptionTypeFloat;
    o->value.f		= MIN_TIMESTEP_DEFAULT;
    o->rest.f.min	= MIN_TIMESTEP_MIN;
    o->rest.f.max	= MIN_TIMESTEP_MAX;
    o->rest.f.precision = MIN_TIMESTEP_PRECISION;

    o = &ms->opt[MIN_SCREEN_OPTION_WINDOW_TYPE];
    o->name	         = "window_types";
    o->shortDesc         = "Window Types";
    o->longDesc	         = "Window types that should be transformed when "
	"minimized";
    o->type	         = CompOptionTypeList;
    o->value.list.type   = CompOptionTypeString;
    o->value.list.nValue = N_WIN_TYPE;
    o->value.list.value  = malloc (sizeof (CompOptionValue) * N_WIN_TYPE);
    for (i = 0; i < N_WIN_TYPE; i++)
	o->value.list.value[i].s = strdup (winType[i]);
    o->rest.s.string     = windowTypeString;
    o->rest.s.nString    = nWindowTypeString;

    ms->wMask = compWindowTypeMaskFromStringList (&o->value);
}

static Bool
minGetWindowIconGeometry (CompWindow *w,
			  XRectangle *rect)
{
    Atom	  actual;
    int		  result, format;
    unsigned long n, left;
    unsigned char *data;

    MIN_DISPLAY (w->screen->display);

    result = XGetWindowProperty (w->screen->display->display, w->id,
				 md->winIconGeometryAtom,
				 0L, 4L, FALSE, XA_CARDINAL, &actual, &format,
				 &n, &left, &data);

    if (result == Success && n && data)
    {
	if (n == 4)
	{
	    unsigned long *geometry = (unsigned long *) data;

	    rect->x	 = geometry[0];
	    rect->y	 = geometry[1];
	    rect->width  = geometry[2];
	    rect->height = geometry[3];

	    XFree (data);

	    return TRUE;
	}

	XFree (data);
    }

    return FALSE;
}

static int
minGetWindowState (CompWindow *w)
{
    Atom	  actual;
    int		  result, format;
    unsigned long n, left;
    unsigned char *data;

    result = XGetWindowProperty (w->screen->display->display, w->id,
				 w->screen->display->wmStateAtom, 0L, 1L, FALSE,
				 w->screen->display->wmStateAtom,
				 &actual, &format, &n, &left, &data);

    if (result == Success && n && data)
    {
	int state;

	memcpy (&state, data, sizeof (int));
	XFree ((void *) data);

	return state;
    }

    return WithdrawnState;
}

static int
adjustMinVelocity (CompWindow *w)
{
    float dx, dy, dxs, dys, adjust, amount;
    float x1, y1, xScale, yScale;

    MIN_WINDOW (w);

    if (mw->newState == IconicState)
    {
	x1 = mw->icon.x;
	y1 = mw->icon.y;
	xScale = (float) mw->icon.width  / w->width;
	yScale = (float) mw->icon.height / w->height;
    }
    else
    {
	x1 = w->serverX;
	y1 = w->serverY;
	xScale = yScale = 1.0f;
    }

    dx = x1 - w->attrib.x;

    adjust = dx * 0.15f;
    amount = fabs (dx) * 1.5f;
    if (amount < 0.5f)
	amount = 0.5f;
    else if (amount > 5.0f)
	amount = 5.0f;

    mw->xVelocity = (amount * mw->xVelocity + adjust) / (amount + 1.0f);

    dy = y1 - w->attrib.y;

    adjust = dy * 0.15f;
    amount = fabs (dy) * 1.5f;
    if (amount < 0.5f)
	amount = 0.5f;
    else if (amount > 5.0f)
	amount = 5.0f;

    mw->yVelocity = (amount * mw->yVelocity + adjust) / (amount + 1.0f);

    dxs = xScale - mw->xScale;

    adjust = dxs * 0.15f;
    amount = fabs (dxs) * 10.0f;
    if (amount < 0.01f)
	amount = 0.01f;
    else if (amount > 0.15f)
	amount = 0.15f;

    mw->xScaleVelocity = (amount * mw->xScaleVelocity + adjust) /
	(amount + 1.0f);

    dys = yScale - mw->yScale;

    adjust = dys * 0.15f;
    amount = fabs (dys) * 10.0f;
    if (amount < 0.01f)
	amount = 0.01f;
    else if (amount > 0.15f)
	amount = 0.15f;

    mw->yScaleVelocity = (amount * mw->yScaleVelocity + adjust) /
	(amount + 1.0f);

    if (fabs (dx) < 0.1f    && fabs (mw->xVelocity)      < 0.2f   &&
	fabs (dy) < 0.1f    && fabs (mw->yVelocity)      < 0.2f   &&
	fabs (dxs) < 0.001f && fabs (mw->xScaleVelocity) < 0.002f &&
	fabs (dys) < 0.001f && fabs (mw->yScaleVelocity) < 0.002f)
    {
	mw->xVelocity = mw->yVelocity = mw->xScaleVelocity =
	    mw->yScaleVelocity = 0.0f;
	mw->tx = x1 - w->attrib.x;
	mw->ty = y1 - w->attrib.y;
	mw->xScale = xScale;
	mw->yScale = yScale;

	return 0;
    }

    return 1;
}

static void
minPreparePaintScreen (CompScreen *s,
		       int	  msSinceLastPaint)
{
    MIN_SCREEN (s);

    if (ms->moreAdjust)
    {
	CompWindow *w;
	int        steps, dx, dy;
	float      amount, chunk;

	amount = msSinceLastPaint * 0.05f * ms->speed;
	steps  = amount / (0.5f * ms->timestep);
	if (!steps) steps = 1;
	chunk  = amount / (float) steps;

	while (steps--)
	{
	    ms->moreAdjust = 0;

	    for (w = s->windows; w; w = w->next)
	    {
		MIN_WINDOW (w);

		if (mw->adjust)
		{
		    mw->adjust = adjustMinVelocity (w);

		    ms->moreAdjust |= mw->adjust;

		    mw->tx += mw->xVelocity * chunk;
		    mw->ty += mw->yVelocity * chunk;
		    mw->xScale += mw->xScaleVelocity * chunk;
		    mw->yScale += mw->yScaleVelocity * chunk;

		    dx = (w->serverX + mw->tx) - w->attrib.x;
		    dy = (w->serverY + mw->ty) - w->attrib.y;

		    moveWindow (w, dx, dy, FALSE);

		    (*s->setWindowScale) (w, mw->xScale, mw->yScale);

		    if (!mw->adjust)
		    {
			mw->state = mw->newState;

			while (mw->unmapCnt)
			{
			    unmapWindow (w);
			    mw->unmapCnt--;
			}
		    }
		}
	    }

	    if (!ms->moreAdjust)
		break;
	}

	if (ms->moreAdjust)
	{
	    for (w = s->windows; w; w = w->next)
	    {
		MIN_WINDOW (w);

		if (mw->adjust)
		    addWindowDamage (w);
	    }
	}
    }

    UNWRAP (ms, s, preparePaintScreen);
    (*s->preparePaintScreen) (s, msSinceLastPaint);
    WRAP (ms, s, preparePaintScreen, minPreparePaintScreen);
}

static void
minDonePaintScreen (CompScreen *s)
{
    MIN_SCREEN (s);

    if (ms->moreAdjust)
    {
	CompWindow *w;

	for (w = s->windows; w; w = w->next)
	{
	    MIN_WINDOW (w);

	    if (mw->adjust)
		addWindowDamage (w);
	}
    }

    UNWRAP (ms, s, donePaintScreen);
    (*s->donePaintScreen) (s);
    WRAP (ms, s, donePaintScreen, minDonePaintScreen);
}

static Bool
minPaintScreen (CompScreen		*s,
		const ScreenPaintAttrib *sAttrib,
		Region		        region,
		unsigned int		mask)
{
    Bool status;

    MIN_SCREEN (s);

    if (ms->moreAdjust)
	mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS_MASK;

    UNWRAP (ms, s, paintScreen);
    status = (*s->paintScreen) (s, sAttrib, region, mask);
    WRAP (ms, s, paintScreen, minPaintScreen);

    return status;
}

static Bool
minPaintWindow (CompWindow		*w,
		const WindowPaintAttrib *attrib,
		Region			region,
		unsigned int		mask)
{
    CompScreen *s = w->screen;
    Bool       status;

    MIN_SCREEN (s);
    MIN_WINDOW (w);

    if (mw->adjust)
	mask |= PAINT_WINDOW_TRANSFORMED_MASK;

    UNWRAP (ms, s, paintWindow);
    status = (*s->paintWindow) (w, attrib, region, mask);
    WRAP (ms, s, paintWindow, minPaintWindow);

    return status;
}

static void
minHandleEvent (CompDisplay *d,
		XEvent      *event)
{
    CompWindow *w;

    MIN_DISPLAY (d);

    switch (event->type) {
    case MapNotify:
	w = findWindowAtDisplay (d, event->xmap.window);
	if (w)
	{
	    MIN_WINDOW (w);

	    if (mw->adjust)
		mw->state = mw->newState;

	    while (mw->unmapCnt)
	    {
		unmapWindow (w);
		mw->unmapCnt--;
	    }
	}
	break;
    case UnmapNotify:
	w = findWindowAtDisplay (d, event->xunmap.window);
	if (w && (w->state & CompWindowStateHiddenMask))
	{
	    MIN_SCREEN (w->screen);

	    if (!w->invisible && (ms->wMask & w->type))
	    {
		MIN_WINDOW (w);

		mw->newState = IconicState;

		if (minGetWindowIconGeometry (w, &mw->icon))
		{
		    mw->adjust     = TRUE;
		    ms->moreAdjust = TRUE;

		    mw->unmapCnt++;
		    w->unmapRefCnt++;

		    addWindowDamage (w);
		}
		else
		{
		    mw->state = mw->newState;
		}
	    }
	}
    default:
	break;
    }

    UNWRAP (md, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (md, d, handleEvent, minHandleEvent);
}

static Bool
minDamageWindowRect (CompWindow *w,
		     Bool	initial,
		     BoxPtr     rect)
{
    Bool status;

    MIN_SCREEN (w->screen);

    if (initial && !w->invisible && (ms->wMask & w->type))
    {
	MIN_WINDOW (w);

	if (mw->state == IconicState)
	{
	    if (minGetWindowIconGeometry (w, &mw->icon))
	    {
		if (!mw->adjust)
		{
		    mw->adjust     = TRUE;
		    ms->moreAdjust = TRUE;

		    mw->tx     = mw->icon.x - w->serverX;
		    mw->ty     = mw->icon.y - w->serverY;
		    mw->xScale = (float) mw->icon.width  / w->width;
		    mw->yScale = (float) mw->icon.height / w->height;

		    moveWindow (w,
				mw->icon.x - w->attrib.x,
				mw->icon.y - w->attrib.y,
				FALSE);

		    (*w->screen->setWindowScale) (w, mw->xScale, mw->yScale);

		    addWindowDamage (w);
		}
	    }
	}

	mw->newState = NormalState;
    }

    UNWRAP (ms, w->screen, damageWindowRect);
    status = (*w->screen->damageWindowRect) (w, initial, rect);
    WRAP (ms, w->screen, damageWindowRect, minDamageWindowRect);

    return status;
}

static Bool
minFocusWindow (CompWindow *w)
{
    Bool status;

    MIN_SCREEN (w->screen);
    MIN_WINDOW (w);

    if (mw->unmapCnt)
	return FALSE;

    UNWRAP (ms, w->screen, focusWindow);
    status = (*w->screen->focusWindow) (w);
    WRAP (ms, w->screen, focusWindow, minFocusWindow);

    return status;
}

static Bool
minInitDisplay (CompPlugin  *p,
		CompDisplay *d)
{
    MinDisplay *md;

    md = malloc (sizeof (MinDisplay));
    if (!md)
	return FALSE;

    md->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (md->screenPrivateIndex < 0)
    {
	free (md);
	return FALSE;
    }

    md->winChangeStateAtom  = XInternAtom (d->display, "WM_CHANGE_STATE", 0);
    md->winIconGeometryAtom =
	XInternAtom (d->display, "_NET_WM_ICON_GEOMETRY", 0);

    WRAP (md, d, handleEvent, minHandleEvent);

    d->privates[displayPrivateIndex].ptr = md;

    return TRUE;
}

static void
minFiniDisplay (CompPlugin  *p,
		CompDisplay *d)
{
    MIN_DISPLAY (d);

    freeScreenPrivateIndex (d, md->screenPrivateIndex);

    UNWRAP (md, d, handleEvent);

    free (md);
}

static Bool
minInitScreen (CompPlugin *p,
	       CompScreen *s)
{
    MinScreen *ms;

    MIN_DISPLAY (s->display);

    ms = malloc (sizeof (MinScreen));
    if (!ms)
	return FALSE;

    ms->windowPrivateIndex = allocateWindowPrivateIndex (s);
    if (ms->windowPrivateIndex < 0)
    {
	free (ms);
	return FALSE;
    }

    ms->moreAdjust = FALSE;

    ms->speed    = MIN_SPEED_DEFAULT;
    ms->timestep = MIN_TIMESTEP_DEFAULT;

    minScreenInitOptions (ms);

    WRAP (ms, s, preparePaintScreen, minPreparePaintScreen);
    WRAP (ms, s, donePaintScreen, minDonePaintScreen);
    WRAP (ms, s, paintScreen, minPaintScreen);
    WRAP (ms, s, paintWindow, minPaintWindow);
    WRAP (ms, s, damageWindowRect, minDamageWindowRect);
    WRAP (ms, s, focusWindow, minFocusWindow);

    s->privates[md->screenPrivateIndex].ptr = ms;

    return TRUE;
}

static void
minFiniScreen (CompPlugin *p,
	       CompScreen *s)
{
    MIN_SCREEN (s);

    freeWindowPrivateIndex (s, ms->windowPrivateIndex);

    UNWRAP (ms, s, preparePaintScreen);
    UNWRAP (ms, s, donePaintScreen);
    UNWRAP (ms, s, paintScreen);
    UNWRAP (ms, s, paintWindow);
    UNWRAP (ms, s, damageWindowRect);
    UNWRAP (ms, s, focusWindow);

    free (ms);
}

static Bool
minInitWindow (CompPlugin *p,
	       CompWindow *w)
{
    MinWindow *mw;

    MIN_SCREEN (w->screen);

    mw = malloc (sizeof (MinWindow));
    if (!mw)
	return FALSE;

    mw->xScale = mw->yScale = 1.0f;
    mw->tx = mw->ty = 0.0f;
    mw->adjust = FALSE;
    mw->xVelocity = mw->yVelocity = 0.0f;
    mw->xScaleVelocity = mw->yScaleVelocity = 1.0f;

    mw->unmapCnt = 0;

    mw->state = mw->newState = minGetWindowState (w);

    w->privates[ms->windowPrivateIndex].ptr = mw;

    return TRUE;
}

static void
minFiniWindow (CompPlugin *p,
	       CompWindow *w)
{
    MIN_WINDOW (w);

    while (mw->unmapCnt--)
	unmapWindow (w);

    free (mw);
}

static Bool
minInit (CompPlugin *p)
{
    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
minFini (CompPlugin *p)
{
    if (displayPrivateIndex >= 0)
	freeDisplayPrivateIndex (displayPrivateIndex);
}

CompPluginDep minDeps[] = {
    { CompPluginRuleBefore, "cube" },
    { CompPluginRuleBefore, "expose" }
};

static CompPluginVTable minVTable = {
    "minimize",
    "Minimize Effect",
    "Transform windows when they are minimized and unminimized",
    minInit,
    minFini,
    minInitDisplay,
    minFiniDisplay,
    minInitScreen,
    minFiniScreen,
    minInitWindow,
    minFiniWindow,
    0, /* GetDisplayOptions */
    0, /* SetDisplayOption */
    minGetScreenOptions,
    minSetScreenOption,
    minDeps,
    sizeof (minDeps) / sizeof (minDeps[0])
};

CompPluginVTable *
getCompPluginInfo (void)
{
    return &minVTable;
}
