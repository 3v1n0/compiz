/*
 *
 * Compiz show mouse pointer plugin
 *
 * showmouse.c
 *
 * Copyright : (C) 2008 by Dennis Kasprzyk
 * E-mail    : onestone@opencompositing.org
 *
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
 */

#include <cmath>

#include <core/core.h>
#include <core/serialization.h>
#include <composite/composite.h>
#include <opengl/opengl.h>
#include <mousepoll/mousepoll.h>

#include "showmouse_options.h"
#include "showmouse_tex.h"

/* =====================  Particle engine  ========================= */

class Particle
{
    public:

	Particle ();
    
	float life;		/* particle life */
	float fade;		/* fade speed */
	float width;		/* particle width */
	float height;		/* particle height */
	float w_mod;		/* particle size modification during life */
	float h_mod;		/* particle size modification during life */
	float r;		/* red value */
	float g;		/* green value */
	float b;		/* blue value */
	float a;		/* alpha value */
	float x;		/* X position */
	float y;		/* Y position */
	float z;		/* Z position */
	float xi;		/* X direction */
	float yi;		/* Y direction */
	float zi;		/* Z direction */
	float xg;		/* X gravity */
	float yg;		/* Y gravity */
	float zg;		/* Z gravity */
	float xo;		/* orginal X position */
	float yo;		/* orginal Y position */
	float zo;		/* orginal Z position */

	template <class Archive>
	void serialize (Archive &ar, const unsigned int version)
	{
	    ar & life;
	    ar & fade;
	    ar & width;
	    ar & w_mod;
	    ar & h_mod;
	    ar & r;
	    ar & g;
	    ar & b;
	    ar & a;
	    ar & x;
	    ar & y;
	    ar & z;
	    ar & xi;
	    ar & yi;
	    ar & zi;
	    ar & xg;
	    ar & yg;
	    ar & zg;
	    ar & xo;
	    ar & yo;
	    ar & zo;
	};
};

class ParticleCache
{
    public:

	GLfloat *cache;
	unsigned int count;
	unsigned int size;
};

class ParticleSystem
{
    public:

	ParticleSystem (int);
	ParticleSystem ();
	~ParticleSystem ();

	std::vector <Particle> particles;
	float    slowdown;
	GLuint   tex;
	bool     active;
	int      x, y;
	float    darken;
	GLuint   blendMode;

	/* Moved from drawParticles to get rid of spurious malloc's */
	ParticleCache vertices_cache;
	ParticleCache coords_cache;
	ParticleCache colors_cache;
	ParticleCache dcolors_cache;

	template <class Archive>
	void serialize (Archive &ar,
			const unsigned int version)
	{
	    ar & particles;
	    ar & slowdown;
	    ar & active;
	    ar & x;
	    ar & y;
	    ar & darken;
	    ar & blendMode;
	}

	void
	initParticles (int            f_numParticles);

	void
	drawParticles ();

	void
	updateParticles (float          time);

	void
	finiParticles ();
};

class ShowmouseScreen :
    public PluginClassHandler <ShowmouseScreen, CompScreen>,
    public PluginStateWriter <ShowmouseScreen>,
    public ShowmouseOptions,
    public CompositeScreenInterface,
    public GLScreenInterface
{
    public:

	ShowmouseScreen (CompScreen *);
	~ShowmouseScreen ();

	CompositeScreen *cScreen;
	GLScreen	*gScreen;

	CompPoint mousePos;

	bool	       active;

	ParticleSystem ps;

	float	       rot;

	MousePoller    pollHandle;
	
	template <class Archive>
	void serialize (Archive &ar, const unsigned int version)
	{
	    ar & active;
	    ar & ps;
	    ar & rot;
	}
	
	void postLoad ();

	void
	preparePaint (int);

	bool
	glPaintOutput (const GLScreenPaintAttrib &,
		       const GLMatrix		 &,
		       const CompRegion		 &,
		       CompOutput		 *,
		       unsigned int		 );

	void
	donePaint ();

	void
	genNewParticles (int);

	void
	doDamageRegion ();

	void
	positionUpdate (const CompPoint &p);

	bool
	terminate (CompAction         *action,
		   CompAction::State  state,
		   CompOption::Vector options);

	bool
	initiate (CompAction         *action,
		  CompAction::State  state,
		  CompOption::Vector options);

};

#define SHOWMOUSE_SCREEN(s)						       \
    ShowmouseScreen *ss = ShowmouseScreen::get (s);

class ShowmousePluginVTable :
    public CompPlugin::VTableForScreen <ShowmouseScreen>
{
    bool init ();
};
