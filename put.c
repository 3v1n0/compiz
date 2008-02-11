/*
 * Copyright (c) 2006 Darryll Truchan <moppsy@comcast.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <X11/Xatom.h>

#include <compiz-core.h>
#include "put_options.h"

#define GET_PUT_DISPLAY(d) \
    ((PutDisplay *) (d)->base.privates[displayPrivateIndex].ptr)
#define PUT_DISPLAY(d) \
    PutDisplay *pd = GET_PUT_DISPLAY (d)

#define GET_PUT_SCREEN(s, pd) \
    ((PutScreen *) (s)->base.privates[(pd)->screenPrivateIndex].ptr)
#define PUT_SCREEN(s) \
    PutScreen *ps = GET_PUT_SCREEN (s, GET_PUT_DISPLAY (s->display))

#define GET_PUT_WINDOW(w, ps) \
    ((PutWindow *) (w)->base.privates[(ps)->windowPrivateIndex].ptr)
#define PUT_WINDOW(w) \
    PutWindow *pw = GET_PUT_WINDOW (w, \
		    GET_PUT_SCREEN (w->screen, \
		    GET_PUT_DISPLAY (w->screen->display)))

static int displayPrivateIndex;

typedef enum
{
    PutUnknown = 0,
    PutBottomLeft = 1,
    PutBottom = 2,
    PutBottomRight = 3,
    PutLeft = 4,
    PutCenter = 5,
    PutRight = 6,
    PutTopLeft = 7,
    PutTop = 8,
    PutTopRight = 9,
    PutRestore = 10,
    PutViewport = 11,
    PutViewportLeft = 12,
    PutViewportRight = 13,
    PutAbsolute = 14,
    PutPointer = 15,
    PutViewportUp = 16,
    PutViewportDown = 17
} PutType;

typedef struct _PutDisplay
{
    int screenPrivateIndex;

    HandleEventProc handleEvent; /* handle event function pointer */

    Window  lastWindow;
    PutType lastType;

    Atom compizPutWindowAtom;    /* client event atom             */
} PutDisplay;

typedef struct _PutScreen
{
    int windowPrivateIndex;

    PreparePaintScreenProc preparePaintScreen;	/* function pointer         */
    DonePaintScreenProc    donePaintScreen;	/* function pointer         */
    PaintOutputProc        paintOutput;	        /* function pointer         */
    PaintWindowProc        paintWindow;	        /* function pointer         */

    int        moreAdjust;			/* animation flag           */
    int        grabIndex;			/* screen grab index        */
} PutScreen;

typedef struct _PutWindow
{
    GLfloat xVelocity, yVelocity;	/* animation velocity       */
    GLfloat tx, ty;			/* animation translation    */

    int lastX, lastY;			/* starting position        */
    int targetX, targetY;               /* target of the animation  */

    Bool adjust;			/* animation flag           */
} PutWindow;

/*
 * calculate the velocity for the moving window
 */
static int
adjustPutVelocity (CompWindow *w)
{
    float dx, dy, adjust, amount;
    float x1, y1;

    PUT_WINDOW (w);

    x1 = pw->targetX;
    y1 = pw->targetY;

    dx = x1 - (w->attrib.x + pw->tx);
    dy = y1 - (w->attrib.y + pw->ty);

    adjust = dx * 0.15f;
    amount = fabs (dx) * 1.5;
    if (amount < 0.5f)
	amount = 0.5f;
    else if (amount > 5.0f)
	amount = 5.0f;

    pw->xVelocity = (amount * pw->xVelocity + adjust) / (amount + 1.0f);

    adjust = dy * 0.15f;
    amount = fabs (dy) * 1.5f;
    if (amount < 0.5f)
	amount = 0.5f;
    else if (amount > 5.0f)
	amount = 5.0f;

    pw->yVelocity = (amount * pw->yVelocity + adjust) / (amount + 1.0f);

    if (fabs (dx) < 0.1f && fabs (pw->xVelocity) < 0.2f &&
	fabs (dy) < 0.1f && fabs (pw->yVelocity) < 0.2f)
    {
	/* animation done */
	pw->xVelocity = pw->yVelocity = 0.0f;

	pw->tx = x1 - w->attrib.x;
	pw->ty = y1 - w->attrib.y;
	return 0;
    }
    return 1;
}

/*
 * setup for paint screen
 */
