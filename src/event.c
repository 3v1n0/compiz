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

#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xrandr.h>

#include <compiz.h>

static void
handleWindowDamageRect (CompWindow *w,
			int	   x,
			int	   y,
			int	   width,
			int	   height)
{
    REGION region;
    Bool   initial = FALSE;

    if (!w->redirected)
	return;

    if (!w->damaged)
    {
	w->damaged = initial = TRUE;
	w->invisible = WINDOW_INVISIBLE (w);
    }

    region.extents.x1 = x;
    region.extents.y1 = y;
    region.extents.x2 = region.extents.x1 + width;
    region.extents.y2 = region.extents.y1 + height;

    if (!(*w->screen->damageWindowRect) (w, initial, &region.extents))
    {
	region.extents.x1 += w->attrib.x + w->attrib.border_width;
	region.extents.y1 += w->attrib.y + w->attrib.border_width;
	region.extents.x2 += w->attrib.x + w->attrib.border_width;
	region.extents.y2 += w->attrib.y + w->attrib.border_width;

	region.rects = &region.extents;
	region.numRects = region.size = 1;

	damageWindowRegion (w, &region);

	if (initial)
	    damageWindowOutputExtents (w);
    }

    if (!w->attrib.override_redirect)
	w->placed = TRUE;
}

void
handleSyncAlarm (CompWindow *w)
{
    if (w->syncWait)
    {
	if (w->syncWaitHandle)
	{
	    compRemoveTimeout (w->syncWaitHandle);
	    w->syncWaitHandle = 0;
	}

	if (resizeWindow (w,
			  w->syncX, w->syncY,
			  w->syncWidth, w->syncHeight,
			  w->syncBorderWidth))
	{
	    XRectangle *rects;
	    int	       nDamage;

	    nDamage = w->nDamage;
	    rects   = w->damageRects;
	    while (nDamage--)
	    {
		handleWindowDamageRect (w,
					rects[nDamage].x,
					rects[nDamage].y,
					rects[nDamage].width,
					rects[nDamage].height);
	    }

	    w->nDamage = 0;
	    w->syncWait = FALSE;
	}
    }
}

static void
moveInputFocusToOtherWindow (CompWindow *w)
{
    CompDisplay *display = w->screen->display;

    if (w->id == display->activeWindow)
    {
	CompWindow *ancestor;

	if (w->transientFor && w->transientFor != w->screen->root)
	{
	    ancestor = findWindowAtDisplay (display, w->transientFor);
	    if (ancestor && !(ancestor->type & (CompWindowTypeDesktopMask |
						CompWindowTypeDockMask)))
	    {
		moveInputFocusToWindow (ancestor);
	    }
	    else
		focusDefaultWindow (display);
	}
	else if (w->type & (CompWindowTypeDialogMask |
			    CompWindowTypeModalDialogMask))
	{
	    CompWindow *a, *focus = NULL;

	    for (a = w->screen->reverseWindows; a; a = a->prev)
	    {
		if (a->clientLeader == w->clientLeader)
		{
		    if ((*w->screen->focusWindow) (a))
		    {
			if (focus)
			{
			    if (a->type & (CompWindowTypeNormalMask |
					   CompWindowTypeDialogMask |
					   CompWindowTypeModalDialogMask))
			    {
				if (a->activeNum > focus->activeNum)
				    focus = a;
			    }
			}
			else
			    focus = a;
		    }
		}
	    }

	    if (focus && !(focus->type & (CompWindowTypeDesktopMask |
					  CompWindowTypeDockMask)))
	    {
		moveInputFocusToWindow (focus);
	    }
	    else
		focusDefaultWindow (display);
	}
	else
	    focusDefaultWindow (display);
    }
}

static Bool
autoRaiseTimeout (void *closure)
{
    CompDisplay *display = closure;

    if (display->autoRaiseWindow == display->activeWindow)
    {
	CompWindow *w;

	w = findWindowAtDisplay (display, display->autoRaiseWindow);
	if (w)
	    updateWindowAttributes (w, FALSE);
    }

    return FALSE;
}

static void
changeWindowOpacity (CompWindow *w,
		     int	direction)
{
    int step, opacity;

    if (w->attrib.override_redirect)
	return;

    if (w->type & CompWindowTypeDesktopMask)
	return;

    step = (OPAQUE * w->screen->opacityStep) / 100;

    opacity = w->paint.opacity + step * direction;
    if (opacity > OPAQUE)
    {
	opacity = OPAQUE;
    }
    else if (opacity < step)
    {
	opacity = step;
    }

    if (w->paint.opacity != opacity)
    {
	w->paint.opacity = opacity;

	setWindowProp32 (w->screen->display, w->id,
			 w->screen->display->winOpacityAtom,
			 w->paint.opacity);
	addWindowDamage (w);
    }
}

#define REAL_MOD_MASK (ShiftMask | ControlMask | Mod1Mask | \
		       Mod2Mask | Mod3Mask | Mod4Mask | Mod5Mask)

static Bool
isCallBackBinding (CompOption	    *option,
		   CompBindingType  type,
		   CompBindingState state)
{
    if (option->type != CompOptionTypeAction)
	return FALSE;

    if (!(option->value.action.type & type))
	return FALSE;

    if (!(option->value.action.state & state))
	return FALSE;

    return TRUE;
}

static Bool
isInitiateBinding (CompOption	    *option,
		   CompBindingType  type,
		   CompBindingState state,
		   CompAction	    **action)
{
    if (!isCallBackBinding (option, type, state))
	return FALSE;

    if (!option->value.action.initiate)
	return FALSE;

    *action = &option->value.action;

    return TRUE;
}

static Bool
isTerminateBinding (CompOption	     *option,
		    CompBindingType  type,
		    CompBindingState state,
		    CompAction       **action)
{
    if (!isCallBackBinding (option, type, state))
	return FALSE;

    if (!option->value.action.terminate)
	return FALSE;

    *action = &option->value.action;

    return TRUE;
}

static Bool
triggerButtonPressBindings (CompDisplay *d,
			    CompOption  *option,
			    int		nOption,
			    XEvent	*event,
			    CompOption  *argument,
			    int		nArgument)
{
    CompBindingState state = CompBindingStateInitButton;
    CompAction	     *action;
    unsigned int     modMask = REAL_MOD_MASK & ~d->ignoredModMask;
    unsigned int     bindMods;

    while (nOption--)
    {
	if (isInitiateBinding (option, CompBindingTypeButton, state, &action))
	{
	    if (action->button.button == event->xbutton.button)
	    {
		bindMods = virtualToRealModMask (d, action->button.modifiers);

		if ((bindMods & modMask) == (event->xbutton.state & modMask))
		    if ((*action->initiate) (d, action, state,
					     argument, nArgument))
			return TRUE;
	    }
	}

	option++;
    }

    return FALSE;
}

static Bool
triggerButtonReleaseBindings (CompDisplay *d,
			      CompOption  *option,
			      int	  nOption,
			      XEvent	  *event,
			      CompOption  *argument,
			      int	  nArgument)
{
    CompBindingState state = CompBindingStateTermButton;
    CompAction	     *action;

    while (nOption--)
    {
	if (isTerminateBinding (option, CompBindingTypeButton, state, &action))
	{
	    if (action->button.button == event->xbutton.button)
	    {
		if ((*action->terminate) (d, action, state,
					  argument, nArgument))
		    return TRUE;
	    }
	}

	option++;
    }

    return FALSE;
}

