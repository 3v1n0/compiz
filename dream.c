/*
 * Animation plugin for compiz/beryl
 *
 * animation.c
 *
 * Copyright : (C) 2006 Erkin Bahceci
 * E-mail    : erkinbah@gmail.com
 *
 * Based on Wobbly and Minimize plugins by
 *           : David Reveman
 * E-mail    : davidr@novell.com>
 *
 * Particle system added by : (C) 2006 Dennis Kasprzyk
 * E-mail                   : onestone@beryl-project.org
 *
 * Beam-Up added by : Florencio Guimaraes
 * E-mail           : florencio@nexcorp.com.br
 *
 * Hexagon tessellator added by : Mike Slegeir
 * E-mail                       : mikeslegeir@mail.utexas.edu>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "animation-internal.h"

static void
fxDreamModelStepObject(CompWindow * w,
		       Model * model, Object * object, float forwardProgress)
{
    float waveAmpMax = MIN(WIN_H(w), WIN_W(w)) * 0.125f;
    float waveWidth = 10.0f;
    float waveSpeed = 7.0f;

    float origx = w->attrib.x + (WIN_W(w) * object->gridPosition.x -
				 w->output.left) * model->scale.x;
    float origy = w->attrib.y + (WIN_H(w) * object->gridPosition.y -
				 w->output.top) * model->scale.y;

    object->position.y = origy;
    object->position.x =
	origx +
	forwardProgress * waveAmpMax * model->scale.x *
	sin(object->gridPosition.y * M_PI * waveWidth +
	    waveSpeed * forwardProgress);

}

Bool fxDreamModelStep(CompScreen * s, CompWindow * w, float time)
{
    if (!defaultAnimStep(s, w, time))
	return FALSE;

    ANIM_SCREEN(s);
    ANIM_WINDOW(w);

    Model *model = aw->model;

    float forwardProgress;
    if ((aw->curWindowEvent == WindowEventMinimize ||
	 aw->curWindowEvent == WindowEventUnminimize) &&
	animGetB(as, aw, ANIM_SCREEN_OPTION_DREAM_Z2TOM))
    {
	float dummy;
	fxZoomAnimProgress(as, aw, &forwardProgress, &dummy, TRUE);
    }
    else
	forwardProgress = defaultAnimProgress(aw);

    int i;
    for (i = 0; i < model->numObjects; i++)
	fxDreamModelStepObject(w,
			       model,
			       &model->objects[i], forwardProgress);

    return TRUE;
}

void
fxDreamUpdateWindowAttrib(AnimScreen * as,
			  CompWindow * w,
			  WindowPaintAttrib * wAttrib)
{
    ANIM_WINDOW(w);

    if ((aw->curWindowEvent == WindowEventMinimize ||
	 aw->curWindowEvent == WindowEventUnminimize) &&
	animGetB(as, aw, ANIM_SCREEN_OPTION_DREAM_Z2TOM))
    {
	fxZoomUpdateWindowAttrib(as, w, wAttrib);
	return;
    }

    float forwardProgress = defaultAnimProgress(aw);

    wAttrib->opacity = (GLushort) (aw->storedOpacity * (1 - forwardProgress));
}
