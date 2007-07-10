/*
 * Copyright (C) 2007 Andrew Riedi <andrewriedi@gmail.com>
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
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * This plug-in for Metacity-like workarounds.
 */

#include <compiz.h>
#include <workarounds_options.h>

static CompMetadata workaroundsMetadata;
static int displayPrivateIndex;

typedef struct _WorkaroundsDisplay {
    int screenPrivateIndex;
} WorkaroundsDisplay;

typedef struct _WorkaroundsScreen {
    int windowPrivateIndex;

    WindowResizeNotifyProc windowResizeNotify;
} WorkaroundsScreen;

typedef struct _WorkaroundsWindow {
} WorkaroundsWindow;

#define GET_WORKAROUNDS_DISPLAY(d) \
    ((WorkaroundsDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define WORKAROUNDS_DISPLAY(d) \
    WorkaroundsDisplay *wd = GET_WORKAROUNDS_DISPLAY (d)

#define GET_WORKAROUNDS_SCREEN(s, wd) \
    ((WorkaroundsScreen *) (s)->privates[(wd)->screenPrivateIndex].ptr)

#define WORKAROUNDS_SCREEN(s) \
    WorkaroundsScreen *ws = GET_WORKAROUNDS_SCREEN (s, \
                            GET_WORKAROUNDS_DISPLAY (s->display))

#define GET_WORKAROUNDS_WINDOW(w, ws) \
    ((WorkaroundsWindow *) (w)->privates[(ws)->windowPrivateIndex].ptr)

#define WORKAROUNDS_WINDOW(w) \
    WorkaroundsWindow *ww = GET_WORKAROUNDS_WINDOW (w, \
                            GET_WORKAROUNDS_SCREEN (w->screen, \
                            GET_WORKAROUNDS_DISPLAY (w->screen->display)))


static void workaroundsDoLegacyApps( CompWindow *w )
{
    unsigned int type;

    type = w->type;

    /* Some code to make Wine and legacy applications work. */
    if ( w->width == w->screen->width && w->height == w->screen->height &&
         !( type & CompWindowTypeDesktopMask ) )
            type = CompWindowTypeFullscreenMask;

    w->type = type;
}

static void workaroundsWindowResizeNotify( CompWindow *w, int dx, int dy,
                                           int dwidth, int dheight )
{
    WORKAROUNDS_SCREEN( w->screen );

    UNWRAP( ws, w->screen, windowResizeNotify );
    (*w->screen->windowResizeNotify) ( w, dx, dy, dwidth, dheight );
    WRAP( ws, w->screen, windowResizeNotify, workaroundsWindowResizeNotify );

    if ( workaroundsGetLegacyApps( w->screen->display ) )
    {
        /* Fix up the window type. */
        recalcWindowType( w );
        workaroundsDoLegacyApps( w );
    }
}

static Bool workaroundsInitDisplay( CompPlugin *plugin, CompDisplay *d )
{
    WorkaroundsDisplay *wd;

    wd = malloc( sizeof( WorkaroundsDisplay) );
    if (!wd)
        return FALSE;

    wd->screenPrivateIndex = allocateScreenPrivateIndex( d );
    if ( wd->screenPrivateIndex < 0 )
    {
        free (wd);
        return FALSE;
    }

    d->privates[displayPrivateIndex].ptr = wd;

    return TRUE;
}

static void workaroundsFiniDisplay( CompPlugin *plugin, CompDisplay *d )
{
    WORKAROUNDS_DISPLAY( d );

    freeScreenPrivateIndex( d, wd->screenPrivateIndex );

    free( wd );
}

static Bool workaroundsInitScreen( CompPlugin *plugin, CompScreen *s )
{
    WorkaroundsScreen *ws;

    WORKAROUNDS_DISPLAY( s->display );

    ws = malloc( sizeof( WorkaroundsScreen ) );
    if (!ws)
        return FALSE;

    ws->windowPrivateIndex = allocateWindowPrivateIndex( s );
    if ( ws->windowPrivateIndex < 0 )
    {
        free (ws);
        return FALSE;
    }

    WRAP( ws, s, windowResizeNotify, workaroundsWindowResizeNotify );

    s->privates[wd->screenPrivateIndex].ptr = ws;

    return TRUE;
}

static void workaroundsFiniScreen( CompPlugin *plugin, CompScreen *s )
{
    WORKAROUNDS_SCREEN( s );

    UNWRAP( ws, s, windowResizeNotify );

    free( ws );
}

static Bool workaroundsInitWindow( CompPlugin *plugin, CompWindow *w )
{
    if ( workaroundsGetLegacyApps( w->screen->display ) )
        workaroundsDoLegacyApps( w );

    return TRUE;
}

static void workaroundsFiniWindow( CompPlugin *plugin, CompWindow *w )
{
}

static Bool workaroundsInit( CompPlugin *plugin )
{
   if ( !compInitPluginMetadataFromInfo( &workaroundsMetadata,
                                         plugin->vTable->name, 0, 0, 0, 0 ) )
        return FALSE;

    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if ( displayPrivateIndex < 0 )
    {
        compFiniMetadata( &workaroundsMetadata );
        return FALSE;
    }

    compAddMetadataFromFile( &workaroundsMetadata, plugin->vTable->name );

    return TRUE;
}

static void workaroundsFini( CompPlugin *plugin )
{
    freeDisplayPrivateIndex( displayPrivateIndex );
    compFiniMetadata( &workaroundsMetadata );
}

static int workaroundsGetVersion( CompPlugin *plugin, int version )
{   
    return ABIVERSION;
}

static CompMetadata *workaroundsGetMetadata( CompPlugin *plugin )
{
    return &workaroundsMetadata;
}

CompPluginVTable workaroundsVTable = 
{
    "workarounds",
    workaroundsGetVersion,
    workaroundsGetMetadata,
    workaroundsInit,
    workaroundsFini,
    workaroundsInitDisplay,
    workaroundsFiniDisplay,
    workaroundsInitScreen,
    workaroundsFiniScreen,
    workaroundsInitWindow,
    workaroundsFiniWindow,
    0, /* GetDisplayOptions */
    0, /* SetDisplayOption */
    0, /* GetScreenOptions */
    0, /* SetScreenOption */
    0, /* Deps */
    0, /* nDeps */
    0, /* Features */
    0  /* nFeatures */
};

CompPluginVTable *getCompPluginInfo( void )
{
    return &workaroundsVTable;
}

