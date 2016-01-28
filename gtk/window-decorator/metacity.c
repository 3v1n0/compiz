/*
 * Copyright © 2006 Novell, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: David Reveman <davidr@novell.com>
 *
 * 2D Mode: Copyright © 2010 Sam Spilsbury <smspillaz@gmail.com>
 * Frames Management: Copright © 2011 Canonical Ltd.
 *        Authored By: Sam Spilsbury <sam.spilsbury@canonical.com>
 */

#include "gtk-window-decorator.h"

#ifdef USE_METACITY

MetaButtonLayout meta_button_layout;

static gboolean
meta_button_present (MetaButtonLayout   *button_layout,
                     MetaButtonFunction  function)
{
    int i;

    for (i = 0; i < META_BUTTON_FUNCTION_LAST; ++i)
        if (button_layout->left_buttons[i] == function)
            return TRUE;

    for (i = 0; i < META_BUTTON_FUNCTION_LAST; ++i)
        if (button_layout->right_buttons[i] == function)
            return TRUE;

    return FALSE;
}

static void
decor_update_meta_window_property (decor_t        *d,
                                   MetaTheme      *theme,
                                   MetaFrameFlags  flags,
                                   MetaFrameType   type,
                                   Region          top,
                                   Region          bottom,
                                   Region          left,
                                   Region          right)
{
    long *data;
    GdkDisplay *display;
    Display *xdisplay;
    gint nQuad;
    decor_extents_t win_extents;
    decor_extents_t frame_win_extents;
    decor_extents_t max_win_extents;
    decor_extents_t frame_max_win_extents;
    decor_quad_t quads[N_QUADS_MAX];
    unsigned int nOffset;
    unsigned int frame_type;
    unsigned int frame_state;
    unsigned int frame_actions;
    gint w;
    gint lh;
    gint rh;
    gint top_stretch_offset;
    gint bottom_stretch_offset;
    gint left_stretch_offset;
    gint right_stretch_offset;

    display = gdk_display_get_default ();
    xdisplay = gdk_x11_display_get_xdisplay (display);

    nOffset = 1;

    frame_type = populate_frame_type (d);
    frame_state = populate_frame_state (d);
    frame_actions = populate_frame_actions (d);

    win_extents = frame_win_extents = d->frame->win_extents;
    max_win_extents = frame_max_win_extents = d->frame->max_win_extents;

    /* Add the invisible grab area padding, but only for
     * pixmap type decorations */
    if (!d->frame_window)
    {
        GdkScreen *screen;
        MetaStyleInfo *style_info;
        MetaFrameBorders borders;

        screen = gtk_widget_get_screen (d->frame->style_window_rgba);
        style_info = meta_theme_create_style_info (theme, screen, NULL);

        meta_theme_get_frame_borders (theme, style_info, type,
                                      d->frame->text_height,
                                      flags, &borders);

        if (flags & META_FRAME_ALLOWS_HORIZONTAL_RESIZE)
        {
            frame_win_extents.left += borders.invisible.left;
            frame_win_extents.right += borders.invisible.right;
            frame_max_win_extents.left += borders.invisible.left;
            frame_max_win_extents.right += borders.invisible.right;
        }

        if (flags & META_FRAME_ALLOWS_VERTICAL_RESIZE)
        {
            frame_win_extents.bottom += borders.invisible.bottom;
            frame_win_extents.top += borders.invisible.top;
            frame_max_win_extents.bottom += borders.invisible.bottom;
            frame_max_win_extents.top += borders.invisible.top;
        }

        meta_style_info_unref (style_info);
    }

    w = d->border_layout.top.x2 - d->border_layout.top.x1 -
        d->context->left_space - d->context->right_space;

    if (d->border_layout.rotation)
        lh = d->border_layout.left.x2 - d->border_layout.left.x1;
    else
        lh = d->border_layout.left.y2 - d->border_layout.left.y1;

    if (d->border_layout.rotation)
        rh = d->border_layout.right.x2 - d->border_layout.right.x1;
    else
        rh = d->border_layout.right.y2 - d->border_layout.right.y1;

    left_stretch_offset = lh / 2;
    right_stretch_offset = rh / 2;
    top_stretch_offset = w - d->button_width - 1;
    bottom_stretch_offset = (d->border_layout.bottom.x2 - d->border_layout.bottom.x1 -
                             d->context->left_space - d->context->right_space) / 2;

    nQuad = decor_set_lXrXtXbX_window_quads (quads, d->context, &d->border_layout,
                                             left_stretch_offset, right_stretch_offset,
                                             top_stretch_offset, bottom_stretch_offset);

    win_extents.top += d->frame->titlebar_height;
    frame_win_extents.top += d->frame->titlebar_height;
    max_win_extents.top += d->frame->max_titlebar_height;
    frame_max_win_extents.top += d->frame->max_titlebar_height;

    if (d->frame_window)
    {
        data = decor_alloc_property (nOffset, WINDOW_DECORATION_TYPE_WINDOW);
        decor_gen_window_property (data, nOffset - 1, &win_extents, &max_win_extents,
                                   20, 20, frame_type, frame_state, frame_actions);
    }
    else
    {
        data = decor_alloc_property (nOffset, WINDOW_DECORATION_TYPE_PIXMAP);
        decor_quads_to_property (data, nOffset - 1, cairo_xlib_surface_get_drawable (d->surface),
                                 &frame_win_extents, &win_extents,
                                 &frame_max_win_extents, &max_win_extents,
                                 ICON_SPACE + d->button_width,
                                 0, quads, nQuad, frame_type, frame_state, frame_actions);
    }

    gdk_error_trap_push ();

    XChangeProperty (xdisplay, d->prop_xid, win_decor_atom, XA_INTEGER,
                     32, PropModeReplace, (guchar *) data,
                     PROP_HEADER_SIZE + BASE_PROP_SIZE + QUAD_PROP_SIZE * N_QUADS_MAX);
    gdk_display_sync (display);

    gdk_error_trap_pop_ignored ();

    free (data);

    decor_update_blur_property (d, w, lh,
                                top, top_stretch_offset,
                                bottom, bottom_stretch_offset,
                                left, left_stretch_offset,
                                right, right_stretch_offset);
}

