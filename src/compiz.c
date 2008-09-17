/*
 * Compiz configuration system library
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

#ifdef HAVE_CONFIG_H
#  include "../config.h"
#endif

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <libintl.h>
#include <dlfcn.h>
#include <dirent.h>
#include <ctype.h>
#include <libgen.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <locale.h>
#include <unistd.h>

#include <compiz-core.h>
#include <ccs.h>
#include "ccs-private.h"

static xmlDoc * globalMetadata = NULL;

static CCSSettingType
getOptionType (char *name)
{
    static struct _TypeMap
    {
	char *name;
	CCSSettingType type;
    } map[] = {
	{ "bool", TypeBool },
	{ "int", TypeInt },
	{ "float", TypeFloat },
      	{ "string", TypeString },
	{ "color", TypeColor },
	{ "key", TypeKey },
	{ "button", TypeButton },
 	{ "edge", TypeEdge },
  	{ "bell", TypeBell },
	{ "match", TypeMatch },
	{ "list", TypeList }
    };
    int i;

    for (i = 0; i < sizeof (map) / sizeof (map[0]); i++)
	if (strcasecmp (name, map[i].name) == 0)
	    return map[i].type;

    return TypeNum;
}

static char *
getGenericNodePath (xmlNode * base)
{
    char *rv = NULL;
    char *parent = NULL;
    xmlChar *name = NULL;
    xmlChar *type = NULL;

    if (base->parent)
	parent = getGenericNodePath (base->parent);
    else
	parent = strdup ("");

    if (!parent)
	return NULL;
    if (!base->name)
    {
	free (parent);
	return strdup ("");
    }

    if (!xmlStrcmp (base->name, BAD_CAST "option"))
    {
	name = xmlGetProp (base, BAD_CAST "name");
	if (!name)
	{
	    free (parent);
	    return NULL;
	}

	if (!strlen ((char *) name))
	{
	    xmlFree (name);
	    free (parent);
	    return NULL;
	}

	type = xmlGetProp (base, BAD_CAST "type");
	if (!type)
	{
	    xmlFree (name);
	    free (parent);
	    return NULL;
	}

	if (!strlen ((char *) type))
	{
	    xmlFree (name);
	    xmlFree (type);
	    free (parent);
	    return NULL;
	}

	asprintf (&rv, "%s/option[@name = '%s' and @type = '%s']", 
		  parent, name, type);

	xmlFree (name);
	xmlFree (type);
	free (parent);
	return rv;
    }
    else if (!xmlStrcmp (base->name, BAD_CAST "plugin"))
    {
	name = xmlGetProp (base, BAD_CAST "name");
	if (!name)
	{
	    free (parent);
	    return NULL;
	}

	if (!strlen ((char *) name))
	{
	    xmlFree (name);
	    free (parent);
	    return NULL;
	}

	asprintf (&rv, "%s/plugin[@name = '%s']", parent, name);

	xmlFree (name);
	free (parent);
	return rv;
    }
    else if (!xmlStrcmp (base->name, BAD_CAST "group") ||
	     !xmlStrcmp (base->name, BAD_CAST "subgroup"))
    {
	return parent;
    }
    else if (!xmlStrcmp (base->name, BAD_CAST "screen") ||
	     !xmlStrcmp (base->name, BAD_CAST "display"))
    {
	asprintf (&rv, "%s/%s/", parent, base->name);
	free (parent);
	return rv;
    }

    asprintf (&rv, "%s/%s", parent, base->name);

    free (parent);
    return rv;
}

static char *
getStringFromXPath (xmlDoc * doc, xmlNode * base, char *path)
{
    xmlXPathObjectPtr xpathObj;
    xmlXPathContextPtr xpathCtx;
    char *rv = NULL;

    xpathCtx = xmlXPathNewContext (doc);
    if (!xpathCtx)
	return NULL;

    if (base)
	xpathCtx->node = base;

    xpathObj = xmlXPathEvalExpression (BAD_CAST path, xpathCtx);

    if (!xpathObj)
    {
	xmlXPathFreeContext (xpathCtx);
	return NULL;
    }

    xpathObj = xmlXPathConvertString (xpathObj);

    if (xpathObj->type == XPATH_STRING && xpathObj->stringval
	&& strlen ((char *) xpathObj->stringval))
    {
	rv = strdup ((char *) xpathObj->stringval);
    }

    xmlXPathFreeObject (xpathObj);
    xmlXPathFreeContext (xpathCtx);
    return rv;
}

static xmlNode **
getNodesFromXPath (xmlDoc * doc, xmlNode * base, char *path, int *num)
{
    xmlXPathObjectPtr xpathObj;
    xmlXPathContextPtr xpathCtx;
    xmlNode **rv = NULL;
    int size;
    int i;

    *num = 0;

    xpathCtx = xmlXPathNewContext (doc);
    if (!xpathCtx)
	return NULL;

    if (base)
	xpathCtx->node = base;

    xpathObj = xmlXPathEvalExpression (BAD_CAST path, xpathCtx);
    if (!xpathObj)
    {
	xmlXPathFreeContext (xpathCtx);
	return NULL;
    }

    size = (xpathObj->nodesetval) ? xpathObj->nodesetval->nodeNr : 0;
    if (!size)
    {
	xmlXPathFreeObject (xpathObj);
	xmlXPathFreeContext (xpathCtx);
	return NULL;
    }

    rv = malloc (size * sizeof (xmlNode *));
    if (!rv)
    {
	xmlXPathFreeObject (xpathObj);
	xmlXPathFreeContext (xpathCtx);
	return NULL;
    }
    *num = size;

    for (i = 0; i < size; i++)
	rv[i] = xpathObj->nodesetval->nodeTab[i];

    xmlXPathFreeObject (xpathObj);
    xmlXPathFreeContext (xpathCtx);

    return rv;
}

static char *
getStringFromPath (xmlDoc * doc, xmlNode * base, char *path)
{
    char *rv = getStringFromXPath (doc, base, path);

    if (!rv && globalMetadata && base)
    {
	char *gPath;
	char *bPath = getGenericNodePath (base);
	if (!bPath)
	    return NULL;

	asprintf (&gPath, "%s/%s", bPath, path);
	if (gPath)
	{
	    rv = getStringFromXPath (globalMetadata, NULL, gPath);
	    free (gPath);
	}

     	free (bPath);
    }
    return rv;
}

static xmlNode **
getNodesFromPath (xmlDoc * doc, xmlNode * base, char *path, int *num)
{

    xmlNode **rv = getNodesFromXPath (doc, base, path, num);

    if (!*num && globalMetadata && base)
    {
	char *gPath;
	char *bPath = getGenericNodePath (base);

	if (!bPath)
	{
	    free (rv);
	    return NULL;
	}

	asprintf (&gPath, "%s/%s", bPath, path);
	if (gPath)
	{
	    rv = getNodesFromXPath (globalMetadata, NULL, gPath, num);
	    free (gPath);
	}

	free (bPath);
    }
    return rv;
}

static xmlNode **
getNodesFromPathGlobal (xmlDoc * doc, xmlNode * base, char *path, int *num)
{
    xmlNode **rv = NULL;

    if (globalMetadata && base)
    {
	char *gPath;
	char *bPath = getGenericNodePath (base);

	if (!bPath)
	    return NULL;

	asprintf (&gPath, "%s/%s", bPath, path);
	if (gPath)
	{
	    rv = getNodesFromXPath (globalMetadata, NULL, gPath, num);
	    free (gPath);
	}

	free (bPath);
    }

    if (!*num)
	rv = getNodesFromXPath (doc, base, path, num);

    return rv;
}

static Bool
nodeExists (xmlNode * node, char *path)
{
    xmlNode **nodes = NULL;
    int num;
    nodes = getNodesFromPath (node->doc, node, path, &num);

    if (num)
    {
	free (nodes);
	return TRUE;
    }

    return FALSE;
}

static char *
stringFromNodeDef (xmlNode * node, char *path, char *def)
{
    char *val;
    char *rv = NULL;

    val = getStringFromPath (node->doc, node, path);

    if (val)
    {
	rv = strdup (val);
	free (val);
    }
    else if (def)
	rv = strdup (def);

    return rv;
}

static char *
stringFromNodeDefTrans (xmlNode * node, char *path, char *def)
{
    char *lang;
    char newPath[1024];
    char *rv = NULL;

    lang = getenv ("LANG");

    if (!lang || !strlen (lang))
	lang = getenv ("LC_ALL");

    if (!lang || !strlen (lang))
	lang = getenv ("LC_MESSAGES");

    if (!lang || !strlen (lang))
	return stringFromNodeDef (node, path, def);

    snprintf (newPath, 1023, "%s[lang('%s')]", path, lang);
    rv = stringFromNodeDef (node, newPath, NULL);
    if (rv)
	return rv;

    snprintf (newPath, 1023, "%s[lang(substring-before('%s','.'))]", path, lang);
    rv = stringFromNodeDef (node, newPath, NULL);
    if (rv)
	return rv;

    snprintf (newPath, 1023, "%s[lang(substring-before('%s','_'))]", path, lang);
    rv = stringFromNodeDef (node, newPath, NULL);
    if (rv)
	return rv;

    snprintf (newPath, 1023, "%s[lang('C')]", path);
    rv = stringFromNodeDef (node, newPath, NULL);
    if (rv)
	return rv;

    return stringFromNodeDef (node, path, def);
}

static void
ccsAddRestrictionToStringInfo (CCSSettingStringInfo *forString,
			       char *name,
			       char *value)
{
    CCSStrRestriction *restriction;

    restriction = calloc (1, sizeof (CCSStrRestriction));
    if (restriction)
    {
	restriction->name = strdup (name);
	restriction->value = strdup (value);
	forString->restriction =
	    ccsStrRestrictionListAppend (forString->restriction,
					 restriction);
    }
}

static void
ccsAddRestrictionToStringExtension (CCSStrExtension *ext,
				    char *name,
				    char *value)
{
    CCSStrRestriction *restriction;

    restriction = calloc (1, sizeof (CCSStrRestriction));
    if (restriction)
    {
	restriction->name = strdup (name);
	restriction->value = strdup (value);
	ext->restriction = ccsStrRestrictionListAppend (ext->restriction,
							restriction);
    }
}

static void
initBoolValue (CCSSettingValue * v, xmlNode * node)
{
    char *value;

    v->value.asBool = FALSE;

    value = getStringFromPath (node->doc, node, "child::text()");

    if (value)
    {
	if (strcasecmp ((char *) value, "true") == 0)
	    v->value.asBool = TRUE;

	free (value);
    }
}

static void
initIntValue (CCSSettingValue * v, CCSSettingInfo * i, xmlNode * node)
{
    char *value;

    v->value.asInt = (i->forInt.min + i->forInt.max) / 2;

    value = getStringFromPath (node->doc, node, "child::text()");

    if (value)
    {
	int val = strtol ((char *) value, NULL, 0);

	if (val >= i->forInt.min && val <= i->forInt.max)
	    v->value.asInt = val;

	free (value);
    }
}

static void
initFloatValue (CCSSettingValue * v, CCSSettingInfo * i, xmlNode * node)
{
    char *value;
    char *loc;

    v->value.asFloat = (i->forFloat.min + i->forFloat.max) / 2;

    loc = setlocale (LC_NUMERIC, NULL);
    setlocale (LC_NUMERIC, "C");
    value = getStringFromPath (node->doc, node, "child::text()");

    if (value)
    {
	float val = strtod ((char *) value, NULL);

	if (val >= i->forFloat.min && val <= i->forFloat.max)
	    v->value.asFloat = val;

	free (value);
    }
    setlocale (LC_NUMERIC, loc);
}

static void
initStringValue (CCSSettingValue * v, CCSSettingInfo * i, xmlNode * node)
{
    char *value;

    value = getStringFromPath (node->doc, node, "child::text()");

    if (value)
    {
	free (v->value.asString);
	v->value.asString = strdup (value);
	free (value);
    }
    else
	v->value.asString = strdup ("");
}

static void
initColorValue (CCSSettingValue * v, xmlNode * node)
{
    char *value;

    memset (&v->value.asColor, 0, sizeof (v->value.asColor));
    v->value.asColor.color.alpha = 0xffff;

    value = getStringFromPath (node->doc, node, "red/child::text()");
    if (value)
    {
	int color = strtol ((char *) value, NULL, 0);

	v->value.asColor.color.red = MAX (0, MIN (0xffff, color));
	free (value);
    }

    value = getStringFromPath (node->doc, node, "green/child::text()");
    if (value)
    {
	int color = strtol ((char *) value, NULL, 0);

	v->value.asColor.color.green = MAX (0, MIN (0xffff, color));
	free (value);
    }

    value = getStringFromPath (node->doc, node, "blue/child::text()");
    if (value)
    {
	int color = strtol ((char *) value, NULL, 0);

	v->value.asColor.color.blue = MAX (0, MIN (0xffff, color));
	free (value);
    }

    value = getStringFromPath (node->doc, node, "alpha/child::text()");
    if (value)
    {
	int color = strtol (value, NULL, 0);

	v->value.asColor.color.alpha = MAX (0, MIN (0xffff, color));
	free (value);
    }
}

static void
initMatchValue (CCSSettingValue * v, xmlNode * node)
{
    char *value;

    value = getStringFromPath (node->doc, node, "child::text()");
    if (value)
    {
	free (v->value.asMatch);
	v->value.asMatch = strdup (value);
	free (value);
    }
    else
	v->value.asMatch = strdup ("");
}

static void
initKeyValue (CCSSettingValue * v, CCSSettingInfo * i, xmlNode * node)
{
    char *value;

    memset (&v->value.asKey, 0, sizeof (v->value.asKey));

    value = getStringFromPath (node->doc, node, "child::text()");
    if (value)
    {
	if (strcasecmp (value, "disabled"))
	{
	    ccsStringToKeyBinding (value, &v->value.asKey);
	}
	free (value);
    }
}

static void
initButtonValue (CCSSettingValue * v, CCSSettingInfo * i, xmlNode * node)
{
    char *value;

    memset (&v->value.asButton, 0, sizeof (v->value.asButton));

    value = getStringFromPath (node->doc, node, "child::text()");
    if (value)
    {
	if (strcasecmp (value, "disabled"))
	{
	    ccsStringToButtonBinding (value, &v->value.asButton);
	}
	free (value);
    }
}

static void
initEdgeValue (CCSSettingValue * v, CCSSettingInfo * i, xmlNode * node)
{
    xmlNode **nodes;
    char *value;
    int j, k, num;

    v->value.asEdge = 0;

    static char *edge[] = {
	"Left",
	"Right",
	"Top",
	"Bottom",
    	"TopLeft",
	"TopRight",
	"BottomLeft",
	"BottomRight"
    };

    nodes = getNodesFromPath (node->doc, node, "edge", &num);

    for (k = 0; k < num; k++)
    {
	value = getStringFromPath (node->doc, nodes[k], "@name");
	if (value)
	{
	    for (j = 0; j < sizeof (edge) / sizeof (edge[0]); j++)
	    {
		if (strcasecmp ((char *) value, edge[j]) == 0)
		    v->value.asEdge |= (1 << j);
	    }
	    free (value);
	}
    }
    if (num)
	free (nodes);
}

static void
initBellValue (CCSSettingValue * v, CCSSettingInfo * i, xmlNode * node)
{
    char *value;

    v->value.asBell = FALSE;

    value = getStringFromPath (node->doc, node, "child::text()");
    if (value)
    {
	if (!strcasecmp (value, "true"))
	    v->value.asBell = TRUE;
	free (value);
    }
}

static void
initListValue (CCSSettingValue * v, CCSSettingInfo * i, xmlNode * node)
{
    xmlNode **nodes;
    int num, j;

    nodes = getNodesFromPath (node->doc, node, "value", &num);
    if (num)
    {
	for (j = 0; j < num; j++)
	{
	    CCSSettingValue *val;
	    val = calloc (1, sizeof (CCSSettingValue));
	    if (!val)
		continue;

	    val->parent = v->parent;
	    val->isListChild = TRUE;

	    switch (i->forList.listType)
	    {
	    case TypeBool:
		initBoolValue (val, nodes[j]);
		break;
	    case TypeInt:
		initIntValue (val, i->forList.listInfo, nodes[j]);
		break;
	    case TypeFloat:
		initFloatValue (val, i->forList.listInfo, nodes[j]);
		break;
	    case TypeString:
		initStringValue (val, i->forList.listInfo, nodes[j]);
		break;
	    case TypeColor:
		initColorValue (val, nodes[j]);
		break;
	    case TypeKey:
		initKeyValue (val, i->forList.listInfo, nodes[j]);
		break;
	    case TypeButton:
		initButtonValue (val, i->forList.listInfo, nodes[j]);
		break;
	    case TypeEdge:
		initEdgeValue (val, i->forList.listInfo, nodes[j]);
		break;
	    case TypeBell:
		initBellValue (val, i->forList.listInfo, nodes[j]);
		break;
	    case TypeMatch:
		initMatchValue (val, nodes[j]);
	    default:
		break;
	    }
	    v->value.asList = ccsSettingValueListAppend (v->value.asList, val);
	}
	free (nodes);
    }
}

static void
initIntInfo (CCSSettingInfo * i, xmlNode * node)
{
    xmlNode **nodes;
    char *value;
    int num, j;
    i->forInt.min = MINSHORT;
    i->forInt.max = MAXSHORT;
    i->forInt.desc = NULL;

    value = getStringFromPath (node->doc, node, "min/child::text()");
    if (value)
    {
	int val = strtol (value, NULL, 0);
	i->forInt.min = val;
	free (value);
    }

    value = getStringFromPath (node->doc, node, "max/child::text()");
    if (value)
    {
	int val = strtol (value, NULL, 0);
	i->forInt.max = val;
	free (value);
    }

    if (!basicMetadata)
    {
	nodes = getNodesFromPath (node->doc, node, "desc", &num);
	if (num)
	{
	    for (j = 0; j < num; j++)
	    {
		value = getStringFromPath (node->doc, nodes[j],
					   "value/child::text()");
		if (value)
		{
		    int val = strtol (value, NULL, 0);
		    free (value);

		    if (val >= i->forInt.min && val <= i->forInt.max)
		    {
			value = stringFromNodeDefTrans (nodes[j],
							"name/child::text()", 
							NULL);
			if (value)
			{
			    CCSIntDesc *intDesc;

			    intDesc = calloc (1, sizeof (CCSIntDesc));
			    if (intDesc)
			    {
				intDesc->name = strdup (value);
				intDesc->value = val;
				i->forInt.desc =
				    ccsIntDescListAppend (i->forInt.desc,
							  intDesc);
				free (value);
			    }
			}
		    }
		}
	    }
	    free (nodes);
	}
    }
}

static void
initFloatInfo (CCSSettingInfo * i, xmlNode * node)
{
    char *value;
    char *loc;

    i->forFloat.min = MINSHORT;
    i->forFloat.max = MAXSHORT;
    i->forFloat.precision = 0.1f;

    loc = setlocale (LC_NUMERIC, NULL);
    setlocale (LC_NUMERIC, "C");
    value = getStringFromPath (node->doc, node, "min/child::text()");
    if (value)
    {
	float val = strtod (value, NULL);
	i->forFloat.min = val;
	free (value);
    }

    value = getStringFromPath (node->doc, node, "max/child::text()");
    if (value)
    {
	float val = strtod (value, NULL);
	i->forFloat.max = val;
	free (value);
    }

    value = getStringFromPath (node->doc, node, "precision/child::text()");
    if (value)
    {
	float val = strtod (value, NULL);
	i->forFloat.precision = val;
	free (value);
    }

    setlocale (LC_NUMERIC, loc);
}

static void
initStringInfo (CCSSettingInfo * i, xmlNode * node)
{
    xmlNode **nodes;
    char *name;
    char *value;
    int num, j;
    i->forString.restriction = NULL;
    i->forString.sortStartsAt = -1;
    i->forString.extensible = FALSE;

    if (nodeExists (node, "extensible"))
    {
	i->forString.extensible = TRUE;
    }

    nodes = getNodesFromPath (node->doc, node, "sort", &num);

    if (num)
    {
	int val = 0; /* Start sorting at 0 unless otherwise specified. */

	value = getStringFromPath (node->doc, nodes[0], "@start");
	if (value)
	{
	    /* Custom starting value specified. */
	    val = strtol (value, NULL, 0);
	    if (val < 0)
		val = 0;
	    free (value);
	}
	i->forString.sortStartsAt = val;

	free (nodes);
    }

    if (!basicMetadata)
    {
	nodes = getNodesFromPath (node->doc, node, "restriction", &num);
	if (num)
	{
	    for (j = 0; j < num; j++)
	    {
		value = getStringFromPath (node->doc, nodes[j],
					   "value/child::text()");
		if (value)
		{
		    name = stringFromNodeDefTrans (nodes[j],
						   "name/child::text()",
						   NULL);
		    if (name)
		    {
			ccsAddRestrictionToStringInfo (&i->forString,
						       name, value);
			free (name);
		    }
		    free (value);
		}
	    }
	    free (nodes);
	}
    }
}