static Bool
triggerKeyPressBindings (CompDisplay *d,
			 CompOption  *option,
			 int	     nOption,
			 XEvent	     *event,
			 CompOption  *argument,
			 int	     nArgument)
{
    CompBindingState state = CompBindingStateInitKey;
    CompAction	     *action;
    unsigned int     modMask = REAL_MOD_MASK & ~d->ignoredModMask;
    unsigned int     bindMods;

    if (event->xkey.keycode == d->escapeKeyCode)
    {
	while (nOption--)
	{
	    if (option->type == CompOptionTypeAction)
	    {
		if (option->value.action.terminate)
		    (*option->value.action.terminate) (d, &option->value.action,
						       0, NULL, 0);
	    }

	    option++;
	}

	return FALSE;
    }

    while (nOption--)
    {
	if (isInitiateBinding (option, CompBindingTypeKey, state, &action))
	{
	    bindMods = virtualToRealModMask (d, action->key.modifiers);

	    if (action->key.keycode == event->xkey.keycode)
	    {
		if ((bindMods & modMask) == (event->xbutton.state & modMask))
		    if ((*action->initiate) (d, action, state,
					     argument, nArgument))
			break;
	    }
	    else if (!d->xkbEvent && action->key.keycode == 0)
	    {
		if (bindMods == (event->xbutton.state & modMask))
		    if ((*action->initiate) (d, action, state,
					     argument, nArgument))
			return TRUE;
	    }
	}

	option++;
    }

    return FALSE;
}

static Bool
triggerKeyReleaseBindings (CompDisplay *d,
			   CompOption  *option,
			   int	       nOption,
			   XEvent      *event,
			   CompOption  *argument,
			   int	       nArgument)
{
    if (!d->xkbEvent)
    {
	CompBindingState state = CompBindingStateTermKey;
	CompAction	 *action;
	unsigned int	 modMask = REAL_MOD_MASK & ~d->ignoredModMask;
	unsigned int	 bindMods;
	unsigned int	 mods;

	mods = keycodeToModifiers (d, event->xkey.keycode);
	if (mods == 0)
	    return FALSE;

	while (nOption--)
	{
	    if (isTerminateBinding (option, CompBindingTypeKey, state, &action))
	    {
		bindMods = virtualToRealModMask (d, action->key.modifiers);

		if ((mods & modMask & bindMods) != bindMods)
		{
		    if ((*action->terminate) (d, action, state,
					      argument, nArgument))
			return TRUE;
		}
	    }

	    option++;
	}
    }

    return FALSE;
}

static Bool
triggerStateNotifyBindings (CompDisplay		*d,
			    CompOption		*option,
			    int			nOption,
			    XkbStateNotifyEvent *event,
			    CompOption		*argument,
			    int			nArgument)
{
    CompBindingState state;
    CompAction       *action;
    unsigned int     modMask = REAL_MOD_MASK & ~d->ignoredModMask;
    unsigned int     bindMods;

    if (event->event_type == KeyPress)
    {
	state = CompBindingStateInitKey;

	while (nOption--)
	{
	    if (isInitiateBinding (option, CompBindingTypeKey, state, &action))
	    {
		if (action->key.keycode == 0)
		{
		    bindMods = virtualToRealModMask (d, action->key.modifiers);

		    if ((event->mods & modMask & bindMods) == bindMods)
		    {
			if ((*action->initiate) (d, action, state,
						 argument, nArgument))
			    return TRUE;
		    }
		}
	    }

	    option++;
	}
    }
    else
    {
	state = CompBindingStateTermKey;

	while (nOption--)
	{
	    if (isTerminateBinding (option, CompBindingTypeKey, state, &action))
	    {
		bindMods = virtualToRealModMask (d, action->key.modifiers);

		if ((event->mods & modMask & bindMods) != bindMods)
		{
		    if ((*action->terminate) (d, action, state,
					      argument, nArgument))
			return TRUE;
		}
	    }

	    option++;
	}
    }

    return FALSE;
}

static Bool
handleBindingEvent (CompDisplay *d,
		    XEvent      *event)
{
    CompOption *option;
    int	       nOption;
    CompPlugin *p;
    CompOption o[6];

    o[0].type = CompOptionTypeInt;
    o[0].name = "window";

    o[1].type = CompOptionTypeInt;
    o[1].name = "modifiers";

    o[2].type = CompOptionTypeInt;
    o[2].name = "x";

    o[3].type = CompOptionTypeInt;
    o[3].name = "y";

    o[4].type = CompOptionTypeInt;
    o[4].name = "root";

    switch (event->type) {
    case ButtonPress:
	o[0].value.i = event->xbutton.window;
	o[1].value.i = event->xbutton.state;
	o[2].value.i = event->xbutton.x_root;
	o[3].value.i = event->xbutton.y_root;
	o[4].value.i = event->xbutton.root;

	o[5].type    = CompOptionTypeInt;
	o[5].name    = "button";
	o[5].value.i = event->xbutton.button;

	for (p = getPlugins (); p; p = p->next)
	{
	    if (p->vTable->getDisplayOptions)
	    {
		option = (*p->vTable->getDisplayOptions) (d, &nOption);
		if (triggerButtonPressBindings (d, option, nOption, event,
						o, 6))
		    return TRUE;
	    }
	}

	option = compGetDisplayOptions (d, &nOption);
	if (triggerButtonPressBindings (d, option, nOption, event, o, 6))
	    return TRUE;

	break;
    case ButtonRelease:
	o[0].value.i = event->xbutton.window;
	o[1].value.i = event->xbutton.state;
	o[2].value.i = event->xbutton.x_root;
	o[3].value.i = event->xbutton.y_root;
	o[4].value.i = event->xbutton.root;

	o[5].type    = CompOptionTypeInt;
	o[5].name    = "button";
	o[5].value.i = event->xbutton.button;

	for (p = getPlugins (); p; p = p->next)
	{
	    if (p->vTable->getDisplayOptions)
	    {
		option = (*p->vTable->getDisplayOptions) (d, &nOption);
		if (triggerButtonReleaseBindings (d, option, nOption, event,
						  o, 6))
		    return TRUE;
	    }
	}

	option = compGetDisplayOptions (d, &nOption);
	if (triggerButtonReleaseBindings (d, option, nOption, event, o, 6))
	    return TRUE;

	break;
    case KeyPress:
	o[0].value.i = d->activeWindow;
	o[1].value.i = event->xkey.state;
	o[2].value.i = event->xkey.x_root;
	o[3].value.i = event->xkey.y_root;
	o[4].value.i = event->xkey.root;

	o[5].type    = CompOptionTypeInt;
	o[5].name    = "keycode";
	o[5].value.i = event->xkey.keycode;

	for (p = getPlugins (); p; p = p->next)
	{
	    if (p->vTable->getDisplayOptions)
	    {
		option = (*p->vTable->getDisplayOptions) (d, &nOption);
		if (triggerKeyPressBindings (d, option, nOption, event, o, 6))
		    return TRUE;
	    }
	}

	option = compGetDisplayOptions (d, &nOption);
	if (triggerKeyPressBindings (d, option, nOption, event, o, 6))
	    return TRUE;

	break;
    case KeyRelease:
	o[0].value.i = d->activeWindow;
	o[1].value.i = event->xkey.state;
	o[2].value.i = event->xkey.x_root;
	o[3].value.i = event->xkey.y_root;
	o[4].value.i = event->xkey.root;

	o[5].type    = CompOptionTypeInt;
	o[5].name    = "keycode";
	o[5].value.i = event->xkey.keycode;

	for (p = getPlugins (); p; p = p->next)
	{
	    if (p->vTable->getDisplayOptions)
	    {
		option = (*p->vTable->getDisplayOptions) (d, &nOption);
		if (triggerKeyReleaseBindings (d, option, nOption, event, o, 6))
		    return TRUE;
	    }
	}

	option = compGetDisplayOptions (d, &nOption);
	if (triggerKeyReleaseBindings (d, option, nOption, event, o, 6))
	    return TRUE;

	break;
    default:
	if (event->type == d->xkbEvent)
	{
	    XkbAnyEvent *xkbEvent = (XkbAnyEvent *) event;

	    if (xkbEvent->xkb_type == XkbStateNotify)
	    {
		XkbStateNotifyEvent *stateEvent = (XkbStateNotifyEvent *) event;

		option = compGetDisplayOptions (d, &nOption);

		o[0].value.i = d->activeWindow;
		o[1].value.i = stateEvent->mods;

		for (p = getPlugins (); p; p = p->next)
		{
		    if (p->vTable->getDisplayOptions)
		    {
			option = (*p->vTable->getDisplayOptions) (d, &nOption);
			if (triggerStateNotifyBindings (d, option, nOption,
							stateEvent, o, 2))
			    return TRUE;
		    }
		}

		option = compGetDisplayOptions (d, &nOption);
		if (triggerStateNotifyBindings (d, option, nOption, stateEvent,
						o, 2))
		    return TRUE;
	    }
	}
	break;
    }

    return FALSE;
}