static void
meta_get_corner_radius (const MetaFrameGeometry *fgeom,
                        int                     *top_left_radius,
                        int                     *top_right_radius,
                        int                     *bottom_left_radius,
                        int                     *bottom_right_radius)
{
    *top_left_radius     = fgeom->top_left_corner_rounded_radius;
    *top_right_radius    = fgeom->top_right_corner_rounded_radius;
    *bottom_left_radius  = fgeom->bottom_left_corner_rounded_radius;
    *bottom_right_radius = fgeom->bottom_right_corner_rounded_radius;
}

static int
radius_to_width (int radius,
                 int i)
{
    float r1 = sqrt (radius) + radius;
    float r2 = r1 * r1 - (r1 - (i + 0.5)) * (r1 - (i + 0.5));

    return floor (0.5f + r1 - sqrt (r2));
}

static Region
meta_get_top_border_region (const MetaFrameGeometry *fgeom,
                            int                      width)
{
    Region corners_xregion;
    Region border_xregion;
    XRectangle xrect;
    int top_left_radius;
    int top_right_radius;
    int bottom_left_radius;
    int bottom_right_radius;
    int w;
    int i;
    int height;

    corners_xregion = XCreateRegion ();

    meta_get_corner_radius (fgeom, &top_left_radius, &top_right_radius,
                            &bottom_left_radius, &bottom_right_radius);

    width = width - fgeom->borders.invisible.left - fgeom->borders.invisible.right;
    height = fgeom->borders.visible.top;

    if (top_left_radius)
    {
        for (i = 0; i < top_left_radius; ++i)
        {
            w = radius_to_width (top_left_radius, i);

            xrect.x = 0;
            xrect.y = i;
            xrect.width = w;
            xrect.height = 1;

            XUnionRectWithRegion (&xrect, corners_xregion, corners_xregion);
        }
    }

    if (top_right_radius)
    {
        for (i = 0; i < top_right_radius; ++i)
        {
            w = radius_to_width (top_right_radius, i);

            xrect.x = width - w;
            xrect.y = i;
            xrect.width = w;
            xrect.height = 1;

            XUnionRectWithRegion (&xrect, corners_xregion, corners_xregion);
        }
    }

    border_xregion = XCreateRegion ();

    xrect.x = 0;
    xrect.y = 0;
    xrect.width = width;
    xrect.height = height;

    XUnionRectWithRegion (&xrect, border_xregion, border_xregion);

    XSubtractRegion (border_xregion, corners_xregion, border_xregion);

    XDestroyRegion (corners_xregion);

    return border_xregion;
}

static Region
meta_get_bottom_border_region (const MetaFrameGeometry *fgeom,
                               int                      width)
{
    Region corners_xregion;
    Region border_xregion;
    XRectangle xrect;
    int top_left_radius;
    int top_right_radius;
    int bottom_left_radius;
    int bottom_right_radius;
    int w;
    int i;
    int height;

    corners_xregion = XCreateRegion ();

    meta_get_corner_radius (fgeom, &top_left_radius, &top_right_radius,
                            &bottom_left_radius, &bottom_right_radius);

    width = width - fgeom->borders.invisible.left - fgeom->borders.invisible.right;
    height = fgeom->borders.visible.bottom;

    if (bottom_left_radius)
    {
        for (i = 0; i < bottom_left_radius; ++i)
        {
            w = radius_to_width (bottom_left_radius, i);

            xrect.x = 0;
            xrect.y = height - i - 1;
            xrect.width = w;
            xrect.height = 1;

            XUnionRectWithRegion (&xrect, corners_xregion, corners_xregion);
        }
    }

    if (bottom_right_radius)
    {
        for (i = 0; i < bottom_right_radius; ++i)
        {
            w = radius_to_width (bottom_right_radius, i);

            xrect.x = width - w;
            xrect.y = height - i - 1;
            xrect.width = w;
            xrect.height = 1;

            XUnionRectWithRegion (&xrect, corners_xregion, corners_xregion);
        }
    }

    border_xregion = XCreateRegion ();

    xrect.x = 0;
    xrect.y = 0;
    xrect.width = width;
    xrect.height = height;

    XUnionRectWithRegion (&xrect, border_xregion, border_xregion);

    XSubtractRegion (border_xregion, corners_xregion, border_xregion);

    XDestroyRegion (corners_xregion);

    return border_xregion;
}

