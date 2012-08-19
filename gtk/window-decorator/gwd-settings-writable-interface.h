/*
 * Copyright © 2010 Canonical Ltd
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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _COMPIZ_GWD_SETTINGS_WRITABLE_INTERFACE_H
#define _COMPIZ_GWD_SETTINGS_WRITABLE_INTERFACE_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GWD_SETTINGS_WRITABLE_INTERFACE(obj) (G_TYPE_CHECK_INSTANCE_CAST (obj))
#define GWD_SETTINGS_WRITABLE_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE(obj, \
										GWD_TYPE_WRITABLE_SETTINGS_INTERFACE, \
										GWDSettingsWritableInterface))
#define GWD_TYPE_WRITABLE_SETTINGS_INTERFACE (gwd_settings_writable_interface_get_type ())

typedef struct _GWDSettingsWritable GWDSettingsWritable;
typedef struct _GWDSettingsWritableInterface GWDSettingsWritableInterface;

struct _GWDSettingsWritableInterface
{
    GTypeInterface parent;

    gboolean (*shadow_property_changed) (GWDSettingsWritable *settings,
					 gdouble     active_shadow_radius,
					 gdouble     active_shadow_opacity,
					 gdouble     active_shadow_offset_x,
					 gdouble     active_shadow_offset_y,
					 gchar	     *active_shadow_color,
					 gdouble     inactive_shadow_radius,
					 gdouble     inactive_shadow_opacity,
					 gdouble     inactive_shadow_offset_x,
					 gdouble     inactive_shadow_offset_y,
					 gchar	     *inactive_shadow_color);
    gboolean (*use_tooltips_changed) (GWDSettingsWritable *settings, gboolean new_value);
    gboolean (*draggable_border_width_changed) (GWDSettingsWritable *settings, gint new_value);
    gboolean (*attach_modal_dialogs_changed) (GWDSettingsWritable *settings, gboolean new_value);
    gboolean (*blur_changed) (GWDSettingsWritable *settings, const gchar *type);
    gboolean (*metacity_theme_changed) (GWDSettingsWritable *settings, gboolean use_metacity_theme, const gchar *metacity_theme);
    gboolean (*opacity_changed) (GWDSettingsWritable *settings,
				 gdouble inactive_opacity,
				 gdouble active_opacity,
				 gboolean inactive_shade_opacity,
				 gboolean active_shade_opacity);
    gboolean (*button_layout_changed) (GWDSettingsWritable *settings, const gchar *button_layout);
};

gboolean
gwd_settings_shadow_property_changed (GWDSettingsWritable *settings,
				      gdouble     active_shadow_radius,
				      gdouble     active_shadow_opacity,
				      gdouble     active_shadow_offset_x,
				      gdouble     active_shadow_offset_y,
				      gchar	     *active_shadow_color,
				      gdouble     inactive_shadow_radius,
				      gdouble     inactive_shadow_opacity,
				      gdouble     inactive_shadow_offset_x,
				      gdouble     inactive_shadow_offset_y,
				      gchar	     *inactive_shadow_color);

gboolean
gwd_settings_use_tooltips_changed (GWDSettingsWritable *settings,
				   gboolean    use_tooltips);

gboolean
gwd_settings_draggable_border_width_changed (GWDSettingsWritable *settings,
					     gint	 draggable_border_width);

gboolean
gwd_settings_attach_modal_dialogs_changed (GWDSettingsWritable *settings,
					   gboolean    attach_modal_dialogs);

gboolean
gwd_settings_blur_changed (GWDSettingsWritable *settings,
			   const gchar *blur_type);

gboolean
gwd_settings_metacity_theme_changed (GWDSettingsWritable *settings,
				     gboolean	 use_metacity_theme,
				     const gchar *metacity_theme);

gboolean
gwd_settings_opacity_changed (GWDSettingsWritable *settings,
			      gdouble inactive_opacity,
			      gdouble active_opacity,
			      gboolean inactive_shade_opacity,
			      gboolean active_shade_opacity);

gboolean
gwd_settings_button_layout_changed (GWDSettingsWritable *settings,
				    const gchar *button_layout);

GType gwd_settings_writable_interface_get_type (void);

G_END_DECLS

#endif