static void
initListInfo (CCSSettingInfo * i, xmlNode * node)
{
    char *value;
    CCSSettingInfo *info;

    i->forList.listType = TypeBool;
    i->forList.listInfo = NULL;

    value = getStringFromPath (node->doc, node, "type/child::text()");

    if (!value)
	return;

    i->forList.listType = getOptionType (value);

    free (value);

    switch (i->forList.listType)
    {
    case TypeInt:
	{
	    info = calloc (1, sizeof (CCSSettingInfo));
	    if (info)
		initIntInfo (info, node);
	    i->forList.listInfo = info;
	}
	break;
    case TypeFloat:
	{
	    info = calloc (1, sizeof (CCSSettingInfo));
	    if (info)
		initFloatInfo (info, node);
	    i->forList.listInfo = info;
	}
	break;
    case TypeString:
	{
	    info = calloc (1, sizeof (CCSSettingInfo));
	    if (info)
		initStringInfo (info, node);
	    i->forList.listInfo = info;
	}
	break;
    default:
	break;
    }
}

static void
initActionInfo (CCSSettingInfo * i, xmlNode * node)
{
    char *value;

    i->forAction.internal = FALSE;

    value = getStringFromPath (node->doc, node, "internal/child::text()");
    if (value)
    {
	if (strcasecmp (value, "true") == 0)
	    i->forAction.internal = TRUE;
	free (value);
	return;
    }
    if (nodeExists (node, "internal"))
	i->forAction.internal = TRUE;
}

