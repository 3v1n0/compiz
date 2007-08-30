/*
 * Copyright (C) 2007 Andrew Riedi <andrewriedi@gmail.com>
 *
 * Sticky window handling by Dennis Kasprzyk <onestone@opencompositing.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * This plug-in for Metacity-like workarounds.
 */

#include <string.h>
#include <limits.h>

#include <compiz-core.h>
#include <X11/Xatom.h>
#include "workarounds_options.h"

static int displayPrivateIndex;

typedef struct _WorkaroundsDisplay {
    int screenPrivateIndex;

    HandleEventProc handleEvent;

    Atom roleAtom;
} WorkaroundsDisplay;

typedef struct _WorkaroundsScreen {
    int windowPrivateIndex;

    WindowResizeNotifyProc  windowResizeNotify;
} WorkaroundsScreen;

typedef struct _WorkaroundsWindow {
    CompTimeoutHandle updateHandle;

    Bool adjustedWinType;
    Bool madeSticky;
} WorkaroundsWindow;

#define GET_WORKAROUNDS_DISPLAY(d) \
    ((WorkaroundsDisplay *) (d)->object.privates[displayPrivateIndex].ptr)

#define WORKAROUNDS_DISPLAY(d) \
    WorkaroundsDisplay *wd = GET_WORKAROUNDS_DISPLAY (d)

#define GET_WORKAROUNDS_SCREEN(s, wd) \
    ((WorkaroundsScreen *) (s)->object.privates[(wd)->screenPrivateIndex].ptr)

#define WORKAROUNDS_SCREEN(s) \
    WorkaroundsScreen *ws = GET_WORKAROUNDS_SCREEN (s, \
                            GET_WORKAROUNDS_DISPLAY (s->display))

#define GET_WORKAROUNDS_WINDOW(w, ns) \
    ((WorkaroundsWindow *) (w)->object.privates[(ns)->windowPrivateIndex].ptr)
#define WORKAROUNDS_WINDOW(w) \
    WorkaroundsWindow *ww = GET_WORKAROUNDS_WINDOW  (w, \
		    GET_WORKAROUNDS_SCREEN  (w->screen, \
		    GET_WORKAROUNDS_DISPLAY (w->screen->display)))

static char *
workaroundsGetWindowRoleAtom (CompWindow *w)
{
    CompDisplay *d = w->screen->display;
    Atom type;
    unsigned long nItems;
    unsigned long bytesAfter;
    unsigned char *str = NULL;
    int format, result;
    char *retval;

    WORKAROUNDS_DISPLAY (d);

    result = XGetWindowProperty (d->display, w->id, wd->roleAtom,
                                 0, LONG_MAX, FALSE, XA_STRING,
                                 &type, &format, &nItems, &bytesAfter,
                                 (unsigned char **) &str);

    if (result != Success)
        return NULL;

    if (type != XA_STRING)
    {
        XFree (str);
        return NULL;
    }

    retval = strdup ((char *) str);

    XFree (str);

    return retval;
}

static void
workaroundsRemoveSticky (CompWindow *w)
{
    WORKAROUNDS_WINDOW (w);

    if (w->state & CompWindowStateStickyMask && ww->madeSticky)
	changeWindowState (w, w->state & ~CompWindowStateStickyMask);
    ww->madeSticky = FALSE;
}

static void
workaroundsUpdateSticky (CompWindow *w)
{
    WORKAROUNDS_WINDOW (w);

    CompDisplay *d  = w->screen->display;
    Bool makeSticky = FALSE;

    if (workaroundsGetStickyAlldesktops (d) && w->desktop == 0xffffffff &&
	matchEval (workaroundsGetAlldesktopStickyMatch (d), w))
	makeSticky = TRUE;

    if (makeSticky)
    {
	if (!(w->state & CompWindowStateStickyMask))
	{
	    ww->madeSticky = TRUE;
	    changeWindowState (w, w->state | CompWindowStateStickyMask);
	}
    }
    else
	workaroundsRemoveSticky (w);
}

static void
workaroundsFixupFullscreen (CompWindow *w)
{
    Bool   isFullSize;
    int    output;
    BoxPtr box;

    if (w->type & CompWindowTypeDesktopMask)
	return;

    /* get output region for window */
    output = outputDeviceForWindow (w);
    box = &w->screen->outputDev[output].region.extents;

    /* does the size match the output rectangle? */
    isFullSize = (w->serverX == box->x1) && (w->serverY == box->y1) &&
	         (w->serverWidth == (box->x2 - box->x1)) &&
		 (w->serverHeight == (box->y2 - box->y1));

    /* if not, check if it matches the whole screen */
    if (!isFullSize)
    {
	if ((w->serverX == 0) && (w->serverY == 0) &&
	    (w->serverWidth == w->screen->width) &&
	    (w->serverHeight == w->screen->height))
	{
	    isFullSize = TRUE;
	}
    }

    if ((isFullSize && !(w->state & CompWindowStateFullscreenMask)) ||
	(!isFullSize && (w->state & CompWindowStateFullscreenMask)))
    {
	unsigned int state = w->state & ~CompWindowStateFullscreenMask;

	if (isFullSize)
	    state |= CompWindowStateFullscreenMask;

	if (state != w->state)
	{
	    changeWindowState (w, state);
	    recalcWindowType (w);
	    recalcWindowActions (w);
	    updateWindowAttributes (w, CompStackingUpdateModeNormal);
	}
    }
}