static Region
meta_get_left_border_region (const MetaFrameGeometry *fgeom,
                             int                      height)
{
    Region border_xregion;
    XRectangle xrect;

    border_xregion = XCreateRegion ();

    xrect.x = 0;
    xrect.y = 0;
    xrect.width = fgeom->borders.visible.left;
    xrect.height = height - fgeom->borders.total.top - fgeom->borders.total.bottom;

    XUnionRectWithRegion (&xrect, border_xregion, border_xregion);

    return border_xregion;
}

static Region
meta_get_right_border_region (const MetaFrameGeometry *fgeom,
                              int                      height)
{
    Region border_xregion;
    XRectangle xrect;

    border_xregion = XCreateRegion ();

    xrect.x = 0;
    xrect.y = 0;
    xrect.width = fgeom->borders.visible.right;
    xrect.height = height - fgeom->borders.total.top - fgeom->borders.total.bottom;

    XUnionRectWithRegion (&xrect, border_xregion, border_xregion);

    return border_xregion;
}

static MetaButtonState
meta_button_state (int state)
{
    if (state & IN_EVENT_WINDOW)
    {
        if (state & PRESSED_EVENT_WINDOW)
            return META_BUTTON_STATE_PRESSED;

        return META_BUTTON_STATE_PRELIGHT;
    }

    return META_BUTTON_STATE_NORMAL;
}

static MetaButtonType
meta_function_to_type (MetaButtonFunction function)
{
    switch (function)
    {
    case META_BUTTON_FUNCTION_MENU:
        return META_BUTTON_TYPE_MENU;
    case META_BUTTON_FUNCTION_MINIMIZE:
        return META_BUTTON_TYPE_MINIMIZE;
    case META_BUTTON_FUNCTION_MAXIMIZE:
        return META_BUTTON_TYPE_MAXIMIZE;
    case META_BUTTON_FUNCTION_CLOSE:
        return META_BUTTON_TYPE_CLOSE;
    case META_BUTTON_FUNCTION_SHADE:
        return META_BUTTON_TYPE_SHADE;
    case META_BUTTON_FUNCTION_ABOVE:
        return META_BUTTON_TYPE_ABOVE;
    case META_BUTTON_FUNCTION_STICK:
        return META_BUTTON_TYPE_STICK;
    case META_BUTTON_FUNCTION_UNSHADE:
        return META_BUTTON_TYPE_UNSHADE;
    case META_BUTTON_FUNCTION_UNABOVE:
        return META_BUTTON_TYPE_UNABOVE;
    case META_BUTTON_FUNCTION_UNSTICK:
        return META_BUTTON_TYPE_UNSTICK;
    default:
        break;
    }

    return META_BUTTON_TYPE_LAST;
}

static MetaButtonState
meta_button_state_for_button_type (decor_t        *d,
                                   MetaButtonType  type)
{
    switch (type)
    {
    case META_BUTTON_TYPE_LEFT_LEFT_BACKGROUND:
        type = meta_function_to_type (meta_button_layout.left_buttons[0]);
        break;
    case META_BUTTON_TYPE_LEFT_MIDDLE_BACKGROUND:
        type = meta_function_to_type (meta_button_layout.left_buttons[1]);
        break;
    case META_BUTTON_TYPE_LEFT_RIGHT_BACKGROUND:
        type = meta_function_to_type (meta_button_layout.left_buttons[2]);
        break;
    case META_BUTTON_TYPE_RIGHT_LEFT_BACKGROUND:
        type = meta_function_to_type (meta_button_layout.right_buttons[0]);
        break;
    case META_BUTTON_TYPE_RIGHT_MIDDLE_BACKGROUND:
        type = meta_function_to_type (meta_button_layout.right_buttons[1]);
        break;
    case META_BUTTON_TYPE_RIGHT_RIGHT_BACKGROUND:
        type = meta_function_to_type (meta_button_layout.right_buttons[2]);
    default:
        break;
    }

    switch (type)
    {
    case META_BUTTON_TYPE_CLOSE:
        return meta_button_state (d->button_states[BUTTON_CLOSE]);
    case META_BUTTON_TYPE_MAXIMIZE:
        return meta_button_state (d->button_states[BUTTON_MAX]);
    case META_BUTTON_TYPE_MINIMIZE:
        return meta_button_state (d->button_states[BUTTON_MIN]);
    case META_BUTTON_TYPE_MENU:
        return meta_button_state (d->button_states[BUTTON_MENU]);
    case META_BUTTON_TYPE_SHADE:
        return meta_button_state (d->button_states[BUTTON_SHADE]);
    case META_BUTTON_TYPE_ABOVE:
        return meta_button_state (d->button_states[BUTTON_ABOVE]);
    case META_BUTTON_TYPE_STICK:
        return meta_button_state (d->button_states[BUTTON_STICK]);
    case META_BUTTON_TYPE_UNSHADE:
        return meta_button_state (d->button_states[BUTTON_UNSHADE]);
    case META_BUTTON_TYPE_UNABOVE:
        return meta_button_state (d->button_states[BUTTON_UNABOVE]);
    case META_BUTTON_TYPE_UNSTICK:
        return meta_button_state (d->button_states[BUTTON_UNSTICK]);
    default:
        break;
    }

    return META_BUTTON_STATE_NORMAL;
}