static void
addOptionForPlugin (CCSPlugin * plugin,
		    char * name,
		    char * type,
		    Bool isScreen,
		    unsigned int screen,
		    xmlNode * node)
{
    xmlNode **nodes;
    int num = 0;
    CCSSetting *setting;

    if (ccsFindSetting (plugin, name, isScreen, screen))
    {
	fprintf (stderr, "[ERROR]: Option \"%s\" already defined\n", name);
	return;
    }

    if (getOptionType (type) == TypeNum)
	return;

    setting = calloc (1, sizeof (CCSSetting));
    if (!setting)
	return;

    setting->parent = plugin;
    setting->isScreen = isScreen;
    setting->screenNum = screen;
    setting->isDefault = TRUE;
    setting->name = strdup (name);

    if (!basicMetadata)
    {
	setting->shortDesc =
	    stringFromNodeDefTrans (node, "short/child::text()", name);
	setting->longDesc =
	    stringFromNodeDefTrans (node, "long/child::text()", "");
	setting->hints = stringFromNodeDef (node, "hints/child::text()", "");
	setting->group =
	    stringFromNodeDefTrans (node, "ancestor::group/short/child::text()",
				    "");
	setting->subGroup =
	    stringFromNodeDefTrans (node,
				    "ancestor::subgroup/short/child::text()",
				    "");
    }
    else
    {
	setting->shortDesc = strdup (name);
	setting->longDesc  = strdup ("");
	setting->hints     = strdup ("");
	setting->group     = strdup ("");
	setting->subGroup  = strdup ("");
    }

    setting->type = getOptionType (type);
    setting->value = &setting->defaultValue;
    setting->defaultValue.parent = setting;

    switch (setting->type)
    {
    case TypeInt:
	initIntInfo (&setting->info, node);
	break;
    case TypeFloat:
	initFloatInfo (&setting->info, node);
	break;
    case TypeString:
	initStringInfo (&setting->info, node);
	break;
    case TypeList:
	initListInfo (&setting->info, node);
	break;
    case TypeKey:
    case TypeButton:
    case TypeEdge:
    case TypeBell:
	initActionInfo (&setting->info, node);
	break;
    default:
	break;
    }

    nodes = getNodesFromPathGlobal (node->doc, node, "default", &num);
    if (num)
    {
	switch (setting->type)
	{
	case TypeInt:
	    initIntValue (&setting->defaultValue, &setting->info, nodes[0]);
	    break;
	case TypeBool:
	    initBoolValue (&setting->defaultValue, nodes[0]);
	    break;
	case TypeFloat:
	    initFloatValue (&setting->defaultValue, &setting->info, nodes[0]);
	    break;
	case TypeString:
	    initStringValue (&setting->defaultValue, &setting->info, nodes[0]);
	    break;
	case TypeColor:
	    initColorValue (&setting->defaultValue, nodes[0]);
	    break;
	case TypeKey:
	    initKeyValue (&setting->defaultValue, &setting->info, nodes[0]);
	    break;
	case TypeButton:
	    initButtonValue (&setting->defaultValue, &setting->info, nodes[0]);
	    break;
	case TypeEdge:
	    initEdgeValue (&setting->defaultValue, &setting->info, nodes[0]);
	    break;
	case TypeBell:
	    initBellValue (&setting->defaultValue, &setting->info, nodes[0]);
	    break;
	case TypeMatch:
	    initMatchValue (&setting->defaultValue, nodes[0]);
	    break;
	case TypeList:
	    initListValue (&setting->defaultValue, &setting->info, nodes[0]);
	    break;
	default:
	    break;
	}
    }
    else
    {
	/* if we have no set defaults, we have at least to set
	   the string defaults to empty strings */
	switch (setting->type)
	{
	case TypeString:
	    setting->defaultValue.value.asString = strdup ("");
	    break;
	case TypeMatch:
	    setting->defaultValue.value.asMatch = strdup ("");
	    break;
	default:
	    break;
	}
    }

    if (nodes)
	free (nodes);

    //	printSetting (setting);
    PLUGIN_PRIV (plugin);

    pPrivate->settings = ccsSettingListAppend (pPrivate->settings, setting);
}

