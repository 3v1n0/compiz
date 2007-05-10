/**
 *
 * GConf libccs backend
 *
 * gconf.c
 *
 * Copyright (c) 2007 Danny Baumann <maniac@beryl-project.org>
 *
 * Parts of this code are taken from libberylsettings 
 * gconf backend, written by:
 *
 * Copyright (c) 2006 Robert Carr <racarr@beryl-project.org>
 * Copyright (c) 2007 Dennis Kasprzyk <onestone@beryl-project.org>
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
 **/



#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <string.h>

#include <ccs.h>

#include <X11/X.h>
#include <X11/Xlib.h>

#include <gconf/gconf.h>
#include <gconf/gconf-client.h>
#include <gconf/gconf-value.h>

#define CompAltMask        (1 << 16)
#define CompMetaMask       (1 << 17)
#define CompSuperMask      (1 << 18)
#define CompHyperMask      (1 << 19)
#define CompModeSwitchMask (1 << 20)
#define CompNumLockMask    (1 << 21)
#define CompScrollLockMask (1 << 22)

#define METACITY    "/apps/metacity"
#define COMPIZ_CCS   "/apps/compiz/ccs"
#define DEFAULTPROF "Default"

#define BUFSIZE 512

#define KEYNAME     char keyName[BUFSIZE]; \
                    if (setting->isScreen) \
                        snprintf(keyName, BUFSIZE, "screen%d/%s", setting->screenNum, setting->name); \
                    else \
                        snprintf(keyName, BUFSIZE, "allscreens/%s", setting->name);

#define PATHNAME    char pathName[BUFSIZE]; \
					snprintf(pathName, BUFSIZE, "%s/%s/%s%s%s/%s", COMPIZ_CCS, currentProfile, \
							 setting->parent->name ? "plugins" : "general", \
							 setting->parent->name ? "/" : "", \
							 setting->parent->name ? setting->parent->name : "", \
							 keyName);

GConfClient *client = NULL;

static guint backendNotifyId = 0;
static guint gnomeNotifyId = 0;
static char *currentProfile = NULL;

/* some forward declarations */
static Bool readInit(CCSContext * context);
static void readSetting(CCSContext * context, CCSSetting * setting);
static void readDone(CCSContext * context);

typedef enum {
	OptionInt,
	OptionBool,
	OptionKey,
	OptionString,
	OptionSpecial,
} SpecialOptionType;