static void
meta_get_decoration_geometry (decor_t           *d,
                              MetaTheme         *theme,
                              MetaFrameFlags    *flags,
                              MetaFrameGeometry *fgeom,
                              MetaButtonLayout  *button_layout,
                              MetaFrameType      frame_type)
{
    GdkScreen *screen;
    MetaStyleInfo *style_info;
    gint client_width;
    gint client_height;

    if (!(frame_type < META_FRAME_TYPE_LAST))
        frame_type = META_FRAME_TYPE_NORMAL;

    if (meta_button_layout_set)
    {
        *button_layout = meta_button_layout;
    }
    else
    {
        gint i;

        button_layout->left_buttons[0] = META_BUTTON_FUNCTION_MENU;

        for (i = 1; i < META_BUTTON_FUNCTION_LAST; ++i)
            button_layout->left_buttons[i] = META_BUTTON_FUNCTION_LAST;

        button_layout->right_buttons[0] = META_BUTTON_FUNCTION_MINIMIZE;
        button_layout->right_buttons[1] = META_BUTTON_FUNCTION_MAXIMIZE;
        button_layout->right_buttons[2] = META_BUTTON_FUNCTION_CLOSE;

        for (i = 3; i < META_BUTTON_FUNCTION_LAST; ++i)
            button_layout->right_buttons[i] = META_BUTTON_FUNCTION_LAST;
    }

    *flags = 0;

    if (d->actions & WNCK_WINDOW_ACTION_CLOSE)
        *flags |= (MetaFrameFlags ) META_FRAME_ALLOWS_DELETE;

    if (d->actions & WNCK_WINDOW_ACTION_MINIMIZE)
        *flags |= (MetaFrameFlags ) META_FRAME_ALLOWS_MINIMIZE;

    if (d->actions & WNCK_WINDOW_ACTION_MAXIMIZE)
        *flags |= (MetaFrameFlags ) META_FRAME_ALLOWS_MAXIMIZE;

    *flags |= (MetaFrameFlags ) META_FRAME_ALLOWS_MENU;

    if (d->actions & WNCK_WINDOW_ACTION_RESIZE)
    {
        if (!(d->state & WNCK_WINDOW_STATE_MAXIMIZED_VERTICALLY))
            *flags |= (MetaFrameFlags ) META_FRAME_ALLOWS_VERTICAL_RESIZE;
        if (!(d->state & WNCK_WINDOW_STATE_MAXIMIZED_HORIZONTALLY))
            *flags |= (MetaFrameFlags ) META_FRAME_ALLOWS_HORIZONTAL_RESIZE;
    }

    if (d->actions & WNCK_WINDOW_ACTION_MOVE)
        *flags |= (MetaFrameFlags ) META_FRAME_ALLOWS_MOVE;

    if (d->actions & WNCK_WINDOW_ACTION_MAXIMIZE)
        *flags |= (MetaFrameFlags ) META_FRAME_ALLOWS_MAXIMIZE;

    if (d->actions & WNCK_WINDOW_ACTION_SHADE)
        *flags |= (MetaFrameFlags ) META_FRAME_ALLOWS_SHADE;

    if (d->active)
        *flags |= (MetaFrameFlags ) META_FRAME_HAS_FOCUS;

    if ((d->state & META_MAXIMIZED) == META_MAXIMIZED)
        *flags |= (MetaFrameFlags ) META_FRAME_MAXIMIZED;

    if (d->state & WNCK_WINDOW_STATE_STICKY)
        *flags |= (MetaFrameFlags ) META_FRAME_STUCK;

    if (d->state & WNCK_WINDOW_STATE_FULLSCREEN)
        *flags |= (MetaFrameFlags ) META_FRAME_FULLSCREEN;

    if (d->state & WNCK_WINDOW_STATE_SHADED)
        *flags |= (MetaFrameFlags ) META_FRAME_SHADED;

    if (d->state & WNCK_WINDOW_STATE_ABOVE)
        *flags |= (MetaFrameFlags ) META_FRAME_ABOVE;

    client_width = d->border_layout.top.x2 - d->border_layout.top.x1;
    client_width -= d->context->right_space + d->context->left_space;

    if (d->border_layout.rotation)
        client_height = d->border_layout.left.x2 - d->border_layout.left.x1;
    else
        client_height = d->border_layout.left.y2 - d->border_layout.left.y1;

    screen = gtk_widget_get_screen (d->frame->style_window_rgba);
    style_info = meta_theme_create_style_info (theme, screen, NULL);

    meta_theme_calc_geometry (theme, style_info, frame_type, d->frame->text_height,
                              *flags, client_width, client_height,
                              button_layout, fgeom);

    meta_style_info_unref (style_info);
}