static void
addOptionFromXMLNode (CCSPlugin * plugin, xmlNode * node)
{
    char *name;
    char *type;
    char *readonly;
    Bool screen;
    int i;

    if (!node)
	return;

    name = getStringFromXPath (node->doc, node, "@name");

    type = getStringFromXPath (node->doc, node, "@type");

    readonly = getStringFromXPath (node->doc, node, "@read_only");

    if (!name || !strlen (name) || !type || !strlen (type) ||
	(readonly && !strcmp (readonly, "true")) )
    {
	if (name)
	    free (name);
	if (type)
	    free (type);
	if (readonly)
	    free (readonly);

	return;
    }

    screen = nodeExists (node, "ancestor::screen");
    if (screen)
    {
	for (i = 0; i < plugin->context->numScreens; i++)
	    addOptionForPlugin (plugin, name, type, TRUE,
				plugin->context->screens[i], node);
    }
    else
	addOptionForPlugin (plugin, name, type, FALSE, 0, node);

    free (name);
    free (type);

    if (readonly)
	free (readonly);
}

static void
initOptionsFromRootNode (CCSPlugin * plugin, xmlNode * node)
{
    xmlNode **nodes;
    int num, i;
    nodes = getNodesFromPath (node->doc, node, ".//option", &num);

    if (num)
    {
	for (i = 0; i < num; i++)
	    addOptionFromXMLNode (plugin, nodes[i]);

	free (nodes);
    }
}