void
handleEvent (CompDisplay *d,
	     XEvent      *event)
{
    CompScreen *s;
    CompWindow *w;

    if (handleBindingEvent (d, event))
	return;

    switch (event->type) {
    case Expose:
	s = findScreenAtDisplay (d, event->xexpose.window);
	if (s)
	{
	    int more = event->xexpose.count + 1;

	    if (s->nExpose == s->sizeExpose)
	    {
		if (s->exposeRects)
		{
		    s->exposeRects = realloc (s->exposeRects,
					      (s->sizeExpose + more) *
					      sizeof (XRectangle));
		    s->sizeExpose += more;
		}
		else
		{
		    s->exposeRects = malloc (more * sizeof (XRectangle));
		    s->sizeExpose = more;
		}
	    }

	    s->exposeRects[s->nExpose].x      = event->xexpose.x;
	    s->exposeRects[s->nExpose].y      = event->xexpose.y;
	    s->exposeRects[s->nExpose].width  = event->xexpose.width;
	    s->exposeRects[s->nExpose].height = event->xexpose.height;
	    s->nExpose++;

	    if (event->xexpose.count == 0)
	    {
		REGION rect;

		rect.rects = &rect.extents;
		rect.numRects = rect.size = 1;

		while (s->nExpose--)
		{
		    rect.extents.x1 = s->exposeRects[s->nExpose].x;
		    rect.extents.y1 = s->exposeRects[s->nExpose].y;
		    rect.extents.x2 = rect.extents.x1 +
			s->exposeRects[s->nExpose].width;
		    rect.extents.y2 = rect.extents.y1 +
			s->exposeRects[s->nExpose].height;

		    damageScreenRegion (s, &rect);
		}
		s->nExpose = 0;
	    }
	}
	break;
    case SelectionRequest:
	handleSelectionRequest (d, event);
	break;
    case SelectionClear:
	handleSelectionClear (d, event);
	break;
    case ConfigureNotify:
	w = findWindowAtDisplay (d, event->xconfigure.window);
	if (w)
	{
	    configureWindow (w, &event->xconfigure);
	}
	else
	{
	    s = findScreenAtDisplay (d, event->xconfigure.window);
	    if (s)
		configureScreen (s, &event->xconfigure);
	}
	break;
    case CreateNotify:
	s = findScreenAtDisplay (d, event->xcreatewindow.parent);
	if (s)
	{
	    addWindow (s, event->xcreatewindow.window, getTopWindow (s));
	}
	break;
    case DestroyNotify:
	w = findWindowAtDisplay (d, event->xdestroywindow.window);
	if (w)
	{
	    moveInputFocusToOtherWindow (w);
	    destroyWindow (w);
	}
	break;
    case MapNotify:
	w = findWindowAtDisplay (d, event->xmap.window);
	if (w)
	{
	    /* been shaded */
	    if (w->height == 0)
	    {
		if (w->id == d->activeWindow)
		    moveInputFocusToWindow (w);
	    }

	    mapWindow (w);
	}
	break;
    case UnmapNotify:
	w = findWindowAtDisplay (d, event->xunmap.window);
	if (w)
	{
	    /* Normal -> Iconic */
	    if (w->pendingUnmaps)
	    {
		setWmState (d, IconicState, w->id);
		w->pendingUnmaps--;
	    }
	    else /* X -> Withdrawn */
	    {
		/* Iconic -> Withdrawn */
		if (w->state & CompWindowStateHiddenMask)
		{
		    w->minimized = FALSE;
		    w->state &= ~CompWindowStateHiddenMask;

		    setWindowState (d, w->state, w->id);
		    updateClientListForScreen (w->screen);
		}

		if (!w->attrib.override_redirect)
		    setWmState (d, WithdrawnState, w->id);

		w->placed  = FALSE;
		w->managed = FALSE;
	    }

	    unmapWindow (w);

	    if (!w->shaded)
		moveInputFocusToOtherWindow (w);
	}
	break;
    case ReparentNotify:
	w = findWindowAtDisplay (d, event->xreparent.window);
	s = findScreenAtDisplay (d, event->xreparent.parent);
	if (s && !w)
	{
	    addWindow (s, event->xreparent.window, getTopWindow (s));
	}
	else if (w)
	{
	    /* This is the only case where a window is removed but not
	       destroyed. We must remove our event mask and all passive
	       grabs. */
	    XSelectInput (d->display, w->id, NoEventMask);
	    XShapeSelectInput (d->display, w->id, NoEventMask);
	    XUngrabButton (d->display, AnyButton, AnyModifier, w->id);

	    moveInputFocusToOtherWindow (w);

	    destroyWindow (w);
	}
	break;
    case CirculateNotify:
	w = findWindowAtDisplay (d, event->xcirculate.window);
	if (w)
	    circulateWindow (w, &event->xcirculate);
	break;
    case ButtonPress:
	s = findScreenAtDisplay (d, event->xbutton.root);
	if (s)
	{
	    int	eventMode = ReplayPointer;

	    if (event->xbutton.button == Button1 ||
		event->xbutton.button == Button2 ||
		event->xbutton.button == Button3)
	    {
		w = findTopLevelWindowAtScreen (s, event->xbutton.window);
		if (w)
		{
		    if (d->opt[COMP_DISPLAY_OPTION_RAISE_ON_CLICK].value.b)
			updateWindowAttributes (w, TRUE);

		    if (!(w->type & CompWindowTypeDockMask))
			moveInputFocusToWindow (w);
		}
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_CLOSE_WINDOW]))
	    {
		w = findTopLevelWindowAtScreen (s, event->xbutton.window);
		if (w)
		    closeWindow (w, event->xbutton.time);

		eventMode = AsyncPointer;
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_MAXIMIZE_WINDOW]))
	    {
		w = findTopLevelWindowAtScreen (s, event->xbutton.window);
		if (w)
		    maximizeWindow (w, MAXIMIZE_STATE);

		eventMode = AsyncPointer;
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_MAXIMIZE_WINDOW_HORZ]))
	    {
		w = findTopLevelWindowAtScreen (s, event->xbutton.window);
		if (w)
		    maximizeWindow (w, w->state |
				    CompWindowStateMaximizedHorzMask);

		eventMode = AsyncPointer;
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_MAXIMIZE_WINDOW_VERT]))
	    {
		w = findTopLevelWindowAtScreen (s, event->xbutton.window);
		if (w)
		    maximizeWindow (w, w->state |
				    CompWindowStateMaximizedVertMask);

		eventMode = AsyncPointer;
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_UNMAXIMIZE_WINDOW]))
	    {
		w = findTopLevelWindowAtScreen (s, event->xbutton.window);
		if (w)
		    maximizeWindow (w, 0);

		eventMode = AsyncPointer;
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_MINIMIZE_WINDOW]))
	    {
		w = findTopLevelWindowAtScreen (s, event->xbutton.window);
		if (w)
		    minimizeWindow (w);

		eventMode = AsyncPointer;
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_SHOW_DESKTOP]))
	    {
		if (s->showingDesktopMask == 0)
		    enterShowDesktopMode (s);
		else
		    leaveShowDesktopMode (s, NULL);

		eventMode = AsyncPointer;
	    }

	    /* avoid toolkit actions when screen is grabbed */
	    if (!d->screens->maxGrab)
	    {
		if (eventMatches (d, event,
				  &d->opt[COMP_DISPLAY_OPTION_MAIN_MENU]))
		{
		    toolkitAction (s, s->display->toolkitActionMainMenuAtom,
				   event->xbutton.time, s->root, 0, 0, 0);
		    eventMode = AsyncPointer;
		}

		if (eventMatches (d, event,
				  &d->opt[COMP_DISPLAY_OPTION_RUN_DIALOG]))
		{
		    toolkitAction (s, s->display->toolkitActionRunDialogAtom,
				   event->xbutton.time, s->root, 0, 0, 0);
		    eventMode = AsyncPointer;
		}

		if (eventMatches (d, event,
				  &d->opt[COMP_DISPLAY_OPTION_WINDOW_MENU]))
		{
		    w = findTopLevelWindowAtScreen (s, event->xbutton.window);
		    if (w)
			toolkitAction (s,
				       s->display->toolkitActionWindowMenuAtom,
				       event->xbutton.time,
				       w->id,
				       event->xbutton.button,
				       event->xbutton.x_root,
				       event->xbutton.y_root);

		    eventMode = AsyncPointer;
		}
	    }