struct _SpecialOption {
	const char* settingName;
	const char* pluginName;
	Bool		screen;
	const char* gnomeName;
	SpecialOptionType type;
} const specialOptions[] = {
	{"run", NULL, FALSE, METACITY "/global_keybindings/panel_run_dialog", OptionKey},
	{"main_menu", NULL, FALSE, METACITY "/global_keybindings/panel_main_menu", OptionKey},
	{"window_menu", NULL, FALSE, METACITY "/window_keybindings/activate_window_menu", OptionKey},
	{"run_command_screenshot", NULL, FALSE, METACITY "/global_keybindings/run_command_screenshot", OptionKey},

	{"toggle_window_maximized", NULL, FALSE, METACITY "/window_keybindings/toggle_maximized", OptionKey},
	{"minimize_window", NULL, FALSE, METACITY "/window_keybindings/minimize", OptionKey},
	{"maximize_window", NULL, FALSE, METACITY "/window_keybindings/maximize", OptionKey},
	{"unmaximize_window", NULL, FALSE, METACITY "/window_keybindings/unmaximize", OptionKey},
	{"toggle_window_maximized_horizontally", NULL, FALSE, METACITY "/window_keybindings/maximize_horizontally", OptionKey},
	{"toggle_window_maximized_vertically", NULL, FALSE, METACITY "/window_keybindings/maximize_vertically", OptionKey},
	{"raise_window", NULL, FALSE, METACITY "/window_keybindings/raise", OptionKey},
	{"lower_window", NULL, FALSE, METACITY "/window_keybindings/lower", OptionKey},
	{"close_window", NULL, FALSE, METACITY "/window_keybindings/close", OptionKey},
	{"toggle_window_shaded", NULL, FALSE, METACITY "/window_keybindings/toggle_shaded", OptionKey},

	{"show_desktop", NULL, FALSE, METACITY "/global_keybindings/show_desktop", OptionKey},

	{"initiate", "move", FALSE, METACITY "/window_keybindings/begin_move", OptionKey},
	{"initiate", "resize", FALSE, METACITY "/window_keybindings/begin_resize", OptionKey},

	{"next", "switcher", FALSE, METACITY "/global_keybindings/switch_windows", OptionKey},
	{"prev", "switcher", FALSE, METACITY "/global_keybindings/switch_windows_backward", OptionKey},

	{"command1", NULL, FALSE, METACITY "/keybinding_commands/command_1", OptionString},
	{"command2", NULL, FALSE, METACITY "/keybinding_commands/command_2", OptionString},
	{"command3", NULL, FALSE, METACITY "/keybinding_commands/command_3", OptionString},
	{"command4", NULL, FALSE, METACITY "/keybinding_commands/command_4", OptionString},
	{"command5", NULL, FALSE, METACITY "/keybinding_commands/command_5", OptionString},
	{"command6", NULL, FALSE, METACITY "/keybinding_commands/command_6", OptionString},
	{"command7", NULL, FALSE, METACITY "/keybinding_commands/command_7", OptionString},
	{"command8", NULL, FALSE, METACITY "/keybinding_commands/command_8", OptionString},
	{"command9", NULL, FALSE, METACITY "/keybinding_commands/command_9", OptionString},
	{"command10", NULL, FALSE, METACITY "/keybinding_commands/command_10", OptionString},
	{"command11", NULL, FALSE, METACITY "/keybinding_commands/command_11", OptionString},
	{"command12", NULL, FALSE, METACITY "/keybinding_commands/command_12", OptionString},

	{"run_command1", NULL, FALSE, METACITY "/global_keybindings/run_command_1", OptionKey},
	{"run_command2", NULL, FALSE, METACITY "/global_keybindings/run_command_2", OptionKey},
	{"run_command3", NULL, FALSE, METACITY "/global_keybindings/run_command_3", OptionKey},
	{"run_command4", NULL, FALSE, METACITY "/global_keybindings/run_command_4", OptionKey},
	{"run_command5", NULL, FALSE, METACITY "/global_keybindings/run_command_5", OptionKey},
	{"run_command6", NULL, FALSE, METACITY "/global_keybindings/run_command_6", OptionKey},
	{"run_command7", NULL, FALSE, METACITY "/global_keybindings/run_command_7", OptionKey},
	{"run_command8", NULL, FALSE, METACITY "/global_keybindings/run_command_8", OptionKey},
	{"run_command9", NULL, FALSE, METACITY "/global_keybindings/run_command_9", OptionKey},
	{"run_command10", NULL, FALSE, METACITY "/global_keybindings/run_command_10", OptionKey},
	{"run_command11", NULL, FALSE, METACITY "/global_keybindings/run_command_11", OptionKey},
	{"run_command12", NULL, FALSE, METACITY "/global_keybindings/run_command_12", OptionKey},

	{"rotate_to_1", "rotate", FALSE, METACITY "/global_keybindings/switch_to_workspace_1", OptionKey},
	{"rotate_to_2", "rotate", FALSE, METACITY "/global_keybindings/switch_to_workspace_2", OptionKey},
	{"rotate_to_3", "rotate", FALSE, METACITY "/global_keybindings/switch_to_workspace_3", OptionKey},
	{"rotate_to_4", "rotate", FALSE, METACITY "/global_keybindings/switch_to_workspace_4", OptionKey},
	{"rotate_to_5", "rotate", FALSE, METACITY "/global_keybindings/switch_to_workspace_5", OptionKey},
	{"rotate_to_6", "rotate", FALSE, METACITY "/global_keybindings/switch_to_workspace_6", OptionKey},
	{"rotate_to_7", "rotate", FALSE, METACITY "/global_keybindings/switch_to_workspace_7", OptionKey},
	{"rotate_to_8", "rotate", FALSE, METACITY "/global_keybindings/switch_to_workspace_8", OptionKey},
	{"rotate_to_9", "rotate", FALSE, METACITY "/global_keybindings/switch_to_workspace_9", OptionKey},
	{"rotate_to_10", "rotate", FALSE, METACITY "/global_keybindings/switch_to_workspace_10", OptionKey},
	{"rotate_to_11", "rotate", FALSE, METACITY "/global_keybindings/switch_to_workspace_11", OptionKey},
	{"rotate_to_12", "rotate", FALSE, METACITY "/global_keybindings/switch_to_workspace_12", OptionKey},

	{"rotate_left", "rotate", FALSE, METACITY "/global_keybindings/switch_to_workspace_left", OptionKey},
	{"rotate_right", "rotate", FALSE, METACITY "/global_keybindings/switch_to_workspace_right", OptionKey},

	{"plane_to_1", "plane", FALSE, METACITY "/global_keybindings/switch_to_workspace_1", OptionKey},
	{"plane_to_2", "plane", FALSE, METACITY "/global_keybindings/switch_to_workspace_2", OptionKey},
	{"plane_to_3", "plane", FALSE, METACITY "/global_keybindings/switch_to_workspace_3", OptionKey},
	{"plane_to_4", "plane", FALSE, METACITY "/global_keybindings/switch_to_workspace_4", OptionKey},
	{"plane_to_5", "plane", FALSE, METACITY "/global_keybindings/switch_to_workspace_5", OptionKey},
	{"plane_to_6", "plane", FALSE, METACITY "/global_keybindings/switch_to_workspace_6", OptionKey},
	{"plane_to_7", "plane", FALSE, METACITY "/global_keybindings/switch_to_workspace_7", OptionKey},
	{"plane_to_8", "plane", FALSE, METACITY "/global_keybindings/switch_to_workspace_8", OptionKey},
	{"plane_to_9", "plane", FALSE, METACITY "/global_keybindings/switch_to_workspace_9", OptionKey},
	{"plane_to_10", "plane", FALSE, METACITY "/global_keybindings/switch_to_workspace_10", OptionKey},
	{"plane_to_11", "plane", FALSE, METACITY "/global_keybindings/switch_to_workspace_11", OptionKey},
	{"plane_to_12", "plane", FALSE, METACITY "/global_keybindings/switch_to_workspace_12", OptionKey},

	{"plane_up", "plane", FALSE, METACITY "/global_keybindings/switch_to_workspace_up", OptionKey},
	{"plane_down", "plane", FALSE, METACITY "/global_keybindings/switch_to_workspace_down", OptionKey},
	{"plane_left", "plane", FALSE, METACITY "/global_keybindings/switch_to_workspace_left", OptionKey},
	{"plane_right", "plane", FALSE, METACITY "/global_keybindings/switch_to_workspace_right", OptionKey},

	{"rotate_to_1_window", "rotate", FALSE, METACITY "/window_keybindings/move_to_workspace_1", OptionKey},
	{"rotate_to_2_window", "rotate", FALSE, METACITY "/window_keybindings/move_to_workspace_2", OptionKey},
	{"rotate_to_3_window", "rotate", FALSE, METACITY "/window_keybindings/move_to_workspace_3", OptionKey},
	{"rotate_to_4_window", "rotate", FALSE, METACITY "/window_keybindings/move_to_workspace_4", OptionKey},
	{"rotate_to_5_window", "rotate", FALSE, METACITY "/window_keybindings/move_to_workspace_5", OptionKey},
	{"rotate_to_6_window", "rotate", FALSE, METACITY "/window_keybindings/move_to_workspace_6", OptionKey},
	{"rotate_to_7_window", "rotate", FALSE, METACITY "/window_keybindings/move_to_workspace_7", OptionKey},
	{"rotate_to_8_window", "rotate", FALSE, METACITY "/window_keybindings/move_to_workspace_8", OptionKey},
	{"rotate_to_9_window", "rotate", FALSE, METACITY "/window_keybindings/move_to_workspace_9", OptionKey},
	{"rotate_to_10_window", "rotate", FALSE, METACITY "/window_keybindings/move_to_workspace_10", OptionKey},
	{"rotate_to_11_window", "rotate", FALSE, METACITY "/window_keybindings/move_to_workspace_11", OptionKey},
	{"rotate_to_12_window", "rotate", FALSE, METACITY "/window_keybindings/move_to_workspace_12", OptionKey},

	{"rotate_left_window", "rotate", FALSE, METACITY "/window_keybindings/move_to_workspace_left", OptionKey},
	{"rotate_right_window", "rotate", FALSE, METACITY "/window_keybindings/move_to_workspace_right", OptionKey},

	{"command_screenshot", NULL, FALSE, METACITY "/keybinding_commands/command_screenshot", OptionString},
	{"command_window_screenshot", NULL, FALSE, METACITY "/keybinding_commands/command_window_screenshot", OptionString},

	{"autoraise", NULL, FALSE, METACITY "/general/auto_raise", OptionBool},
	{"autoraise_delay", NULL, FALSE, METACITY "/general/auto_raise_delay", OptionInt},
	{"raise_on_click", NULL, FALSE, METACITY "/general/raise_on_click", OptionBool},
	{"click_to_focus", NULL, FALSE, METACITY "/general/focus_mode", OptionSpecial},

	{"initiate", "move", FALSE, METACITY "/general/mouse_button_modifier", OptionSpecial},
	{"initiate", "resize", FALSE, METACITY "/general/mouse_button_modifier", OptionSpecial},
	{"window_menu", NULL, FALSE, METACITY "/general/mouse_button_modifier", OptionSpecial},

	{"audible_bell", NULL, FALSE, METACITY "/general/audible_bell", OptionBool},
	{"size", NULL, TRUE, METACITY "/general/num_workspaces", OptionInt},
};