static void
putPreparePaintScreen (CompScreen *s,
		       int        msSinceLastPaint)
{
    PUT_SCREEN (s);

    if (ps->moreAdjust && ps->grabIndex)
    {
	CompWindow *w;
	int        steps;
	float      amount, chunk;

	amount = msSinceLastPaint * 0.025f * putGetSpeed (s);
	steps = amount / (0.5f * putGetTimestep (s));
	if (!steps)
	    steps = 1;
	chunk = amount / (float)steps;

	while (steps--)
	{
	    Window endAnimationWindow = None;

	    ps->moreAdjust = 0;
	    for (w = s->windows; w; w = w->next)
	    {
		PUT_WINDOW (w);

		if (pw->adjust)
		{
		    pw->adjust = adjustPutVelocity (w);
		    ps->moreAdjust |= pw->adjust;

		    pw->tx += pw->xVelocity * chunk;
		    pw->ty += pw->yVelocity * chunk;

		    if (!pw->adjust)
		    {
			/* animation done */
			moveWindow (w, pw->targetX - w->attrib.x,
				    pw->targetY - w->attrib.y, TRUE, TRUE);
			syncWindowPosition (w);
			updateWindowAttributes (w, CompStackingUpdateModeNone);
			endAnimationWindow = w->id;
			pw->tx = pw->ty = 0;
		    }
		}
    	    }
	    if (!ps->moreAdjust)
	    {
		/* unfocus moved window if enabled */
		if (putGetUnfocusWindow (s))
		    focusDefaultWindow (s);
		else if (endAnimationWindow)
		    sendWindowActivationRequest (s, endAnimationWindow);
		break;
    	    }
	}
    }

    UNWRAP (ps, s, preparePaintScreen);
    (*s->preparePaintScreen) (s, msSinceLastPaint);
    WRAP (ps, s, preparePaintScreen, putPreparePaintScreen);
}

/*
 * after painting clean up
 */
static void
putDonePaintScreen (CompScreen *s)
{
    PUT_SCREEN (s);

    if (ps->moreAdjust && ps->grabIndex)
	damageScreen (s);
    else
    {
	if (ps->grabIndex)
	{
	    /* release the screen grab */
	    removeScreenGrab (s, ps->grabIndex, NULL);
	    ps->grabIndex = 0;
	}
    }

    UNWRAP (ps, s, donePaintScreen);
    (*s->donePaintScreen) (s);
    WRAP (ps, s, donePaintScreen, putDonePaintScreen);
}

static Bool
putPaintOutput (CompScreen              *s,
		const ScreenPaintAttrib *sAttrib,
     		const CompTransform     *transform,
		Region                  region,
		CompOutput              *output,
		unsigned int            mask)
{
    Bool status;

    PUT_SCREEN (s);

    if (ps->moreAdjust)
	mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS_MASK;

    UNWRAP (ps, s, paintOutput);
    status = (*s->paintOutput) (s, sAttrib, transform, region, output, mask);
    WRAP (ps, s, paintOutput, putPaintOutput);

    return status;
}

static Bool
putPaintWindow (CompWindow              *w,
		const WindowPaintAttrib *attrib,
     		const CompTransform     *transform,
		Region                  region,
		unsigned int            mask)
{
    CompScreen *s = w->screen;
    Bool status;

    PUT_SCREEN (s);
    PUT_WINDOW (w);

    if (pw->adjust)
    {
	CompTransform wTransform = *transform;

	matrixTranslate (&wTransform, pw->tx, pw->ty, 0.0f);

	mask |= PAINT_WINDOW_TRANSFORMED_MASK;

	UNWRAP (ps, s, paintWindow);
	status = (*s->paintWindow) (w, attrib, &wTransform, region, mask);
	WRAP (ps, s, paintWindow, putPaintWindow);
    }
    else
    {
    	UNWRAP (ps, s, paintWindow);
	status = (*s->paintWindow) (w, attrib, transform, region, mask);
	WRAP (ps, s, paintWindow, putPaintWindow);
    }

    return status;
}

/*
 * initiate action callback
 */
