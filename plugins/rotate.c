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

#define _GNU_SOURCE /* for asprintf */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include <X11/Xatom.h>
#include <X11/Xproto.h>

#include <compiz.h>

#define ROTATE_POINTER_INVERT_Y_DEFAULT FALSE

#define ROTATE_POINTER_SENSITIVITY_DEFAULT   1.0f
#define ROTATE_POINTER_SENSITIVITY_MIN       0.01f
#define ROTATE_POINTER_SENSITIVITY_MAX       100.0f
#define ROTATE_POINTER_SENSITIVITY_PRECISION 0.01f

#define ROTATE_POINTER_SENSITIVITY_FACTOR 0.05f

#define ROTATE_ACCELERATION_DEFAULT   4.0f
#define ROTATE_ACCELERATION_MIN       1.0f
#define ROTATE_ACCELERATION_MAX       20.0f
#define ROTATE_ACCELERATION_PRECISION 0.1f

#define ROTATE_INITIATE_BUTTON_DEFAULT    Button1
#define ROTATE_INITIATE_MODIFIERS_DEFAULT (ControlMask | CompAltMask)

#define ROTATE_LEFT_KEY_DEFAULT       "Left"
#define ROTATE_LEFT_MODIFIERS_DEFAULT (ControlMask | CompAltMask)

#define ROTATE_RIGHT_KEY_DEFAULT       "Right"
#define ROTATE_RIGHT_MODIFIERS_DEFAULT (ControlMask | CompAltMask)

#define ROTATE_LEFT_WINDOW_KEY_DEFAULT       "Left"
#define ROTATE_LEFT_WINDOW_MODIFIERS_DEFAULT		    \
    (ShiftMask | ControlMask | CompAltMask)

#define ROTATE_RIGHT_WINDOW_KEY_DEFAULT       "Right"
#define ROTATE_RIGHT_WINDOW_MODIFIERS_DEFAULT		    \
    (ShiftMask | ControlMask | CompAltMask)

#define ROTATE_SNAP_TOP_DEFAULT FALSE

#define ROTATE_SPEED_DEFAULT   1.5f
#define ROTATE_SPEED_MIN       0.1f
#define ROTATE_SPEED_MAX       50.0f
#define ROTATE_SPEED_PRECISION 0.1f

#define ROTATE_TIMESTEP_DEFAULT   1.2f
#define ROTATE_TIMESTEP_MIN       0.1f
#define ROTATE_TIMESTEP_MAX       50.0f
#define ROTATE_TIMESTEP_PRECISION 0.1f

#define ROTATE_EDGEFLIP_POINTER_DEFAULT FALSE
#define ROTATE_EDGEFLIP_WINDOW_DEFAULT  TRUE
#define ROTATE_EDGEFLIP_DND_DEFAULT     TRUE

#define ROTATE_FLIPTIME_DEFAULT 350
#define ROTATE_FLIPTIME_MIN     0
#define ROTATE_FLIPTIME_MAX     1000

static int displayPrivateIndex;

typedef struct _RotateDisplay {
    int		    screenPrivateIndex;
    HandleEventProc handleEvent;

    Window xdndWindow;
} RotateDisplay;

#define ROTATE_SCREEN_OPTION_POINTER_INVERT_Y	 0
#define ROTATE_SCREEN_OPTION_POINTER_SENSITIVITY 1
#define ROTATE_SCREEN_OPTION_ACCELERATION        2
#define ROTATE_SCREEN_OPTION_INITIATE		 3
#define ROTATE_SCREEN_OPTION_LEFT		 4
#define ROTATE_SCREEN_OPTION_RIGHT		 5
#define ROTATE_SCREEN_OPTION_LEFT_WINDOW	 6
#define ROTATE_SCREEN_OPTION_RIGHT_WINDOW	 7
#define ROTATE_SCREEN_OPTION_SNAP_TOP		 8
#define ROTATE_SCREEN_OPTION_SPEED		 9
#define ROTATE_SCREEN_OPTION_TIMESTEP		 10
#define ROTATE_SCREEN_OPTION_EDGEFLIP_POINTER	 11
#define ROTATE_SCREEN_OPTION_EDGEFLIP_WINDOW	 12
#define ROTATE_SCREEN_OPTION_EDGEFLIP_DND	 13
#define ROTATE_SCREEN_OPTION_FLIPTIME		 14
#define ROTATE_SCREEN_OPTION_TO_1		 15
#define ROTATE_SCREEN_OPTION_TO_2		 16
#define ROTATE_SCREEN_OPTION_TO_3		 17
#define ROTATE_SCREEN_OPTION_TO_4		 18
#define ROTATE_SCREEN_OPTION_TO_5		 19
#define ROTATE_SCREEN_OPTION_TO_6		 20
#define ROTATE_SCREEN_OPTION_TO_7		 21
#define ROTATE_SCREEN_OPTION_TO_8		 22
#define ROTATE_SCREEN_OPTION_TO_9		 23
#define ROTATE_SCREEN_OPTION_TO_10		 24
#define ROTATE_SCREEN_OPTION_TO_11		 25
#define ROTATE_SCREEN_OPTION_TO_12		 26
#define ROTATE_SCREEN_OPTION_TO_1_WINDOW	 27
#define ROTATE_SCREEN_OPTION_TO_2_WINDOW	 28
#define ROTATE_SCREEN_OPTION_TO_3_WINDOW	 29
#define ROTATE_SCREEN_OPTION_TO_4_WINDOW	 30
#define ROTATE_SCREEN_OPTION_TO_5_WINDOW	 31
#define ROTATE_SCREEN_OPTION_TO_6_WINDOW	 32
#define ROTATE_SCREEN_OPTION_TO_7_WINDOW	 33
#define ROTATE_SCREEN_OPTION_TO_8_WINDOW	 34
#define ROTATE_SCREEN_OPTION_TO_9_WINDOW	 35
#define ROTATE_SCREEN_OPTION_TO_10_WINDOW	 36
#define ROTATE_SCREEN_OPTION_TO_11_WINDOW	 37
#define ROTATE_SCREEN_OPTION_TO_12_WINDOW	 38
#define ROTATE_SCREEN_OPTION_NUM		 39

typedef struct _RotateScreen {
    PreparePaintScreenProc	 preparePaintScreen;
    DonePaintScreenProc		 donePaintScreen;
    PaintScreenProc		 paintScreen;
    SetScreenOptionForPluginProc setScreenOptionForPlugin;
    WindowGrabNotifyProc	 windowGrabNotify;
    WindowUngrabNotifyProc	 windowUngrabNotify;

    CompOption opt[ROTATE_SCREEN_OPTION_NUM];

    Bool  pointerInvertY;
    float pointerSensitivity;
    Bool  snapTop;
    float acceleration;

    float speed;
    float timestep;

    int grabIndex;

    GLfloat xrot, xVelocity;
    GLfloat yrot, yVelocity;

    GLfloat baseXrot;

    Bool    moving;
    GLfloat moveTo;

    int invert;

    Window moveWindow;
    int    moveWindowX;

    XPoint savedPointer;
    Bool   grabbed;

    Bool	      edges;
    int		      flipTime;
    CompTimeoutHandle rotateHandle;
    Bool	      slow;
    unsigned int      grabMask;
    CompWindow	      *grabWindow;
} RotateScreen;