#define N_SOPTIONS (sizeof (specialOptions) / sizeof (struct _SpecialOption))

static Bool isIntegratedOption(CCSSetting * setting, int * index)
{
	unsigned int i;
	for (i = 0; i < N_SOPTIONS; i++)
	{
		if ((strcmp(setting->name, specialOptions[i].settingName) == 0) &&
			(strcmp(setting->parent->name, specialOptions[i].pluginName) == 0) &&
			((setting->isScreen && specialOptions[i].screen) || 
			 (!setting->isScreen && !specialOptions[i].screen)))
		{
			if (index)
				*index = i;
			return TRUE;
		}
	}
	return FALSE;
}

static void valueChanged(GConfClient *client, guint cnxn_id, GConfEntry *entry,
				 		 gpointer user_data)
{
	CCSContext *context = (CCSContext *)user_data;

	char *keyName = (char*) gconf_entry_get_key(entry);
	char *pluginName;
	char *screenName;
	char *settingName;
	Bool isScreen;
	unsigned int screenNum;

	keyName += strlen(COMPIZ_CCS) + 1;

	pluginName = keyName;
	keyName = strchr(keyName, '/');
	*keyName = 0;
	keyName++;

	if (strcmp(pluginName, "general") == 0)
	{
		pluginName = NULL;
	} else {
		pluginName = keyName;
	}

	screenName = keyName;
	keyName = strchr(keyName, '/');
	*keyName = 0;
	keyName++;

	settingName = keyName;

	CCSPlugin *plugin = ccsFindPlugin(context, pluginName);
	if (!plugin)
		return;

	if (strcmp(screenName, "allscreens") == 0)
		isScreen = FALSE;
	else
	{
		isScreen = TRUE;
		sscanf(screenName, "screen%d", &screenNum);
	}

	CCSSetting *setting = ccsFindSetting(plugin, settingName, isScreen, screenNum);
	if (!setting)
		return;

	if (ccsGetIntegrationEnabled(context) && !isIntegratedOption(setting, NULL))
	{
		readInit(context);
		readSetting(context, setting);
		readDone(context);
	}
}

