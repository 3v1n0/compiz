
/* Copyright Robert Carr 2007 blablabla GPL etc
 * racarr@beryl-project.org */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <compiz.h>
#include <cairo-xlib-xrender.h>
#include <math.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>


#include "resizeinfo_options.h"

static int displayPrivateIndex;


typedef struct _InfoDisplay 
{
	int screenPrivateIndex;
} InfoDisplay;

typedef struct _InfoLayer
{
  Pixmap pixmap;
  CompTexture texture;
  cairo_surface_t * surface;
  cairo_t * cr;
} InfoLayer;

typedef struct _InfoScreen
{
  
  WindowGrabNotifyProc windowGrabNotify;
  WindowUngrabNotifyProc windowUngrabNotify;
  PaintScreenProc paintScreen;
  PreparePaintScreenProc preparePaintScreen;
  
  CompWindow * pWindow;
  

  float opacity;
  Bool drawing;

  InfoLayer backgroundLayer;
  InfoLayer textLayer;
  
  int savedx;
  int savedy;
} InfoScreen;

#define RESIZE_POPUP_WIDTH 75
#define RESIZE_POPUP_HEIGHT 50

#define GET_INFO_DISPLAY(d) \
((InfoDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define INFO_DISPLAY(d)			   \
    InfoDisplay *id = GET_INFO_DISPLAY (d)

#define GET_INFO_SCREEN(s, id)					 \
    ((InfoScreen *) (s)->privates[(id)->screenPrivateIndex].ptr)

#define INFO_SCREEN(s)							\
    InfoScreen *is = GET_INFO_SCREEN (s, GET_INFO_DISPLAY (s->display))

/* Set up an InfoLayer to build a cairo->opengl texture pipeline */
static void setupCairoLayer (CompScreen *s, InfoLayer * il)
{
	XRenderPictFormat * format;
	Screen * screen;
	int w, h;
	
	screen = ScreenOfDisplay (s->display->display, s->screenNum);
	
	w = RESIZE_POPUP_WIDTH;
	h = RESIZE_POPUP_HEIGHT;
	
	format = XRenderFindStandardFormat (s->display->display,
					    PictStandardARGB32);
	il->pixmap = XCreatePixmap (s->display->display, s->root, w, h, 32);
	
	if (!bindPixmapToTexture (s, &il->texture, il->pixmap, w, h, 32))
	{
	  printf("Bind Pixmap to Texture failure \n");
		XFreePixmap (s->display->display, il->pixmap);

		return;
	}
	
	il->surface = cairo_xlib_surface_create_with_xrender_format (s->display->display,
								     il->pixmap, screen,
								     format, w, h);
	il->cr = cairo_create(il->surface);
	
}


// Draw the window "size" derived from the window hints.
// We calculate width or height - base_width or base_height and divide
// it by the increment in each direction. For windows like terminals
// setting the proper size hints this gives us the number of columns/rows.
void updateTextLayer(CompScreen *s)
{
  INFO_SCREEN(s);

  if (!is->pWindow)
    return;
  
  int base_width = is->pWindow->sizeHints.base_width;
  int base_height = is->pWindow->sizeHints.base_height;
  int width_inc = is->pWindow->sizeHints.width_inc;
  int height_inc = is->pWindow->sizeHints.height_inc;
  int width = is->pWindow->serverWidth;
  int height = is->pWindow->serverHeight;
  int xv = (width-base_width)/width_inc;
  int yv = (height-base_height)/height_inc;
  unsigned short * color = resizeinfoGetTextColor(s->display);
  
  if (height_inc == 1)
    yv = height-1;
  if (width_inc == 1)
    xv = width-1;
  
  if ((is->savedx == xv) && (is->savedy == yv))
    return;

  char * info;
  
  cairo_t * cr = is->textLayer.cr;

  
  PangoLayout * layout;
  PangoFontDescription * font;
  
  int w;
  int h;

  xv++;
  yv++;
  
  // Clear the context.
  cairo_save(cr);
  cairo_set_operator(cr,CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_restore(cr);
  cairo_set_operator(cr,CAIRO_OPERATOR_OVER);

  asprintf(&info, "%d x %d", xv, yv);
  
  font = pango_font_description_new();
  layout = pango_cairo_create_layout(is->textLayer.cr);
  
  pango_font_description_set_family(font,"Sans");
  pango_font_description_set_absolute_size(font, 12 * PANGO_SCALE);
  pango_font_description_set_style(font, PANGO_STYLE_NORMAL);
  pango_font_description_set_weight(font, PANGO_WEIGHT_BOLD);
 
  pango_layout_set_font_description(layout, font);
  
  pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
 
  pango_layout_set_text(layout, info, -1);


  
  pango_layout_get_pixel_size(layout, &w, &h);
  
  cairo_move_to(cr, RESIZE_POPUP_WIDTH/2.0f-w/2.0f, RESIZE_POPUP_HEIGHT/2.0f-h/2.0f);
  
  
  pango_layout_set_width(layout, RESIZE_POPUP_WIDTH * PANGO_SCALE);
  pango_cairo_update_layout(cr, layout);
  
  cairo_set_source_rgba(cr, *(color)/(float)0xffff,*(color+1)/(float)0xffff,*(color+2)/(float)0xffff,*(color+3)/(float)0xffff);
  pango_cairo_show_layout(cr, layout);

  pango_font_description_free(font);
  g_object_unref(layout);

  
  is->savedx = xv;
  is->savedy = yv;

}

#define PI 3.1415
// Draw the background. We draw this on a second layer so that we do
// not have to draw it each time we have to update. Granted we could
// use some cairo trickery for this...
static void drawCairoBackground (CompScreen *s)
{
	INFO_SCREEN(s);
	cairo_t * cr = is->backgroundLayer.cr;
	cairo_pattern_t * pattern;	
	int border = 7.5;
	int height = RESIZE_POPUP_HEIGHT;
	int width = RESIZE_POPUP_WIDTH;
	
#define GET_GRADIENT(NUM) \
	float r##NUM = resizeinfoGetGradient##NUM##Red(s->display) / (float) 0xffff; \
	float g##NUM = resizeinfoGetGradient##NUM##Green(s->display) / (float) 0xffff; \
	float b##NUM = resizeinfoGetGradient##NUM##Blue(s->display) / (float) 0xffff; \
	float a##NUM = resizeinfoGetGradient##NUM##Alpha(s->display) / (float) 0xffff;

	GET_GRADIENT(1);
	GET_GRADIENT(2);
	GET_GRADIENT(3);
	
	
	cairo_set_line_width(cr, 1.0f);

	/* Clear */
	cairo_save(cr);
	cairo_set_operator(cr,CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_restore(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	
	/* Setup Gradient */
 	pattern = cairo_pattern_create_linear(0, 0, width, height);	
	cairo_pattern_add_color_stop_rgba(pattern, 0.00f, r1, g1, b1, a1);
	cairo_pattern_add_color_stop_rgba(pattern, 0.65f, r2, g2, b2, a2);
	cairo_pattern_add_color_stop_rgba(pattern, 0.85f, r3, g3, b3, a3);
	cairo_set_source(cr, pattern);
	
	/* Rounded Rectangle! */
	cairo_arc(cr, border, border, border, PI, 1.5f*PI);
	cairo_arc(cr, border+width-2*border, border, border, 1.5f*PI, 2.0*PI);
	cairo_arc(cr, width-border, height-border, border, 0,  PI/2.0f);
	cairo_arc(cr, border, height-border, border,  PI/2.0f, PI);
	cairo_close_path(cr);
	cairo_fill_preserve(cr);
	
	/* Outline */
	cairo_set_source_rgba(cr, 0.9f,0.9f,0.9f,1.0f);
	cairo_stroke(cr);
	
		cairo_pattern_destroy(pattern);
}

static void gradientChanged (CompDisplay *d, CompOption *o, ResizeinfoDisplayOptions num)
{
  CompScreen *s;
  for (s = d->screens; s; s=s->next)
    drawCairoBackground(s);
}

// Setup the background and draw to it
static void buildCairoBackground (CompScreen *s)
{
  INFO_SCREEN(s);

  setupCairoLayer(s, &is->backgroundLayer);
  drawCairoBackground(s);
  
  resizeinfoSetGradient1Notify(s->display, gradientChanged);
  resizeinfoSetGradient2Notify(s->display, gradientChanged);
  resizeinfoSetGradient3Notify(s->display, gradientChanged);
}

// Set up the layer for text.
static void buildCairoText (CompScreen *s)
{
  INFO_SCREEN(s);
  setupCairoLayer(s, &is->textLayer);
}

//  Handle the fade in /fade out.
static void infoPreparePaintScreen(CompScreen *s, int ms)
{
	INFO_SCREEN(s);
	
	if ((is->opacity != 0.0f) || is->drawing)
	{
		/* Look at me I'm DENNIS! */
		if (is->drawing)
		  is->opacity = MIN(1,is->opacity + 0.05); /* This is me 
							      pretending
							      to be
							      Dennis */
		else
		  is->opacity = MAX(0,is->opacity - 0.05); /* Here too */
	}
	
	
	UNWRAP(is,s,preparePaintScreen);
	(*s->preparePaintScreen)(s,ms);
	WRAP(is,s,preparePaintScreen,infoPreparePaintScreen);
}


// For when we start resizing windows.
static void infoWindowGrabNotify (CompWindow * w,
				  int x,
				  int y,
				  unsigned int state,
				  unsigned int mask)
{
	CompScreen * s = w->screen;
	INFO_SCREEN(s);
	if ((!((w->sizeHints.width_inc == 1) || (w->sizeHints.height_inc == 1))) || resizeinfoGetAlwaysShow(s->display))
	{
		if (mask & CompWindowGrabResizeMask)
		{
			is->pWindow = w;
			is->drawing = TRUE;
			is->opacity = 0.0f;
			
		}
	}
	
	UNWRAP(is,s,windowGrabNotify);
	(*s->windowGrabNotify)(w,x,y,state,mask);
	WRAP(is,s,windowGrabNotify,infoWindowGrabNotify);
}

// For when we finish.
static void infoWindowUngrabNotify (CompWindow * w)
{
	CompScreen *s = w->screen;

	INFO_SCREEN(s);
	
	if (is->pWindow)
	{
		is->drawing = FALSE;
	}
	
	UNWRAP(is, s, windowUngrabNotify);
	(*s->windowUngrabNotify)(w);
	WRAP(is,s,windowUngrabNotify,infoWindowUngrabNotify);
	
}
// Draw a texture at tlx/tly on a quad of RESIZE_POPUP_WIDTH /
// RESIZE_POPUP_HEIGHT with the opacity in InfoScreen.
static void drawLayer (CompScreen *s, int tlx, int tly, CompMatrix matrix, CompTexture *t)
{
  BOX box;
  INFO_SCREEN(s);


  enableTexture(s, t, COMP_TEXTURE_FILTER_FAST);
  matrix.x0 -= tlx * matrix.xx;
  matrix.y0 -= tly * matrix.yy;
  
  box.x1 = tlx;
  box.x2 = tlx + RESIZE_POPUP_WIDTH;
  box.y1 = tly;
  box.y2 = tly + RESIZE_POPUP_HEIGHT;

  // Using the blend function is for lamers.
  glColor4f(is->opacity*1.0f,is->opacity*1.0f,is->opacity*1.0f,is->opacity); 
  glBegin(GL_QUADS);
  glTexCoord2f(COMP_TEX_COORD_X(&matrix, box.x1), COMP_TEX_COORD_Y(&matrix, box.y2));
  glVertex2i(box.x1, box.y2);
  glTexCoord2f(COMP_TEX_COORD_X(&matrix, box.x2), COMP_TEX_COORD_Y(&matrix, box.y2));
  glVertex2i(box.x2, box.y2);
  glTexCoord2f(COMP_TEX_COORD_X(&matrix, box.x2), COMP_TEX_COORD_Y(&matrix, box.y1));
  glVertex2i(box.x2, box.y1);
  glTexCoord2f(COMP_TEX_COORD_X(&matrix, box.x1), COMP_TEX_COORD_Y(&matrix, box.y1));
  glVertex2i(box.x1, box.y1);	
  glEnd();
  glColor4usv(defaultColor);

  disableTexture (s, t);

}

// Draw the popup on the screen.
static Bool
infoPaintScreen (CompScreen *s,
		 const ScreenPaintAttrib *sAttrib,
		 const CompTransform * transform,
		 Region region,
		 int output,
		 unsigned int mask)
{
  Bool status;
  INFO_SCREEN(s);
  
  UNWRAP(is,s,paintScreen);
  status = (*s->paintScreen)(s,sAttrib,transform,region,output,mask);
  WRAP(is,s,paintScreen,infoPaintScreen);
  

  if (((is->drawing) || (is->opacity != 0.0f)) && is->pWindow)
    {

      int tlx = is->pWindow->attrib.x+is->pWindow->attrib.width/2.0f-RESIZE_POPUP_WIDTH/2.0f;
      int tly = is->pWindow->attrib.y+is->pWindow->attrib.height/2.0f-RESIZE_POPUP_HEIGHT/2.0f;
      CompMatrix matrix = is->backgroundLayer.texture.matrix;
      
      glPushMatrix();
      prepareXCoords(s, output, -DEFAULT_Z_CAMERA);
      glDisableClientState(GL_TEXTURE_COORD_ARRAY);
      glEnable(GL_BLEND);
      screenTexEnvMode(s, GL_MODULATE);
  

      drawLayer(s, tlx, tly, matrix, &is->backgroundLayer.texture);
      updateTextLayer(s);
      drawLayer(s, tlx, tly, is->textLayer.texture.matrix, &is->textLayer.texture);
  
      glDisable(GL_BLEND);
      glEnableClientState(GL_TEXTURE_COORD_ARRAY);
      glPopMatrix();

      addWindowDamage(is->pWindow); // For fade effect.
    }
  
  return status;
}


// Setup info display.
static Bool
infoInitDisplay (CompPlugin *p,
		 CompDisplay *d)
{
	InfoDisplay * id;
	id = malloc(sizeof(InfoDisplay));
	if (!id)
		return FALSE;
	id->screenPrivateIndex = allocateScreenPrivateIndex(d);
	d->privates[displayPrivateIndex].ptr = id;

	return TRUE;
}
// Finish info display
static void infoFiniDisplay (CompPlugin * p,
			     CompDisplay *d)
{
	INFO_DISPLAY(d);
	freeScreenPrivateIndex(d, id->screenPrivateIndex);
	
	free(id);
}

// Setup info screen.
static Bool 
infoInitScreen (CompPlugin *p,
		CompScreen *s)
{
	InfoScreen * is;
	INFO_DISPLAY(s->display);
	
	is = malloc(sizeof(InfoScreen));
	if (!is)
		return FALSE;

	is->opacity = 0;
	is->pWindow = 0;
	is->savedx = 0;
	is->savedy = 0;

	initTexture(s, &is->backgroundLayer.texture);
	initTexture(s, &is->textLayer.texture);

	
	WRAP (is, s, windowGrabNotify, infoWindowGrabNotify);
	WRAP (is, s, windowUngrabNotify, infoWindowUngrabNotify);
	WRAP (is, s, preparePaintScreen, infoPreparePaintScreen);
	WRAP (is, s, paintScreen, infoPaintScreen);


	s->privates[id->screenPrivateIndex].ptr = is;
	buildCairoBackground(s);
	buildCairoText(s);
	

	return TRUE;

}

// Free an InfoLayer struct.
static void freeInfoLayer(CompScreen *s, InfoLayer * is)
{
  if (is->cr)
    cairo_destroy(is->cr);
  if (is->surface)
    cairo_surface_destroy(is->surface);
  
  finiTexture(s, &is->texture);
  
  if (is->pixmap)
    XFreePixmap(s->display->display, is->pixmap);

}
// Finish info screen.
static void
infoFiniScreen (CompPlugin *p,
		CompScreen *s)
{
	INFO_SCREEN(s);
	
	freeInfoLayer(s, &is->backgroundLayer);
	freeInfoLayer(s, &is->textLayer);
      
   
	UNWRAP (is, s, windowGrabNotify);
	UNWRAP (is, s, windowUngrabNotify);
	UNWRAP (is, s, preparePaintScreen);
	UNWRAP (is, s, paintScreen);
	
	free (is);
}

// Init the info plugin.
static Bool
infoInit (CompPlugin * p)
{
	displayPrivateIndex = allocateDisplayPrivateIndex();
	if (displayPrivateIndex < 0)
		return FALSE;
	
	return TRUE;
}

// Finish the info plugin.
static void
infoFini (CompPlugin *p)
{
	freeDisplayPrivateIndex(displayPrivateIndex);
}

// For version checking.
static int
infoGetVersion(CompPlugin * plugin,
	       int version)
{
	return ABIVERSION;
}

static CompPluginVTable infoVTable = 
{
	"resizeinfo",
	infoGetVersion,
	0,
	infoInit,
	infoFini,
	infoInitDisplay,
	infoFiniDisplay,
	infoInitScreen,
	infoFiniScreen,
	0, /* InitWindow */
	0, /* FiniWindow */
	0, /* GetDisplayOptions */
	0, /* SetDisplayOption */
	0, /* GetScreenOptions */
	0, /* SetScreenOption */
	0, /* Deps */
	0, /* nDeps */
	0, /* Features */
	0 /* nFeatures */
};

// Get the info vtable.
CompPluginVTable * getCompPluginInfo(void)
{
		return &infoVTable;
}