static void
addStringsFromPath (CCSStringList * list, char * path, xmlNode * node)
{
    xmlNode **nodes;
    int num, i;
    nodes = getNodesFromPath (node->doc, node, path, &num);

    if (num)
    {
	for (i = 0; i < num; i++)
	{
	    char *value = stringFromNodeDef (nodes[i], "child::text()", NULL);

	    if (value && strlen (value))
		*list = ccsStringListAppend (*list, value);

	    if (value && !strlen (value))
		free (value);
	}

	free (nodes);
    }
}

static void
addStringExtensionFromXMLNode (CCSPlugin * plugin, xmlNode * node)
{
    xmlNode **nodes;
    int num, j;
    CCSStrExtension *extension;
    char *name;
    char *value;
    char *isDisplay;

    extension = calloc (1, sizeof (CCSStrExtension));
    if (!extension)
	return;

    isDisplay = getStringFromXPath (node->doc, node, "@display");

    extension->isScreen = !(isDisplay && !strcmp (isDisplay, "true"));

    if (isDisplay)
	free (isDisplay);

    extension->restriction = NULL;

    extension->basePlugin = getStringFromPath (node->doc, node, "@base_plugin");
    if (!extension->basePlugin)
	extension->basePlugin = strdup ("");

    addStringsFromPath (&extension->baseSettings, "base_option", node);

    nodes = getNodesFromPath (node->doc, node, "restriction", &num);
    if (!num)
    {
	free (extension);
	return;
    }

    for (j = 0; j < num; j++)
    {
	value = getStringFromPath (node->doc, nodes[j], "value/child::text()");
	if (value)
	{
	    name = stringFromNodeDefTrans (nodes[j], "name/child::text()",
					   NULL);
	    if (name)
	    {
		ccsAddRestrictionToStringExtension (extension, name, value);
		free (name);
	    }
	    free (value);
	}
    }
    free (nodes);

    PLUGIN_PRIV (plugin);

    pPrivate->stringExtensions =
	ccsStrExtensionListAppend (pPrivate->stringExtensions, extension);
}