static void gnomeValueChanged(GConfClient *client, guint cnxn_id, GConfEntry *entry,
					  		  gpointer user_data)
{
	CCSContext *context = (CCSContext *)user_data;
	char *keyName = (char*) gconf_entry_get_key(entry);
	int i,num = -1;

	for (i = 0; i < N_SOPTIONS; i++)
	{
		if (strcmp(specialOptions[i].gnomeName, keyName) == 0)
		{
			num = i;
			break;
		}
	}

	if (num < 0)
		return;

	CCSPlugin * plugin = NULL;
	plugin = ccsFindPlugin(context, (char*) specialOptions[num].pluginName);

	if (!plugin)
		return;

	CCSSetting * setting = NULL;
	/* FIXME: where should we get the screen num from? */
	setting = ccsFindSetting(plugin, (char*) specialOptions[num].settingName, 
							specialOptions[num].screen, 0);

	if (!setting)
		return;

	readInit(context);
	readSetting(context, setting);
	readDone(context);
}

static Bool readActionValue(CCSSetting * setting, char * pathName)
{
	char itemPath[BUFSIZE];
	GError *err = NULL;
	char *buffer;
	Bool boolVal;
	int intVal;
	Bool ret = FALSE;

	CCSSettingActionValue action;
	memset(&action, 0, sizeof(CCSSettingActionValue));

	snprintf(itemPath, 512, "%s/bell", pathName);
	boolVal = gconf_client_get_bool(client, itemPath, &err);
	if (!err) {
		action.onBell = boolVal;
		ret = TRUE;
	} else {
		g_error_free(err);
	}

	snprintf(itemPath, 512, "%s/edge", pathName);
	buffer = gconf_client_get_string(client, itemPath, &err);
	if (!err && buffer) {
		ccsStringToEdge(buffer, &action);
		ret = TRUE;
		g_free(buffer);
	} else if (err) {
		g_error_free(err);
	}

	snprintf(itemPath, 512, "%s/edgebutton", pathName);
	intVal = gconf_client_get_int(client, itemPath, &err);
	if (!err) {
		action.edgeButton = intVal;
		ret = TRUE;
	} else {
		g_error_free(err);
	}

	snprintf(itemPath, 512, "%s/key", pathName);
	buffer = gconf_client_get_string(client, itemPath, &err);
	if (!err && buffer) {
		ccsStringToKeyBinding(buffer, &action);
		ret = TRUE;
		g_free(buffer);
	} else if (err) {
		g_error_free(err);
	}

	snprintf(itemPath, 512, "%s/button", pathName);
	buffer = gconf_client_get_string(client, itemPath, &err);
	if (!err && buffer) {
		ccsStringToButtonBinding(buffer, &action);
		ret = TRUE;
		g_free(buffer);
	} else if (err) {
		g_error_free(err);
	}

	if (ret) 
		ccsSetAction(setting, action);

	return ret;
}

static Bool readListValue(CCSSetting * setting, char * pathName)
{
	GSList *valueList = NULL;
	GError *err = NULL;
	GConfValueType valueType;
	unsigned int nItems, i = 0;
	CCSSettingValueList list = NULL;

	switch (setting->info.forList.listType)
	{
		case TypeString:
		case TypeMatch:
		case TypeColor:
			valueType = GCONF_VALUE_STRING;
			break;
		case TypeBool:
			valueType = GCONF_VALUE_BOOL;
			break;
		case TypeInt:
			valueType = GCONF_VALUE_INT;
			break;
		case TypeFloat:
			valueType = GCONF_VALUE_FLOAT;
			break;
		default:
			valueType = GCONF_VALUE_INVALID;
			break;
	}

	if (valueType == GCONF_VALUE_INVALID)
		return FALSE;

	valueList = gconf_client_get_list(client, pathName, valueType, &err);
	if (err)
	{
		g_error_free(err);
		return FALSE;
	}

	/* FIXME: distinguish between unset value and empty list */
	if (!valueList)
		return FALSE;

	nItems = g_slist_length(valueList);

	switch (setting->info.forList.listType)
	{
		case TypeBool:
			{
				Bool *array = malloc(nItems * sizeof(Bool));
				GSList *tmpList = valueList;
				for (; tmpList; tmpList = tmpList->next, i++)
					array[i] = (GPOINTER_TO_INT(tmpList->data)) ? TRUE : FALSE;
				list = ccsGetValueListFromBoolArray(array, nItems, setting);
				free(array);
			}
			break;
		case TypeInt:
			{
				int *array = malloc(nItems * sizeof(int));
				GSList *tmpList = valueList;
				for (; tmpList; tmpList = tmpList->next, i++)
					array[i] = GPOINTER_TO_INT(tmpList->data);
				list = ccsGetValueListFromIntArray(array, nItems, setting);
				free(array);
			}
			break;
		case TypeFloat:
			{
				float *array = malloc(nItems * sizeof(float));
				GSList *tmpList = valueList;
				for (; tmpList; tmpList = tmpList->next, i++)
				{
					array[i] = *((gdouble*)tmpList->data);
					g_free(tmpList->data);
				}
				list = ccsGetValueListFromFloatArray(array, nItems, setting);
				free(array);
			}
			break;
		case TypeString:
		case TypeMatch:
			{
				char **array = malloc(nItems * sizeof(char*));
				GSList *tmpList = valueList;
				for (; tmpList; tmpList = tmpList->next, i++)
				{
					array[i] = strdup(tmpList->data);
					g_free(tmpList->data);
				}
				list = ccsGetValueListFromStringArray(array, nItems, setting);
				for (i = 0; i < nItems; i++)
					free(array[i]);
				free(array);
			}
			break;
		case TypeColor:
			{
				CCSSettingColorValue *array = malloc(nItems * sizeof(CCSSettingColorValue));
				GSList *tmpList = valueList;
				for (; tmpList; tmpList = tmpList->next, i++)
				{
					memset(&array[i], 0, sizeof(CCSSettingColorValue));
					ccsStringToColor(tmpList->data, &array[i]);
					g_free(tmpList->data);
				}
				list = ccsGetValueListFromColorArray(array, nItems, setting);
				free(array);
			}
			break;
		default:
			break;
	}

	if (valueList)
		g_slist_free(valueList);

	if (list)
	{
		ccsSetList(setting, list);
		ccsSettingValueListFree(list, TRUE);
		return TRUE;
	}

	return FALSE;
}