static Bool
putInitiateCommon (CompDisplay     *d,
		   CompAction      *action,
		   CompActionState state,
		   CompOption      *option,
		   int             nOption,
		   PutType         type)
{
    CompWindow *w;
    Window     xid;

    xid = getIntOptionNamed (option, nOption, "window", 0);
    if (!xid)
	xid = d->activeWindow;

    w = findWindowAtDisplay (d, xid);
    if (w)
    {
	CompScreen *s = w->screen;

	PUT_SCREEN (s);

	if (!ps->grabIndex)
	{
	    /* this will keep put from working while something
	       else has a screen grab */
	    if (otherScreenGrabExist (s, "put", 0))
		return FALSE;

	    /* we are ok, so grab the screen */
	    ps->grabIndex = pushScreenGrab (s, s->invisibleCursor, "put");
	}

	if (ps->grabIndex)
	{
	    int        px, py, x, y, dx, dy;
	    int        head, width, height, hx, hy;
	    XRectangle workArea;

	    PUT_DISPLAY (d);
	    PUT_WINDOW (w);

	    px = getIntOptionNamed (option, nOption, "x", 0);
	    py = getIntOptionNamed (option, nOption, "y", 0);

	    /* we don't want to do anything with override redirect windows */
	    if (w->attrib.override_redirect)
		return FALSE;

	    /* we don't want to be moving the desktop, docks,
	       or fullscreen windows */
	    if (w->type & CompWindowTypeDesktopMask ||
		w->type & CompWindowTypeDockMask ||
		w->type & CompWindowTypeFullscreenMask)
	    {
		return FALSE;
	    }

	    /* get the Xinerama head from the options list */
	    head = getIntOptionNamed(option, nOption, "head", -1);
	    /* no head in options list so we use the current head */
	    if (head == -1)
	    {
		/* no head given, so use the current head if this wasn't
		   a double tap */
		if (pd->lastType != type || pd->lastWindow != w->id)
		    head = outputDeviceForWindow (w);
	    }
	    else
	    {
		/* make sure the head number is not out of bounds */
		head = MIN (head, s->nOutputDev - 1);
	    }

	    if (head == -1)
	    {
		/* user double-tapped the key, so use the screen work area */
		workArea = s->workArea;
		/* set the type to unknown to have a toggle-type behaviour
		   between 'use head's work area' and 'use screen work area' */
		pd->lastType = PutUnknown;
	    }
	    else
	    {
		/* single tap or head provided via options list,
		   use the head's work area */
		getWorkareaForOutput (s, head, &workArea);
		pd->lastType = type;
	    }

	    width  = workArea.width;
	    height = workArea.height;
	    hx     = workArea.x;
	    hy     = workArea.y;

	    pd->lastWindow = w->id;

    	    /* the windows location */
	    x = w->attrib.x;
	    y = w->attrib.y;

	    /*
	     * handle the put types
	     */
	    switch (type)
	    {
	    case PutCenter:
		/* center of the screen */
		dx = (width / 2) - (w->width / 2) - (x - hx);
		dy = (height / 2) - (w->height / 2) - (y - hy);
		break;
	    case PutLeft:
		/* center of the left edge */
		dx = -(x - hx) + w->input.left + putGetPadLeft (s);
		dy = (height / 2) - (w->height / 2) - (y - hy);
		break;
	    case PutTopLeft:
		/* top left corner */
		dx = -(x - hx) + w->input.left + putGetPadLeft (s);
		dy = -(y - hy) + w->input.top + putGetPadTop (s);
		break;
	    case PutTop:
		/* center of top edge */
		dx = (width / 2) - (w->width / 2) - (x - hx);
		dy = -(y - hy) + w->input.top + putGetPadTop (s);
		break;
	    case PutTopRight:
		/* top right corner */
		dx = width - w->width - (x - hx) -
		     w->input.right - putGetPadRight (s);
		dy = -(y - hy) + w->input.top + putGetPadTop (s);
		break;
	    case PutRight:
		/* center of right edge */
		dx = width - w->width - (x - hx) -
		     w->input.right - putGetPadRight (s);
		dy = (height / 2) - (w->height / 2) - (y - hy);
		break;
	    case PutBottomRight:
		/* bottom right corner */
		dx = width - w->width - (x - hx) -
		     w->input.right - putGetPadRight (s);
		dy = height - w->height - (y - hy) -
		     w->input.bottom - putGetPadBottom (s);
		break;
	    case PutBottom:
		/* center of bottom edge */
		dx = (width / 2) - (w->width / 2) - (x - hx);
		dy = height - w->height - (y - hy) -
		     w->input.bottom - putGetPadBottom (s);
		break;
	    case PutBottomLeft:
		/* bottom left corner */
		dx = -(x - hx) + w->input.left + putGetPadLeft (s);
		dy = height - w->height - (y - hy) -
		     w->input.bottom - putGetPadBottom (s);
		break;
	    case PutRestore:
		/* back to last position */
		dx = pw->lastX - x;
		dy = pw->lastY - y;
		break;
	    case PutViewport:
		{
		    int face, faceX, faceY, hDirection, vDirection;

		    /* get the face to move to from the options list */
		    face = getIntOptionNamed(option, nOption, "face", -1);

		    /* if it wasn't supplied, bail out */
		    if (face < 0)
			return FALSE;

		    /* split 1D face value into 2D x and y face */
		    faceX = face % s->hsize;
		    faceY = face / s->hsize;
		    if (faceY > s->vsize)
			faceY = s->vsize - 1;

	    	    /* take the shortest horizontal path to the
		       destination viewport */
    		    hDirection = (faceX - s->x);
		    if (hDirection > s->hsize / 2)
			hDirection = (hDirection - s->hsize);
		    else if (hDirection < -s->hsize / 2)
			hDirection = (hDirection + s->hsize);

		    /* we need to do this for the vertical
		       destination viewport too */
    		    vDirection = (faceY - s->y);
		    if (vDirection > s->vsize / 2)
			vDirection = (vDirection - s->vsize);
		    else if (vDirection < -s->hsize / 2)
			vDirection = (vDirection + s->vsize);

		    dx = s->width * hDirection;
	    	    dy = s->height * vDirection;
    		    break;
		}
	    case PutViewportLeft:
		/* move to the viewport on the left */
		dx = -s->width;
		dy = 0;
		break;
	    case PutViewportRight:
		/* move to the viewport on the right */
		dx = s->width;
		dy = 0;
		break;
	    case PutViewportUp:
		/* move to the viewport above */
		dx = 0;
		dy = -s->height;
		break;
	    case PutViewportDown:
		/* move to the viewport below */
		dx = 0;
		dy = s->height;
		break;
	    case PutAbsolute:
		/* move the window to an exact position */
		if (px < 0)
		    /* account for a specified negative position,
		       like geometry without (-0) */
		    dx = px + s->width - w->width - x - w->input.right;
		else
		    dx = px - x + w->input.left;

		if (py < 0)
		    /* account for a specified negative position,
		       like geometry without (-0) */
		    dy = py + s->height - w->height - y - w->input.bottom;
		else
		    dy = py - y + w->input.top;
		break;
	    case PutPointer:
		{
		    /* move the window to the pointers position
		     * using the current quad of the screen to determine
		     * which corner to move to the pointer
		     */
		    int          rx, ry;
		    Window       root, child;
		    int          winX, winY;
		    unsigned int pMask;

		    /* get the pointers position from the X server */
    		    if (XQueryPointer (d->display, w->id, &root, &child,
				       &rx, &ry, &winX, &winY, &pMask)) 
		    {

		        if (putGetWindowCenter (s))
			    {
			        /* window center */
			        dx = rx - (w->width / 2) - x;
				dy = ry - (w->height / 2) - y;
			    }
			else if (rx < s->workArea.width / 2 &&
				ry < s->workArea.height / 2)
			    {
			        /* top left quad */
			        dx = rx - x + w->input.left;
				dy = ry - y + w->input.top;
			    }
			else if (rx < s->workArea.width / 2 &&
				ry >= s->workArea.height / 2)
			    {
			        /* bottom left quad */
			        dx = rx - x + w->input.left;
				dy = ry - w->height - y - w->input.bottom;
			    }
			else if (rx >= s->workArea.width / 2 &&
				ry < s->workArea.height / 2)
			    {
			        /* top right quad */
			        dx = rx - w->width - x - w->input.right;
				dy = ry - y + w->input.top;
			    }
			else
			    {
			        /* bottom right quad */
			        dx = rx - w->width - x - w->input.right;
				dy = ry - w->height - y - w->input.bottom;
			    }
		    } else {
		        dx = dy = 0;
		    }
		}
		break;
	    default:
		/* if an unknown type is specified, do nothing */
		dx = dy = 0;
		break;
	    }

	    /* don't do anything if there is nothing to do */
	    if (dx != 0 || dy != 0)
	    {
		if (putGetAvoidOffscreen (s))
		{
		    /* avoids window borders offscreen */
		    if ((-(x - hx) + w->input.left + putGetPadLeft (s)) > dx)
		    {
			dx = -(x - hx) + w->input.left + putGetPadLeft (s);
		    }
	    	    else if ((width - w->width - (x - hx) -
			      w->input.right - putGetPadRight (s)) < dx)
		    {
			dx = width - w->width - (x - hx) -
			     w->input.right - putGetPadRight (s);
		    }

	    	    if ((-(y - hy) + w->input.top + putGetPadTop (s)) > dy)
		    {
			dy = -(y - hy) + w->input.top + putGetPadTop (s);
		    }
	    	    else if ((height - w->height - (y - hy) - w->input.bottom -
			      putGetPadBottom (s)) < dy)
		    {
			dy = height - w->height - (y - hy) -
			     w->input.bottom - putGetPadBottom (s);
		    }
		}
		/* save the windows position in the saveMask
		 * this is used when unmaximizing the window
		 */
		if (w->saveMask & CWX)
		    w->saveWc.x += dx;

		if (w->saveMask & CWY)
		    w->saveWc.y += dy;

		/* Make sure everyting starts out at the windows
		   current position */
		pw->lastX = x;
		pw->lastY = y;

		pw->targetX = x + dx;
		pw->targetY = y + dy;

		/* mark for animation */
		pw->adjust = TRUE;
		ps->moreAdjust = TRUE;

		/* cause repainting */
		addWindowDamage (w);
	    }
	}
    }

    /* tell event.c handleEvent to not call XAllowEvents */
    return FALSE;
}