#define EV_BUTTON_COMMAND(num)						    \
    if (eventMatches (d, event,						    \
		      &d->opt[COMP_DISPLAY_OPTION_RUN_COMMAND ## num]))	    \
    {									    \
	runCommand (s, d->opt[COMP_DISPLAY_OPTION_COMMAND ## num].value.s); \
	eventMode = AsyncPointer;					    \
    }

	    EV_BUTTON_COMMAND (0);
	    EV_BUTTON_COMMAND (1);
	    EV_BUTTON_COMMAND (2);
	    EV_BUTTON_COMMAND (3);
	    EV_BUTTON_COMMAND (4);
	    EV_BUTTON_COMMAND (5);
	    EV_BUTTON_COMMAND (6);
	    EV_BUTTON_COMMAND (7);
	    EV_BUTTON_COMMAND (8);
	    EV_BUTTON_COMMAND (9);
	    EV_BUTTON_COMMAND (10);
	    EV_BUTTON_COMMAND (11);

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_SLOW_ANIMATIONS]))
	    {
		s->slowAnimations = !s->slowAnimations;
		eventMode = AsyncPointer;
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_LOWER_WINDOW]))
	    {
		w = findTopLevelWindowAtScreen (s, event->xbutton.window);
		if (w)
		    lowerWindow (w);

		eventMode = AsyncPointer;
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_OPACITY_INCREASE]))
	    {
		w = findTopLevelWindowAtScreen (s, event->xbutton.window);
		if (w)
		    changeWindowOpacity (w, 1);

		eventMode = AsyncPointer;
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_OPACITY_DECREASE]))
	    {
		w = findTopLevelWindowAtScreen (s, event->xbutton.window);
		if (w)
		    changeWindowOpacity (w, -1);

		eventMode = AsyncPointer;
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_RUN_SCREENSHOT]))
	    {
		runCommand (s, d->opt[COMP_DISPLAY_OPTION_SCREENSHOT].value.s);
		eventMode = AsyncPointer;
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_RUN_WINDOW_SCREENSHOT]))
	    {
		runCommand (s, d->opt[COMP_DISPLAY_OPTION_WINDOW_SCREENSHOT].value.s);
		eventMode = AsyncPointer;
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_TOGGLE_WINDOW_MAXIMIZED]))
	    {
		w = findTopLevelWindowAtScreen (s, event->xbutton.window);
		if (w)
		{
		    if ((w->state & MAXIMIZE_STATE) == MAXIMIZE_STATE)
			maximizeWindow (w, 0);
		    else
			maximizeWindow (w, MAXIMIZE_STATE);
		}
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_TOGGLE_WINDOW_MAXIMIZED_HORZ]))
	    {
		w = findTopLevelWindowAtScreen (s, event->xbutton.window);
		if (w)
		    maximizeWindow (w, w->state ^
				    CompWindowStateMaximizedHorzMask);
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_TOGGLE_WINDOW_MAXIMIZED_VERT]))
	    {
		w = findTopLevelWindowAtScreen (s, event->xbutton.window);
		if (w)
		    maximizeWindow (w, w->state ^
				    CompWindowStateMaximizedVertMask);
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_TOGGLE_WINDOW_SHADED]))
	    {
		w = findTopLevelWindowAtScreen (s, event->xbutton.window);
		if (w && (w->actions & CompWindowActionShadeMask))
		{
		    w->state ^= CompWindowStateShadedMask;
		    updateWindowAttributes (w, FALSE);
		}
	    }

	    if (!d->screens->maxGrab)
		XAllowEvents (d->display, eventMode, event->xbutton.time);
	}
	else if (!d->screens->maxGrab)
	{
	    XAllowEvents (d->display, ReplayPointer, event->xbutton.time);
	}
	break;
    case ButtonRelease:
	break;
    case KeyPress:
	s = findScreenAtDisplay (d, event->xkey.root);
	if (s)
	{
	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_CLOSE_WINDOW]))
	    {
		w = findTopLevelWindowAtScreen (s, d->activeWindow);
		if (w)
		    closeWindow (w, event->xkey.time);
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_MAXIMIZE_WINDOW]))
	    {
		w = findTopLevelWindowAtScreen (s, d->activeWindow);
		if (w)
		    maximizeWindow (w, MAXIMIZE_STATE);
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_MAXIMIZE_WINDOW_HORZ]))
	    {
		w = findTopLevelWindowAtScreen (s, d->activeWindow);
		if (w)
		    maximizeWindow (w, w->state |
				    CompWindowStateMaximizedHorzMask);
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_MAXIMIZE_WINDOW_VERT]))
	    {
		w = findTopLevelWindowAtScreen (s, d->activeWindow);
		if (w)
		    maximizeWindow (w, w->state |
				    CompWindowStateMaximizedVertMask);
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_UNMAXIMIZE_WINDOW]))
	    {
		w = findTopLevelWindowAtScreen (s, d->activeWindow);
		if (w)
		    maximizeWindow (w, 0);
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_MINIMIZE_WINDOW]))
	    {
		w = findTopLevelWindowAtScreen (s, d->activeWindow);
		if (w)
		    minimizeWindow (w);
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_SHOW_DESKTOP]))
	    {
		if (s->showingDesktopMask == 0)
		    enterShowDesktopMode (s);
		else
		    leaveShowDesktopMode (s, NULL);
	    }

	    /* avoid toolkit actions when screen is grabbed */
	    if (!d->screens->maxGrab)
	    {
		if (eventMatches (d, event,
				  &d->opt[COMP_DISPLAY_OPTION_MAIN_MENU]))
		{
		    toolkitAction (s, s->display->toolkitActionMainMenuAtom,
				   event->xkey.time, s->root, 0, 0, 0);
		}

		if (eventMatches (d, event,
				  &d->opt[COMP_DISPLAY_OPTION_RUN_DIALOG]))
		{
		    toolkitAction (s, s->display->toolkitActionRunDialogAtom,
				   event->xkey.time, s->root, 0, 0, 0);
		}

		if (eventMatches (d, event,
				  &d->opt[COMP_DISPLAY_OPTION_WINDOW_MENU]))
		{
		    toolkitAction (s, s->display->toolkitActionWindowMenuAtom,
				   event->xkey.time,
				   d->activeWindow,
				   0,
				   event->xkey.x_root,
				   event->xkey.y_root);
		}
	    }