static Bool readIntegratedOption(CCSContext * context, CCSSetting * setting, int index)
{
	GError *err = NULL;
	Bool ret = FALSE;

	switch (specialOptions[index].type)
	{
		case OptionInt:
			{
				guint value;
				value = gconf_client_get_int(client, specialOptions[index].gnomeName, &err);

				if (!err)
				{
					ccsSetInt(setting, value);
					ret = TRUE;
				}
			}
			break;
		case OptionBool:
			{
				gboolean value;
				value = gconf_client_get_bool(client, specialOptions[index].gnomeName, &err);

				if (!err)
				{
					ccsSetBool(setting, value ? TRUE : FALSE);
					ret = TRUE;
				}
			}
			break;
		case OptionString:
			{
				char *value;
				value = gconf_client_get_string(client, specialOptions[index].gnomeName, &err);

				if (!err && value)
				{
					ccsSetString(setting, value);
					ret = TRUE;
					g_free(value);
				}
			}
			break;
		case OptionKey:
			{
				char *value;
				value = gconf_client_get_string(client, specialOptions[index].gnomeName, &err);

				if (!err && value)
				{
					CCSSettingActionValue action;
					if (ccsStringToKeyBinding(value, &action))
					{
						ccsSetAction(setting, action);
						ret = TRUE;
					}
					g_free(value);
				}
			}
			break;
		case OptionSpecial:
			if (strcmp(specialOptions[index].settingName, "click_to_focus") == 0)
			{
				char *focusMode;
				focusMode = gconf_client_get_string(client, specialOptions[index].gnomeName, &err);

				if (!err && focusMode)
				{
					Bool clickToFocus = (strcmp(focusMode, "click") == 0);
					ccsSetBool(setting, clickToFocus);
					g_free(focusMode);
				}
			}
			else if (strcmp(specialOptions[index].gnomeName, METACITY "/general/mouse_button_modifier") == 0)
			{
				char *modifier;
				modifier = gconf_client_get_string(client, specialOptions[index].gnomeName, &err);

				if (!err && modifier)
				{
					unsigned int i;
					CCSPlugin *p;
					CCSSetting *s;

					for (i = 0; i < N_SOPTIONS; i++)
					{
						if (strcmp(METACITY "/general/mouse_button_modifier", 
									specialOptions[i].gnomeName) == 0)
						{
							p = ccsFindPlugin (context, (char*) specialOptions[i].pluginName);
							if (p)
							{
								s = ccsFindSetting (p, (char*) specialOptions[i].settingName,
													specialOptions[i].screen, 0);
								if (s && (s != setting))
								{
									CCSSettingActionValue action;
									action = s->value->value.asAction;
									action.buttonModMask = setting->value->value.asAction.buttonModMask;
									ccsSetAction (s, action);
								}
							}
						}
					}
				}
			}
			break;
		default:
			break;
	}

	if (err)
		g_error_free(err);

	return ret;
}

static Bool readOption(CCSSetting * setting)
{
	GError *err = NULL;
	Bool ret = FALSE;
	KEYNAME;
	PATHNAME;

	switch (setting->type)
	{
		case TypeString:
			{
				gchar *value;
				value = gconf_client_get_string(client, pathName, &err);

				if (!err && value) 
				{
					ccsSetString(setting, value);
					ret = TRUE;
				}
			}
			break;
		case TypeMatch:
			{
				gchar * value;
				value = gconf_client_get_string(client, pathName, &err);

				if (!err && value)
				{
					ccsSetMatch(setting, value);
					ret = TRUE;
				}
			}
			break;
		case TypeInt:
			{
				int value;
				value = gconf_client_get_int(client, pathName, &err);

				if (!err)
				{
					ccsSetInt(setting, value);
					ret = TRUE;
				}
			}
			break;
		case TypeBool:
			{
				gboolean value;
				value = gconf_client_get_bool(client, pathName, &err);

				if (!err)
				{
					ccsSetBool(setting, value ? TRUE : FALSE);
					ret = TRUE;
				}
			}
			break;
		case TypeFloat:
			{
				float value;
				value = gconf_client_get_float(client, pathName, &err);

				if (!err)
				{
					ccsSetFloat(setting, value);
					ret = TRUE;
				}
			}
			break;
		case TypeColor:
			{
				gchar *value;
				CCSSettingColorValue color;
				value = gconf_client_get_string(client, pathName, &err);

				if (!err && value && ccsStringToColor(value, &color))
				{
					ccsSetColor(setting, color);
					ret = TRUE;
				}

				if (value)
					g_free(value);
			}
			break;
		case TypeList:
			ret = readListValue(setting, pathName);
			break;
		case TypeAction:
			ret = readActionValue(setting, pathName);
			break;
		default:
			printf("GConf backend: attempt to read unsupported setting type %d!\n", setting->type);
			break;
	}

	if (err)
		g_error_free(err);

	return ret;
}