static PutType
putTypeFromString (char *type)
{
    if (strcasecmp (type, "absolute") == 0)
	return PutAbsolute;
    else if (strcasecmp (type, "pointer") == 0)
	return PutPointer;
    else if (strcasecmp (type, "viewport") == 0)
	return PutViewport;
    else if (strcasecmp (type, "viewportleft") == 0)
	return PutViewportLeft;
    else if (strcasecmp (type, "viewportright") == 0)
	return PutViewportRight;
    else if (strcasecmp (type, "viewportup") == 0)
	return PutViewportUp;
    else if (strcasecmp (type, "viewportdown") == 0)
	return PutViewportDown;
    else if (strcasecmp (type, "restore") == 0)
	return PutRestore;
    else if (strcasecmp (type, "bottomleft") == 0)
	return PutBottomLeft;
    else if (strcasecmp (type, "left") == 0)
	return PutLeft;
    else if (strcasecmp (type, "topleft") == 0)
	return PutTopLeft;
    else if (strcasecmp (type, "top") == 0)
	return PutTop;
    else if (strcasecmp (type, "topright") == 0)
	return PutTopRight;
    else if (strcasecmp (type, "right") == 0)
	return PutRight;
    else if (strcasecmp (type, "bottomright") == 0)
	return PutBottomRight;
    else if (strcasecmp (type, "bottom") == 0)
	return PutBottom;
    else if (strcasecmp (type, "center") == 0)
	return PutCenter;
    else
	return PutUnknown;
}