static void
initStringExtensionsFromRootNode (CCSPlugin * plugin, xmlNode * node)
{
    xmlNode **nodes;
    int num, i;
    nodes = getNodesFromPath (node->doc, node, ".//extension", &num);

    if (num)
    {
	for (i = 0; i < num; i++)
	    addStringExtensionFromXMLNode (plugin, nodes[i]);

	free (nodes);
    }
}

static void
initRulesFromRootNode (CCSPlugin * plugin, xmlNode * node)
{
    addStringsFromPath (&plugin->providesFeature, "feature", node);

    addStringsFromPath (&plugin->loadAfter,
			"deps/relation[@type = 'after']/plugin", node);
    addStringsFromPath (&plugin->loadBefore,
			"deps/relation[@type = 'before']/plugin", node);
    addStringsFromPath (&plugin->requiresPlugin,
			"deps/requirement/plugin", node);
    addStringsFromPath (&plugin->requiresFeature,
			"deps/requirement/feature", node);
    addStringsFromPath (&plugin->conflictPlugin,
			"deps/conflict/plugin", node);
    addStringsFromPath (&plugin->conflictFeature,
			"deps/conflict/feature", node);
}

static void
addPluginFromXMLNode (CCSContext * context, xmlNode * node, char *file)
{
    char *name;
    CCSPlugin *plugin;
    CCSPluginPrivate *pPrivate;

    if (!node)
	return;

    name = getStringFromXPath (node->doc, node, "@name");

    if (!name || !strlen (name))
    {
	if (name)
	    free (name);
	return;
    }

    if (ccsFindPlugin (context, name))
    {
	free (name);
	return;
    }

    if (!strcmp (name, "ini") || !strcmp (name, "gconf") ||
	!strcmp (name, "ccp") || !strcmp (name, "kconfig"))
    {
	free (name);
	return;
    }

    plugin = calloc (1, sizeof (CCSPlugin));
    if (!plugin)
	return;

    pPrivate = calloc (1, sizeof (CCSPluginPrivate));
    if (!pPrivate)
    {
	free (plugin);
	return;
    }

    plugin->ccsPrivate = (void *) pPrivate;

    if (file)
	pPrivate->xmlFile = strdup (file);

    asprintf (&pPrivate->xmlPath, "/compiz/plugin[@name = '%s']", name);
    plugin->context = context;
    plugin->name = strdup (name);

    if (!basicMetadata)
    {
	plugin->shortDesc =
	    stringFromNodeDefTrans (node, "short/child::text()", name);
	plugin->longDesc =
	    stringFromNodeDefTrans (node, "long/child::text()",	name);
	plugin->category =
	    stringFromNodeDef (node, "category/child::text()", "");
    }
    else
    {
	plugin->shortDesc = strdup (name);
	plugin->longDesc  = strdup (name);
	plugin->category  = strdup ("");
    }

    initRulesFromRootNode (plugin, node);
    D (D_FULL, "Adding plugin %s (%s)\n", name, plugin->shortDesc);

    context->plugins = ccsPluginListAppend (context->plugins, plugin);
    free (name);
}

