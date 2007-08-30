/*
 * Compiz configuration system library plugin
 *
 * Copyright (C) 2007  Dennis Kasprzyk <onestone@opencompositing.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.

 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#define _GNU_SOURCE

#ifdef HAVE_CONFIG_H
#  include "../config.h"
#endif

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include <compiz-core.h>

#include <ccs.h>

static int displayPrivateIndex;
static CompMetadata ccpMetadata;

typedef struct _CCPDisplay
{
    int screenPrivateIndex;

    CCSContext *context;
    Bool applyingSettings;

    CompTimeoutHandle timeoutHandle;
    CompTimeoutHandle reloadHandle;

    InitPluginForDisplayProc      initPluginForDisplay;
    SetDisplayOptionForPluginProc setDisplayOptionForPlugin;
}

CCPDisplay;

typedef struct _CCPScreen
{
    InitPluginForScreenProc      initPluginForScreen;
    SetScreenOptionForPluginProc setScreenOptionForPlugin;
}

CCPScreen;

#define GET_CCP_DISPLAY(d)				      \
    ((CCPDisplay *) (d)->object.privates[displayPrivateIndex].ptr)

#define CCP_DISPLAY(d)		     \
    CCPDisplay *cd = GET_CCP_DISPLAY (d)

#define GET_CCP_SCREEN(s, cd)				         \
    ((CCPScreen *) (s)->object.privates[(cd)->screenPrivateIndex].ptr)

#define CCP_SCREEN(s)						           \
    CCPScreen *cs = GET_CCP_SCREEN (s, GET_CCP_DISPLAY (s->display))

#define CCP_UPDATE_TIMEOUT 250
#define CORE_VTABLE_NAME  "core"

static void
ccpSetValueToValue (CompDisplay     *d,
		    CCSSettingValue *sv,
		    CompOptionValue *v,
		    CCSSettingType  type)
{
    switch (type)
    {
    case TypeInt:
	v->i = sv->value.asInt;
	break;
    case TypeFloat:
	v->f = sv->value.asFloat;
	break;
    case TypeBool:
	v->b = sv->value.asBool;
	break;
    case TypeColor:
	{
	    int i;

	    for (i = 0; i < 4; i++)
		v->c[i] = sv->value.asColor.array.array[i];
	}
	break;
    case TypeString:
	v->s = strdup (sv->value.asString);
	break;
    case TypeMatch:
	matchInit (&v->match);
	matchAddFromString (&v->match, sv->value.asMatch);
	break;
    case TypeKey:
	{
	    v->action.key.keycode =
		(sv->value.asKey.keysym != NoSymbol) ?
		XKeysymToKeycode (d->display, sv->value.asKey.keysym) : 0;

	    v->action.key.modifiers = sv->value.asKey.keyModMask;

	    if (v->action.key.keycode || v->action.key.modifiers)
		v->action.type = CompBindingTypeKey;
	    else
		v->action.type = CompBindingTypeNone;
	}
	break;
    case TypeButton:
	{
	    v->action.button.button = sv->value.asButton.button;
	    v->action.button.modifiers = sv->value.asButton.buttonModMask;
	    v->action.edgeMask = sv->value.asButton.edgeMask;

	    if (v->action.button.button || v->action.button.modifiers)
	    {
		if (sv->value.asButton.edgeMask)
		    v->action.type = CompBindingTypeEdgeButton;
		else
		    v->action.type = CompBindingTypeButton;
	    }
	    else
		v->action.type = CompBindingTypeNone;
	}
	break;
    case TypeEdge:
	{
	    v->action.edgeMask = sv->value.asEdge;
	}
	break;
    case TypeBell:
	{
	    v->action.bell = sv->value.asBell;
	}
	break;
    default:
	break;
    }
}

static void
ccpConvertPluginList (CompDisplay         *d,
		      CCSSetting          *s,
		      CCSSettingValueList list,
		      CompOptionValue     *v)
{
    CCSStringList       sl, l;
    int                 i;

    sl = ccsGetStringListFromValueList (list);

    while (ccsStringListFind(sl,"ccp"))
	sl = ccsStringListRemove (sl, "ccp", TRUE);

    while (ccsStringListFind(sl,"core"))
	sl = ccsStringListRemove (sl, "core", TRUE);

    sl = ccsStringListPrepend (sl, strdup ("ccp"));
    sl = ccsStringListPrepend (sl, strdup ("core"));

    v->list.nValue = ccsStringListLength (sl);
    v->list.value  = calloc (v->list.nValue, sizeof (CompOptionValue));
    if (!v->list.value)
    {
	v->list.nValue = 0;
	return;
    }

    for (l = sl, i = 0; l; l = l->next)
    {
	if (l->data)
	    v->list.value[i].s = strdup (l->data);
	i++;
    }

    ccsStringListFree (sl, TRUE);
}

static void
ccpSettingToValue (CompDisplay     *d,
		   CCSSetting      *s,
		   CompOptionValue *v)
{
    if (s->type != TypeList)
	ccpSetValueToValue (d, s->value, v, s->type);
    else
    {
	CCSSettingValueList list;
	int                 i = 0;

	ccsGetList (s, &list);

	if ((strcmp (s->name, "active_plugins") == 0) &&
	    (strcmp (s->parent->name, CORE_VTABLE_NAME) == 0))
	{
	    ccpConvertPluginList (d, s, list, v);
	}
	else
	{
    	    v->list.nValue = ccsSettingValueListLength (list);
    	    v->list.value  = calloc (v->list.nValue, sizeof (CompOptionValue));

    	    while (list)
    	    {
    		ccpSetValueToValue (d, list->data,
    				    &v->list.value[i],
				    s->info.forList.listType);
		list = list->next;
		i++;
	    }
	}
    }
}

static void
ccpInitValue (CompDisplay     *d,
	      CCSSettingValue *value,
	      CompOptionValue *from,
	      CCSSettingType  type)
{
    switch (type)
    {
    case TypeInt:
	value->value.asInt = from->i;
	break;
    case TypeFloat:
	value->value.asFloat = from->f;
	break;
    case TypeBool:
	value->value.asBool = from->b;
	break;
    case TypeColor:
	{
	    int i;

	    for (i = 0; i < 4; i++)
		value->value.asColor.array.array[i] = from->c[i];
	}
	break;
    case TypeString:
	value->value.asString = strdup (from->s);
	break;
    case TypeMatch:
	value->value.asMatch = matchToString (&from->match);
	break;
    case TypeKey:
	if (from->action.type & CompBindingTypeKey)
	{
	    value->value.asKey.keysym =
		XKeycodeToKeysym (d->display, from->action.key.keycode, 0);
	    value->value.asKey.keyModMask = from->action.key.modifiers;
	}
	else
	{
	    value->value.asKey.keysym = 0;
	    value->value.asKey.keyModMask = 0;
	}
    case TypeButton:
	if (from->action.type & CompBindingTypeButton)
	{
	    value->value.asButton.button = from->action.button.button;
	    value->value.asButton.buttonModMask =
		from->action.button.modifiers;
	    value->value.asButton.edgeMask = 0;
	}
	else if (from->action.type & CompBindingTypeEdgeButton)
	{
	    value->value.asButton.button = from->action.button.button;
	    value->value.asButton.buttonModMask =
		from->action.button.modifiers;
	    value->value.asButton.edgeMask = from->action.edgeMask;
	}
	else
	{
	    value->value.asButton.button = 0;
	    value->value.asButton.buttonModMask = 0;
	    value->value.asButton.edgeMask = 0;
	}
	break;
    case TypeEdge:
	value->value.asEdge = from->action.edgeMask;
	break;
    case TypeBell:
	value->value.asBell = from->action.bell;
	break;
    default:
	break;
    }
}

static void
ccpValueToSetting (CompDisplay     *d,
		   CCSSetting      *s,
		   CompOptionValue *v)
{
    CCSSettingValue *value;

    value = calloc (1, sizeof (CCSSettingValue));
    if (!value)
	return;

    value->parent = s;

    if (s->type == TypeList)
    {
	int i;

	for (i = 0; i < v->list.nValue; i++)
	{
	    CCSSettingValue *val;

	    val = calloc (1, sizeof (CCSSettingValue));
	    if (val)
	    {
		val->parent = s;
		val->isListChild = TRUE;
		ccpInitValue (d, val, &v->list.value[i],
			      s->info.forList.listType);
		value->value.asList =
		    ccsSettingValueListAppend (value->value.asList, val);
	    }
	}
    }
    else
	ccpInitValue (d, value, v, s->type);

    ccsSetValue (s, value);
    ccsFreeSettingValue (value);
}

static void
ccpFreeValue (CompOptionValue *v,
	      CCSSettingType  type)
{
    switch (type)
    {
    case TypeString:
	free (v->s);
	break;
    case TypeMatch:
	matchFini (&v->match);
	break;
    default:
	break;
    }
}

static void
ccpFreeCompValue (CCSSetting      *s,
		  CompOptionValue *v)
{
    if (s->type != TypeList)
	ccpFreeValue (v, s->type);
    else
    {
	int i = 0;

	for (i = 0; i < v->list.nValue; i++)
	    ccpFreeValue (&v->list.value[i], s->info.forList.listType);

	free (v->list.value);
    }
}

static Bool
ccpSameType (CCSSettingType st, CompOptionType ot)
{
    if (st == TypeBool && ot == CompOptionTypeBool)
	return TRUE;
    if (st == TypeInt && ot == CompOptionTypeInt)
	return TRUE;
    if (st == TypeFloat && ot == CompOptionTypeFloat)
	return TRUE;
    if (st == TypeColor && ot == CompOptionTypeColor)
	return TRUE;
    if (st == TypeString && ot == CompOptionTypeString)
	return TRUE;
    if (st == TypeMatch && ot == CompOptionTypeMatch)
	return TRUE;
    if (st == TypeKey && ot == CompOptionTypeKey)
	return TRUE;
    if (st == TypeButton && ot == CompOptionTypeButton)
	return TRUE;
    if (st == TypeEdge && ot == CompOptionTypeEdge)
	return TRUE;
    if (st == TypeBell && ot == CompOptionTypeBell)
	return TRUE;
    if (st == TypeList && ot == CompOptionTypeList)
	return TRUE;
    return FALSE;
}

static Bool
ccpTypeCheck (CCSSetting *s, CompOption *o)
{
    switch (s->type)
    {
    case TypeList:
	return ccpSameType (s->type, o->type) &&
	       ccpSameType (s->info.forList.listType, o->value.list.type);
	break;
    default:
	return ccpSameType (s->type, o->type);
	break;
    }
    return FALSE;
}

static void
ccpSetOptionFromContext (CompDisplay *d,
			 const char  *plugin,
			 const char  *name,
			 Bool        screen,
			 int         screenNum)
{
    CompPlugin      *p = NULL;
    CompScreen      *s = NULL;
    CompOption      *o = NULL;
    CompOptionValue value;
    CompOption      *option = NULL;
    int	            nOption;
    CCSPlugin      *bsp;
    CCSSetting      *setting;

    CCP_DISPLAY (d);

    if (plugin)
	p = findActivePlugin (plugin);

    if (!p)
	return;

    if (!name)
	return;

    if (screen)
    {
	Bool found = FALSE;

	for (s = d->screens; s; s = s->next)
	    if (s->screenNum == screenNum)
	    {
		found = TRUE;
		break;
	    }

	if (!found)
	    return;
    }

    if (p->vTable->getObjectOptions)
	option = (*p->vTable->getObjectOptions) (p,
						 s ? &s->object : &d->object,
						 &nOption);

    if (!option)
	return;

    o = compFindOption (option, nOption, name, 0);
    if (!o)
	return;

    bsp = ccsFindPlugin (cd->context, (plugin) ? plugin : CORE_VTABLE_NAME);
    if (!bsp)
	return;

    setting = ccsFindSetting (bsp, name, screen, screenNum);
    if (!setting)
	return;

    if (!ccpTypeCheck (setting, o))
	return;

    value = o->value;
    ccpSettingToValue (d, setting, &value);

    cd->applyingSettings = TRUE;

    if (s)
	(*s->setScreenOptionForPlugin) (s, plugin, name, &value);
    else
	(*d->setDisplayOptionForPlugin) (d, plugin, name, &value);

    cd->applyingSettings = FALSE;

    ccpFreeCompValue (setting, &value);
}

static void
ccpSetContextFromOption (CompDisplay *d,
			 const char  *plugin,
			 const char  *name,
			 Bool        screen,
			 int         screenNum)
{
    CompPlugin *p = NULL;
    CompScreen *s = NULL;
    CompOption *o = NULL;
    CompOption *option = NULL;
    int	       nOption;
    CCSPlugin *bsp;
    CCSSetting *setting;

    CCP_DISPLAY (d);

    if (plugin)
	p = findActivePlugin (plugin);

    if (!p)
	return;

    if (!name)
	return;

    if (screen)
    {
	Bool found = FALSE;

	for (s = d->screens; s; s = s->next)
	    if (s->screenNum == screenNum)
	    {
		found = TRUE;
		break;
	    }

	if (!found)
	    return;
    }

    if (p->vTable->getObjectOptions)
	option = (*p->vTable->getObjectOptions) (p,
						 s ? &s->object : &d->object,
						 &nOption);

    if (!option)
	return;

    o = compFindOption (option, nOption, name, 0);
    if (!o)
	return;

    bsp = ccsFindPlugin (cd->context, (plugin) ? plugin : CORE_VTABLE_NAME);
    if (!bsp)
	return;

    setting = ccsFindSetting (bsp, name, screen, screenNum);
    if (!setting)
	return;

    if (!ccpTypeCheck (setting, o))
	return;

    ccpValueToSetting (d, setting, &o->value);
    ccsWriteChangedSettings (cd->context);
}

static Bool
ccpSetDisplayOptionForPlugin (CompDisplay     *d,
			      const char      *plugin,
			      const char      *name,
			      CompOptionValue *value)
{
    Bool status;

    CCP_DISPLAY (d);

    UNWRAP (cd, d, setDisplayOptionForPlugin);
    status = (*d->setDisplayOptionForPlugin) (d, plugin, name, value);
    WRAP (cd, d, setDisplayOptionForPlugin, ccpSetDisplayOptionForPlugin);

    if (status && !cd->reloadHandle && !cd->applyingSettings)
	ccpSetContextFromOption (d, plugin, name, FALSE, 0);

    return status;
}

static Bool
ccpSetScreenOptionForPlugin (CompScreen      *s,
			     const char	     *plugin,
			     const char	     *name,
			     CompOptionValue *value)
{
    Bool status;

    CCP_SCREEN (s);

    UNWRAP (cs, s, setScreenOptionForPlugin);
    status = (*s->setScreenOptionForPlugin) (s, plugin, name, value);
    WRAP (cs, s, setScreenOptionForPlugin, ccpSetScreenOptionForPlugin);

    if (status)
    {
	CCP_DISPLAY (s->display);

	if (!cd->reloadHandle && !cd->applyingSettings)
	    ccpSetContextFromOption (s->display, plugin,
				     name, TRUE, s->screenNum);
    }

    return status;
}

static Bool
ccpInitPluginForDisplay (CompPlugin  *p,
			 CompDisplay *d)
{
    Bool status;

    CCP_DISPLAY (d);

    UNWRAP (cd, d, initPluginForDisplay);
    status = (*d->initPluginForDisplay) (p, d);
    WRAP (cd, d, initPluginForDisplay, ccpInitPluginForDisplay);

    if (status && p->vTable->getObjectOptions)
    {
	CompOption *option;
	int	   nOption;
	int        i;

	option = (*p->vTable->getObjectOptions) (p, &d->object, &nOption);

	for (i = 0; i < nOption; i++)
	    ccpSetOptionFromContext (d, p->vTable->name,
				     option[i].name, FALSE, 0);
    }

    return status;
}

static Bool
ccpInitPluginForScreen (CompPlugin *p,
			CompScreen *s)
{
    Bool status;

    CCP_SCREEN (s);

    UNWRAP (cs, s, initPluginForScreen);
    status = (*s->initPluginForScreen) (p, s);
    WRAP (cs, s, initPluginForScreen, ccpInitPluginForScreen);

    if (status && p->vTable->getObjectOptions)
    {
	CompOption *option;
	int	   nOption;
	int i;

	option = (*p->vTable->getObjectOptions) (p, &s->object, &nOption);

	for (i = 0; i < nOption; i++)
	    ccpSetOptionFromContext (s->display, p->vTable->name,
				     option[i].name,
				     TRUE, s->screenNum);
    }

    return status;
}

static Bool
ccpReload (void *closure)
{
    CompDisplay *d = (CompDisplay *) closure;
    CompScreen  *s;
    CompPlugin  *p;
    CompOption  *option;
    int		nOption;

    CCP_DISPLAY (d);

    for (p = getPlugins (); p; p = p->next)
    {
	if (!p->vTable->getObjectOptions)
	    continue;

	option = (*p->vTable->getObjectOptions) (p, &d->object, &nOption);
	while (nOption--)
	{
	    ccpSetOptionFromContext (d, p->vTable->name,
				     option->name, FALSE, 0);
	    option++;
	}
    }

    for (s = d->screens; s; s = s->next)
    {
	for (p = getPlugins (); p; p = p->next)
	{
	    if (!p->vTable->getObjectOptions)
		continue;

	    option = (*p->vTable->getObjectOptions) (p, &s->object, &nOption);
	    while (nOption--)
	    {
		ccpSetOptionFromContext (d, p->vTable->name,
					 option->name, TRUE, s->screenNum);
		option++;
	    }
	}
    }

    cd->reloadHandle = 0;

    return FALSE;
}

static Bool
ccpTimeout (void *closure)
{
    CompDisplay *d = (CompDisplay *) closure;
    unsigned int flags = 0;

    CCP_DISPLAY (d);

    if (findActivePlugin ("glib"))
	flags |= ProcessEventsNoGlibMainLoopMask;

    ccsProcessEvents (cd->context, flags);

    if (ccsSettingListLength (cd->context->changedSettings))
    {
	CCSSettingList list = cd->context->changedSettings;
	CCSSettingList l = list;
	cd->context->changedSettings = NULL;
	CCSSetting *s;

	while (l)
	{
	    s = l->data;
	    ccpSetOptionFromContext (d, s->parent->name, s->name,
				     s->isScreen, s->screenNum);
	    D (D_FULL, "Setting Update \"%s\"\n", s->name);
	    l = l->next;
	}

	ccsSettingListFree (list, FALSE);
	cd->context->changedSettings =
	    ccsSettingListFree (cd->context->changedSettings, FALSE);
    }

    return TRUE;
}

static Bool
ccpInitDisplay (CompPlugin  *p,
		CompDisplay *d)
{
    CCPDisplay *cd;
    CompScreen *s;
    int i;
    unsigned int *screens;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    cd = malloc (sizeof (CCPDisplay));
    if (!cd)
	return FALSE;

    cd->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (cd->screenPrivateIndex < 0)
    {
	free (cd);
	return FALSE;
    }

    WRAP (cd, d, initPluginForDisplay, ccpInitPluginForDisplay);
    WRAP (cd, d, setDisplayOptionForPlugin, ccpSetDisplayOptionForPlugin);

    d->object.privates[displayPrivateIndex].ptr = cd;

    for (s = d->screens, i = 0; s; s = s->next, i++);
    screens = calloc (i, sizeof (unsigned int));
    if (!screens)
    {
	free (cd);
	return FALSE;
    }

    for (s = d->screens, i = 0; s; s = s->next)
	screens[i++] = s->screenNum;

    ccsSetBasicMetadata (TRUE);

    cd->context = ccsContextNew (screens, i);

    free (screens);

    if (!cd->context)
    {
	free (cd);
	return FALSE;
    }

    ccsReadSettings (cd->context);

    cd->context->changedSettings =
	ccsSettingListFree (cd->context->changedSettings, FALSE);

    cd->applyingSettings = FALSE;

    cd->reloadHandle = compAddTimeout (0, ccpReload, (void *) d);
    cd->timeoutHandle = compAddTimeout (CCP_UPDATE_TIMEOUT,
					ccpTimeout, (void *) d);

    return TRUE;
}

static void
ccpFiniDisplay (CompPlugin  *p,
		CompDisplay *d)
{
    CCP_DISPLAY (d);

    compRemoveTimeout (cd->timeoutHandle);

    UNWRAP (cd, d, initPluginForDisplay);
    UNWRAP (cd, d, setDisplayOptionForPlugin);

    freeScreenPrivateIndex (d, cd->screenPrivateIndex);

    ccsContextDestroy (cd->context);

    free (cd);
}

static Bool
ccpInitScreen (CompPlugin *p,
	       CompScreen *s)
{
    CCPScreen *cs;

    CCP_DISPLAY (s->display);

    cs = malloc (sizeof (CCPScreen));

    if (!cs)
	return FALSE;

    WRAP (cs, s, initPluginForScreen, ccpInitPluginForScreen);
    WRAP (cs, s, setScreenOptionForPlugin, ccpSetScreenOptionForPlugin);

    s->object.privates[cd->screenPrivateIndex].ptr = cs;

    return TRUE;
}

static void
ccpFiniScreen (CompPlugin *p,
	       CompScreen *s)
{
    CCP_SCREEN (s);

    UNWRAP (cs, s, initPluginForScreen);
    UNWRAP (cs, s, setScreenOptionForPlugin);

    free (cs);
}

static CompBool
ccpInitObject (CompPlugin *p,
	       CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) ccpInitDisplay,
	(InitPluginObjectProc) ccpInitScreen
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
ccpFiniObject (CompPlugin *p,
	       CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) ccpFiniDisplay,
	(FiniPluginObjectProc) ccpFiniScreen
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}


static Bool
ccpInit (CompPlugin *p)
{
    if (!compInitPluginMetadataFromInfo (&ccpMetadata, p->vTable->name,
					 0, 0, 0, 0))
	return FALSE;

    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
    {
	compFiniMetadata (&ccpMetadata);
	return FALSE;
    }

    compAddMetadataFromFile (&ccpMetadata, p->vTable->name);

    return TRUE;
}

static void
ccpFini (CompPlugin *p)
{
    freeDisplayPrivateIndex (displayPrivateIndex);
    compFiniMetadata (&ccpMetadata);
}

static CompMetadata*
ccpGetMetadata (CompPlugin *plugin)
{
    return &ccpMetadata;
}

CompPluginVTable ccpVTable = {
    "ccp",
    ccpGetMetadata,
    ccpInit,
    ccpFini,
    ccpInitObject,
    ccpFiniObject,
    0,
    0
};

CompPluginVTable *
getCompPluginInfo20070830 (void)
{
    return &ccpVTable;
}
