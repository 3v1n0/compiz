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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include <X11/cursorfont.h>

#include <compiz.h>

#define WIN_X(w) ((w)->attrib.x - (w)->input.left)
#define WIN_Y(w) ((w)->attrib.y - (w)->input.top)
#define WIN_W(w) ((w)->width + (w)->input.left + (w)->input.right)
#define WIN_H(w) ((w)->height + (w)->input.top + (w)->input.bottom)

#define SCALE_SPACING_DEFAULT 25
#define SCALE_SPACING_MIN     0
#define SCALE_SPACING_MAX     250

#define SCALE_SLOPPY_FOCUS_DEFAULT TRUE

#define SCALE_INITIATE_KEY_DEFAULT       "Pause"
#define SCALE_INITIATE_MODIFIERS_DEFAULT 0

#define SCALE_SPEED_DEFAULT   1.5f
#define SCALE_SPEED_MIN       0.1f
#define SCALE_SPEED_MAX       50.0f
#define SCALE_SPEED_PRECISION 0.1f

#define SCALE_TIMESTEP_DEFAULT   1.2f
#define SCALE_TIMESTEP_MIN       0.1f
#define SCALE_TIMESTEP_MAX       50.0f
#define SCALE_TIMESTEP_PRECISION 0.1f

#define SCALE_STATE_NONE 0
#define SCALE_STATE_OUT  1
#define SCALE_STATE_WAIT 2
#define SCALE_STATE_IN   3

#define SCALE_DARKEN_BACK_DEFAULT TRUE

#define SCALE_OPACITY_DEFAULT 75
#define SCALE_OPACITY_MIN     0
#define SCALE_OPACITY_MAX     100

static char *winType[] = {
    "Toolbar",
    "Utility",
    "Dialog",
    "ModalDialog",
    "Fullscreen",
    "Normal"
};
#define N_WIN_TYPE (sizeof (winType) / sizeof (winType[0]))

char *cornerTypeString[] = {
    "TopLeft",
    "TopRight",
    "BottomLeft",
    "BottomRight"
};
int  nCornerTypeString =
    sizeof (cornerTypeString) / sizeof (cornerTypeString[0]);

static char *cornerType[] = {
#ifdef SCALE_CORNER
    SCALE_CORNER
#endif
};
#define N_CORNER_TYPE (sizeof (cornerType) / sizeof (cornerType[0]))

typedef enum {
    ScaleIconNone = 0,
    ScaleIconEmblem,
    ScaleIconBig
} IconOverlay;

static char *iconOverlayString[] = {
    "None",
    "Emblem",
    "Big"
};

static IconOverlay iconOverlay[] = {
    ScaleIconNone,
    ScaleIconEmblem,
    ScaleIconBig
};
#define N_ICON_TYPE (sizeof (iconOverlayString) / sizeof (iconOverlayString[0]))
#define SCALE_ICON_DEFAULT (iconOverlayString[1])

static int displayPrivateIndex;

typedef struct _ScaleSlot {
    int x1, y1, x2, y2;
    int line;
} ScaleSlot;

#define SCALE_DISPLAY_OPTION_INITIATE 0
#define SCALE_DISPLAY_OPTION_NUM      1

typedef struct _ScaleDisplay {
    int		    screenPrivateIndex;
    HandleEventProc handleEvent;

    CompOption opt[SCALE_DISPLAY_OPTION_NUM];

    unsigned int lastActiveNum;
    KeyCode	 leftKeyCode, rightKeyCode, upKeyCode, downKeyCode;
} ScaleDisplay;

#define SCALE_SCREEN_OPTION_SPACING      0
#define SCALE_SCREEN_OPTION_SLOPPY_FOCUS 1
#define SCALE_SCREEN_OPTION_SPEED	 2
#define SCALE_SCREEN_OPTION_TIMESTEP	 3
#define SCALE_SCREEN_OPTION_WINDOW_TYPE  4
#define SCALE_SCREEN_OPTION_DARKEN_BACK  5
#define SCALE_SCREEN_OPTION_OPACITY      6
#define SCALE_SCREEN_OPTION_CORNERS      7
#define SCALE_SCREEN_OPTION_ICON         8
#define SCALE_SCREEN_OPTION_NUM          9

typedef struct _ScaleScreen {
    int windowPrivateIndex;

    PreparePaintScreenProc preparePaintScreen;
    DonePaintScreenProc    donePaintScreen;
    PaintScreenProc        paintScreen;
    PaintWindowProc        paintWindow;
    DamageWindowRectProc   damageWindowRect;

    CompOption opt[SCALE_SCREEN_OPTION_NUM];

    int spacing;

    float speed;
    float timestep;

    unsigned int wMask;

    int grabIndex;

    int state;
    int moreAdjust;

    Cursor cursor;

    ScaleSlot *slots;
    int        slotsSize;
    int        nSlots;

    int *line;
    int lineSize;
    int nLine;

    /* only used for sorting */
    CompWindow **windows;
    int        windowsSize;
    int        nWindows;

    GLfloat scale;

    Bool     darkenBack;
    GLushort opacity;

    unsigned int cornerMask;

    IconOverlay iconOverlay;
} ScaleScreen;

typedef struct _ScaleWindow {
    ScaleSlot *slot;

    GLfloat xVelocity, yVelocity, scaleVelocity;
    GLfloat scale;
    GLfloat tx, ty;
    Bool    adjust;
} ScaleWindow;