#define GET_ROTATE_DISPLAY(d)				       \
    ((RotateDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define ROTATE_DISPLAY(d)		       \
    RotateDisplay *rd = GET_ROTATE_DISPLAY (d)

#define GET_ROTATE_SCREEN(s, rd)				   \
    ((RotateScreen *) (s)->privates[(rd)->screenPrivateIndex].ptr)

#define ROTATE_SCREEN(s)						      \
    RotateScreen *rs = GET_ROTATE_SCREEN (s, GET_ROTATE_DISPLAY (s->display))

#define NUM_OPTIONS(s) (sizeof ((s)->opt) / sizeof (CompOption))

static CompOption *
rotateGetScreenOptions (CompScreen *screen,
			int	   *count)
{
    ROTATE_SCREEN (screen);

    *count = NUM_OPTIONS (rs);
    return rs->opt;
}

static Bool
rotateSetScreenOption (CompScreen      *screen,
		       char	       *name,
		       CompOptionValue *value)
{
    CompOption *o;
    int	       index;

    ROTATE_SCREEN (screen);

    o = compFindOption (rs->opt, NUM_OPTIONS (rs), name, &index);
    if (!o)
	return FALSE;

    switch (index) {
    case ROTATE_SCREEN_OPTION_POINTER_INVERT_Y:
	if (compSetBoolOption (o, value))
	{
	    rs->pointerInvertY = o->value.b;
	    return TRUE;
	}
	break;
    case ROTATE_SCREEN_OPTION_POINTER_SENSITIVITY:
	if (compSetFloatOption (o, value))
	{
	    rs->pointerSensitivity = o->value.f *
		ROTATE_POINTER_SENSITIVITY_FACTOR;
	    return TRUE;
	}
	break;
    case ROTATE_SCREEN_OPTION_ACCELERATION:
	if (compSetFloatOption (o, value))
	{
	    rs->acceleration = o->value.f;
	    return TRUE;
	}
	break;
    case ROTATE_SCREEN_OPTION_INITIATE:
    case ROTATE_SCREEN_OPTION_LEFT:
    case ROTATE_SCREEN_OPTION_RIGHT:
    case ROTATE_SCREEN_OPTION_TO_1:
    case ROTATE_SCREEN_OPTION_TO_2:
    case ROTATE_SCREEN_OPTION_TO_3:
    case ROTATE_SCREEN_OPTION_TO_4:
    case ROTATE_SCREEN_OPTION_TO_5:
    case ROTATE_SCREEN_OPTION_TO_6:
    case ROTATE_SCREEN_OPTION_TO_7:
    case ROTATE_SCREEN_OPTION_TO_8:
    case ROTATE_SCREEN_OPTION_TO_9:
    case ROTATE_SCREEN_OPTION_TO_10:
    case ROTATE_SCREEN_OPTION_TO_11:
    case ROTATE_SCREEN_OPTION_TO_12:
    case ROTATE_SCREEN_OPTION_LEFT_WINDOW:
    case ROTATE_SCREEN_OPTION_RIGHT_WINDOW:
    case ROTATE_SCREEN_OPTION_TO_1_WINDOW:
    case ROTATE_SCREEN_OPTION_TO_2_WINDOW:
    case ROTATE_SCREEN_OPTION_TO_3_WINDOW:
    case ROTATE_SCREEN_OPTION_TO_4_WINDOW:
    case ROTATE_SCREEN_OPTION_TO_5_WINDOW:
    case ROTATE_SCREEN_OPTION_TO_6_WINDOW:
    case ROTATE_SCREEN_OPTION_TO_7_WINDOW:
    case ROTATE_SCREEN_OPTION_TO_8_WINDOW:
    case ROTATE_SCREEN_OPTION_TO_9_WINDOW:
    case ROTATE_SCREEN_OPTION_TO_10_WINDOW:
    case ROTATE_SCREEN_OPTION_TO_11_WINDOW:
    case ROTATE_SCREEN_OPTION_TO_12_WINDOW:
	if (addScreenBinding (screen, &value->bind))
	{
	    removeScreenBinding (screen, &o->value.bind);

	    if (compSetBindingOption (o, value))
		return TRUE;
	}
	break;
    case ROTATE_SCREEN_OPTION_SNAP_TOP:
	if (compSetBoolOption (o, value))
	{
	    rs->snapTop = o->value.b;
	    return TRUE;
	}
	break;
    case ROTATE_SCREEN_OPTION_SPEED:
	if (compSetFloatOption (o, value))
	{
	    rs->speed = o->value.f;
	    return TRUE;
	}
	break;
    case ROTATE_SCREEN_OPTION_TIMESTEP:
	if (compSetFloatOption (o, value))
	{
	    rs->timestep = o->value.f;
	    return TRUE;
	}
	break;
    case ROTATE_SCREEN_OPTION_EDGEFLIP_POINTER:
    case ROTATE_SCREEN_OPTION_EDGEFLIP_WINDOW:
    case ROTATE_SCREEN_OPTION_EDGEFLIP_DND:
	if (compSetBoolOption (o, value))
	{
	    if (o->value.b)
	    {
		if (!rs->edges)
		{
		    enableScreenEdge (screen, SCREEN_EDGE_LEFT);
		    enableScreenEdge (screen, SCREEN_EDGE_RIGHT);

		    rs->edges = TRUE;
		}
	    }
	    else if (rs->edges)
	    {
		if (!rs->opt[ROTATE_SCREEN_OPTION_EDGEFLIP_POINTER].value.b &&
		    !rs->opt[ROTATE_SCREEN_OPTION_EDGEFLIP_WINDOW].value.b  &&
		    !rs->opt[ROTATE_SCREEN_OPTION_EDGEFLIP_DND].value.b)
		{
		    disableScreenEdge (screen, SCREEN_EDGE_LEFT);
		    disableScreenEdge (screen, SCREEN_EDGE_RIGHT);

		    rs->edges = FALSE;
		}
	    }

	    return TRUE;
	}
	break;
    case ROTATE_SCREEN_OPTION_FLIPTIME:
	if (compSetIntOption (o, value))
	{
	    rs->flipTime = o->value.i;
	    return TRUE;
	}
    default:
	break;
    }

    return FALSE;
}

static void
rotateScreenInitOptions (RotateScreen *rs,
			 Display      *display)
{
    CompOption *o;
    char       *str;

    o = &rs->opt[ROTATE_SCREEN_OPTION_POINTER_INVERT_Y];
    o->name      = "invert_y";
    o->shortDesc = N_("Pointer Invert Y");
    o->longDesc  = N_("Invert Y axis for pointer movement");
    o->type      = CompOptionTypeBool;
    o->value.b   = ROTATE_POINTER_INVERT_Y_DEFAULT;

    o = &rs->opt[ROTATE_SCREEN_OPTION_POINTER_SENSITIVITY];
    o->name		= "sensitivity";
    o->shortDesc	= N_("Pointer Sensitivity");
    o->longDesc		= N_("Sensitivity of pointer movement");
    o->type		= CompOptionTypeFloat;
    o->value.f		= ROTATE_POINTER_SENSITIVITY_DEFAULT;
    o->rest.f.min	= ROTATE_POINTER_SENSITIVITY_MIN;
    o->rest.f.max	= ROTATE_POINTER_SENSITIVITY_MAX;
    o->rest.f.precision = ROTATE_POINTER_SENSITIVITY_PRECISION;

    o = &rs->opt[ROTATE_SCREEN_OPTION_ACCELERATION];
    o->name		= "acceleration";
    o->shortDesc	= N_("Acceleration");
    o->longDesc		= N_("Rotation Acceleration");
    o->type		= CompOptionTypeFloat;
    o->value.f		= ROTATE_ACCELERATION_DEFAULT;
    o->rest.f.min	= ROTATE_ACCELERATION_MIN;
    o->rest.f.max	= ROTATE_ACCELERATION_MAX;
    o->rest.f.precision = ROTATE_ACCELERATION_PRECISION;

    o = &rs->opt[ROTATE_SCREEN_OPTION_INITIATE];
    o->name			     = "initiate";
    o->shortDesc		     = N_("Initiate");
    o->longDesc			     = N_("Start Rotation");
    o->type			     = CompOptionTypeBinding;
    o->value.bind.type		     = CompBindingTypeButton;
    o->value.bind.u.button.modifiers = ROTATE_INITIATE_MODIFIERS_DEFAULT;
    o->value.bind.u.button.button    = ROTATE_INITIATE_BUTTON_DEFAULT;

    o = &rs->opt[ROTATE_SCREEN_OPTION_LEFT];
    o->name			  = "rotate_left";
    o->shortDesc		  = N_("Rotate Left");
    o->longDesc			  = N_("Rotate left");
    o->type			  = CompOptionTypeBinding;
    o->value.bind.type		  = CompBindingTypeKey;
    o->value.bind.u.key.modifiers = ROTATE_LEFT_MODIFIERS_DEFAULT;
    o->value.bind.u.key.keycode   =
	XKeysymToKeycode (display,
			  XStringToKeysym (ROTATE_LEFT_KEY_DEFAULT));

    o = &rs->opt[ROTATE_SCREEN_OPTION_RIGHT];
    o->name			  = "rotate_right";
    o->shortDesc		  = N_("Rotate Right");
    o->longDesc			  = N_("Rotate right");
    o->type			  = CompOptionTypeBinding;
    o->value.bind.type		  = CompBindingTypeKey;
    o->value.bind.u.key.modifiers = ROTATE_RIGHT_MODIFIERS_DEFAULT;
    o->value.bind.u.key.keycode   =
	XKeysymToKeycode (display,
			  XStringToKeysym (ROTATE_RIGHT_KEY_DEFAULT));

    o = &rs->opt[ROTATE_SCREEN_OPTION_LEFT_WINDOW];
    o->name			  = "rotate_left_window";
    o->shortDesc		  = N_("Rotate Left with Window");
    o->longDesc			  = N_("Rotate left and bring active window "
	"along");
    o->type			  = CompOptionTypeBinding;
    o->value.bind.type		  = CompBindingTypeKey;
    o->value.bind.u.key.modifiers = ROTATE_LEFT_WINDOW_MODIFIERS_DEFAULT;
    o->value.bind.u.key.keycode   =
		XKeysymToKeycode (display,
			  XStringToKeysym (ROTATE_LEFT_WINDOW_KEY_DEFAULT));

    o = &rs->opt[ROTATE_SCREEN_OPTION_RIGHT_WINDOW];
    o->name			  = "rotate_right_window";
    o->shortDesc		  = N_("Rotate Right with Window");
    o->longDesc			  = N_("Rotate right and bring active window "
	"along");
    o->type			  = CompOptionTypeBinding;
    o->value.bind.type		  = CompBindingTypeKey;
    o->value.bind.u.key.modifiers = ROTATE_RIGHT_WINDOW_MODIFIERS_DEFAULT;
    o->value.bind.u.key.keycode   =
	XKeysymToKeycode (display,
			  XStringToKeysym (ROTATE_RIGHT_WINDOW_KEY_DEFAULT));

#define ROTATE_TO_SHORT        N_("Rotate To Face %d")
#define ROTATE_TO_LONG         N_("Rotate to face %d")
#define ROTATE_TO_WINDOW_SHORT N_("Rotate To Face %d with Window")
#define ROTATE_TO_WINDOW_LONG  N_("Rotate to face %d and bring active " \
				  "window along")

#define ROTATE_TO_OPTION(n)						 \
    o = &rs->opt[ROTATE_SCREEN_OPTION_TO_ ## n];			 \
    o->name			  = "rotate_to_" #n;			 \
    asprintf (&str, ROTATE_TO_SHORT, n);				 \
    o->shortDesc		  = str;				 \
    asprintf (&str, ROTATE_TO_LONG, n);					 \
    o->longDesc			  = str;				 \
    o->type			  = CompOptionTypeBinding;		 \
    o->value.bind.type		  = CompBindingTypeNone;		 \
									 \
    o = &rs->opt[ROTATE_SCREEN_OPTION_TO_ ## n ## _WINDOW];		 \
    o->name			  = "rotate_to_" #n "_window";		 \
    asprintf (&str, ROTATE_TO_WINDOW_SHORT, n);				 \
    o->shortDesc		  = str;				 \
    asprintf (&str, ROTATE_TO_WINDOW_LONG, n);				 \
    o->longDesc			  = str;				 \
    o->type			  = CompOptionTypeBinding;		 \
    o->value.bind.type		  = CompBindingTypeNone

    ROTATE_TO_OPTION (1);
    ROTATE_TO_OPTION (2);
    ROTATE_TO_OPTION (3);
    ROTATE_TO_OPTION (4);
    ROTATE_TO_OPTION (5);
    ROTATE_TO_OPTION (6);
    ROTATE_TO_OPTION (7);
    ROTATE_TO_OPTION (8);
    ROTATE_TO_OPTION (9);
    ROTATE_TO_OPTION (10);
    ROTATE_TO_OPTION (11);
    ROTATE_TO_OPTION (12);

    o = &rs->opt[ROTATE_SCREEN_OPTION_SNAP_TOP];
    o->name      = "snap_top";
    o->shortDesc = N_("Snap To Top Face");
    o->longDesc  = N_("Snap Cube Rotation to Top Face");
    o->type      = CompOptionTypeBool;
    o->value.b   = ROTATE_SNAP_TOP_DEFAULT;

    o = &rs->opt[ROTATE_SCREEN_OPTION_SPEED];
    o->name		= "speed";
    o->shortDesc	= N_("Speed");
    o->longDesc		= N_("Rotation Speed");
    o->type		= CompOptionTypeFloat;
    o->value.f		= ROTATE_SPEED_DEFAULT;
    o->rest.f.min	= ROTATE_SPEED_MIN;
    o->rest.f.max	= ROTATE_SPEED_MAX;
    o->rest.f.precision = ROTATE_SPEED_PRECISION;

    o = &rs->opt[ROTATE_SCREEN_OPTION_TIMESTEP];
    o->name		= "timestep";
    o->shortDesc	= N_("Timestep");
    o->longDesc		= N_("Rotation Timestep");
    o->type		= CompOptionTypeFloat;
    o->value.f		= ROTATE_TIMESTEP_DEFAULT;
    o->rest.f.min	= ROTATE_TIMESTEP_MIN;
    o->rest.f.max	= ROTATE_TIMESTEP_MAX;
    o->rest.f.precision = ROTATE_TIMESTEP_PRECISION;

    o = &rs->opt[ROTATE_SCREEN_OPTION_EDGEFLIP_POINTER];
    o->name      = "edge_flip_pointer";
    o->shortDesc = N_("Edge Flip Pointer");
    o->longDesc  = N_("Flip to next viewport when moving pointer to screen edge");
    o->type      = CompOptionTypeBool;
    o->value.b   = ROTATE_EDGEFLIP_POINTER_DEFAULT;

    o = &rs->opt[ROTATE_SCREEN_OPTION_EDGEFLIP_WINDOW];
    o->name      = "edge_flip_move";
    o->shortDesc = N_("Edge Flip Move");
    o->longDesc  = N_("Flip to next viewport when moving window to screen edge");
    o->type      = CompOptionTypeBool;
    o->value.b   = ROTATE_EDGEFLIP_WINDOW_DEFAULT;

    o = &rs->opt[ROTATE_SCREEN_OPTION_EDGEFLIP_DND];
    o->name      = "edge_flip_dnd";
    o->shortDesc = N_("Edge Flip DnD");
    o->longDesc  = N_("Flip to next viewport when dragging object to screen edge");
    o->type      = CompOptionTypeBool;
    o->value.b   = ROTATE_EDGEFLIP_DND_DEFAULT;

    o = &rs->opt[ROTATE_SCREEN_OPTION_FLIPTIME];
    o->name	  = "flip_time";
    o->shortDesc  = N_("Flip Time");
    o->longDesc	  = N_("Timeout before flipping viewport");
    o->type	  = CompOptionTypeInt;
    o->value.i	  = ROTATE_FLIPTIME_DEFAULT;
    o->rest.i.min = ROTATE_FLIPTIME_MIN;
    o->rest.i.max = ROTATE_FLIPTIME_MAX;
}

static int
adjustVelocity (RotateScreen *rs,
		int	     size)
{
    float xrot, yrot, adjust, amount;

    if (rs->moving)
    {
	xrot = rs->moveTo + (rs->xrot + rs->baseXrot);
    }
    else
    {
	xrot = rs->xrot;
	if (rs->xrot < -180.0f / size)
	    xrot = 360.0f / size + rs->xrot;
	else if (rs->xrot > 180.0f / size)
	    xrot = rs->xrot - 360.0f / size;
    }

    adjust = -xrot * 0.05f * rs->acceleration;
    amount = fabs (xrot);
    if (amount < 10.0f)
	amount = 10.0f;
    else if (amount > 30.0f)
	amount = 30.0f;

    if (rs->slow)
	adjust *= 0.05f;

    rs->xVelocity = (amount * rs->xVelocity + adjust) / (amount + 2.0f);

    if (rs->snapTop && rs->yrot > 50.0f)
	yrot = -(90.f - rs->yrot);
    else
	yrot = rs->yrot;

    adjust = -yrot * 0.05f * rs->acceleration;
    amount = fabs (rs->yrot);
    if (amount < 10.0f)
	amount = 10.0f;
    else if (amount > 30.0f)
	amount = 30.0f;

    rs->yVelocity = (amount * rs->yVelocity + adjust) / (amount + 2.0f);

    return (fabs (xrot) < 0.1f && fabs (rs->xVelocity) < 0.2f &&
	    fabs (yrot) < 0.1f && fabs (rs->yVelocity) < 0.2f);
}

static void
rotatePreparePaintScreen (CompScreen *s,
			  int	     msSinceLastPaint)
{
    ROTATE_SCREEN (s);

    if (rs->grabIndex || rs->moving)
    {
	int   steps;
	float amount, chunk;

	amount = msSinceLastPaint * 0.05f * rs->speed;
	steps  = amount / (0.5f * rs->timestep);
	if (!steps) steps = 1;
	chunk  = amount / (float) steps;

	while (steps--)
	{
	    rs->xrot += rs->xVelocity * chunk;
	    rs->yrot += rs->yVelocity * chunk;

	    if (rs->xrot > 360.0f / s->size)
	    {
		rs->baseXrot += 360.0f / s->size;
		rs->xrot -= 360.0f / s->size;
	    }
	    else if (rs->xrot < 0.0f)
	    {
		rs->baseXrot -= 360.0f / s->size;
		rs->xrot += 360.0f / s->size;
	    }

	    if (rs->invert == -1)
	    {
		if (rs->yrot > 45.0f)
		{
		    rs->yVelocity = 0.0f;
		    rs->yrot = 45.0f;
		}
		else if (rs->yrot < -45.0f)
		{
		    rs->yVelocity = 0.0f;
		    rs->yrot = -45.0f;
		}
	    }
	    else
	    {
		if (rs->yrot > 100.0f)
		{
		    rs->yVelocity = 0.0f;
		    rs->yrot = 100.0f;
		}
		else if (rs->yrot < -100.0f)
		{
		    rs->yVelocity = 0.0f;
		    rs->yrot = -100.0f;
		}
	    }

	    if (rs->grabbed)
	    {
		rs->xVelocity /= 1.25f;
		rs->yVelocity /= 1.25f;

		if (fabs (rs->xVelocity) < 0.01f)
		    rs->xVelocity = 0.0f;
		if (fabs (rs->yVelocity) < 0.01f)
		    rs->yVelocity = 0.0f;
	    }
	    else if (adjustVelocity (rs, s->size))
	    {
		rs->xVelocity = 0.0f;
		rs->yVelocity = 0.0f;

		if (fabs (rs->yrot) < 0.1f)
		{
		    float xrot;
		    int   tx;

		    xrot = rs->baseXrot + rs->xrot;
		    if (xrot < 0.0f)
			tx = (s->size * xrot / 360.0f) - 0.5f;
		    else
			tx = (s->size * xrot / 360.0f) + 0.5f;

		    moveScreenViewport (s, tx, TRUE);

		    rs->xrot = 0.0f;
		    rs->yrot = 0.0f;
		    rs->baseXrot = rs->moveTo = 0.0f;
		    rs->moving = FALSE;

		    if (rs->grabIndex)
		    {
			removeScreenGrab (s, rs->grabIndex, &rs->savedPointer);
			rs->grabIndex = 0;
		    }

		    if (rs->moveWindow)
		    {
			CompWindow *w;

			w = findWindowAtScreen (s, rs->moveWindow);
			if (w)
			{
			    moveWindow (w, w->attrib.x - rs->moveWindowX, 0,
					TRUE, TRUE);
			    syncWindowPosition (w);
			}
		    }
		    else
			focusDefaultWindow (s->display);

		    rs->moveWindow = 0;
		}
		break;
	    }
	}

	if (rs->moveWindow)
	{
	    CompWindow *w;

	    w = findWindowAtScreen (s, rs->moveWindow);
	    if (w)
	    {
		float xrot = (s->size * (rs->baseXrot + rs->xrot)) / 360.0f;

		moveWindowToViewportPosition (w,
					      rs->moveWindowX - xrot * s->width,
					      FALSE);
	    }
	}
    }

    UNWRAP (rs, s, preparePaintScreen);
    (*s->preparePaintScreen) (s, msSinceLastPaint);
    WRAP (rs, s, preparePaintScreen, rotatePreparePaintScreen);
}

static void
rotateDonePaintScreen (CompScreen *s)
{
    ROTATE_SCREEN (s);

    if (rs->grabIndex || rs->moving)
    {
	if ((!rs->grabbed && !rs->snapTop) || rs->xVelocity || rs->yVelocity)
	    damageScreen (s);
    }

    UNWRAP (rs, s, donePaintScreen);
    (*s->donePaintScreen) (s);
    WRAP (rs, s, donePaintScreen, rotateDonePaintScreen);
}

static Bool
rotatePaintScreen (CompScreen		   *s,
		   const ScreenPaintAttrib *sAttrib,
		   Region		   region,
		   unsigned int		   mask)
{
    Bool status;

    ROTATE_SCREEN (s);

    if (rs->grabIndex || rs->moving)
    {
	ScreenPaintAttrib sa = *sAttrib;

	sa.xRotate += rs->baseXrot + rs->xrot;
	sa.vRotate += rs->yrot;

	mask &= ~PAINT_SCREEN_REGION_MASK;
	mask |= PAINT_SCREEN_TRANSFORMED_MASK;

	UNWRAP (rs, s, paintScreen);
	status = (*s->paintScreen) (s, &sa, region, mask);
	WRAP (rs, s, paintScreen, rotatePaintScreen);
    }
    else
    {
	UNWRAP (rs, s, paintScreen);
	status = (*s->paintScreen) (s, sAttrib, region, mask);
	WRAP (rs, s, paintScreen, rotatePaintScreen);
    }

    return status;
}

static void
rotateInitiate (CompScreen *s,
		int	   x,
		int	   y)
{
    ROTATE_SCREEN (s);

    rs->moving = FALSE;
    rs->moveTo = 0.0f;
    rs->slow   = FALSE;

    if (!rs->grabIndex)
    {
	rs->grabIndex = pushScreenGrab (s, s->invisibleCursor, "rotate");
	if (rs->grabIndex)
	{
	    rs->savedPointer.x = x;
	    rs->savedPointer.y = y;
	}
    }

    if (rs->grabIndex)
    {
	rs->grabbed = TRUE;
	rs->snapTop = rs->opt[ROTATE_SCREEN_OPTION_SNAP_TOP].value.b;
    }
}

static void
rotate (CompScreen *s,
	int	   x,
	int	   y,
	int        direction)
{
    ROTATE_SCREEN (s);

    /* we allow the grab to fail here so that we can rotate on drag-and-drop */
    if (!rs->grabIndex)
	rotateInitiate (s, x, y);

    rs->moving  = TRUE;
    rs->moveTo += (360.0f / s->size) * direction;
    rs->grabbed = FALSE;

    damageScreen (s);
}

static void
rotateWithWindow (CompScreen *s,
		  int	     x,
		  int	     y,
		  int        direction)
{
    CompWindow *w;

    ROTATE_SCREEN (s);

    if (!rs->grabIndex)
    {
	for (w = s->windows; w; w = w->next)
	    if (s->display->activeWindow == w->id)
		break;

	if (!w)
	    return;

	if (w->type & (CompWindowTypeDesktopMask | CompWindowTypeDockMask))
	    return;

	if (w->state & CompWindowStateStickyMask)
	    return;

	rotateInitiate (s, x, y);

	rs->moveWindow  = w->id;
	rs->moveWindowX = w->attrib.x;
    }

    if (rs->grabIndex)
    {
	rs->moving  = TRUE;
	rs->moveTo += (360.0f / s->size) * direction;
	rs->grabbed = FALSE;

	damageScreen (s);
    }
}

static void
rotateTerminate (CompScreen *s)
{
    ROTATE_SCREEN (s);

    if (rs->grabIndex)
    {
	rs->grabbed = FALSE;
	damageScreen (s);
    }
}

static Bool
rotateLeft (void *closure)
{
    CompScreen *s = closure;
    int        warpX;

    ROTATE_SCREEN (s);

    if (otherScreenGrabExist (s, "rotate", "move", 0))
    {
	rs->moveTo = 0.0f;
	rs->slow = FALSE;

	return FALSE;
    }

    warpX = pointerX + s->width;
    warpPointer (s->display, s->width - 10, 0);
    lastPointerX = warpX;

    rotate (s, 0, pointerY, -1);

    XWarpPointer (s->display->display, None, None, 0, 0, 0, 0, -1, 0);
    rs->savedPointer.x = lastPointerX - 9;

    rs->rotateHandle = 0;

    return FALSE;
}

static Bool
rotateRight (void *closure)
{
    CompScreen *s = closure;
    int        warpX;

    ROTATE_SCREEN (s);

    if (otherScreenGrabExist (s, "rotate", "move", 0))
    {
	rs->moveTo = 0.0f;
	rs->slow = FALSE;

	return FALSE;
    }

    warpX = pointerX - s->width;
    warpPointer (s->display, 10 - s->width, 0);
    lastPointerX = warpX;

    rotate (s, 0, lastPointerY, 1);

    XWarpPointer (s->display->display, None, None, 0, 0, 0, 0, 1, 0);

    rs->savedPointer.x = lastPointerX + 9;

    rs->rotateHandle = 0;

    return FALSE;
}

static void
handleEnterScreenEdge (CompScreen *s,
		       Window     id)
{
    ROTATE_SCREEN (s);

    if (rs->edges && id == s->screenEdge[SCREEN_EDGE_LEFT].id)
    {
	if (rs->flipTime == 0 || rs->grabIndex)
	{
	    int pointerDx = pointerX - lastPointerX;
	    int warpX;

	    warpX = pointerX + s->width;
	    warpPointer (s->display, s->width - 10, 0);
	    lastPointerX = warpX - pointerDx;

	    rotate (s, 0, pointerY, -1);

	    XWarpPointer (s->display->display, None, None,
			  0, 0, 0, 0, -1, 0);
	    rs->savedPointer.x = lastPointerX - 9;
	}
	else
	{
	    if (!rs->rotateHandle)
		rs->rotateHandle =
		    compAddTimeout (rs->flipTime, rotateLeft, s);

	    rs->moving  = TRUE;
	    rs->moveTo -= 360.0f / s->size;
	    rs->slow    = TRUE;

	    damageScreen (s);
	}
    }
    else if (rs->edges && id == s->screenEdge[SCREEN_EDGE_RIGHT].id)
    {
	if (rs->flipTime == 0 || rs->grabIndex)
	{
	    int pointerDx = pointerX - lastPointerX;
	    int warpX;

	    warpX = pointerX - s->width;
	    warpPointer (s->display, 10 - s->width, 0);
	    lastPointerX = warpX - pointerDx;

	    rotate (s, 0, pointerY, 1);

	    XWarpPointer (s->display->display, None, None,
			  0, 0, 0, 0, 1, 0);
	    rs->savedPointer.x = lastPointerX + 9;
	}
	else
	{
	    if (!rs->rotateHandle)
		rs->rotateHandle =
		    compAddTimeout (rs->flipTime, rotateRight, s);

	    rs->moving  = TRUE;
	    rs->moveTo += 360.0f / s->size;
	    rs->slow    = TRUE;

	    damageScreen (s);
	}
    }
    else if (rs->rotateHandle)
    {
	compRemoveTimeout (rs->rotateHandle);
	rs->rotateHandle = 0;

	if (rs->slow)
	{
	    rs->moveTo = 0.0f;
	    rs->slow = FALSE;
	}

	damageScreen (s);
    }
}

static int
rotateRotationTo (CompScreen *s,
		  int	     face)
{
    int delta;

    ROTATE_SCREEN (s);

    delta = face - s->x - (rs->moveTo / (360.0f / s->size));
    if (delta > s->size / 2)
	delta -= s->size;
    else if (delta < -(s->size / 2))
	delta += s->size;

    return delta;
}

static void
rotateHandleEvent (CompDisplay *d,
		   XEvent      *event)
{
    CompScreen *s;

    ROTATE_DISPLAY (d);

    switch (event->type) {
    case KeyPress:
    case KeyRelease:
	s = findScreenAtDisplay (d, event->xkey.root);
	if (s)
	{
	    ROTATE_SCREEN (s);

	    if (!otherScreenGrabExist (s, "rotate", "switcher", "cube", 0))
	    {
		int face;

		if (eventMatches (d, event,
				  &rs->opt[ROTATE_SCREEN_OPTION_INITIATE]))
		    rotateInitiate (s, event->xkey.x_root, event->xkey.y_root);

		if (eventMatches (d, event,
				  &rs->opt[ROTATE_SCREEN_OPTION_LEFT_WINDOW]))
		    rotateWithWindow (s, event->xkey.x_root, event->xkey.y_root,
				      -1);
		else if (eventMatches (d, event,
				       &rs->opt[ROTATE_SCREEN_OPTION_LEFT]))
		    rotate (s, event->xkey.x_root, event->xkey.y_root, -1);

		if (eventMatches (d, event,
				  &rs->opt[ROTATE_SCREEN_OPTION_RIGHT_WINDOW]))
		    rotateWithWindow (s, event->xkey.x_root,
				      event->xkey.y_root, 1);
		else if (eventMatches (d, event,
				       &rs->opt[ROTATE_SCREEN_OPTION_RIGHT]))
		    rotate (s, event->xkey.x_root, event->xkey.y_root, 1);

		for (face = 0; face < 12 && face < s->size; face++)
		{
		    if (eventMatches (d, event,
				      &rs->opt[ROTATE_SCREEN_OPTION_TO_1_WINDOW
					       + face]))
		    {
			rotateWithWindow (s, event->xkey.x_root,
					  event->xkey.y_root,
					  rotateRotationTo (s, face));
			break;
		    }
		    else if (eventMatches (d, event,
					   &rs->opt[ROTATE_SCREEN_OPTION_TO_1
						    + face]))
		    {
			rotate (s, event->xkey.x_root, event->xkey.y_root,
				rotateRotationTo (s, face));
			break;
		    }
		}
	    }

	    if (event->type	    == KeyPress &&
		event->xkey.keycode == s->escapeKeyCode)
	    {
		rs->snapTop = FALSE;
		rotateTerminate (s);
	    }

	    if (eventTerminates (d, event,
				 &rs->opt[ROTATE_SCREEN_OPTION_INITIATE]))
		rotateTerminate (s);
	}
	break;
    case ButtonPress:
    case ButtonRelease:
	s = findScreenAtDisplay (d, event->xbutton.root);
	if (s)
	{
	    ROTATE_SCREEN (s);

	    if (!otherScreenGrabExist (s, "rotate", "cube", 0))
	    {
		if (eventMatches (d, event,
				  &rs->opt[ROTATE_SCREEN_OPTION_INITIATE]))
		    rotateInitiate (s,
				    event->xbutton.x_root,
				    event->xbutton.y_root);

		if (eventMatches (d, event,
				  &rs->opt[ROTATE_SCREEN_OPTION_LEFT_WINDOW]))
		    rotateWithWindow (s, event->xbutton.x_root,
				      event->xbutton.y_root, -1);
		else if (eventMatches (d, event,
				       &rs->opt[ROTATE_SCREEN_OPTION_LEFT]))
		    rotate (s, event->xbutton.x_root,
			    event->xbutton.y_root, -1);

		if (eventMatches (d, event,
				  &rs->opt[ROTATE_SCREEN_OPTION_RIGHT_WINDOW]))
		    rotateWithWindow (s, event->xbutton.x_root,
				      event->xbutton.y_root, 1);
		else if (eventMatches (d, event,
				       &rs->opt[ROTATE_SCREEN_OPTION_RIGHT]))
		    rotate (s, event->xbutton.x_root, event->xbutton.y_root, 1);
	    }

	    if (eventTerminates (d, event,
				 &rs->opt[ROTATE_SCREEN_OPTION_INITIATE]))
		rotateTerminate (s);
	}
	break;
    case MotionNotify:
	s = findScreenAtDisplay (d, event->xmotion.root);
	if (s)
	{
	    ROTATE_SCREEN (s);

	    if (rs->grabIndex)
	    {
		if (rs->grabbed)
		{
		    GLfloat pointerDx, pointerDy;

		    pointerDx = pointerX - lastPointerX;
		    pointerDy = pointerY - lastPointerY;

		    if (event->xmotion.x_root < 50	       ||
			event->xmotion.y_root < 50	       ||
			event->xmotion.x_root > s->width  - 50 ||
			event->xmotion.y_root > s->height - 50)
		    {
			warpPointer (d,
				     (s->width  / 2) - pointerX,
				     (s->height / 2) - pointerY);
		    }

		    if (rs->pointerInvertY)
			pointerDy = -pointerDy;

		    rs->xVelocity += pointerDx * rs->pointerSensitivity *
			rs->invert;
		    rs->yVelocity += pointerDy * rs->pointerSensitivity;

		    damageScreen (s);
		}
		else
		{
		    rs->savedPointer.x += pointerX - lastPointerX;
		    rs->savedPointer.y += pointerY - lastPointerY;
		}
	    }
	}
	break;
    case ClientMessage:
	if (event->xclient.message_type == d->winActiveAtom)
	{
	    CompWindow *w;

	    w = findWindowAtDisplay (d, event->xclient.window);
	    if (w)
	    {
		int dx;

		ROTATE_SCREEN (w->screen);

		s = w->screen;

		/* window must be placed */
		if (!w->placed)
		    break;

		if (otherScreenGrabExist (s, "rotate", "switcher", "cube", 0))
		    break;

		/* reset movement */
		rs->moving = FALSE;
		rs->moveTo = 0.0f;

		dx = defaultViewportForWindow (w) - s->x;
		if (dx)
		{
		    Window	 win;
		    int		 i, x, y;
		    unsigned int ui;

		    XQueryPointer (d->display, s->root,
				   &win, &win, &x, &y, &i, &i, &ui);

		    if (dx > (s->size + 1) / 2)
			dx -= s->size;
		    else if (dx < -(s->size + 1) / 2)
			dx += s->size;

		    rotate (s, x, y, dx);
		}
	    }
	}
	else if (event->xclient.message_type == d->desktopViewportAtom)
	{
	    s = findScreenAtDisplay (d, event->xclient.window);
	    if (s)
	    {
		int dx;

		if (otherScreenGrabExist (s, "rotate", "switcher", "cube", 0))
		    break;

		dx = event->xclient.data.l[0] / s->width - s->x;
		if (dx)
		{
		    Window	 win;
		    int		 i, x, y;
		    unsigned int ui;

		    XQueryPointer (d->display, s->root,
				   &win, &win, &x, &y, &i, &i, &ui);

		    if (dx > (s->size + 1) / 2)
			dx -= s->size;
		    else if (dx < -(s->size + 1) / 2)
			dx += s->size;

		    rotate (s, x, y, dx);
		}
	    }
	}
	else if (event->xclient.message_type == d->xdndEnterAtom)
	{
	    rd->xdndWindow = event->xclient.window;
	}
	else if (event->xclient.message_type == d->xdndLeaveAtom)
	{
	    if (!rd->xdndWindow)
	    {
		CompWindow *w;

		w = findWindowAtDisplay (d, event->xclient.window);
		if (w)
		    handleEnterScreenEdge (w->screen, None);
	    }
	}
	else if (event->xclient.message_type == d->xdndPositionAtom)
	{
	    if (rd->xdndWindow == event->xclient.window)
	    {
		CompWindow *w;

		w = findWindowAtDisplay (d, event->xclient.window);
		if (w)
		{
		    ROTATE_SCREEN (w->screen);

		    if (rs->opt[ROTATE_SCREEN_OPTION_EDGEFLIP_DND].value.b)
		    {
			if (!otherScreenGrabExist (w->screen, "rotate", 0))
			    handleEnterScreenEdge (w->screen, rd->xdndWindow);
		    }
		}

		rd->xdndWindow = None;
	    }
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
		ROTATE_SCREEN (s);

		if (otherScreenGrabExist (s, "rotate", "move", 0))
		    break;

		if (otherScreenGrabExist (s, "rotate", 0))
		{
		    if (!rs->opt[ROTATE_SCREEN_OPTION_EDGEFLIP_WINDOW].value.b)
			break;

		    /* bail out if window is horizontally maximized or
		       fullscreen */
		    if (rs->grabWindow)
		    {
			CompWindow *w = rs->grabWindow;

			if (w->state & (CompWindowStateMaximizedHorzMask |
					CompWindowStateFullscreenMask))
			    break;
		    }
		}
		else
		{
		    if (!rs->opt[ROTATE_SCREEN_OPTION_EDGEFLIP_POINTER].value.b)
			break;
		}

		handleEnterScreenEdge (s, event->xcrossing.window);
	    }
	}
    default:
	break;
    }

    UNWRAP (rd, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (rd, d, handleEvent, rotateHandleEvent);
}

static void
rotateWindowGrabNotify (CompWindow   *w,
			int	     x,
			int	     y,
			unsigned int state,
			unsigned int mask)
{
    ROTATE_SCREEN (w->screen);

    rs->grabMask   = mask;
    rs->grabWindow = w;

    UNWRAP (rs, w->screen, windowGrabNotify);
    (*w->screen->windowGrabNotify) (w, x, y, state, mask);
    WRAP (rs, w->screen, windowGrabNotify, rotateWindowGrabNotify);
}

static void
rotateWindowUngrabNotify (CompWindow *w)
{
    ROTATE_SCREEN (w->screen);

    rs->grabMask   = 0;
    rs->grabWindow = NULL;

    UNWRAP (rs, w->screen, windowUngrabNotify);
    (*w->screen->windowUngrabNotify) (w);
    WRAP (rs, w->screen, windowUngrabNotify, rotateWindowUngrabNotify);
}

static void
rotateUpdateCubeOptions (CompScreen *s)
{
    CompPlugin *p;

    ROTATE_SCREEN (s);

    p = findActivePlugin ("cube");
    if (p && p->vTable->getScreenOptions)
    {
	CompOption *options, *option;
	int	   nOptions;

	options = (*p->vTable->getScreenOptions) (s, &nOptions);
	option = compFindOption (options, nOptions, "in", 0);
	if (option)
	    rs->invert = option->value.b ? -1 : 1;
    }
}

static Bool
rotateSetScreenOptionForPlugin (CompScreen      *s,
				char	        *plugin,
				char	        *name,
				CompOptionValue *value)
{
    Bool status;

    ROTATE_SCREEN (s);

    UNWRAP (rs, s, setScreenOptionForPlugin);
    status = (*s->setScreenOptionForPlugin) (s, plugin, name, value);
    WRAP (rs, s, setScreenOptionForPlugin, rotateSetScreenOptionForPlugin);

    if (status && strcmp (plugin, "cube") == 0 && strcmp (name, "in") == 0)
	rotateUpdateCubeOptions (s);

    return status;
}

static Bool
rotateInitDisplay (CompPlugin  *p,
		   CompDisplay *d)
{
    RotateDisplay *rd;

    rd = malloc (sizeof (RotateDisplay));
    if (!rd)
	return FALSE;

    rd->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (rd->screenPrivateIndex < 0)
    {
	free (rd);
	return FALSE;
    }

    rd->xdndWindow = None;

    WRAP (rd, d, handleEvent, rotateHandleEvent);

    d->privates[displayPrivateIndex].ptr = rd;

    return TRUE;
}

static void
rotateFiniDisplay (CompPlugin  *p,
		   CompDisplay *d)
{
    ROTATE_DISPLAY (d);

    freeScreenPrivateIndex (d, rd->screenPrivateIndex);

    UNWRAP (rd, d, handleEvent);

    free (rd);
}

static Bool
rotateInitScreen (CompPlugin *p,
		  CompScreen *s)
{
    RotateScreen *rs;

    ROTATE_DISPLAY (s->display);

    rs = malloc (sizeof (RotateScreen));
    if (!rs)
	return FALSE;

    rs->grabIndex = 0;

    rs->xrot = 0.0f;
    rs->xVelocity = 0.0f;
    rs->yrot = 0.0f;
    rs->yVelocity = 0.0f;

    rs->baseXrot = 0.0f;

    rs->moving = FALSE;
    rs->moveTo = 0.0f;

    rs->moveWindow = 0;

    rs->savedPointer.x = 0;
    rs->savedPointer.y = 0;

    rs->grabbed = FALSE;
    rs->snapTop = FALSE;

    rs->slow       = FALSE;
    rs->grabMask   = FALSE;
    rs->grabWindow = NULL;

    rs->acceleration = ROTATE_ACCELERATION_DEFAULT;

    rs->pointerInvertY     = ROTATE_POINTER_INVERT_Y_DEFAULT;
    rs->pointerSensitivity = ROTATE_POINTER_SENSITIVITY_DEFAULT *
	ROTATE_POINTER_SENSITIVITY_FACTOR;

    rs->speed    = ROTATE_SPEED_DEFAULT;
    rs->timestep = ROTATE_TIMESTEP_DEFAULT;
    rs->flipTime = ROTATE_FLIPTIME_DEFAULT;

    rs->rotateHandle = 0;

    rotateScreenInitOptions (rs, s->display->display);

    addScreenBinding (s, &rs->opt[ROTATE_SCREEN_OPTION_INITIATE].value.bind);
    addScreenBinding (s, &rs->opt[ROTATE_SCREEN_OPTION_LEFT].value.bind);
    addScreenBinding (s, &rs->opt[ROTATE_SCREEN_OPTION_RIGHT].value.bind);
    addScreenBinding (s,
		      &rs->opt[ROTATE_SCREEN_OPTION_LEFT_WINDOW].value.bind);
    addScreenBinding (s,
		      &rs->opt[ROTATE_SCREEN_OPTION_RIGHT_WINDOW].value.bind);

    WRAP (rs, s, preparePaintScreen, rotatePreparePaintScreen);
    WRAP (rs, s, donePaintScreen, rotateDonePaintScreen);
    WRAP (rs, s, paintScreen, rotatePaintScreen);
    WRAP (rs, s, setScreenOptionForPlugin, rotateSetScreenOptionForPlugin);
    WRAP (rs, s, windowGrabNotify, rotateWindowGrabNotify);
    WRAP (rs, s, windowUngrabNotify, rotateWindowUngrabNotify);

    s->privates[rd->screenPrivateIndex].ptr = rs;

    rotateUpdateCubeOptions (s);

    if (rs->opt[ROTATE_SCREEN_OPTION_EDGEFLIP_POINTER].value.b ||
	rs->opt[ROTATE_SCREEN_OPTION_EDGEFLIP_WINDOW].value.b  ||
	rs->opt[ROTATE_SCREEN_OPTION_EDGEFLIP_DND].value.b)
    {
	enableScreenEdge (s, SCREEN_EDGE_LEFT);
	enableScreenEdge (s, SCREEN_EDGE_RIGHT);

	rs->edges = TRUE;
    }
    else
    {
	rs->edges = FALSE;
    }

    return TRUE;
}

static void
rotateFiniScreen (CompPlugin *p,
		  CompScreen *s)
{
    ROTATE_SCREEN (s);

    if (rs->edges)
    {
	disableScreenEdge (s, SCREEN_EDGE_LEFT);
	disableScreenEdge (s, SCREEN_EDGE_RIGHT);
    }

    UNWRAP (rs, s, preparePaintScreen);
    UNWRAP (rs, s, donePaintScreen);
    UNWRAP (rs, s, paintScreen);
    UNWRAP (rs, s, setScreenOptionForPlugin);
    UNWRAP (rs, s, windowGrabNotify);
    UNWRAP (rs, s, windowUngrabNotify);

    free (rs);
}

static Bool
rotateInit (CompPlugin *p)
{
    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
rotateFini (CompPlugin *p)
{
    if (displayPrivateIndex >= 0)
	freeDisplayPrivateIndex (displayPrivateIndex);
}

CompPluginDep rotateDeps[] = {
    { CompPluginRuleAfter, "cube" }
};

CompPluginVTable rotateVTable = {
    "rotate",
    N_("Rotate Cube"),
    N_("Rotate desktop cube"),
    rotateInit,
    rotateFini,
    rotateInitDisplay,
    rotateFiniDisplay,
    rotateInitScreen,
    rotateFiniScreen,
    0, /* InitWindow */
    0, /* FiniWindow */
    0, /* GetDisplayOptions */
    0, /* SetDisplayOption */
    rotateGetScreenOptions,
    rotateSetScreenOption,
    rotateDeps,
    sizeof (rotateDeps) / sizeof (rotateDeps[0])
};

CompPluginVTable *
getCompPluginInfo (void)
{
    return &rotateVTable;
}