static void writeActionValue(CCSSettingActionValue * action, char * pathName)
{
	char *buffer;
	char itemPath[BUFSIZE];

	snprintf(itemPath, BUFSIZE, "%s/edge", pathName);
	buffer = ccsEdgeToString(action);
	if (buffer)
	{
		gconf_client_set_string(client, itemPath, buffer, NULL);
		free(buffer);
	}

	snprintf(itemPath, BUFSIZE, "%s/bell", pathName);
	gconf_client_set_bool(client, itemPath, action->onBell, NULL);

	snprintf(itemPath, BUFSIZE, "%s/edgebutton", pathName);
	gconf_client_set_int(client, itemPath, action->edgeButton, NULL);

	snprintf(itemPath, BUFSIZE, "%s/button", pathName);
	buffer = ccsButtonBindingToString(action);
	if (buffer)
	{
		gconf_client_set_string(client, itemPath, buffer, NULL);
		free(buffer);
	}

	snprintf(itemPath, BUFSIZE, "%s/key", pathName);
	buffer = ccsKeyBindingToString(action);
	if (buffer)
	{
		gconf_client_set_string(client, itemPath, buffer, NULL);
		free(buffer);
	}
}

static void writeListValue(CCSSetting * setting, char * pathName)
{
	GSList *valueList = NULL;
	GConfValueType valueType;
	Bool freeItems = FALSE;

	CCSSettingValueList list;
	if (!ccsGetList(setting, &list))
		return;

	switch (setting->info.forList.listType)
	{
		case TypeBool:
			{
				while (list)
				{
					valueList = g_slist_append(valueList, 
									GINT_TO_POINTER(list->data->value.asBool));
					list = list->next;
				}
				valueType = GCONF_VALUE_BOOL;
			}
			break;
		case TypeInt:
			{
				while (list)
				{
					valueList = g_slist_append(valueList, 
									GINT_TO_POINTER(list->data->value.asInt));
					list = list->next;
				}
				valueType = GCONF_VALUE_INT;
			}
			break;
		case TypeFloat:
			{
				float *item;
				while (list)
				{
					item = malloc(sizeof(float));
					*item = list->data->value.asFloat;
					valueList = g_slist_append(valueList, item);
					list = list->next;
				}
				freeItems = TRUE;
				valueType = GCONF_VALUE_FLOAT;
			}
			break;
		case TypeString:
			{
				while (list)
				{
					valueList = g_slist_append(valueList, 
									list->data->value.asString);
					list = list->next;
				}
				valueType = GCONF_VALUE_STRING;
			}
			break;
		case TypeMatch:
			{
				while (list)
				{
					valueList = g_slist_append(valueList, 
									list->data->value.asMatch);
					list = list->next;
				}
				valueType = GCONF_VALUE_STRING;
			}
			break;
		case TypeColor:
			{
				char *item;
				while (list)
				{
					item = ccsColorToString(&list->data->value.asColor);
					valueList = g_slist_append(valueList, item);
					list = list->next;
				}
				freeItems = TRUE;
				valueType = GCONF_VALUE_STRING;
			}
			break;
		default:
			printf("GConf backend: attempt to write unsupported list type %d!\n", setting->info.forList.listType);
			valueType = GCONF_VALUE_INVALID;
			break;
	}

	if (valueType != GCONF_VALUE_INVALID)
	{
		gconf_client_set_list(client, pathName, valueType, valueList, NULL);
		
		if (freeItems) 
		{
			GSList *tmpList = valueList;
			for (; tmpList; tmpList = tmpList->next)
				if (tmpList->data)
					free(tmpList->data);
		}
	}
	if (valueList)
		g_slist_free(valueList);
}