static Bool
putInitiate (CompDisplay     *d,
	     CompAction      *action,
	     CompActionState state,
	     CompOption      *option,
	     int             nOption)
{
    PutType type = PutUnknown;
    char    *typeString;

    typeString = getStringOptionNamed (option, nOption, "type", NULL);
    if (typeString)
    	type = putTypeFromString (typeString);

    if (type == PutViewport)
	return putToViewport (d, action, state, option, nOption);
    else
	return putInitiateCommon (d, action, state, option, nOption, type);
}

static Bool
putToViewport (CompDisplay     *d,
	       CompAction      *action,
	       CompActionState state,
	       CompOption      *option,
	       int             nOption)
{
    int        face;
    CompOption o[4];

    /* get the face option */
    face = getIntOptionNamed (option, nOption, "face", -1);

    /* if it's not supplied, lets figure it out */
    if (face < 0)
    {
	PutDisplayOptions i;
	CompOption        *opt;

	i = PutDisplayOptionPutViewport1Key;

	while (i <= PutDisplayOptionPutViewport12Key)
	{
	    opt = putGetDisplayOption (d, i);
	    if (&opt->value.action == action)
	    {
		face = i - PutDisplayOptionPutViewport1Key;
		break;
	    }
	    i++;
	}
    }

    if (face < 0)
	return FALSE;

    /* setup the options for putInitiate */
    o[0].type    = CompOptionTypeInt;
    o[0].name    = "x";
    o[0].value.i = getIntOptionNamed (option, nOption, "x", 0);

    o[1].type    = CompOptionTypeInt;
    o[1].name    = "y";
    o[1].value.i = getIntOptionNamed (option, nOption, "y", 0);

    o[2].type    = CompOptionTypeInt;
    o[2].name    = "face";
    o[2].value.i = face;

    o[3].type    = CompOptionTypeInt;
    o[3].name    = "window";
    o[3].value.i = getIntOptionNamed (option, nOption, "window", 0);

    return putInitiateCommon (d, NULL, 0, o, 4, PutViewport);
}