static void
addCoreSettingsFromXMLNode (CCSContext * context, xmlNode * node, char *file)
{
    CCSPlugin *plugin;
    CCSPluginPrivate *pPrivate;

    if (!node)
	return;

    if (ccsFindPlugin (context, "core"))
	return;

    plugin = calloc (1, sizeof (CCSPlugin));
    if (!plugin)
	return;

    pPrivate = calloc (1, sizeof (CCSPluginPrivate));
    if (!pPrivate)
    {
	free (plugin);
	return;
    }

    plugin->ccsPrivate = (void *) pPrivate;

    if (file)
	pPrivate->xmlFile = strdup (file);

    pPrivate->xmlPath = strdup ("/compiz/core");
    plugin->context = context;
    plugin->name = strdup ("core");
    plugin->category = strdup ("General");

    if (!basicMetadata)
    {
	plugin->shortDesc =
	    stringFromNodeDefTrans (node, "short/child::text()",
				    "General Options");
	plugin->longDesc =
	    stringFromNodeDefTrans (node, "long/child::text()",
				    "General Compiz Options");
    }
    else
    {
	plugin->shortDesc = strdup ("General Options");
	plugin->longDesc  = strdup ("General Compiz Options");
    }
    
    initRulesFromRootNode (plugin, node);
    D (D_FULL, "Adding core settings (%s)\n", plugin->shortDesc);
    context->plugins = ccsPluginListAppend (context->plugins, plugin);
}

static void
loadPluginsFromXML (CCSContext * context, xmlDoc * doc, char *filename)
{
    xmlNode **nodes;
    int num, i;
    nodes = getNodesFromXPath (doc, NULL, "/compiz/core", &num);

    if (num)
    {
	addCoreSettingsFromXMLNode (context, nodes[0], filename);
	free (nodes);
    }

    nodes = getNodesFromXPath (doc, NULL, "/compiz/plugin", &num);

    if (num)
    {
	for (i = 0; i < num; i++)
	    addPluginFromXMLNode (context, nodes[i], filename);

	free (nodes);
    }
}

static int
pluginNameFilter (const struct dirent *name)
{
    int length = strlen (name->d_name);

    if (length < 7)
	return 0;

    if (strncmp (name->d_name, "lib", 3) ||
	strncmp (name->d_name + length - 3, ".so", 3))
	return 0;

    return 1;
}

static int
pluginXMLFilter (const struct dirent *name)
{
    int length = strlen (name->d_name);

    if (length < 5)
	return 0;

    if (strncmp (name->d_name + length - 4, ".xml", 4))
	return 0;

    return 1;
}

static void
loadPluginsFromXMLFile (CCSContext * context, char *name)
{
    xmlDoc *doc = NULL;
    FILE *fp = fopen (name, "r");

    if (fp)
    {
	fclose (fp);
	doc = xmlReadFile (name, NULL, 0);
	if (doc)
	    loadPluginsFromXML (context, doc, name);
	xmlFreeDoc (doc);
    }
}

static void
loadPluginsFromXMLFiles (CCSContext * context, char *path)
{

    struct dirent **nameList;
    char *name;
    int nFile, i;

    if (!path)
	return;

    nFile = scandir (path, &nameList, pluginXMLFilter, NULL);

    if (nFile <= 0)
	return;

    for (i = 0; i < nFile; i++)
    {
	asprintf (&name, "%s/%s", path, nameList[i]->d_name);
	free (nameList[i]);

	if (name)
	{
	    loadPluginsFromXMLFile (context, name);
    	    free (name);
	}
    }
    free (nameList);
}