void
meta_draw_window_decoration (decor_t *d)
{
    GdkDisplay *display;
    GdkScreen *screen;
    Display *xdisplay;
    cairo_surface_t *surface;
    Picture src;
    MetaButtonState button_states [META_BUTTON_TYPE_LAST];
    MetaButtonLayout button_layout;
    MetaFrameGeometry fgeom;
    MetaFrameFlags flags;
    MetaFrameType frame_type;
    MetaTheme *theme;
    MetaStyleInfo *style_info;
    cairo_t *cr;
    gint i;
    Region top_region;
    Region bottom_region;
    Region left_region;
    Region right_region;
    gdouble meta_active_opacity;
    gdouble meta_inactive_opacity;
    gboolean meta_active_shade_opacity;
    gboolean meta_inactive_shade_opacity;
    double alpha;
    gboolean shade_alpha;

    if (!d->surface || !d->picture)
        return;

    display = gdk_display_get_default ();
    xdisplay = gdk_x11_display_get_xdisplay (display);

    top_region = NULL;
    bottom_region = NULL;
    left_region = NULL;
    right_region = NULL;

    g_object_get (settings, "metacity-active-opacity", &meta_active_opacity, NULL);
    g_object_get (settings, "metacity-inactive-opacity", &meta_inactive_opacity, NULL);
    g_object_get (settings, "metacity-active-shade-opacity", &meta_active_shade_opacity, NULL);
    g_object_get (settings, "metacity-inactive-shade-opacity", &meta_inactive_shade_opacity, NULL);

    alpha = (d->active) ? meta_active_opacity : meta_inactive_opacity;
    shade_alpha = (d->active) ? meta_active_shade_opacity : meta_inactive_shade_opacity;

    if (decoration_alpha == 1.0)
        alpha = 1.0;

    cr = cairo_create (d->buffer_surface ? d->buffer_surface : d->surface);

    cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);

    theme = meta_theme_get_current ();

    frame_type = meta_frame_type_from_string (d->frame->type);

    if (frame_type == META_FRAME_TYPE_LAST)
      frame_type = META_FRAME_TYPE_NORMAL;

    meta_get_decoration_geometry (d, theme, &flags, &fgeom, &button_layout,
                                  frame_type);

    if ((d->prop_xid || !d->buffer_surface) && !d->frame_window)
        draw_shadow_background (d, cr, d->shadow, d->context);

    for (i = 0; i < META_BUTTON_TYPE_LAST; ++i)
        button_states[i] = meta_button_state_for_button_type (d, i);

    /* Draw something that will be almost invisible to user. This is hacky way
     * to fix invisible decorations. */
    cairo_set_source_rgba (cr, 0, 0, 0, 0.01);
    cairo_rectangle (cr, 0, 0, 1, 1);
    cairo_fill (cr);
    /* ------------ */

    cairo_destroy (cr);

    if (d->frame_window)
        surface = create_surface (fgeom.width, fgeom.height, d->frame->style_window_rgb);
    else
        surface = create_surface (fgeom.width, fgeom.height, d->frame->style_window_rgba);

    cr = cairo_create (surface);
    cairo_set_operator (cr, CAIRO_OPERATOR_OVER);

    src = XRenderCreatePicture (xdisplay, cairo_xlib_surface_get_drawable (surface),
                                get_format_for_surface (d, surface), 0, NULL);

    screen = gtk_widget_get_screen (d->frame->style_window_rgba);
    style_info = meta_theme_create_style_info (theme, screen, NULL);

    cairo_paint (cr);
    meta_theme_draw_frame (theme, style_info, cr, frame_type, flags,
                           fgeom.width - fgeom.borders.total.left - fgeom.borders.total.right,
                           fgeom.height - fgeom.borders.total.top - fgeom.borders.total.bottom,
                           d->layout, d->frame->text_height, &button_layout,
                           button_states, d->icon_pixbuf, NULL);

    meta_style_info_unref (style_info);

    if (fgeom.borders.visible.top)
    {
        top_region = meta_get_top_border_region (&fgeom, fgeom.width);

        decor_blend_border_picture (xdisplay, d->context, src,
                                    fgeom.borders.invisible.left,
                                    fgeom.borders.invisible.top,
                                    d->picture, &d->border_layout,
                                    BORDER_TOP, top_region,
                                    alpha * 0xffff, shade_alpha, 0);
    }

    if (fgeom.borders.visible.bottom)
    {
        bottom_region = meta_get_bottom_border_region (&fgeom, fgeom.width);

        decor_blend_border_picture (xdisplay, d->context, src,
                                    fgeom.borders.invisible.left,
                                    fgeom.height - fgeom.borders.total.bottom,
                                    d->picture, &d->border_layout,
                                    BORDER_BOTTOM, bottom_region,
                                    alpha * 0xffff, shade_alpha, 0);
    }

    if (fgeom.borders.visible.left)
    {
        left_region = meta_get_left_border_region (&fgeom, fgeom.height);

        decor_blend_border_picture (xdisplay, d->context, src,
                                    fgeom.borders.invisible.left,
                                    fgeom.borders.total.top,
                                    d->picture, &d->border_layout,
                                    BORDER_LEFT, left_region,
                                    alpha * 0xffff, shade_alpha, 0);
    }

    if (fgeom.borders.visible.right)
    {
        right_region = meta_get_right_border_region (&fgeom, fgeom.height);

        decor_blend_border_picture (xdisplay, d->context, src,
                                    fgeom.width - fgeom.borders.total.right,
                                    fgeom.borders.total.top,
                                    d->picture, &d->border_layout,
                                    BORDER_RIGHT, right_region,
                                    alpha * 0xffff, shade_alpha, 0);
    }

    cairo_destroy (cr);
    cairo_surface_destroy (surface);
    XRenderFreePicture (xdisplay, src);

    copy_to_front_buffer (d);

    if (d->frame_window)
    {
        GdkWindow *gdk_frame_window;
        GdkPixbuf *pixbuf;

        gdk_frame_window = gtk_widget_get_window (d->decor_window);

        pixbuf = gdk_pixbuf_get_from_surface (d->surface, 0, 0, d->width, d->height);
        gtk_image_set_from_pixbuf (GTK_IMAGE (d->decor_image), pixbuf);
        g_object_unref (pixbuf);

        gdk_window_move_resize (gdk_frame_window,
                                d->context->left_corner_space - 1,
                                d->context->top_corner_space - 1,
                                d->width,
                                d->height);

        gdk_window_lower (gdk_frame_window);
    }

    if (d->prop_xid)
    {
        /* translate from frame to client window space */
        if (top_region)
            XOffsetRegion (top_region, -fgeom.borders.total.left, -fgeom.borders.total.top);
        if (bottom_region)
            XOffsetRegion (bottom_region, -fgeom.borders.total.left, 0);
        if (left_region)
            XOffsetRegion (left_region, -fgeom.borders.total.left, 0);

        decor_update_meta_window_property (d, theme, flags, frame_type,
                                           top_region, bottom_region,
                                           left_region, right_region);

        d->prop_xid = 0;
    }

    if (top_region)
        XDestroyRegion (top_region);
    if (bottom_region)
        XDestroyRegion (bottom_region);
    if (left_region)
        XDestroyRegion (left_region);
    if (right_region)
        XDestroyRegion (right_region);
}