static void
workaroundsDoFixes (CompWindow *w)
{
    CompDisplay  *d = w->screen->display;
    unsigned int newWmType;

    w->wmType = newWmType = getWindowType (d, w->id);

    /* FIXME: Is this the best way to detect a notification type window? */
    if (workaroundsGetNotificationDaemonFix (d))
    {
        if (w->wmType == CompWindowTypeNormalMask &&
            w->attrib.override_redirect && w->resName &&
            strcmp (w->resName, "notification-daemon") == 0)
        {
            newWmType = CompWindowTypeNotificationMask;
        }
    }

    if ((w->wmType == newWmType) && workaroundsGetFirefoxMenuFix (d))
    {
        if (w->wmType == CompWindowTypeNormalMask &&
            w->attrib.override_redirect && w->resName &&
	    strcmp (w->resName, "gecko"))
        {
            newWmType = CompWindowTypeDropdownMenuMask;
        }
    }

    /* FIXME: Basic hack to get Java windows working correctly. */
    if ((w->wmType == newWmType) && workaroundsGetJavaFix (d) && w->resName)
    {
        if ((strcmp (w->resName, "sun-awt-X11-XMenuWindow") == 0) ||
            (strcmp (w->resName, "sun-awt-X11-XWindowPeer") == 0))
        {
            newWmType = CompWindowTypeDropdownMenuMask;
        }
        else if (strcmp (w->resName, "sun-awt-X11-XDialogPeer") == 0)
        {
            newWmType = CompWindowTypeDialogMask;
        }
        else if (strcmp (w->resName, "sun-awt-X11-XFramePeer") == 0)
        {
            newWmType = CompWindowTypeNormalMask;
        }
    }

    if ((newWmType == w->wmType) && workaroundsGetQtFix (d))
    {
        char *windowRole;

        /* fix tooltips */
        windowRole = workaroundsGetWindowRoleAtom (w);
        if (windowRole)
        {
            if ((strcmp (windowRole, "toolTipTip") == 0) ||
                (strcmp (windowRole, "qtooltip_label") == 0))
            {
                newWmType = CompWindowTypeTooltipMask;
            }

            free (windowRole);
        }

        /* fix Qt transients - FIXME: is there a better way to detect them?
           Especially we have to take care of windows which get a class name
           later on */
        if (w->wmType == newWmType)
        {
            if (!w->resName && w->attrib.override_redirect &&
		(w->attrib.class == InputOutput) &&
                (w->wmType == CompWindowTypeUnknownMask))
            {
                newWmType = CompWindowTypeDropdownMenuMask;
            }
        }
    }

    if (newWmType != w->wmType)
    {
	WORKAROUNDS_WINDOW (w);

	ww->adjustedWinType = TRUE;
	w->wmType = newWmType;

	recalcWindowType (w);
	recalcWindowActions (w);

	(*d->matchPropertyChanged) (d, w);
    }

    if (workaroundsGetLegacyFullscreen (w->screen->display))
	workaroundsFixupFullscreen (w);
}

static Bool
workaroundsUpdateTimeout (void *closure)
{
    CompWindow *w = (CompWindow *) closure;

    WORKAROUNDS_WINDOW (w);

    workaroundsUpdateSticky (w);
    workaroundsDoFixes (w);

    ww->updateHandle = 0;

    return FALSE;
}

static void
workaroundsDisplayOptionChanged (CompDisplay               *d,
				 CompOption                *opt,
				 WorkaroundsDisplayOptions num)
{
    CompScreen *s;
    CompWindow *w;

    for (s = d->screens; s; s = s->next)
	for (w = s->windows; w; w = w->next)
	    workaroundsUpdateSticky (w);
}