static void
addPluginNamed (CCSContext * context, char *name)
{
    CCSPlugin *plugin;
    CCSPluginPrivate *pPrivate;

    if (ccsFindPlugin (context, name))
	return;

    if (!strcmp (name, "ini") || !strcmp (name, "gconf") ||
	!strcmp (name, "ccp") || !strcmp (name, "kconfig"))
	return;

    if (globalMetadata)
    {
	xmlNode **nodes;
	int num, i;
	char *path;
	asprintf (&path, "/compiz/plugin[@name = '%s']", name);

	nodes = getNodesFromPath (globalMetadata, NULL, path, &num);
	free (path);

	if (num)
	{
	    for (i = 0; i < num; i++)
		addPluginFromXMLNode (context, nodes[i], NULL);

	    free (nodes);

	    return;
	}
    }

    plugin = calloc (1, sizeof (CCSPlugin));
    if (!plugin)
	return;

    pPrivate = calloc (1, sizeof (CCSPluginPrivate));
    if (!pPrivate)
    {
	free (plugin);
	return;
    }

    plugin->ccsPrivate = (void *) pPrivate;

    plugin->context = context;
    plugin->name = strdup (name);

    D (D_FULL, "Adding plugin named %s\n", name);

    if (!plugin->shortDesc)
	plugin->shortDesc = strdup (name);
    if (!plugin->longDesc)
	plugin->longDesc = strdup (name);
    if (!plugin->category)
	plugin->category = strdup ("");

    pPrivate->loaded = TRUE;
    collateGroups (pPrivate);
    context->plugins = ccsPluginListAppend (context->plugins, plugin);
}

static void
loadPluginsFromName (CCSContext * context, char *path)
{
    struct dirent **nameList;
    int nFile, i;

    if (!path)
	return;

    nFile = scandir (path, &nameList, pluginNameFilter, NULL);
    if (nFile <= 0)
	return;

    for (i = 0; i < nFile; i++)
    {
	char name[1024];
	sscanf (nameList[i]->d_name, "lib%s", name);
	if (strlen (name) > 3)
	    name[strlen (name) - 3] = 0;
	free (nameList[i]);
	addPluginNamed (context, name);
    }
    free (nameList);
}

Bool
ccsLoadPlugin (CCSContext * context, char *name)
{
    char *path = NULL;
    char *home = getenv ("HOME");
    if (home && strlen (home))
    {
	asprintf (&path, "%s/.compiz/metadata/%s.xml", home, name);
	loadPluginsFromXMLFile (context, path);
	free (path);
    }

    asprintf (&path, "%s/%s.xml", METADATADIR, name);
    if (path)
    {
	loadPluginsFromXMLFile (context, path);
	free (path);
    }

    return (ccsFindPlugin (context, name) != NULL);
}

void
ccsLoadPlugins (CCSContext * context)
{
    FILE *fp;
    fp = fopen (GLOBALMETADATA, "r");
    if (fp)
    {
	fclose (fp);
	globalMetadata = xmlReadFile (GLOBALMETADATA, NULL, 0);
    }

    char *home = getenv ("HOME");
    if (home && strlen (home))
    {
	char *homeplugins = NULL;
	asprintf (&homeplugins, "%s/.compiz/metadata", home);
	if (homeplugins)
	{
	    loadPluginsFromXMLFiles (context, homeplugins);
	    free (homeplugins);
	}
    }

    loadPluginsFromXMLFiles (context, METADATADIR);
    if (home && strlen (home))
    {
	char *homeplugins = NULL;
	asprintf (&homeplugins, "%s/.compiz/plugins", home);
	if (homeplugins)
	{
	    loadPluginsFromName (context, homeplugins);
	    free (homeplugins);
	}
    }

    loadPluginsFromName (context, PLUGINDIR);
    if (globalMetadata)
    {
	xmlFreeDoc (globalMetadata);
	globalMetadata = NULL;
    }
}

void
ccsLoadPluginSettings (CCSPlugin * plugin)
{
    xmlDoc *doc = NULL;
    xmlNode **nodes;
    int num;

    PLUGIN_PRIV (plugin);

    if (pPrivate->loaded)
	return;

    pPrivate->loaded = TRUE;
    D (D_FULL, "Initializing %s options...", plugin->name);

    FILE *fp;
    fp = fopen (GLOBALMETADATA, "r");
    if (fp)
    {
	fclose (fp);
	globalMetadata = xmlReadFile (GLOBALMETADATA, NULL, 0);
    }

    if (pPrivate->xmlFile)
    {
	fp = fopen (pPrivate->xmlFile, "r");
	if (fp)
	{
	    fclose (fp);
	    doc = xmlReadFile (pPrivate->xmlFile, NULL, 0);
	}
    }

    nodes = getNodesFromPath (doc, NULL, pPrivate->xmlPath, &num);
    if (num)
    {
	initOptionsFromRootNode (plugin, nodes[0]);
	initStringExtensionsFromRootNode (plugin, nodes[0]);
	free (nodes);
    }

    if (doc)
	xmlFreeDoc (doc);

    if (globalMetadata)
    {
	xmlFreeDoc (globalMetadata);
	globalMetadata = NULL;
    }

    D (D_FULL, "done\n");

    collateGroups (pPrivate);
    ccsReadPluginSettings (plugin);
}