static Bool
putViewportLeft (CompDisplay     *d,
		 CompAction      *action,
 		 CompActionState state,
		 CompOption      *option,
		 int             nOption)
{
    return putInitiateCommon (d, action, state,
			      option, nOption, PutViewportLeft);
}

static Bool
putViewportRight (CompDisplay     *d,
		  CompAction      *action,
 		  CompActionState state,
		  CompOption      *option,
		  int             nOption)
{
    return putInitiateCommon (d, action, state,
			      option, nOption, PutViewportRight);
}

static Bool
putViewportUp (CompDisplay     *d,
	       CompAction      *action,
	       CompActionState state,
	       CompOption      *option,
	       int             nOption)
{
    return putInitiateCommon (d, action, state,
			      option, nOption, PutViewportUp);
}

static Bool
putViewportDown (CompDisplay     *d,
		 CompAction      *action,
 		 CompActionState state,
		 CompOption      *option,
		 int             nOption)
{
    return putInitiateCommon (d, action, state,
			      option, nOption, PutViewportDown);
}

static Bool
restore (CompDisplay     *d,
	 CompAction      *action,
	 CompActionState state,
	 CompOption      *option,
	 int             nOption)
{
    return putInitiateCommon (d, action, state,
			      option, nOption, PutRestore);
}

static Bool
putPointer (CompDisplay     *d,
   	    CompAction      *action,
	    CompActionState state,
	    CompOption      *option,
	    int             nOption)
{
    return putInitiateCommon (d, action, state,
			      option, nOption, PutPointer);
}

static Bool
putCenter (CompDisplay     *d,
	   CompAction      *action,
	   CompActionState state,
	   CompOption      *option,
	   int             nOption)
{
    return putInitiateCommon (d, action, state,
			      option, nOption, PutCenter);
}

static Bool
putLeft (CompDisplay     *d,
	 CompAction      *action,
	 CompActionState state,
	 CompOption      *option,
	 int             nOption)
{
    return putInitiateCommon (d, action, state,
			      option, nOption, PutLeft);
}

static Bool
putTopLeft (CompDisplay     *d,
   	    CompAction      *action,
	    CompActionState state,
	    CompOption      *option,
	    int             nOption)
{
    return putInitiateCommon (d, action, state,
			      option, nOption, PutTopLeft);
}

static Bool
putTop (CompDisplay     *d,
	CompAction      *action,
	CompActionState state,
	CompOption      *option,
	int             nOption)
{
    return putInitiateCommon (d, action, state,
			      option, nOption, PutTop);
}

static Bool
putTopRight (CompDisplay     *d,
	     CompAction      *action,
	     CompActionState state,
	     CompOption      *option,
	     int             nOption)
{
    return putInitiateCommon (d, action, state,
			      option, nOption, PutTopRight);
}

static Bool
putRight (CompDisplay     *d,
 	  CompAction      *action,
	  CompActionState state,
	  CompOption      *option,
	  int             nOption)
{
    return putInitiateCommon (d, action, state,
			      option, nOption, PutRight);
}

static Bool
putBottomRight (CompDisplay     *d,
		CompAction      *action,
		CompActionState state,
		CompOption      *option,
		int             nOption)
{
    return putInitiateCommon (d, action, state,
			      option, nOption, PutBottomRight);
}

static Bool
putBottom (CompDisplay     *d,
	   CompAction      *action,
	   CompActionState state,
	   CompOption      *option,
	   int             nOption)
{
    return putInitiateCommon (d, action, state,
			      option, nOption, PutBottom);
}

