/*
 * Copyright © 2012 Canonical Ltd
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

#ifndef _COMPIZ_GWD_SETTINGS_STORAGE_GSETTINGS_H
#define _COMPIZ_GWD_SETTINGS_STORAGE_GSETTINGS_H

#include <glib-object.h>

typedef struct _GWDSettingsWritable GWDSettingsWritable;
typedef struct _GWDSettingsStorage  GWDSettingsStorage;
typedef struct _GSettings	    GSettings;

G_BEGIN_DECLS

#define GWD_TYPE_SETTINGS_STORAGE_GSETTINGS (gwd_settings_storage_gsettings_get_type ())
GType gwd_settings_storage_gsettings_get_type ();

GWDSettingsStorage *
gwd_settings_storage_gsettings_new (GSettings *orgGNOMEDesktopSettings,
				    GSettings *mutterSettings,
				    GSettings *gwdSettings,
				    GWDSettingsWritable *writableSettings);

extern const guint GWD_SETTINGS_STORAGE_GSETTINGS_N_CONSTRUCTION_PARAMS;

G_END_DECLS

#endif