static void
meta_calc_button_size (decor_t *d)
{
    MetaTheme *theme;
    MetaFrameType frame_type;
    MetaFrameFlags flags;
    MetaFrameGeometry fgeom;
    MetaButtonLayout button_layout;
    gint i, min_x, x, y, w, h, width;

    if (!d->context)
    {
        d->button_width = 0;
        return;
    }

    theme = meta_theme_get_current ();

    frame_type = meta_frame_type_from_string (d->frame->type);
    if (!(frame_type < META_FRAME_TYPE_LAST))
        frame_type = META_FRAME_TYPE_NORMAL;

    meta_get_decoration_geometry (d, theme, &flags, &fgeom, &button_layout,
                                  frame_type);

    width = d->border_layout.top.x2 - d->border_layout.top.x1 -
            d->context->left_space - d->context->right_space +
            fgeom.borders.total.left + fgeom.borders.total.right;

    min_x = width;

    for (i = 0; i < 3; ++i)
    {
        static guint button_actions[3] = {
            WNCK_WINDOW_ACTION_CLOSE,
            WNCK_WINDOW_ACTION_MAXIMIZE,
            WNCK_WINDOW_ACTION_MINIMIZE
        };

        if (d->actions & button_actions[i])
        {
            if (meta_get_button_position (d, i, width, 256, &x, &y, &w, &h))
            {
                if (x > width / 2 && x < min_x)
                    min_x = x;
            }
        }
    }

    d->button_width = width - min_x;
}

static MetaButtonFunction
button_to_meta_button_function (gint i)
{
    switch (i)
    {
    case BUTTON_MENU:
        return META_BUTTON_FUNCTION_MENU;
    case BUTTON_MIN:
        return META_BUTTON_FUNCTION_MINIMIZE;
    case BUTTON_MAX:
        return META_BUTTON_FUNCTION_MAXIMIZE;
    case BUTTON_CLOSE:
        return META_BUTTON_FUNCTION_CLOSE;
    case BUTTON_SHADE:
        return META_BUTTON_FUNCTION_SHADE;
    case BUTTON_ABOVE:
        return META_BUTTON_FUNCTION_ABOVE;
    case BUTTON_STICK:
        return META_BUTTON_FUNCTION_STICK;
    case BUTTON_UNSHADE:
        return META_BUTTON_FUNCTION_UNSHADE;
    case BUTTON_UNABOVE:
        return META_BUTTON_FUNCTION_UNABOVE;
    case BUTTON_UNSTICK:
        return META_BUTTON_FUNCTION_UNSTICK;
    default:
        break;
    }

    return META_BUTTON_FUNCTION_LAST;
}

gboolean
meta_get_button_position (decor_t *d,
                          gint     i,
                          gint     width,
                          gint     height,
                          gint    *x,
                          gint    *y,
                          gint    *w,
                          gint    *h)
{
    MetaButtonLayout button_layout;
    MetaFrameGeometry fgeom;
    MetaFrameType frame_type;
    MetaFrameFlags flags;
    MetaTheme *theme;
    MetaButtonFunction button_function;
    MetaButtonSpace *space;

    if (!d->context)
    {
        /* undecorated windows implicitly have no buttons */
        return FALSE;
    }

    theme = meta_theme_get_current ();

    frame_type = meta_frame_type_from_string (d->frame->type);
    if (!(frame_type < META_FRAME_TYPE_LAST))
        frame_type = META_FRAME_TYPE_NORMAL;

    meta_get_decoration_geometry (d, theme, &flags, &fgeom, &button_layout,
                                  frame_type);

    button_function = button_to_meta_button_function (i);
    if (!meta_button_present (&button_layout, button_function))
        return FALSE;

    switch (i)
    {
    case BUTTON_MENU:
        space = &fgeom.menu_rect;
        break;
    case BUTTON_MIN:
        space = &fgeom.min_rect;
        break;
    case BUTTON_MAX:
        space = &fgeom.max_rect;
        break;
    case BUTTON_CLOSE:
        space = &fgeom.close_rect;
        break;
    case BUTTON_SHADE:
        space = &fgeom.shade_rect;
        break;
    case BUTTON_ABOVE:
        space = &fgeom.above_rect;
        break;
    case BUTTON_STICK:
        space = &fgeom.stick_rect;
        break;
    case BUTTON_UNSHADE:
        space = &fgeom.unshade_rect;
        break;
    case BUTTON_UNABOVE:
        space = &fgeom.unabove_rect;
        break;
    case BUTTON_UNSTICK:
        space = &fgeom.unstick_rect;
        break;
    default:
        return FALSE;
    }

    if (!space->clickable.width && !space->clickable.height)
        return FALSE;

    *x = space->clickable.x;
    *y = space->clickable.y;
    *w = space->clickable.width;
    *h = space->clickable.height;

    if (d->frame_window)
    {
        *x += d->frame->win_extents.left + 4;
        *y += d->frame->win_extents.top + 2;
    }

    return TRUE;
}