#define EV_KEY_COMMAND(num)						    \
    if (eventMatches (d, event,						    \
		      &d->opt[COMP_DISPLAY_OPTION_RUN_COMMAND ## num]))	    \
	runCommand (s, d->opt[COMP_DISPLAY_OPTION_COMMAND ## num].value.s); \

	    EV_KEY_COMMAND (0);
	    EV_KEY_COMMAND (1);
	    EV_KEY_COMMAND (2);
	    EV_KEY_COMMAND (3);
	    EV_KEY_COMMAND (4);
	    EV_KEY_COMMAND (5);
	    EV_KEY_COMMAND (6);
	    EV_KEY_COMMAND (7);
	    EV_KEY_COMMAND (8);
	    EV_KEY_COMMAND (9);
	    EV_KEY_COMMAND (10);
	    EV_KEY_COMMAND (11);

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_SLOW_ANIMATIONS]))
		s->slowAnimations = !s->slowAnimations;

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_LOWER_WINDOW]))
	    {
		w = findTopLevelWindowAtScreen (s, d->activeWindow);
		if (w)
		    lowerWindow (w);
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_OPACITY_INCREASE]))
	    {
		w = findTopLevelWindowAtScreen (s, d->activeWindow);
		if (w)
		    changeWindowOpacity (w, 1);
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_OPACITY_DECREASE]))
	    {
		w = findTopLevelWindowAtScreen (s, d->activeWindow);
		if (w)
		    changeWindowOpacity (w, -1);
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_RUN_SCREENSHOT]))
		runCommand (s, d->opt[COMP_DISPLAY_OPTION_SCREENSHOT].value.s);

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_RUN_WINDOW_SCREENSHOT]))
		runCommand (s, d->opt[COMP_DISPLAY_OPTION_WINDOW_SCREENSHOT].value.s);

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_TOGGLE_WINDOW_MAXIMIZED]))
	    {
		w = findTopLevelWindowAtScreen (s, d->activeWindow);
		if (w)
		{
		    if ((w->state & MAXIMIZE_STATE) == MAXIMIZE_STATE)
			maximizeWindow (w, 0);
		    else
			maximizeWindow (w, MAXIMIZE_STATE);
		}
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_TOGGLE_WINDOW_MAXIMIZED_HORZ]))
	    {
		w = findTopLevelWindowAtScreen (s, d->activeWindow);
		if (w)
		    maximizeWindow (w, w->state ^
				    CompWindowStateMaximizedHorzMask);
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_TOGGLE_WINDOW_MAXIMIZED_VERT]))
	    {
		w = findTopLevelWindowAtScreen (s, d->activeWindow);
		if (w)
		    maximizeWindow (w, w->state ^
				    CompWindowStateMaximizedVertMask);
	    }

	    if (eventMatches (d, event,
			      &d->opt[COMP_DISPLAY_OPTION_TOGGLE_WINDOW_SHADED]))
	    {
		w = findTopLevelWindowAtScreen (s, d->activeWindow);
		if (w && (w->actions & CompWindowActionShadeMask))
		{
		    w->state ^= CompWindowStateShadedMask;
		    updateWindowAttributes (w, FALSE);
		}
	    }
	}
	break;
    case KeyRelease:
	break;
    case PropertyNotify:
	if (event->xproperty.atom == d->winActiveAtom)
	{
	    Window newActiveWindow;

	    newActiveWindow = getActiveWindow (d, event->xproperty.window);
	    if (newActiveWindow != d->activeWindow)
	    {
		d->activeWindow = newActiveWindow;

		s = findScreenAtDisplay (d, event->xproperty.window);
		if (s)
		{
		    w = findWindowAtDisplay (d, newActiveWindow);
		    if (w)
			w->activeNum = s->activeNum++;
		}
	    }
	}
	else if (event->xproperty.atom == d->winTypeAtom)
	{
	    w = findWindowAtDisplay (d, event->xproperty.window);
	    if (w)
	    {
		unsigned int type;

		type = getWindowType (d, w->id);

		if (type != w->wmType)
		{
		    if (w->attrib.map_state == IsViewable)
		    {
			if (w->type == CompWindowTypeDesktopMask)
			    w->screen->desktopWindowCount--;
			else if (type == CompWindowTypeDesktopMask)
			    w->screen->desktopWindowCount++;
		    }

		    w->wmType = type;

		    recalcWindowType (w);
		    recalcWindowActions (w);

		    if (w->type & CompWindowTypeDesktopMask)
			w->paint.opacity = OPAQUE;

		    updateClientListForScreen (w->screen);
		}
	    }
	}
	else if (event->xproperty.atom == d->winStateAtom)
	{
	    w = findWindowAtDisplay (d, event->xproperty.window);
	    if (w)
	    {
		unsigned int state;

		state = getWindowState (d, w->id);

		if (state != w->state)
		{
		    w->state = state;

		    recalcWindowType (w);
		    recalcWindowActions (w);

		    if (w->type & CompWindowTypeDesktopMask)
			w->paint.opacity = OPAQUE;
		}
	    }
	}
	else if (event->xproperty.atom == XA_WM_NORMAL_HINTS)
	{
	    w = findWindowAtDisplay (d, event->xproperty.window);
	    if (w)
	    {
		updateNormalHints (w);
		recalcWindowActions (w);
	    }
	}
	else if (event->xproperty.atom == XA_WM_HINTS)
	{
	    w = findWindowAtDisplay (d, event->xproperty.window);
	    if (w)
		updateWmHints (w);
	}
	else if (event->xproperty.atom == XA_WM_TRANSIENT_FOR)
	{
	    w = findWindowAtDisplay (d, event->xproperty.window);
	    if (w)
		updateTransientHint (w);
	}
	else if (event->xproperty.atom == d->wmClientLeaderAtom)
	{
	    w = findWindowAtDisplay (d, event->xproperty.window);
	    if (w)
		w->clientLeader = getClientLeader (w);
	}
	else if (event->xproperty.atom == d->winOpacityAtom)
	{
	    w = findWindowAtDisplay (d, event->xproperty.window);
	    if (w && (w->type & CompWindowTypeDesktopMask) == 0)
	    {
		GLushort opacity;

		opacity = getWindowProp32 (d, w->id,
					   d->winOpacityAtom,
					   OPAQUE);
		if (opacity != w->opacity)
		{
		    w->opacity = w->paint.opacity = opacity;
		    addWindowDamage (w);
		}
	    }
	}
	else if (event->xproperty.atom == d->winBrightnessAtom)
	{
	    w = findWindowAtDisplay (d, event->xproperty.window);
	    if (w)
	    {
		GLushort brightness;

		brightness = getWindowProp32 (d, w->id,
					      d->winBrightnessAtom,
					      BRIGHT);
		if (brightness != w->brightness)
		{
		    w->brightness = brightness;
		    if (w->alive)
		    {
			w->paint.brightness = w->brightness;
			addWindowDamage (w);
		    }
		}
	    }
	}
	else if (event->xproperty.atom == d->winSaturationAtom)
	{
	    w = findWindowAtDisplay (d, event->xproperty.window);
	    if (w && w->screen->canDoSaturated)
	    {
		GLushort saturation;

		saturation = getWindowProp32 (d, w->id,
					      d->winSaturationAtom,
					      COLOR);
		if (saturation != w->saturation)
		{
		    w->saturation = saturation;
		    if (w->alive)
		    {
			w->paint.saturation = w->saturation;
			addWindowDamage (w);
		    }
		}
	    }
	}
	else if (event->xproperty.atom == d->xBackgroundAtom[0] ||
		 event->xproperty.atom == d->xBackgroundAtom[1])
	{
	    s = findScreenAtDisplay (d, event->xproperty.window);
	    if (s)
	    {
		finiTexture (s, &s->backgroundTexture);
		initTexture (s, &s->backgroundTexture);

		damageScreen (s);
	    }
	}
	else if (event->xproperty.atom == d->wmStrutAtom ||
		 event->xproperty.atom == d->wmStrutPartialAtom)
	{
	    w = findWindowAtDisplay (d, event->xproperty.window);
	    if (w)
	    {
		if (updateWindowStruts (w))
		    updateWorkareaForScreen (w->screen);
	    }
	}
	else if (event->xproperty.atom == d->mwmHintsAtom)
	{
	    w = findWindowAtDisplay (d, event->xproperty.window);
	    if (w)
	    {
		w->mwmDecor = getMwmDecor (d, w->id);

		updateWindowAttributes (w, FALSE);
	    }
	}
	else if (event->xproperty.atom == d->wmProtocolsAtom)
	{
	    w = findWindowAtDisplay (d, event->xproperty.window);
	    if (w)
		w->protocols = getProtocols (d, w->id);
	}
	else if (event->xproperty.atom == d->wmIconAtom)
	{
	    w = findWindowAtDisplay (d, event->xproperty.window);
	    if (w)
		freeWindowIcons (w);
	}
	else if (event->xproperty.atom == d->startupIdAtom)
	{
	    w = findWindowAtDisplay (d, event->xproperty.window);
	    if (w)
	    {
		if (w->startupId)
		    free (w->startupId);

		w->startupId = getStartupId (w);
	    }
	}
	else if (event->xproperty.atom == d->winDesktopAtom)
	{
	    if (getWindowProp32 (d, event->xproperty.window,
				 d->winDesktopAtom, 1) != 0)
		setWindowProp32 (d, event->xproperty.window,
				 d->winDesktopAtom, 0);
	}
	else if (event->xproperty.atom == XA_WM_CLASS)
	{
	    w = findWindowAtDisplay (d, event->xproperty.window);
	    if (w)
		updateWindowClassHints (w);
	}
	break;
    case MotionNotify:
	break;
    case ClientMessage:
	if (event->xclient.message_type == d->winActiveAtom)
	{
	    w = findWindowAtDisplay (d, event->xclient.window);
	    if (w)
		activateWindow (w);
	}
	else if (event->xclient.message_type == d->winOpacityAtom)
	{
	    w = findWindowAtDisplay (d, event->xclient.window);
	    if (w && (w->type & CompWindowTypeDesktopMask) == 0)
	    {
		GLushort opacity;

		opacity = event->xclient.data.l[0] >> 16;
		if (opacity != w->paint.opacity)
		{
		    w->paint.opacity = opacity;

		    setWindowProp32 (d, w->id, d->winOpacityAtom,
				     w->paint.opacity);
		    addWindowDamage (w);
		}
	    }
	}
	else if (event->xclient.message_type == d->winBrightnessAtom)
	{
	    w = findWindowAtDisplay (d, event->xclient.window);
	    if (w)
	    {
		GLushort brightness;

		brightness = event->xclient.data.l[0] >> 16;
		if (brightness != w->paint.brightness)
		{
		    w->paint.brightness = brightness;

		    setWindowProp32 (d, w->id, d->winBrightnessAtom,
				     w->paint.brightness);
		    addWindowDamage (w);
		}
	    }
	}
	else if (event->xclient.message_type == d->winSaturationAtom)
	{
	    w = findWindowAtDisplay (d, event->xclient.window);
	    if (w && w->screen->canDoSaturated)
	    {
		GLushort saturation;

		saturation = event->xclient.data.l[0] >> 16;
		if (saturation != w->saturation)
		{
		    w->saturation = saturation;

		    setWindowProp32 (d, w->id, d->winSaturationAtom,
				     w->saturation);

		    if (w->alive)
		    {
			w->paint.saturation = w->saturation;
			addWindowDamage (w);
		    }
		}
	    }
	}
	else if (event->xclient.message_type == d->winStateAtom)
	{
	    w = findWindowAtDisplay (d, event->xclient.window);
	    if (w)
	    {
		unsigned long wState, state;
		int	      i;

		wState = w->state;

		for (i = 1; i < 3; i++)
		{
		    state = windowStateMask (d, event->xclient.data.l[i]);
		    if (state & ~CompWindowStateHiddenMask)
		    {

#define _NET_WM_STATE_REMOVE 0
#define _NET_WM_STATE_ADD    1
#define _NET_WM_STATE_TOGGLE 2

			switch (event->xclient.data.l[0]) {
			case _NET_WM_STATE_REMOVE:
			    wState &= ~state;
			    break;
			case _NET_WM_STATE_ADD:
			    wState |= state;
			    break;
			case _NET_WM_STATE_TOGGLE:
			    wState ^= state;
			    break;
			}
		    }
		}

		wState = constrainWindowState (wState, w->actions);

		if (wState != w->state)
		{
		    w->state = wState;

		    recalcWindowType (w);
		    recalcWindowActions (w);

		    updateWindowAttributes (w, FALSE);

		    setWindowState (d, w->state, w->id);
		}
	    }
	}
	else if (event->xclient.message_type == d->wmProtocolsAtom)
	{
	    if (event->xclient.data.l[0] == d->wmPingAtom)
	    {
		w = findWindowAtDisplay (d, event->xclient.data.l[2]);
		if (w)
		{
		    if (!w->alive)
		    {
			w->alive	    = TRUE;
			w->paint.saturation = w->saturation;
			w->paint.brightness = w->brightness;

			if (w->lastCloseRequestTime)
			{
			    toolkitAction (w->screen,
					   d->toolkitActionForceQuitDialogAtom,
					   w->lastCloseRequestTime,
					   w->id,
					   FALSE,
					   0,
					   0);

			    w->lastCloseRequestTime = 0;
			}

			addWindowDamage (w);
		    }
		    w->lastPong = d->lastPing;
		}
	    }
	}
	else if (event->xclient.message_type == d->closeWindowAtom)
	{
	    w = findWindowAtDisplay (d, event->xclient.window);
	    if (w)
		closeWindow (w, event->xclient.data.l[0]);
	}
	else if (event->xclient.message_type == d->desktopGeometryAtom)
	{
	    s = findScreenAtDisplay (d, event->xclient.window);
	    if (s)
	    {
		CompOptionValue value;

		value.i = event->xclient.data.l[0] / s->width;

		(*s->setScreenOption) (s, "size", &value);
	    }
	}
	else if (event->xclient.message_type == d->moveResizeWindowAtom)
	{
	    w = findWindowAtDisplay (d, event->xclient.window);
	    if (w)
	    {
		unsigned int   xwcm = 0;
		XWindowChanges xwc;
		int            gravity;

		memset (&xwc, 0, sizeof (xwc));

		if (event->xclient.data.l[0] & (1 << 8))
		{
		    xwcm |= CWX;
		    xwc.x = event->xclient.data.l[1];
		}

		if (event->xclient.data.l[0] & (1 << 9))
		{
		    xwcm |= CWY;
		    xwc.y = event->xclient.data.l[2];
		}

		if (event->xclient.data.l[0] & (1 << 10))
		{
		    xwcm |= CWWidth;
		    xwc.width = event->xclient.data.l[3];
		}

		if (event->xclient.data.l[0] & (1 << 11))
		{
		    xwcm |= CWHeight;
		    xwc.height = event->xclient.data.l[4];
		}

		gravity = event->xclient.data.l[0] & 0xFF;

		moveResizeWindow (w, &xwc, xwcm, gravity);
	    }
	}
	else if (event->xclient.message_type == d->restackWindowAtom)
	{
	    w = findWindowAtDisplay (d, event->xclient.window);
	    if (w)
	    {
		/* TODO: other stack modes than Above and Below */
		if (event->xclient.data.l[1])
		{
		    CompWindow *sibling;

		    sibling = findWindowAtDisplay (d, event->xclient.data.l[1]);
		    if (sibling)
		    {
			if (event->xclient.data.l[2] == Above)
			    restackWindowAbove (w, sibling);
			else if (event->xclient.data.l[2] == Below)
			    restackWindowBelow (w, sibling);
		    }
		}
		else
		{
		    if (event->xclient.data.l[2] == Above)
			raiseWindow (w);
		    else if (event->xclient.data.l[2] == Below)
			lowerWindow (w);
		}
	    }
	}
	else if (event->xclient.message_type == d->wmChangeStateAtom)
	{
	    w = findWindowAtDisplay (d, event->xclient.window);
	    if (w && w->type & CompWindowTypeNormalMask)
	    {
		if (event->xclient.data.l[0] == IconicState)
		    minimizeWindow (w);
		else if (event->xclient.data.l[0] == NormalState)
		    unminimizeWindow (w);
	    }
	}
	else if (event->xclient.message_type == d->showingDesktopAtom)
	{
	    for (s = d->screens; s; s = s->next)
	    {
		if (event->xclient.window == s->root ||
		    event->xclient.window == None)
		{
		    if (event->xclient.data.l[0])
			enterShowDesktopMode (s);
		    else
			leaveShowDesktopMode (s, NULL);
		}
	    }
	}
	break;
    case MappingNotify:
	updateModifierMappings (d);
	break;
    case MapRequest:
	w = findWindowAtDisplay (d, event->xmaprequest.window);
	if (w)
	{
	    if (w->minimized)
		unminimizeWindow (w);

	    leaveShowDesktopMode (w->screen, w);

	    w->managed = TRUE;

	    if (!(w->state & CompWindowStateHiddenMask))
	    {
		w->initialViewport = w->screen->x;

		applyStartupProperties (w->screen, w);

		XMapWindow (d->display, event->xmaprequest.window);

		updateWindowAttributes (w, FALSE);

		if (focusWindowOnMap (w))
		    moveInputFocusToWindow (w);
	    }
	}
	else
	{
	    XMapWindow (d->display, event->xmaprequest.window);
	}
	break;
    case ConfigureRequest:
	w = findWindowAtDisplay (d, event->xconfigurerequest.window);
	if (w && w->managed)
	{
	    XWindowChanges xwc;

	    memset (&xwc, 0, sizeof (xwc));

	    xwc.x	     = event->xconfigurerequest.x;
	    xwc.y	     = event->xconfigurerequest.y;
	    xwc.width	     = event->xconfigurerequest.width;
	    xwc.height       = event->xconfigurerequest.height;
	    xwc.border_width = event->xconfigurerequest.border_width;

	    moveResizeWindow (w, &xwc, event->xconfigurerequest.value_mask, 0);
	}
	else
	{
	    XWindowChanges xwc;
	    unsigned int   xwcm;

	    xwcm = event->xconfigurerequest.value_mask &
		(CWX | CWY | CWWidth | CWHeight | CWBorderWidth);

	    xwc.x	     = event->xconfigurerequest.x;
	    xwc.y	     = event->xconfigurerequest.y;
	    xwc.width	     = event->xconfigurerequest.width;
	    xwc.height	     = event->xconfigurerequest.height;
	    xwc.border_width = event->xconfigurerequest.border_width;

	    XConfigureWindow (d->display, event->xconfigurerequest.window,
			      xwcm, &xwc);
	}
	break;
    case CirculateRequest:
	break;
    case FocusIn:
	if (event->xfocus.mode != NotifyGrab)
	{
	    w = findWindowAtDisplay (d, event->xfocus.window);
	    if (w && w->managed && w->id != d->activeWindow)
	    {
		XChangeProperty (d->display, w->screen->root,
				 d->winActiveAtom,
				 XA_WINDOW, 32, PropModeReplace,
				 (unsigned char *) &w->id, 1);
	    }
	}
	break;
    case EnterNotify:
	if (!d->screens->maxGrab		    &&
	    event->xcrossing.mode   != NotifyGrab   &&
	    event->xcrossing.detail != NotifyInferior)
	{
	    Bool raise;
	    int  delay;

	    raise = d->opt[COMP_DISPLAY_OPTION_AUTORAISE].value.b;
	    delay = d->opt[COMP_DISPLAY_OPTION_AUTORAISE_DELAY].value.i;

	    s = findScreenAtDisplay (d, event->xcrossing.root);
	    if (s)
	    {
		w = findTopLevelWindowAtScreen (s, event->xcrossing.window);
	    }
	    else
		w = NULL;

	    if (w && w->id != d->below)
	    {
		d->below = w->id;

		if (!d->opt[COMP_DISPLAY_OPTION_CLICK_TO_FOCUS].value.b)
		{
		    if (d->autoRaiseHandle &&
			d->autoRaiseWindow != w->id)
		    {
			compRemoveTimeout (d->autoRaiseHandle);
			d->autoRaiseHandle = 0;
		    }

		    if (w->type & ~(CompWindowTypeDockMask |
				    CompWindowTypeDesktopMask))
		    {
			moveInputFocusToWindow (w);

			if (raise)
			{
			    if (delay > 0)
			    {
				d->autoRaiseWindow = w->id;
				d->autoRaiseHandle =
				    compAddTimeout (delay, autoRaiseTimeout, d);
			    }
			    else
				updateWindowAttributes (w, FALSE);
			}
		    }
		}
	    }
	}
	break;
    case LeaveNotify:
	if (event->xcrossing.detail != NotifyInferior)
	{
	    if (event->xcrossing.window == d->below)
		d->below = None;
	}
	break;
    default:
	if (event->type == d->damageEvent + XDamageNotify)
	{
	    XDamageNotifyEvent *de = (XDamageNotifyEvent *) event;

	    if (lastDamagedWindow && de->drawable == lastDamagedWindow->id)
	    {
		w = lastDamagedWindow;
	    }
	    else
	    {
		w = findWindowAtDisplay (d, de->drawable);
		if (w)
		    lastDamagedWindow = w;
	    }

	    if (w)
	    {
		w->texture.oldMipmaps = TRUE;

		if (w->syncWait)
		{
		    if (w->nDamage == w->sizeDamage)
		    {
			if (w->damageRects)
			{
			    w->damageRects = realloc (w->damageRects,
						      (w->sizeDamage + 1) *
						      sizeof (XRectangle));
			    w->sizeDamage += 1;
			}
			else
			{
			    w->damageRects = malloc (sizeof (XRectangle));
			    w->sizeDamage  = 1;
			}
		    }

		    w->damageRects[w->nDamage].x      = de->area.x;
		    w->damageRects[w->nDamage].y      = de->area.y;
		    w->damageRects[w->nDamage].width  = de->area.width;
		    w->damageRects[w->nDamage].height = de->area.height;
		    w->nDamage++;
		}
		else
		{
		    handleWindowDamageRect (w,
					    de->area.x,
					    de->area.y,
					    de->area.width,
					    de->area.height);
		}
	    }
	}
	else if (d->shapeExtension &&
		 event->type == d->shapeEvent + ShapeNotify)
	{
	    w = findWindowAtDisplay (d, ((XShapeEvent *) event)->window);
	    if (w)
	    {
		if (w->mapNum)
		{
		    addWindowDamage (w);
		    updateWindowRegion (w);
		    addWindowDamage (w);
		}
	    }
	}
	else if (event->type == d->randrEvent + RRScreenChangeNotify)
	{
	    XRRScreenChangeNotifyEvent *rre;

	    rre = (XRRScreenChangeNotifyEvent *) event;

	    s = findScreenAtDisplay (d, rre->root);
	    if (s)
		detectRefreshRateOfScreen (s);
	}
	else if (event->type == d->syncEvent + XSyncAlarmNotify)
	{
	    XSyncAlarmNotifyEvent *sa;

	    sa = (XSyncAlarmNotifyEvent *) event;

	    w = NULL;

	    for (s = d->screens; s; s = s->next)
		for (w = s->windows; w; w = w->next)
		    if (w->syncAlarm == sa->alarm)
			break;

	    if (w)
		handleSyncAlarm (w);
	}
	break;
    }
}