static void writeIntegratedOption(CCSContext * context, CCSSetting * setting, int index)
{
	GError *err = NULL;

	switch (specialOptions[index].type)
	{
		case OptionInt:
			{
				int newValue, currentValue;
				if (!ccsGetInt(setting, &newValue))
					break;
				currentValue = gconf_client_get_int(client, 
													specialOptions[index].gnomeName, &err);

				if (!err && (currentValue != newValue))
					gconf_client_set_int(client, specialOptions[index].gnomeName, 
										 newValue, NULL);
			}
			break;
		case OptionBool:
			{
				Bool newValue;
				gboolean currentValue;
				if (!ccsGetBool(setting, &newValue))
					break;
				currentValue = gconf_client_get_bool(client, 
													 specialOptions[index].gnomeName, &err);

				if (!err && ((currentValue && !newValue) || (!currentValue && newValue)))
					gconf_client_set_bool(client, specialOptions[index].gnomeName,
										  newValue, NULL);
			}
			break;
		case OptionString:
			{
				char *newValue;
				gchar *currentValue;
				if (!ccsGetString(setting, &newValue))
					break;
				currentValue = gconf_client_get_string(client,
													   specialOptions[index].gnomeName, &err);

				if (!err && currentValue)
				{
					if (strcmp(currentValue, newValue) != 0)
						gconf_client_set_string(client, specialOptions[index].gnomeName, 
												newValue, NULL);
					g_free(currentValue);
				}
			}
			break;
		case OptionKey:
			{
				char *newValue;
				gchar *currentValue;

				newValue = ccsKeyBindingToString(&setting->value->value.asAction);
				if (newValue)
				{
					currentValue = gconf_client_get_string(client, 
														   specialOptions[index].gnomeName, &err);

					if (!err && currentValue)
					{
						if (strcmp(currentValue, newValue) != 0)
							gconf_client_set_string(client, specialOptions[index].gnomeName,
													newValue, NULL);
						g_free(currentValue);
					}
					free(newValue);
				}
			}
			break;
		case OptionSpecial:
			if (strcmp(specialOptions[index].settingName, "click_to_focus") == 0)
			{
				Bool clickToFocus;
				gchar *newValue, *currentValue;
				if (!ccsGetBool(setting, &clickToFocus))
					break;

				newValue = clickToFocus ? "click" : "mouse";
				currentValue = gconf_client_get_string(client, 
													   specialOptions[index].gnomeName, &err);

				if (!err && currentValue)
				{
					if (strcmp(currentValue, newValue) != 0)
						gconf_client_set_string(client, specialOptions[index].gnomeName, 
												newValue,NULL);
					g_free(currentValue);
				}
			}
			else if (strcmp(specialOptions[index].gnomeName, METACITY "/general/mouse_button_modifier") == 0)
			{
				char *newBinding;

				newBinding = ccsKeyBindingToString(&setting->value->value.asAction);
				if (newBinding)
				{
					char *modsEnd;
					gchar *currentValue;
					modsEnd = strrchr(newBinding, '>');
					if (modsEnd)
					{
						*(modsEnd+1) = 0;
						/* newBinding now only contains the modifiers */

						currentValue = gconf_client_get_string(client, 
															   specialOptions[index].gnomeName, &err);

						/* write it to GConf */
						if (!err && currentValue)
						{
							unsigned int i;
							CCSPlugin *p;
							CCSSetting *s;

							if (strcmp(currentValue, newBinding) != 0)
								gconf_client_set_string(client, 
														specialOptions[index].gnomeName, 
														newBinding, NULL);
							g_free(currentValue);

							/* and set the other two options */

							for (i = 0; i < N_SOPTIONS; i++)
							{
								if (strcmp(METACITY "/general/mouse_button_modifier", 
										   specialOptions[i].gnomeName) == 0)
								{
									p = ccsFindPlugin (context, (char*) specialOptions[i].pluginName);
									if (p)
									{
										s = ccsFindSetting (p, (char*) specialOptions[i].settingName,
															specialOptions[i].screen, 0);
										if (s && (s != setting))
										{
											CCSSettingActionValue action;
											action = s->value->value.asAction;
											action.buttonModMask = setting->value->value.asAction.buttonModMask;
											ccsSetAction (s, action);
										}
									}
								}
							}
						}
					}
					free(newBinding);
				}
			}
			break;
	}

	if (err)
		g_error_free(err);
}

static void resetOptionToDefault(CCSSetting * setting)
{
	KEYNAME;
	PATHNAME;

	gconf_client_recursive_unset(client, pathName, 0, NULL);
	gconf_client_suggest_sync(client,NULL);
}

static void writeOption(CCSSetting * setting)
{
	KEYNAME;
	PATHNAME;

	switch (setting->type)
	{
		case TypeString:
			{
				char *value;
				if (ccsGetString(setting, &value))
					gconf_client_set_string(client, pathName, value, NULL);
			}
			break;
		case TypeMatch:
			{
				char *value;
				if (ccsGetMatch(setting, &value))
					gconf_client_set_string(client, pathName, value, NULL);
			}
		case TypeFloat:
			{
				float value;
				if (ccsGetFloat(setting, &value))
					gconf_client_set_float(client, pathName, value, NULL);
			}
			break;
		case TypeInt:
			{
				int value;
				if (ccsGetInt(setting, &value))
					gconf_client_set_int(client, pathName, value, NULL);
			}
			break;
		case TypeBool:
			{
				Bool value;
				if (ccsGetBool(setting, &value))
					gconf_client_set_bool(client, pathName, value, NULL);
			}
			break;
		case TypeColor:
			{
				CCSSettingColorValue value;
				char *colString;

				if (!ccsGetColor(setting, &value))
					break;

				colString = ccsColorToString(&value);
				if (!colString)
					break;

				gconf_client_set_string(client, pathName, colString, NULL);
				free(colString);
			}
			break;
		case TypeAction:
			{
			    CCSSettingActionValue value;
			    if (!ccsGetAction(setting, &value))
					break;

				writeActionValue(&value, pathName);
			}
			break;
		case TypeList:
			writeListValue(setting, pathName);
			break;
		default:
			printf("GConf backend: attempt to write unsupported setting type %d\n", setting->type);
			break;
	}
}