gfloat
meta_get_title_scale (decor_frame_t *frame)
{
    MetaTheme *theme;
    MetaFrameType type;
    MetaFrameFlags flags; 

    theme = meta_theme_get_current ();
    type = meta_frame_type_from_string (frame->type);
    flags = 0xc33; /* fixme */

    if (type == META_FRAME_TYPE_LAST)
        return 1.0f;

    gfloat scale = meta_theme_get_title_scale (theme, type, flags);

    return scale;
}

gboolean
meta_calc_decoration_size (decor_t *d,
                           gint     w,
                           gint     h,
                           gint     name_width,
                           gint    *width,
                           gint    *height)
{
    decor_layout_t layout;
    decor_context_t *context;
    decor_shadow_t *shadow;

    if (!d->decorated)
        return FALSE;

    if ((d->state & META_MAXIMIZED) == META_MAXIMIZED)
    {
        if (!d->frame_window)
        {
            if (d->active)
            {
                context = &d->frame->max_window_context_active;
                shadow = d->frame->max_border_shadow_active;
            }
            else
            {
                context = &d->frame->max_window_context_inactive;
                shadow  = d->frame->max_border_shadow_inactive;
            }
        }
        else
        {
            context = &d->frame->max_window_context_no_shadow;
            shadow = d->frame->max_border_no_shadow;
        }
    }
    else
    {
        if (!d->frame_window)
        {
            if (d->active)
            {
                context = &d->frame->window_context_active;
                shadow = d->frame->border_shadow_active;
            }
            else
            {
                context = &d->frame->window_context_inactive;
                shadow = d->frame->border_shadow_inactive;
            }
        }
        else
        {
            context = &d->frame->window_context_no_shadow;
            shadow  = d->frame->border_no_shadow;
        }
    }

    if (!d->frame_window)
    {
        decor_get_best_layout (context, w, h, &layout);

        if (context != d->context || memcmp (&layout, &d->border_layout, sizeof (layout)))
        {
            *width = layout.width;
            *height = layout.height;

            d->border_layout = layout;
            d->context = context;
            d->shadow = shadow;

            meta_calc_button_size (d);

            return TRUE;
        }
    }
    else
    {
        if ((d->state & META_MAXIMIZED) == META_MAXIMIZED)
            decor_get_default_layout (context, d->client_width,
                                      d->client_height - d->frame->titlebar_height,
                                      &layout);
        else
            decor_get_default_layout (context, d->client_width,
                                      d->client_height, &layout);

        *width = layout.width;
        *height = layout.height;

        d->border_layout = layout;
        d->shadow = shadow;
        d->context = context;

        meta_calc_button_size (d);

        return TRUE;
    }

    return FALSE;
}

#define TOP_RESIZE_HEIGHT 2
#define RESIZE_EXTENDS 15