static Bool
putBottomLeft (CompDisplay     *d,
	       CompAction      *action,
	       CompActionState state,
	       CompOption      *option,
	       int             nOption)
{
    return putInitiateCommon (d, action, state,
			      option, nOption, PutBottomLeft);
}

static void
putHandleEvent (CompDisplay *d,
		XEvent      *event)
{
    PUT_DISPLAY (d);

    switch (event->type)
    {
	/* handle client events */
    case ClientMessage:
	/* accept the custom atom for putting windows */
	if (event->xclient.message_type == pd->compizPutWindowAtom)
	{
	    CompWindow *w;

	    w = findWindowAtDisplay(d, event->xclient.window);
	    if (w)
	    {
		/*
		 * get the values from the xclientmessage event and populate
		 * the options for put initiate
		 *
		 * the format is 32
		 * and the data is
		 * l[0] = x position - unused (for future PutExact)
		 * l[1] = y position - unused (for future PutExact)
		 * l[2] = face number
		 * l[3] = put type, int value from enum
		 * l[4] = Xinerama head number
		 */
		CompOption opt[5];

		opt[0].type    = CompOptionTypeInt;
		opt[0].name    = "window";
		opt[0].value.i = event->xclient.window;

		opt[1].type    = CompOptionTypeInt;
		opt[1].name    = "x";
		opt[1].value.i = event->xclient.data.l[0];

		opt[2].type    = CompOptionTypeInt;
		opt[2].name    = "y";
		opt[2].value.i = event->xclient.data.l[1];

		opt[3].type    = CompOptionTypeInt;
		opt[3].name    = "face";
		opt[3].value.i = event->xclient.data.l[2];

		opt[4].type    = CompOptionTypeInt;
		opt[4].name    = "head";
		opt[4].value.i = event->xclient.data.l[4];

		putInitiateCommon (w->screen->display, NULL, 0, opt, 5,
				   event->xclient.data.l[3]);
	    }
	}
	break;
    default:
	break;
    }

    UNWRAP (pd, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (pd, d, handleEvent, putHandleEvent);
}

static Bool
putInitDisplay (CompPlugin  *p,
		CompDisplay *d)
{
    PutDisplay *pd;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    pd = malloc (sizeof (PutDisplay));
    if (!pd)
	return FALSE;

    pd->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (pd->screenPrivateIndex < 0)
    {
	free (pd);
	return FALSE;
    }

    /* custom atom for client events */
    pd->compizPutWindowAtom = XInternAtom(d->display,
					  "_COMPIZ_PUT_WINDOW", 0);

    pd->lastWindow = None;
    pd->lastType   = PutUnknown;

    putSetPutViewportInitiate (d, putToViewport);
    putSetPutViewport1KeyInitiate (d, putToViewport);
    putSetPutViewport2KeyInitiate (d, putToViewport);
    putSetPutViewport3KeyInitiate (d, putToViewport);
    putSetPutViewport4KeyInitiate (d, putToViewport);
    putSetPutViewport5KeyInitiate (d, putToViewport);
    putSetPutViewport6KeyInitiate (d, putToViewport);
    putSetPutViewport7KeyInitiate (d, putToViewport);
    putSetPutViewport8KeyInitiate (d, putToViewport);
    putSetPutViewport9KeyInitiate (d, putToViewport);
    putSetPutViewport10KeyInitiate (d, putToViewport);
    putSetPutViewport11KeyInitiate (d, putToViewport);
    putSetPutViewport12KeyInitiate (d, putToViewport);
    putSetPutViewportLeftKeyInitiate (d, putViewportLeft);
    putSetPutViewportRightKeyInitiate (d, putViewportRight);
    putSetPutViewportUpKeyInitiate (d, putViewportUp);
    putSetPutViewportDownKeyInitiate (d, putViewportDown);
    putSetPutRestoreKeyInitiate (d, restore);
    putSetPutPointerKeyInitiate (d, putPointer);
    putSetPutRestoreButtonInitiate (d, restore);
    putSetPutPointerButtonInitiate (d, putPointer);
    putSetPutPutInitiate (d, putInitiate);
    putSetPutCenterKeyInitiate (d, putCenter);
    putSetPutLeftKeyInitiate (d, putLeft);
    putSetPutRightKeyInitiate (d, putRight);
    putSetPutTopKeyInitiate (d, putTop);
    putSetPutBottomKeyInitiate (d, putBottom);
    putSetPutTopleftKeyInitiate (d, putTopLeft);
    putSetPutToprightKeyInitiate (d, putTopRight);
    putSetPutBottomleftKeyInitiate (d, putBottomLeft);
    putSetPutBottomrightKeyInitiate (d, putBottomRight);
    putSetPutCenterButtonInitiate (d, putCenter);
    putSetPutLeftButtonInitiate (d, putLeft);
    putSetPutRightButtonInitiate (d, putRight);
    putSetPutTopButtonInitiate (d, putTop);
    putSetPutBottomButtonInitiate (d, putBottom);
    putSetPutTopleftButtonInitiate (d, putTopLeft);
    putSetPutToprightButtonInitiate (d, putTopRight);
    putSetPutBottomleftButtonInitiate (d, putBottomLeft);
    putSetPutBottomrightButtonInitiate (d, putBottomRight);

    WRAP (pd, d, handleEvent, putHandleEvent);
    d->base.privates[displayPrivateIndex].ptr = pd;

    return TRUE;
}

static void
putFiniDisplay (CompPlugin  *p,
		CompDisplay *d)
{
    PUT_DISPLAY (d);

    freeScreenPrivateIndex (d, pd->screenPrivateIndex);
    UNWRAP (pd, d, handleEvent);

    free (pd);
}

static Bool
putInitScreen (CompPlugin *p,
	       CompScreen *s)
{
    PutScreen *ps;

    PUT_DISPLAY (s->display);

    ps = malloc (sizeof (PutScreen));
    if (!ps)
	return FALSE;

    ps->windowPrivateIndex = allocateWindowPrivateIndex (s);
    if (ps->windowPrivateIndex < 0)
    {
	free (ps);
	return FALSE;
    }

    /* initialize variables
     * bad stuff happens if we don't do this
     */
    ps->moreAdjust = FALSE;
    ps->grabIndex = 0;

    /* wrap the overloaded functions */
    WRAP (ps, s, preparePaintScreen, putPreparePaintScreen);
    WRAP (ps, s, donePaintScreen, putDonePaintScreen);
    WRAP (ps, s, paintOutput, putPaintOutput);
    WRAP (ps, s, paintWindow, putPaintWindow);

    s->base.privates[pd->screenPrivateIndex].ptr = ps;
    return TRUE;
}

static void
putFiniScreen (CompPlugin *p,
	       CompScreen *s)
{
    PUT_SCREEN (s);

    freeWindowPrivateIndex (s, ps->windowPrivateIndex);

    UNWRAP (ps, s, preparePaintScreen);
    UNWRAP (ps, s, donePaintScreen);
    UNWRAP (ps, s, paintOutput);
    UNWRAP (ps, s, paintWindow);

    free (ps);
}

static Bool
putInitWindow (CompPlugin *p,
	       CompWindow *w)
{
    PutWindow *pw;

    PUT_SCREEN (w->screen);

    pw = malloc (sizeof (PutWindow));
    if (!pw)
	return FALSE;

    /* initialize variables
     * I don't need to repeat it
     */
    pw->tx = pw->ty = pw->xVelocity = pw->yVelocity = 0.0f;
    pw->lastX = w->serverX;
    pw->lastY = w->serverY;
    pw->adjust = FALSE;

    w->base.privates[ps->windowPrivateIndex].ptr = pw;

    return TRUE;
}

static void
putFiniWindow (CompPlugin *p,
	       CompWindow *w)
{
    PUT_WINDOW (w);

    free (pw);
}

static CompBool
putInitObject (CompPlugin *p,
	       CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) putInitDisplay,
	(InitPluginObjectProc) putInitScreen,
	(InitPluginObjectProc) putInitWindow
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
putFiniObject (CompPlugin *p,
	       CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) 0, /* FiniCore */
	(FiniPluginObjectProc) putFiniDisplay,
	(FiniPluginObjectProc) putFiniScreen,
	(FiniPluginObjectProc) putFiniWindow
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static Bool
putInit (CompPlugin *p)
{
    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
putFini (CompPlugin *p)
{
    freeDisplayPrivateIndex (displayPrivateIndex);
}

/*
 * vTable tells the dl
 * what we offer
 */
CompPluginVTable putVTable = {
    "put",
    0,
    putInit,
    putFini,
    putInitObject,
    putFiniObject,
    0,
    0
};

CompPluginVTable*
getCompPluginInfo (void)
{
	return &putVTable;
}