static void
workaroundsHandleEvent (CompDisplay *d,
			XEvent      *event)
{
    CompWindow *w;

    WORKAROUNDS_DISPLAY (d);

    UNWRAP (wd, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (wd, d, handleEvent, workaroundsHandleEvent);

    if (event->type == ClientMessage)
    {
	if (event->xclient.message_type == d->winDesktopAtom)
        {
            w = findWindowAtDisplay (d, event->xclient.window);
	    if (w)
	        workaroundsUpdateSticky (w);
        }
    }
}

static void
workaroundsWindowResizeNotify (CompWindow *w, int dx, int dy,
                               int dwidth, int dheight)
{
    WORKAROUNDS_SCREEN (w->screen);

    if (workaroundsGetLegacyFullscreen (w->screen->display))
	workaroundsFixupFullscreen (w);

    UNWRAP (ws, w->screen, windowResizeNotify);
    (*w->screen->windowResizeNotify) (w, dx, dy, dwidth, dheight);
    WRAP (ws, w->screen, windowResizeNotify, workaroundsWindowResizeNotify);
}

static Bool
workaroundsInitDisplay (CompPlugin *plugin, CompDisplay *d)
{
    WorkaroundsDisplay *wd;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    wd = malloc (sizeof (WorkaroundsDisplay));
    if (!wd)
        return FALSE;

    wd->screenPrivateIndex = allocateScreenPrivateIndex( d );
    if (wd->screenPrivateIndex < 0)
    {
        free (wd);
        return FALSE;
    }

    wd->roleAtom = XInternAtom (d->display, "WM_WINDOW_ROLE", 0);

    workaroundsSetStickyAlldesktopsNotify (d, workaroundsDisplayOptionChanged);
    workaroundsSetAlldesktopStickyMatchNotify (d,
					       workaroundsDisplayOptionChanged);

    d->object.privates[displayPrivateIndex].ptr = wd;

    WRAP (wd, d, handleEvent, workaroundsHandleEvent);

    return TRUE;
}

static void
workaroundsFiniDisplay (CompPlugin *plugin, CompDisplay *d)
{
    WORKAROUNDS_DISPLAY (d);

    freeScreenPrivateIndex (d, wd->screenPrivateIndex);

    UNWRAP (wd, d, handleEvent);

    free (wd);
}

static Bool
workaroundsInitScreen (CompPlugin *plugin, CompScreen *s)
{
    WorkaroundsScreen *ws;

    WORKAROUNDS_DISPLAY (s->display);

    ws = malloc (sizeof (WorkaroundsScreen));
    if (!ws)
        return FALSE;

    ws->windowPrivateIndex = allocateWindowPrivateIndex (s);
    if (ws->windowPrivateIndex < 0)
    {
	free (ws);
	return FALSE;
    }

    WRAP (ws, s, windowResizeNotify, workaroundsWindowResizeNotify);

    s->object.privates[wd->screenPrivateIndex].ptr = ws;

    return TRUE;
}

static void
workaroundsFiniScreen (CompPlugin *plugin, CompScreen *s)
{
    WORKAROUNDS_SCREEN (s);

    freeWindowPrivateIndex (s, ws->windowPrivateIndex);

    UNWRAP (ws, s, windowResizeNotify);

    free (ws);
}

static Bool
workaroundsInitWindow (CompPlugin *plugin, CompWindow *w)
{
    WorkaroundsWindow *ww;

    WORKAROUNDS_SCREEN (w->screen);

    ww = malloc (sizeof (WorkaroundsWindow));
    if (!ww)
	return FALSE;

    ww->madeSticky = FALSE;
    ww->adjustedWinType = FALSE;

    w->object.privates[ws->windowPrivateIndex].ptr = ww;

    ww->updateHandle = compAddTimeout (0, workaroundsUpdateTimeout, (void *) w);

    return TRUE;
}

static void
workaroundsFiniWindow (CompPlugin *plugin, CompWindow *w)
{
    WORKAROUNDS_WINDOW (w);

    if (ww->adjustedWinType)
    {
	w->wmType = getWindowType (w->screen->display, w->id);
	recalcWindowType (w);
	recalcWindowActions (w);
    }

     if (w->state & CompWindowStateStickyMask && ww->madeSticky)
	setWindowState (w->screen->display,
			w->state & ~CompWindowStateStickyMask, w->id);

    if (ww->updateHandle)
	compRemoveTimeout (ww->updateHandle);

    free (ww);
}

static CompBool
workaroundsInitObject (CompPlugin *p,
		       CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) workaroundsInitDisplay,
	(InitPluginObjectProc) workaroundsInitScreen,
	(InitPluginObjectProc) workaroundsInitWindow
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
workaroundsFiniObject (CompPlugin *p,
		       CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) workaroundsFiniDisplay,
	(FiniPluginObjectProc) workaroundsFiniScreen,
	(FiniPluginObjectProc) workaroundsFiniWindow
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static Bool
workaroundsInit (CompPlugin *plugin)
{
    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
        return FALSE;

    return TRUE;
}

static void
workaroundsFini (CompPlugin *plugin)
{
    freeDisplayPrivateIndex (displayPrivateIndex);
}

CompPluginVTable workaroundsVTable =
{
    "workarounds",
    0,
    workaroundsInit,
    workaroundsFini,
    workaroundsInitObject,
    workaroundsFiniObject,
    0,
    0
};

CompPluginVTable *
getCompPluginInfo (void)
{
    return &workaroundsVTable;
}