void
meta_get_event_window_position (decor_t *d,
                                gint     i,
                                gint     j,
                                gint     width,
                                gint     height,
                                gint    *x,
                                gint    *y,
                                gint    *w,
                                gint    *h)
{
    MetaButtonLayout button_layout;
    MetaFrameGeometry fgeom;
    MetaFrameFlags flags;
    MetaTheme *theme;

    theme = meta_theme_get_current ();

    meta_get_decoration_geometry (d, theme, &flags, &fgeom, &button_layout,
                                  meta_frame_type_from_string (d->frame->type));

    width += fgeom.borders.total.right + fgeom.borders.total.left;
    height += fgeom.borders.total.top  + fgeom.borders.total.bottom;

    switch (i)
    {
    case 2: /* bottom */
        switch (j)
        {
        case 2: /* bottom right */
            *x = width - fgeom.borders.total.right - RESIZE_EXTENDS;
            *y = height - fgeom.borders.total.bottom - RESIZE_EXTENDS;

            if (d->frame_window)
            {
              *x += d->frame->win_extents.left + 2;
              *y += d->frame->win_extents.top + 2;
            }

            *w = fgeom.borders.total.right + RESIZE_EXTENDS;
            *h = fgeom.borders.total.bottom + RESIZE_EXTENDS;
            break;
        case 1: /* bottom */
            *x = fgeom.borders.total.left + RESIZE_EXTENDS;
            *y = height - fgeom.borders.total.bottom;

            if (d->frame_window)
                *y += d->frame->win_extents.top + 2;

            *w = width - fgeom.borders.total.left - fgeom.borders.total.right - (2 * RESIZE_EXTENDS);
            *h = fgeom.borders.total.bottom;
            break;
        case 0: /* bottom left */
        default:
            *x = 0;
            *y = height - fgeom.borders.total.bottom - RESIZE_EXTENDS;

            if (d->frame_window)
            {
              *x += d->frame->win_extents.left + 4;
              *y += d->frame->win_extents.bottom + 2;
            }

            *w = fgeom.borders.total.left + RESIZE_EXTENDS;
            *h = fgeom.borders.total.bottom + RESIZE_EXTENDS;
            break;
        }
        break;
    case 1: /* middle */
        switch (j)
        {
        case 2: /* right */
            *x = width - fgeom.borders.total.right;
            *y = fgeom.borders.total.top + RESIZE_EXTENDS;

            if (d->frame_window)
                *x += d->frame->win_extents.left + 2;

            *w = fgeom.borders.total.right;
            *h = height - fgeom.borders.total.top - fgeom.borders.total.bottom - (2 * RESIZE_EXTENDS);
            break;
        case 1: /* middle */
            *x = fgeom.borders.total.left;
            *y = fgeom.title_rect.y + TOP_RESIZE_HEIGHT;
            *w = width - fgeom.borders.total.left - fgeom.borders.total.right;
            *h = height - fgeom.top_titlebar_edge - fgeom.borders.total.bottom;
            break;
        case 0: /* left */
        default:
            *x = 0;
            *y = fgeom.borders.total.top + RESIZE_EXTENDS;

            if (d->frame_window)
                *x += d->frame->win_extents.left + 4;

            *w = fgeom.borders.total.left;
            *h = height - fgeom.borders.total.top - fgeom.borders.total.bottom - (2 * RESIZE_EXTENDS);
            break;
        }
        break;
    case 0: /* top */
    default:
        switch (j)
        {
        case 2: /* top right */
            *x = width - fgeom.borders.total.right - RESIZE_EXTENDS;
            *y = 0;

            if (d->frame_window)
            {
                *x += d->frame->win_extents.left + 2;
                *y += d->frame->win_extents.top + 2 - fgeom.title_rect.height;
            }

            *w = fgeom.borders.total.right + RESIZE_EXTENDS;
            *h = fgeom.borders.total.top + RESIZE_EXTENDS;
            break;
        case 1: /* top */
            *x = fgeom.borders.total.left + RESIZE_EXTENDS;
            *y = 0;

            if (d->frame_window)
                *y += d->frame->win_extents.top + 2;

            *w = width - fgeom.borders.total.left - fgeom.borders.total.right - (2 * RESIZE_EXTENDS);
            *h = fgeom.borders.total.top - fgeom.title_rect.height;
            break;
        case 0: /* top left */
        default:
            *x = 0;
            *y = 0;

            if (d->frame_window)
            {
                *x += d->frame->win_extents.left + 4;
                *y += d->frame->win_extents.top + 2 - fgeom.title_rect.height;
            }

            *w = fgeom.borders.total.left + RESIZE_EXTENDS;
            *h = fgeom.borders.total.top + RESIZE_EXTENDS;
            break;
        }
        break;
    }

    if (!(flags & META_FRAME_ALLOWS_VERTICAL_RESIZE))
    {
        /* turn off top and bottom event windows */
        if (i == 0 || i == 2)
            *w = *h = 0;
    }

    if (!(flags & META_FRAME_ALLOWS_HORIZONTAL_RESIZE))
    {
        /* turn off left and right event windows */
        if (j == 0 || j == 2)
            *w = *h = 0;
    }
}

void
meta_update_button_layout (const char *value)
{
    gboolean invert;

    invert = gtk_widget_get_default_direction () == GTK_TEXT_DIR_RTL;
    meta_button_layout = meta_button_layout_new (value, invert);
}

void
meta_update_border_extents (decor_frame_t *frame)
{
    MetaTheme *theme;
    GdkScreen *screen;
    MetaStyleInfo *style_info;
    MetaFrameBorders borders;
    MetaFrameType frame_type;
    gint top_height;
    gint bottom_height;
    gint left_width;
    gint right_width;

    gwd_decor_frame_ref (frame);

    frame_type = meta_frame_type_from_string (frame->type);
    if (!(frame_type < META_FRAME_TYPE_LAST))
        frame_type = META_FRAME_TYPE_NORMAL;

    theme = meta_theme_get_current ();

    screen = gtk_widget_get_screen (frame->style_window_rgba);
    style_info = meta_theme_create_style_info (theme, screen, NULL);

    meta_theme_get_frame_borders (theme, style_info, frame_type, frame->text_height,
                                  0, &borders);

    top_height = borders.visible.top;
    bottom_height = borders.visible.bottom;
    left_width = borders.visible.left;
    right_width = borders.visible.right;

    frame->win_extents.top    = frame->win_extents.top;
    frame->win_extents.bottom = bottom_height;
    frame->win_extents.left   = left_width;
    frame->win_extents.right  = right_width;

    frame->titlebar_height = top_height - frame->win_extents.top;

    meta_theme_get_frame_borders (theme, style_info, frame_type, frame->text_height,
                                  META_FRAME_MAXIMIZED, &borders);

    top_height = borders.visible.top;
    bottom_height = borders.visible.bottom;
    left_width = borders.visible.left;
    right_width = borders.visible.right;

    frame->max_win_extents.top    = frame->win_extents.top;
    frame->max_win_extents.bottom = bottom_height;
    frame->max_win_extents.left   = left_width;
    frame->max_win_extents.right  = right_width;

    frame->max_titlebar_height = top_height - frame->max_win_extents.top;

    meta_style_info_unref (style_info);

    gwd_decor_frame_unref (frame);
}

#endif