static void processEvents(void)
{
	while (g_main_context_pending(NULL))
		g_main_context_iteration(NULL, FALSE);
}

static Bool initBackend(CCSContext * context)
{
	g_type_init();

	client = gconf_client_get_default();

	backendNotifyId = gconf_client_notify_add(client, COMPIZ_CCS, valueChanged,
											  context, NULL, NULL);

	if (ccsGetIntegrationEnabled(context))
		gnomeNotifyId = gconf_client_notify_add(client, METACITY,
												gnomeValueChanged, context, NULL,NULL);

	gconf_client_add_dir(client, COMPIZ_CCS, GCONF_CLIENT_PRELOAD_NONE, NULL);
	gconf_client_add_dir(client, METACITY, GCONF_CLIENT_PRELOAD_NONE, NULL);

	return TRUE;
}

static Bool finiBackend(CCSContext * context)
{
	if (backendNotifyId)
	{
		gconf_client_notify_remove(client, backendNotifyId);
		backendNotifyId = 0;
	}
	if (gnomeNotifyId)
	{
		gconf_client_notify_remove(client, gnomeNotifyId);
		gnomeNotifyId = 0;
	}

	if (currentProfile)
	{
		free (currentProfile);
		currentProfile = NULL;
	}

	gconf_client_remove_dir(client, COMPIZ_CCS, NULL);
	gconf_client_remove_dir(client, METACITY, NULL);

	g_object_unref(client);
	client = NULL;

	return TRUE;
}

static Bool readInit(CCSContext * context)
{
	if (currentProfile)
		free (currentProfile);

	currentProfile = ccsGetProfile(context);
	if (!currentProfile)
		currentProfile = strdup (DEFAULTPROF);
	else if (!strlen(currentProfile))
	{
		free (currentProfile);
		currentProfile = strdup (DEFAULTPROF);
	}

	return TRUE;
}

static void readSetting(CCSContext * context, CCSSetting * setting)
{
	Bool status;
	int index;

	if (ccsGetIntegrationEnabled(context) && isIntegratedOption(setting, &index))
		status = readIntegratedOption(context, setting, index);
	else
		status = readOption(setting);

	if (!status)
		ccsResetToDefault(setting);
}

static void readDone(CCSContext * context)
{
}

static Bool writeInit(CCSContext * context)
{
	if (currentProfile)
		free (currentProfile);

	currentProfile = ccsGetProfile(context);
	if (!currentProfile)
		currentProfile = strdup (DEFAULTPROF);
	else if (!strlen(currentProfile))
	{
		free (currentProfile);
		currentProfile = strdup (DEFAULTPROF);
	}

	return TRUE;
}

static void writeSetting(CCSContext * context, CCSSetting * setting)
{
	int index;

	if (ccsGetIntegrationEnabled(context) && isIntegratedOption(setting, &index))
		writeIntegratedOption(context, setting, index);
	else if (setting->isDefault)
		resetOptionToDefault(setting);
	else
		writeOption(setting);

}

static void writeDone(CCSContext * context)
{
}

static Bool getSettingIsIntegrated(CCSSetting * setting)
{
	if (!ccsGetIntegrationEnabled(setting->parent->context))
		return FALSE;

	if (!isIntegratedOption(setting, NULL))
		return FALSE;

	return TRUE;
}

static Bool getSettingIsReadOnly(CCSSetting * setting)
{
	/* FIXME */
	return FALSE;
}

static CCSStringList getExistingProfiles(void)
{
	gconf_client_suggest_sync(client,NULL);
	GSList * data = gconf_client_all_dirs(client, COMPIZ_CCS, NULL);
	CCSStringList ret = NULL;
	GSList * tmp = data;
	char *name;

	for (;tmp;tmp = g_slist_next(tmp))
	{
		name = strrchr(tmp->data, '/');
		if (name && (strcmp(name, DEFAULTPROF) != 0))
			ret = ccsStringListAppend(ret, name+1);

		g_free(tmp->data);
	}
	g_slist_free(data);

	return ret;
}

static Bool deleteProfile(char * profile)
{
	char path[BUFSIZE];

	if (profile && strlen(profile))
		snprintf(path, BUFSIZE, "%s/%s", COMPIZ_CCS, profile);
	else
		snprintf(path, BUFSIZE, "%s/Default", COMPIZ_CCS);

	gboolean status = FALSE;
	if (gconf_client_dir_exists(client, path, NULL))
	{
		status = gconf_client_recursive_unset(client, path, 0, NULL);
		gconf_client_suggest_sync(client,NULL);
	}

	return status;
}


static CCSBackendVTable gconfVTable = {
    "gconf",
    "GConf Configuration Backend",
    "GConf Configuration Backend for libccs",
    TRUE,
    TRUE,
    processEvents,
    initBackend,
    finiBackend,
	readInit,
	readSetting,
	readDone,
	writeInit,
	writeSetting,
	writeDone,
	getSettingIsIntegrated,
	getSettingIsReadOnly,
	getExistingProfiles,
	deleteProfile
};

CCSBackendVTable *
getBackendInfo (void)
{
    return &gconfVTable;
}