#define GET_SCALE_DISPLAY(d)				      \
    ((ScaleDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define SCALE_DISPLAY(d)		     \
    ScaleDisplay *sd = GET_SCALE_DISPLAY (d)

#define GET_SCALE_SCREEN(s, sd)					  \
    ((ScaleScreen *) (s)->privates[(sd)->screenPrivateIndex].ptr)

#define SCALE_SCREEN(s)							   \
    ScaleScreen *ss = GET_SCALE_SCREEN (s, GET_SCALE_DISPLAY (s->display))

#define GET_SCALE_WINDOW(w, ss)					  \
    ((ScaleWindow *) (w)->privates[(ss)->windowPrivateIndex].ptr)

#define SCALE_WINDOW(w)					       \
    ScaleWindow *sw = GET_SCALE_WINDOW  (w,		       \
		      GET_SCALE_SCREEN  (w->screen,	       \
		      GET_SCALE_DISPLAY (w->screen->display)))

static int scaleEdge[] = {
    SCREEN_EDGE_TOPLEFT,
    SCREEN_EDGE_TOPRIGHT,
    SCREEN_EDGE_BOTTOMLEFT,
    SCREEN_EDGE_BOTTOMRIGHT
};

static unsigned int
scaleUpdateCorners (CompScreen *s,
		    CompOption *o)
{
    unsigned int i, mask = 0;

    SCALE_SCREEN (s);

    for (i = 0; i < o->value.list.nValue; i++)
    {
	if (!strcasecmp (o->value.list.value[i].s, "topleft"))
	    mask |= (1 << 0);
	else if (!strcasecmp (o->value.list.value[i].s, "topright"))
	    mask |= (1 << 1);
	else if (!strcasecmp (o->value.list.value[i].s, "bottomleft"))
	    mask |= (1 << 2);
	else if (!strcasecmp (o->value.list.value[i].s, "bottomright"))
	    mask |= (1 << 3);
    }

    for (i = 0; i < 4; i++)
    {
	if ((mask & (1 << i)) != (ss->cornerMask  & (1 << i)))
	{
	    if (mask & (1 << i))
		enableScreenEdge (s, scaleEdge[i]);
	    else
		disableScreenEdge (s, scaleEdge[i]);
	}
    }

    return mask;
}

#define NUM_OPTIONS(s) (sizeof ((s)->opt) / sizeof (CompOption))

static CompOption *
scaleGetScreenOptions (CompScreen *screen,
		       int	  *count)
{
    SCALE_SCREEN (screen);

    *count = NUM_OPTIONS (ss);
    return ss->opt;
}

static Bool
scaleSetScreenOption (CompScreen      *screen,
		      char	      *name,
		      CompOptionValue *value)
{
    CompOption *o;
    int	       index;

    SCALE_SCREEN (screen);

    o = compFindOption (ss->opt, NUM_OPTIONS (ss), name, &index);

    if (!o)
	return FALSE;

    switch (index) {
    case SCALE_SCREEN_OPTION_SPACING:
	if (compSetIntOption (o, value))
	{
	    ss->spacing = o->value.i;
	    return TRUE;
	}
	break;
    case SCALE_SCREEN_OPTION_SLOPPY_FOCUS:
	if (compSetBoolOption (o, value))
	    return TRUE;
	break;
    case SCALE_SCREEN_OPTION_SPEED:
	if (compSetFloatOption (o, value))
	{
	    ss->speed = o->value.f;
	    return TRUE;
	}
	break;
    case SCALE_SCREEN_OPTION_TIMESTEP:
	if (compSetFloatOption (o, value))
	{
	    ss->timestep = o->value.f;
	    return TRUE;
	}
	break;
    case SCALE_SCREEN_OPTION_WINDOW_TYPE:
	if (compSetOptionList (o, value))
	{
	    ss->wMask = compWindowTypeMaskFromStringList (&o->value);
	    return TRUE;
	}
	break;
    case SCALE_SCREEN_OPTION_DARKEN_BACK:
	if (compSetBoolOption (o, value))
	{
	    ss->darkenBack = o->value.b;
	    return TRUE;
	}
	break;
    case SCALE_SCREEN_OPTION_OPACITY:
	if (compSetIntOption (o, value))
	{
	    ss->opacity = (OPAQUE * o->value.i) / 100;
	    return TRUE;
	}
	break;
    case SCALE_SCREEN_OPTION_CORNERS:
	if (compSetOptionList (o, value))
	{
	    ss->cornerMask = scaleUpdateCorners (screen, o);
	    return TRUE;
	}
	break;
    case SCALE_SCREEN_OPTION_ICON:
	if (compSetStringOption (o, value))
	{
	    int i;

	    for (i = 0; i < N_ICON_TYPE; i++)
	    {
		if (strcmp (o->value.s, iconOverlayString[i]) == 0)
		{
		    ss->iconOverlay = iconOverlay[i];
		    return TRUE;
		}
	    }
	}
	break;
    default:
	break;
    }

    return FALSE;
}

static void
scaleScreenInitOptions (ScaleScreen *ss)
{
    CompOption *o;
    int	       i;

    o = &ss->opt[SCALE_SCREEN_OPTION_SPACING];
    o->name	  = "spacing";
    o->shortDesc  = "Spacing";
    o->longDesc   = "Space between windows";
    o->type	  = CompOptionTypeInt;
    o->value.i	  = SCALE_SPACING_DEFAULT;
    o->rest.i.min = SCALE_SPACING_MIN;
    o->rest.i.max = SCALE_SPACING_MAX;

    o = &ss->opt[SCALE_SCREEN_OPTION_SLOPPY_FOCUS];
    o->name	  = "sloppy_focus";
    o->shortDesc  = "Sloppy Focus";
    o->longDesc   = "Focus window when mouse moves over them";
    o->type	  = CompOptionTypeBool;
    o->value.b	  = SCALE_SLOPPY_FOCUS_DEFAULT;

    o = &ss->opt[SCALE_SCREEN_OPTION_SPEED];
    o->name		= "speed";
    o->shortDesc	= "Speed";
    o->longDesc		= "Scale speed";
    o->type		= CompOptionTypeFloat;
    o->value.f		= SCALE_SPEED_DEFAULT;
    o->rest.f.min	= SCALE_SPEED_MIN;
    o->rest.f.max	= SCALE_SPEED_MAX;
    o->rest.f.precision = SCALE_SPEED_PRECISION;

    o = &ss->opt[SCALE_SCREEN_OPTION_TIMESTEP];
    o->name		= "timestep";
    o->shortDesc	= "Timestep";
    o->longDesc		= "Scale timestep";
    o->type		= CompOptionTypeFloat;
    o->value.f		= SCALE_TIMESTEP_DEFAULT;
    o->rest.f.min	= SCALE_TIMESTEP_MIN;
    o->rest.f.max	= SCALE_TIMESTEP_MAX;
    o->rest.f.precision = SCALE_TIMESTEP_PRECISION;

    o = &ss->opt[SCALE_SCREEN_OPTION_WINDOW_TYPE];
    o->name	         = "window_types";
    o->shortDesc         = "Window Types";
    o->longDesc	         = "Window types that should scaled in scale mode";
    o->type	         = CompOptionTypeList;
    o->value.list.type   = CompOptionTypeString;
    o->value.list.nValue = N_WIN_TYPE;
    o->value.list.value  = malloc (sizeof (CompOptionValue) * N_WIN_TYPE);
    for (i = 0; i < N_WIN_TYPE; i++)
	o->value.list.value[i].s = strdup (winType[i]);
    o->rest.s.string     = windowTypeString;
    o->rest.s.nString    = nWindowTypeString;

    ss->wMask = compWindowTypeMaskFromStringList (&o->value);

    o = &ss->opt[SCALE_SCREEN_OPTION_DARKEN_BACK];
    o->name      = "darken_back";
    o->shortDesc = "Darken Background";
    o->longDesc  = "Darken background when scaling windows";
    o->type      = CompOptionTypeBool;
    o->value.b   = SCALE_DARKEN_BACK_DEFAULT;

    o = &ss->opt[SCALE_SCREEN_OPTION_OPACITY];
    o->name	  = "opacity";
    o->shortDesc  = "Opacity";
    o->longDesc	  = "Amount of opacity in percent";
    o->type	  = CompOptionTypeInt;
    o->value.i    = SCALE_OPACITY_DEFAULT;
    o->rest.i.min = SCALE_OPACITY_MIN;
    o->rest.i.max = SCALE_OPACITY_MAX;

    o = &ss->opt[SCALE_SCREEN_OPTION_CORNERS];
    o->name	         = "corners";
    o->shortDesc         = "Corners";
    o->longDesc	         = "Hot corners that should initiate scale mode";
    o->type	         = CompOptionTypeList;
    o->value.list.type   = CompOptionTypeString;
    o->value.list.nValue = N_CORNER_TYPE;
    o->value.list.value  = malloc (sizeof (CompOptionValue) * N_CORNER_TYPE);
    for (i = 0; i < N_CORNER_TYPE; i++)
	o->value.list.value[i].s = strdup (cornerType[i]);
    o->rest.s.string     = cornerTypeString;
    o->rest.s.nString    = nCornerTypeString;

    o = &ss->opt[SCALE_SCREEN_OPTION_ICON];
    o->name	      = "overlay_icon";
    o->shortDesc      = "Overlay Icon";
    o->longDesc	      = "Overlay an icon on windows once they are scaled";
    o->type	      = CompOptionTypeString;
    o->value.s	      = strdup (SCALE_ICON_DEFAULT);
    o->rest.s.string  = iconOverlayString;
    o->rest.s.nString = N_ICON_TYPE;
}

static Bool
isScaleWin (CompWindow *w)
{
    SCALE_SCREEN (w->screen);

    if (!(*w->screen->focusWindow) (w))
	return FALSE;

    if (!(ss->wMask & w->type))
	return FALSE;

    if (w->state & CompWindowStateSkipPagerMask)
	return FALSE;

    if (w->state & CompWindowStateShadedMask)
	return FALSE;

    return TRUE;
}

static Bool
scalePaintWindow (CompWindow		  *w,
		  const WindowPaintAttrib *attrib,
		  Region		  region,
		  unsigned int		  mask)
{
    CompScreen *s = w->screen;
    Bool       status;

    SCALE_SCREEN (s);

    if (ss->grabIndex)
    {
	WindowPaintAttrib sAttrib = *attrib;

	SCALE_WINDOW (w);

	if (sw->adjust || sw->slot)
	{
	    mask |= PAINT_WINDOW_TRANSFORMED_MASK;

	    if (w->id	    != s->display->activeWindow &&
		ss->opacity != OPAQUE			&&
		ss->state   != SCALE_STATE_IN)
	    {
		/* modify opacity of windows that are not active */
		sAttrib.opacity = (sAttrib.opacity * ss->opacity) >> 16;
	    }
	}
	else if (ss->darkenBack && ss->state != SCALE_STATE_IN)
	{
	    /* modify brightness of the other windows */
	    sAttrib.brightness = sAttrib.brightness / 2;
	}

	UNWRAP (ss, s, paintWindow);
	status = (*s->paintWindow) (w, &sAttrib, region, mask);
	WRAP (ss, s, paintWindow, scalePaintWindow);

	if ((ss->iconOverlay != ScaleIconNone) && (sw->adjust || sw->slot))
	{
	    CompIcon *icon;

	    icon = getWindowIcon (w, 96, 96);
	    if (!icon)
		icon = w->screen->defaultIcon;

	    if (icon && (icon->texture.name || iconToTexture (w->screen, icon)))
	    {
		REGION	    iconReg;
		CompMatrix  matrix;
		int	    wx, wy, width, height;
		int	    scaledWinWidth, scaledWinHeight;
		float	    ds;

		scaledWinWidth  = w->width  * sw->scale;
		scaledWinHeight = w->height * sw->scale;

		switch (ss->iconOverlay) {
		case ScaleIconNone:
		case ScaleIconEmblem:
		    sAttrib.xScale = sAttrib.yScale = 1.0f;
		    break;
		case ScaleIconBig:
		    sAttrib.opacity /= 3;
		    sAttrib.xScale = sAttrib.yScale =
			MIN (((float) scaledWinWidth / (float) icon->width),
			     ((float) scaledWinHeight / (float) icon->height));
		default:
		    break;
		}

		width  = icon->width  * sAttrib.xScale;
		height = icon->height * sAttrib.yScale;

		switch (ss->iconOverlay) {
		case ScaleIconNone:
		case ScaleIconEmblem:
		    wx = w->attrib.x + (w->width  * sw->scale) - icon->width;
		    wy = w->attrib.y + (w->height * sw->scale) - icon->height;
		    break;
		case ScaleIconBig:
		    wx = w->attrib.x + ((scaledWinWidth - width) / 2) /
			sAttrib.xScale;
		    wy = w->attrib.y + ((scaledWinHeight - height) / 2) /
			sAttrib.yScale;
		    break;
		default:
		    wx = wy = 0;
		    break;
		}

		ds = 1.0f - ss->scale;
		if (ds)
		{
		    sAttrib.opacity =
			(fabs (1.0f - sw->scale) * sAttrib.opacity) / ds;
		}
		else if (!sw->slot)
		{
		    sAttrib.opacity = 0;
		}

		mask |= PAINT_WINDOW_TRANSLUCENT_MASK;

		iconReg.rects    = &iconReg.extents;
		iconReg.numRects = 1;

		iconReg.extents.x1 = wx;
		iconReg.extents.y1 = wy;
		iconReg.extents.x2 = iconReg.extents.x1 + width;
		iconReg.extents.y2 = iconReg.extents.y1 + height;

		matrix = icon->texture.matrix;
		matrix.x0 -= wx * icon->texture.matrix.xx;
		matrix.y0 -= wy * icon->texture.matrix.yy;

		w->vCount = 0;
		if (iconReg.extents.x1 < iconReg.extents.x2 &&
		    iconReg.extents.y1 < iconReg.extents.y2)
		    (*w->screen->addWindowGeometry) (w, &matrix, 1, &iconReg,
						     &iconReg);

		if (w->vCount)
		    (*w->screen->drawWindowTexture) (w,
						     &icon->texture, &sAttrib,
						     mask);
	    }
	}
    }
    else
    {
	UNWRAP (ss, s, paintWindow);
	status = (*s->paintWindow) (w, attrib, region, mask);
	WRAP (ss, s, paintWindow, scalePaintWindow);
    }

    return status;
}

static int
compareWindows (const void *elem1,
		const void *elem2)
{
    CompWindow *w1 = *((CompWindow **) elem1);
    CompWindow *w2 = *((CompWindow **) elem2);

    return w2->activeNum - w1->activeNum;
}

/* TODO: Place window thumbnails at smarter positions */
static Bool
layoutThumbs (CompScreen *s)
{
    CompWindow *w;
    int	       i, j, y2;
    int        cx, cy;
    int        lineLength, itemsPerLine;
    float      scaleW, scaleH;
    int        totalWidth, totalHeight;

    SCALE_SCREEN (s);

    cx = cy = ss->nWindows = 0;

    for (w = s->windows; w; w = w->next)
    {
	SCALE_WINDOW (w);

	if (sw->slot)
	    sw->adjust = TRUE;

	sw->slot = 0;

	if (!isScaleWin (w))
	    continue;

	if (ss->windowsSize <= ss->nWindows)
	{
	    ss->windows = realloc (ss->windows,
				   sizeof (CompWindow *) * (ss->nWindows + 32));
	    if (!ss->windows)
		return FALSE;

	    ss->windowsSize = ss->nWindows + 32;
	}

	ss->windows[ss->nWindows++] = w;
    }

    if (ss->nWindows == 0)
	return FALSE;

    qsort (ss->windows, ss->nWindows, sizeof (CompWindow *), compareWindows);

    itemsPerLine = (sqrt (ss->nWindows) * s->width) / s->height;
    if (itemsPerLine < 1)
	itemsPerLine = 1;

    if (ss->lineSize <= ss->nWindows / itemsPerLine + 1)
    {
	ss->line = realloc (ss->line, sizeof (int) *
			    (ss->nWindows / itemsPerLine + 2));
	if (!ss->line)
	    return FALSE;

	ss->lineSize = ss->nWindows / itemsPerLine + 2;
    }

    totalWidth = totalHeight = 0;

    ss->line[0] = 0;
    ss->nLine = 1;
    lineLength = itemsPerLine;

    if (ss->slotsSize <= ss->nWindows)
    {
	ss->slots = realloc (ss->slots, sizeof (ScaleSlot) *
			     (ss->nWindows + 1));
	if (!ss->slots)
	    return FALSE;

	ss->slotsSize = ss->nWindows + 1;
    }
    ss->nSlots = 0;

    for (i = 0; i < ss->nWindows; i++)
    {
	SCALE_WINDOW (ss->windows[i]);

	w = ss->windows[i];

	/* find a good place between other elements */
	for (j = 0; j < ss->nSlots; j++)
	{
	    y2 = ss->slots[j].y2 + ss->spacing + WIN_H (w);
	    if (w->width < ss->slots[j].x2 - ss->slots[j].x1 &&
		y2 <= ss->line[ss->slots[j].line])
		break;
	}

	/* otherwise append or start a new line */
	if (j == ss->nSlots)
	{
	    if (lineLength < itemsPerLine)
	    {
		lineLength++;

		ss->slots[ss->nSlots].x1 = cx;
		ss->slots[ss->nSlots].y1 = cy;
		ss->slots[ss->nSlots].x2 = cx + WIN_W (w);
		ss->slots[ss->nSlots].y2 = cy + WIN_H (w);
		ss->slots[ss->nSlots].line = ss->nLine - 1;

		ss->line[ss->nLine - 1] = MAX (ss->line[ss->nLine - 1],
					       ss->slots[ss->nSlots].y2);
	    }
	    else
	    {
		lineLength = 1;

		cx = ss->spacing;
		cy = ss->line[ss->nLine - 1] + ss->spacing;

		ss->slots[ss->nSlots].x1 = cx;
		ss->slots[ss->nSlots].y1 = cy;
		ss->slots[ss->nSlots].x2 = cx + WIN_W (w);
		ss->slots[ss->nSlots].y2 = cy + WIN_H (w);
		ss->slots[ss->nSlots].line = ss->nLine - 1;

		ss->line[ss->nLine] = ss->slots[ss->nSlots].y2;

		ss->nLine++;
	    }

	    if (ss->slots[ss->nSlots].y2 > totalHeight)
		totalHeight = ss->slots[ss->nSlots].y2;
	}
	else
	{
	    ss->slots[ss->nSlots].x1 = ss->slots[j].x1;
	    ss->slots[ss->nSlots].y1 = ss->slots[j].y2 + ss->spacing;
	    ss->slots[ss->nSlots].x2 = ss->slots[ss->nSlots].x1 + WIN_W (w);
	    ss->slots[ss->nSlots].y2 = ss->slots[ss->nSlots].y1 + WIN_H (w);
	    ss->slots[ss->nSlots].line = ss->slots[j].line;

	    ss->slots[j].line = 0;
	}

	cx = ss->slots[ss->nSlots].x2;
	if (cx > totalWidth)
	    totalWidth = cx;

	cx += ss->spacing;

	sw->slot = &ss->slots[ss->nSlots];
	sw->adjust = TRUE;

	ss->nSlots++;
    }

    totalWidth  += ss->spacing;
    totalHeight += ss->spacing;

    scaleW = (float) s->workArea.width / totalWidth;
    scaleH = (float) s->workArea.height / totalHeight;

    ss->scale = MIN (MIN (scaleH, scaleW), 1.0f);

    for (i = 0; i < ss->nWindows; i++)
    {
	SCALE_WINDOW (ss->windows[i]);

	if (sw->slot)
	{
	    ss->slots[i].y1 += ss->windows[i]->input.top;
	    ss->slots[i].x1 += ss->windows[i]->input.left;
	    ss->slots[i].y2 += ss->windows[i]->input.top;
	    ss->slots[i].x2 += ss->windows[i]->input.left;
	    ss->slots[i].y1 = (float) ss->slots[i].y1 * ss->scale;
	    ss->slots[i].x1 = (float) ss->slots[i].x1 * ss->scale;
	    ss->slots[i].y2 = (float) ss->slots[i].y2 * ss->scale;
	    ss->slots[i].x2 = (float) ss->slots[i].x2 * ss->scale;
	    ss->slots[i].x1 += s->workArea.x;
	    ss->slots[i].y1 += s->workArea.y;
	    ss->slots[i].x2 += s->workArea.x;
	    ss->slots[i].y2 += s->workArea.y;
	}
    }

    return TRUE;
}

static int
adjustScaleVelocity (CompWindow *w)
{
    float dx, dy, ds, adjust, amount;
    float x1, y1, scale;

    SCALE_SCREEN (w->screen);
    SCALE_WINDOW (w);

    if (sw->slot)
    {
	x1 = sw->slot->x1;
	y1 = sw->slot->y1;
	scale = ss->scale;
    }
    else
    {
	x1 = w->serverX;
	y1 = w->serverY;
	scale = 1.0f;
    }

    dx = x1 - (w->serverX + sw->tx);

    adjust = dx * 0.15f;
    amount = fabs (dx) * 1.5f;
    if (amount < 0.5f)
	amount = 0.5f;
    else if (amount > 5.0f)
	amount = 5.0f;

    sw->xVelocity = (amount * sw->xVelocity + adjust) / (amount + 1.0f);

    dy = y1 - (w->serverY + sw->ty);

    adjust = dy * 0.15f;
    amount = fabs (dy) * 1.5f;
    if (amount < 0.5f)
	amount = 0.5f;
    else if (amount > 5.0f)
	amount = 5.0f;

    sw->yVelocity = (amount * sw->yVelocity + adjust) / (amount + 1.0f);

    ds = scale - sw->scale;

    adjust = ds * 0.1f;
    amount = fabs (ds) * 7.0f;
    if (amount < 0.01f)
	amount = 0.01f;
    else if (amount > 0.15f)
	amount = 0.15f;

    sw->scaleVelocity = (amount * sw->scaleVelocity + adjust) /
	(amount + 1.0f);

    if (fabs (dx) < 0.1f && fabs (sw->xVelocity) < 0.2f &&
	fabs (dy) < 0.1f && fabs (sw->yVelocity) < 0.2f &&
	fabs (ds) < 0.001f && fabs (sw->scaleVelocity) < 0.002f)
    {
	sw->xVelocity = sw->yVelocity = sw->scaleVelocity = 0.0f;
	sw->tx = x1 - w->serverX;
	sw->ty = y1 - w->serverY;
	sw->scale = scale;

	return 0;
    }

    return 1;
}

static Bool
scalePaintScreen (CompScreen		  *s,
		  const ScreenPaintAttrib *sAttrib,
		  Region		  region,
		  unsigned int		  mask)
{
    Bool status;

    SCALE_SCREEN (s);

    if (ss->grabIndex)
	mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS_MASK;

    UNWRAP (ss, s, paintScreen);
    status = (*s->paintScreen) (s, sAttrib, region, mask);
    WRAP (ss, s, paintScreen, scalePaintScreen);

    return status;
}

static void
scalePreparePaintScreen (CompScreen *s,
			 int	     msSinceLastPaint)
{
    SCALE_SCREEN (s);

    if (ss->grabIndex && ss->state != SCALE_STATE_WAIT)
    {
	CompWindow *w;
	int        steps, dx, dy;
	float      amount, chunk;

	amount = msSinceLastPaint * 0.05f * ss->speed;
	steps  = amount / (0.5f * ss->timestep);
	if (!steps) steps = 1;
	chunk  = amount / (float) steps;

	while (steps--)
	{
	    ss->moreAdjust = 0;

	    for (w = s->windows; w; w = w->next)
	    {
		SCALE_WINDOW (w);

		if (sw->adjust)
		{
		    sw->adjust = adjustScaleVelocity (w);

		    ss->moreAdjust |= sw->adjust;

		    sw->tx += sw->xVelocity * chunk;
		    sw->ty += sw->yVelocity * chunk;
		    sw->scale += sw->scaleVelocity * chunk;

		    dx = (w->serverX + sw->tx) - w->attrib.x;
		    dy = (w->serverY + sw->ty) - w->attrib.y;

		    moveWindow (w, dx, dy, FALSE, FALSE);

		    (*s->setWindowScale) (w, sw->scale, sw->scale);
		}
	    }

	    if (!ss->moreAdjust)
		break;
	}
    }

    UNWRAP (ss, s, preparePaintScreen);
    (*s->preparePaintScreen) (s, msSinceLastPaint);
    WRAP (ss, s, preparePaintScreen, scalePreparePaintScreen);
}

static void
scaleDonePaintScreen (CompScreen *s)
{
    SCALE_SCREEN (s);

    if (ss->grabIndex)
    {
	if (ss->moreAdjust)
	{
	    damageScreen (s);
	}
	else
	{
	    if (ss->state == SCALE_STATE_IN)
	    {
		removeScreenGrab (s, ss->grabIndex, 0);
		ss->grabIndex = 0;
	    }
	    else if (ss->state == SCALE_STATE_OUT)
		ss->state = SCALE_STATE_WAIT;
	}
    }

    UNWRAP (ss, s, donePaintScreen);
    (*s->donePaintScreen) (s);
    WRAP (ss, s, donePaintScreen, scaleDonePaintScreen);
}

static CompWindow *
scaleCheckForWindowAt (CompScreen *s,
		       int        x,
		       int        y)
{
    int        x1, y1, x2, y2;
    CompWindow *w;

    for (w = s->reverseWindows; w; w = w->prev)
    {
	SCALE_WINDOW (w);

	if (sw->slot)
	{
	    x1 = w->attrib.x - w->input.left * sw->scale;
	    y1 = w->attrib.y - w->input.top  * sw->scale;
	    x2 = w->attrib.x + (w->width  + w->input.right)  * sw->scale;
	    y2 = w->attrib.y + (w->height + w->input.bottom) * sw->scale;

	    if (x1 <= x && y1 <= y && x2 > x && y2 > y)
		return w;
	}
    }

    return 0;
}

static Bool
scaleTerminate (CompDisplay     *d,
		CompAction      *action,
		CompActionState state,
		CompOption      *option,
		int	        nOption)
{
    CompScreen *s;
    Window     xid;

    SCALE_DISPLAY (d);

    xid = getIntOptionNamed (option, nOption, "root", 0);

    for (s = d->screens; s; s = s->next)
    {
	SCALE_SCREEN (s);

	if (xid && s->root != xid)
	    continue;

	if (ss->grabIndex)
	{
	    if (ss->state == SCALE_STATE_NONE)
	    {
		removeScreenGrab (s, ss->grabIndex, 0);
		ss->grabIndex = 0;
	    }
	    else
	    {
		CompWindow *w;

		for (w = s->windows; w; w = w->next)
		{
		    SCALE_WINDOW (w);

		    if (sw->slot)
		    {
			sw->slot = 0;
			sw->adjust = TRUE;
		    }
		}

		ss->state = SCALE_STATE_IN;

		damageScreen (s);
	    }

	    sd->lastActiveNum = None;
	}
    }

    return FALSE;
}

static Bool
scaleInitiate (CompDisplay     *d,
	       CompAction      *action,
	       CompActionState state,
	       CompOption      *option,
	       int	       nOption)
{
    CompScreen *s;
    Window     xid;

    xid = getIntOptionNamed (option, nOption, "root", 0);

    s = findScreenAtDisplay (d, xid);
    if (s)
    {
	SCALE_DISPLAY (s->display);
	SCALE_SCREEN (s);

	if (ss->state != SCALE_STATE_WAIT &&
	    ss->state != SCALE_STATE_OUT)
	{
	    if (!layoutThumbs (s))
		return FALSE;

	    if (!ss->grabIndex)
	    {
		if (otherScreenGrabExist (s, "scale", 0))
		    return FALSE;

		ss->grabIndex = pushScreenGrab (s, ss->cursor, "scale");
	    }

	    if (ss->grabIndex)
	    {
		if (!sd->lastActiveNum)
		    sd->lastActiveNum = s->activeNum - 1;

		ss->state = SCALE_STATE_OUT;

		damageScreen (s);
	    }
	}
	else
	{
	    scaleTerminate (d, action, state, option, nOption);
	}
    }

    return FALSE;
}

static Bool
scaleSelectWindowAt (CompScreen *s,
		     int	 x,
		     int	 y)

{
    CompWindow *w;

    w = scaleCheckForWindowAt (s, x, y);
    if (w && isScaleWin (w))
    {
	activateWindow (w);

	return TRUE;
    }

    return FALSE;
}

static void
scaleMoveFocusWindow (CompScreen *s,
		      int	 dx,
		      int	 dy)

{
    CompWindow *active;

    active = findWindowAtScreen (s, s->display->activeWindow);
    if (active)
    {
	CompWindow *w, *focus = NULL;
	ScaleSlot  *slot;
	int	   x, y, cx, cy, d, min = MAXSHORT;

	SCALE_SCREEN (s);
	SCALE_WINDOW (active);

	if (!sw->slot)
	    return;

	cx = (sw->slot->x1 + sw->slot->x2) / 2;
	cy = (sw->slot->y1 + sw->slot->y2) / 2;

	for (w = s->windows; w; w = w->next)
	{
	    slot = GET_SCALE_WINDOW (w, ss)->slot;
	    if (!slot)
		continue;

	    x = (slot->x1 + slot->x2) / 2;
	    y = (slot->y1 + slot->y2) / 2;

	    d = abs (x - cx) + abs (y - cy);
	    if (d < min)
	    {
		if ((dx > 0 && slot->x1 < sw->slot->x2) ||
		    (dx < 0 && slot->x2 > sw->slot->x1) ||
		    (dy > 0 && slot->y1 < sw->slot->y2) ||
		    (dy < 0 && slot->y2 > sw->slot->y1))
		    continue;

		min   = d;
		focus = w;
	    }
	}

	if (focus)
	{
	    SCALE_DISPLAY (s->display);

	    sd->lastActiveNum = focus->activeNum;

	    activateWindow (focus);
	}
    }
}

static void
scaleWindowRemove (CompDisplay *d,
		   Window      id)
{
    CompWindow *w;

    w = findWindowAtDisplay (d, id);
    if (w)
    {
	SCALE_SCREEN (w->screen);

	if (ss->grabIndex && ss->state != SCALE_STATE_IN)
	{
	    int i;

	    for (i = 0; i < ss->nWindows; i++)
	    {
		if (ss->windows[i] == w)
		{
		    if (layoutThumbs (w->screen))
		    {
			ss->state = SCALE_STATE_OUT;
			damageScreen (w->screen);
			break;
		    }
		}
	    }
	}
    }
}

static void
scaleHandleEvent (CompDisplay *d,
		  XEvent      *event)
{
    CompScreen *s;

    SCALE_DISPLAY (d);

    switch (event->type) {
    case KeyPress:
	s = findScreenAtDisplay (d, event->xkey.root);
	if (s)
	{
	    SCALE_SCREEN (s);

	    if (ss->grabIndex)
	    {
		if (event->xkey.keycode == sd->leftKeyCode)
		    scaleMoveFocusWindow (s, -1, 0);
		else if (event->xkey.keycode == sd->rightKeyCode)
		    scaleMoveFocusWindow (s, 1, 0);
		else if (event->xkey.keycode == sd->upKeyCode)
		    scaleMoveFocusWindow (s, 0, -1);
		else if (event->xkey.keycode == sd->downKeyCode)
		    scaleMoveFocusWindow (s, 0, 1);
	    }
	}
	break;
    case ButtonPress:
	if (event->xbutton.button == Button1)
	{
	    s = findScreenAtDisplay (d, event->xbutton.root);
	    if (s)
	    {
		CompAction *action =
		    &sd->opt[SCALE_DISPLAY_OPTION_INITIATE].value.action;

		SCALE_SCREEN (s);

		if (ss->grabIndex && ss->state != SCALE_STATE_IN)
		{
		    CompOption o;

		    o.type    = CompOptionTypeInt;
		    o.name    = "root";
		    o.value.i = s->root;

		    if (scaleSelectWindowAt (s,
					     event->xbutton.x_root,
					     event->xbutton.y_root))
		    {
			scaleTerminate (d, action, 0, &o, 1);
		    }
		    else if (event->xbutton.x_root > s->workArea.x &&
			     event->xbutton.x_root < (s->workArea.x +
						      s->workArea.width) &&
			     event->xbutton.y_root > s->workArea.y &&
			     event->xbutton.y_root < (s->workArea.y +
						      s->workArea.height))
		    {
			scaleTerminate (d, action, 0, &o, 1);
			enterShowDesktopMode (s);
		    }
		}
	    }
	}
	break;
    case MotionNotify:
	s = findScreenAtDisplay (d, event->xmotion.root);
	if (s)
	{
	    SCALE_SCREEN (s);

	    if (ss->grabIndex		    &&
		ss->state != SCALE_STATE_IN &&
		ss->opt[SCALE_SCREEN_OPTION_SLOPPY_FOCUS].value.b)
		scaleSelectWindowAt (s,
				     event->xmotion.x_root,
				     event->xmotion.y_root);
	}
	break;
    case EnterNotify:
	if (event->xcrossing.mode   != NotifyGrab   &&
	    event->xcrossing.mode   != NotifyUngrab &&
	    event->xcrossing.detail != NotifyInferior)
	{
	    s = findScreenAtDisplay (d, event->xcrossing.root);
	    if (s)
	    {
		unsigned int i;
		Window       id = event->xcrossing.window;
		CompAction *action =
		    &sd->opt[SCALE_DISPLAY_OPTION_INITIATE].value.action;
		CompOption o;

		SCALE_SCREEN (s);

		o.type    = CompOptionTypeInt;
		o.name    = "root";
		o.value.i = s->root;

		for (i = 0; i < 4; i++)
		{
		    if (id == s->screenEdge[scaleEdge[i]].id)
		    {
			if (ss->cornerMask & (1 << i))
			    scaleInitiate (d, action, 0, &o, 1);
		    }
		}

	    }
	}
    default:
	break;
    }

    UNWRAP (sd, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (sd, d, handleEvent, scaleHandleEvent);

    switch (event->type) {
    case UnmapNotify:
	scaleWindowRemove (d, event->xunmap.window);
	break;
    case DestroyNotify:
	scaleWindowRemove (d, event->xdestroywindow.window);
	break;
    }

}

static Bool
scaleDamageWindowRect (CompWindow *w,
		       Bool	  initial,
		       BoxPtr     rect)
{
    Bool status;

    SCALE_SCREEN (w->screen);

    if (initial)
    {
	if (isScaleWin (w))
	{
	    if (ss->grabIndex && layoutThumbs (w->screen))
	    {
		ss->state = SCALE_STATE_OUT;
		damageScreen (w->screen);
	    }
	}
    }

    UNWRAP (ss, w->screen, damageWindowRect);
    status = (*w->screen->damageWindowRect) (w, initial, rect);
    WRAP (ss, w->screen, damageWindowRect, scaleDamageWindowRect);

    return status;
}

static CompOption *
scaleGetDisplayOptions (CompDisplay *display,
			int	    *count)
{
    SCALE_DISPLAY (display);

    *count = NUM_OPTIONS (sd);
    return sd->opt;
}

static Bool
scaleSetDisplayOption (CompDisplay     *display,
		       char	       *name,
		       CompOptionValue *value)
{
    CompOption *o;
    int	       index;

    SCALE_DISPLAY (display);

    o = compFindOption (sd->opt, NUM_OPTIONS (sd), name, &index);

    if (!o)
	return FALSE;

    switch (index) {
    case SCALE_DISPLAY_OPTION_INITIATE:
	if (setDisplayAction (display, o, value))
	    return TRUE;
    default:
	break;
    }

    return FALSE;
}

static void
scaleDisplayInitOptions (ScaleDisplay *sd,
			 Display      *display)
{
    CompOption *o;

    o = &sd->opt[SCALE_DISPLAY_OPTION_INITIATE];
    o->name			  = "initiate";
    o->shortDesc		  = "Initiate Window Picker";
    o->longDesc			  = "Layout and start transforming windows";
    o->type			  = CompOptionTypeAction;
    o->value.action.initiate	  = scaleInitiate;
    o->value.action.terminate	  = scaleTerminate;
    o->value.action.bell	  = FALSE;
    o->value.action.type	  = CompBindingTypeKey;
    o->value.action.state	  = CompActionStateInitKey;
    o->value.action.key.modifiers = SCALE_INITIATE_MODIFIERS_DEFAULT;
    o->value.action.key.keycode   =
	XKeysymToKeycode (display,
			  XStringToKeysym (SCALE_INITIATE_KEY_DEFAULT));
}

static Bool
scaleInitDisplay (CompPlugin  *p,
		  CompDisplay *d)
{
    ScaleDisplay *sd;

    sd = malloc (sizeof (ScaleDisplay));
    if (!sd)
	return FALSE;

    sd->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (sd->screenPrivateIndex < 0)
    {
	free (sd);
	return FALSE;
    }

    sd->lastActiveNum = None;

    scaleDisplayInitOptions (sd, d->display);

    sd->leftKeyCode  = XKeysymToKeycode (d->display, XStringToKeysym ("Left"));
    sd->rightKeyCode = XKeysymToKeycode (d->display, XStringToKeysym ("Right"));
    sd->upKeyCode    = XKeysymToKeycode (d->display, XStringToKeysym ("Up"));
    sd->downKeyCode  = XKeysymToKeycode (d->display, XStringToKeysym ("Down"));

    WRAP (sd, d, handleEvent, scaleHandleEvent);

    d->privates[displayPrivateIndex].ptr = sd;

    return TRUE;
}

static void
scaleFiniDisplay (CompPlugin  *p,
		  CompDisplay *d)
{
    SCALE_DISPLAY (d);

    freeScreenPrivateIndex (d, sd->screenPrivateIndex);

    UNWRAP (sd, d, handleEvent);

    free (sd);
}

static Bool
scaleInitScreen (CompPlugin *p,
		 CompScreen *s)
{
    ScaleScreen *ss;

    SCALE_DISPLAY (s->display);

    ss = malloc (sizeof (ScaleScreen));
    if (!ss)
	return FALSE;

    ss->windowPrivateIndex = allocateWindowPrivateIndex (s);
    if (ss->windowPrivateIndex < 0)
    {
	free (ss);
	return FALSE;
    }

    ss->grabIndex = 0;

    ss->state = SCALE_STATE_NONE;

    ss->slots = 0;
    ss->slotsSize = 0;

    ss->windows = 0;
    ss->windowsSize = 0;

    ss->line = 0;
    ss->lineSize = 0;

    ss->scale = 1.0f;

    ss->spacing = SCALE_SPACING_DEFAULT;

    ss->speed    = SCALE_SPEED_DEFAULT;
    ss->timestep = SCALE_TIMESTEP_DEFAULT;
    ss->opacity  = (OPAQUE * SCALE_OPACITY_DEFAULT) / 100;

    ss->darkenBack = SCALE_DARKEN_BACK_DEFAULT;
    ss->cornerMask = 0;

    ss->iconOverlay = ScaleIconEmblem;

    scaleScreenInitOptions (ss);

    addScreenAction (s, &sd->opt[SCALE_DISPLAY_OPTION_INITIATE].value.action);

    WRAP (ss, s, preparePaintScreen, scalePreparePaintScreen);
    WRAP (ss, s, donePaintScreen, scaleDonePaintScreen);
    WRAP (ss, s, paintScreen, scalePaintScreen);
    WRAP (ss, s, paintWindow, scalePaintWindow);
    WRAP (ss, s, damageWindowRect, scaleDamageWindowRect);

    ss->cursor = XCreateFontCursor (s->display->display, XC_left_ptr);

    s->privates[sd->screenPrivateIndex].ptr = ss;

    ss->cornerMask =
	scaleUpdateCorners (s, &ss->opt[SCALE_SCREEN_OPTION_CORNERS]);

    return TRUE;
}

static void
scaleFiniScreen (CompPlugin *p,
		 CompScreen *s)
{
    unsigned int i;

    SCALE_SCREEN (s);

    for (i = 0; i < 4; i++)
	if (ss->cornerMask & (1 << i))
	    disableScreenEdge (s, scaleEdge[i]);

    UNWRAP (ss, s, preparePaintScreen);
    UNWRAP (ss, s, donePaintScreen);
    UNWRAP (ss, s, paintScreen);
    UNWRAP (ss, s, paintWindow);
    UNWRAP (ss, s, damageWindowRect);

    if (ss->slotsSize)
	free (ss->slots);

    if (ss->lineSize)
	free (ss->line);

    if (ss->windowsSize)
	free (ss->windows);

    free (ss);
}

static Bool
scaleInitWindow (CompPlugin *p,
		 CompWindow *w)
{
    ScaleWindow *sw;

    SCALE_SCREEN (w->screen);

    sw = malloc (sizeof (ScaleWindow));
    if (!sw)
	return FALSE;

    sw->slot = 0;
    sw->scale = 1.0f;
    sw->tx = sw->ty = 0.0f;
    sw->adjust = FALSE;
    sw->xVelocity = sw->yVelocity = 0.0f;
    sw->scaleVelocity = 1.0f;

    w->privates[ss->windowPrivateIndex].ptr = sw;

    return TRUE;
}

static void
scaleFiniWindow (CompPlugin *p,
		 CompWindow *w)
{
    SCALE_WINDOW (w);

    free (sw);
}

static Bool
scaleInit (CompPlugin *p)
{
    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
scaleFini (CompPlugin *p)
{
    if (displayPrivateIndex >= 0)
	freeDisplayPrivateIndex (displayPrivateIndex);
}

CompPluginVTable scaleVTable = {
    "scale",
    "Scale",
    "Scale windows",
    scaleInit,
    scaleFini,
    scaleInitDisplay,
    scaleFiniDisplay,
    scaleInitScreen,
    scaleFiniScreen,
    scaleInitWindow,
    scaleFiniWindow,
    scaleGetDisplayOptions,
    scaleSetDisplayOption,
    scaleGetScreenOptions,
    scaleSetScreenOption,
    0, /* Deps */
    0  /* nDeps */
};

CompPluginVTable *
getCompPluginInfo (void)
{
    return &scaleVTable;
}