#define REAL_MOD_MASK (ShiftMask | ControlMask | Mod1Mask | \
		       Mod2Mask | Mod3Mask | Mod4Mask | Mod5Mask)

Bool
eventMatches (CompDisplay *display,
	      XEvent      *event,
	      CompOption  *option)
{
    unsigned int modMask, bindMods, state = 0;
    CompBinding *bind = &option->value.bind;

    switch (event->type) {
    case ButtonPress:
	if (bind->type != CompBindingTypeButton ||
	    bind->u.button.button != event->xbutton.button)
	    return FALSE;

	modMask = REAL_MOD_MASK & ~display->ignoredModMask;
	bindMods = virtualToRealModMask (display, bind->u.button.modifiers);

	return (bindMods & modMask) == (event->xbutton.state & modMask);

    case KeyPress:
	if (bind->type != CompBindingTypeKey)
	    return FALSE;

	modMask = REAL_MOD_MASK & ~display->ignoredModMask;
	bindMods = virtualToRealModMask (display, bind->u.key.modifiers);

	if (bind->u.key.keycode == 0)
	{
	    state = keycodeToModifiers (display, event->xkey.keycode);
	    if (state == 0)
		return FALSE;
	    modMask = bindMods;
	}
	else if (bind->u.key.keycode != event->xkey.keycode)
	    return FALSE;

	state |= event->xkey.state;

	return (bindMods & modMask) == (state & modMask);

    default:
	return FALSE;
    }
}

Bool
eventTerminates (CompDisplay *display,
		 XEvent      *event,
		 CompOption  *option)
{
    unsigned int modMask, bindMods, state;
    CompBinding *bind = &option->value.bind;
    CompScreen *s;

    switch (event->type) {
    case ButtonRelease:
	return (bind->type == CompBindingTypeButton &&
		bind->u.button.button == event->xbutton.button);

    case KeyPress:
	s = findScreenAtDisplay (display, event->xkey.root);
	return (s && event->xkey.keycode == s->escapeKeyCode);

    case KeyRelease:
	if (bind->type != CompBindingTypeKey)
	    return FALSE;

	state = keycodeToModifiers (display, event->xkey.keycode);
	if (state == 0)
	    return FALSE;

	modMask = REAL_MOD_MASK & ~display->ignoredModMask;
	bindMods = virtualToRealModMask (display, bind->u.key.modifiers);
	state = event->xkey.state & ~state;

	return (state & modMask & bindMods) != bindMods;

    default:
	return FALSE;
    }
}
