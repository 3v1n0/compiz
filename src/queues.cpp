/**
 *
 * Compiz group plugin
 *
 * queues.c
 *
 * Copyright : (C) 2006-2007 by Patrick Niklaus, Roi Cohen, Danny Baumann
 * Authors: Patrick Niklaus <patrick.niklaus@googlemail.com>
 *          Roi Cohen       <roico.beryl@gmail.com>
 *          Danny Baumann   <maniac@opencompositing.org>
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
 **/

#include "group.h"

/*
 * functions enqueuing pending notifies
 *
 */

/* forward declaration */

void
GroupWindow::groupEnqueueMoveNotify (int  dx,
				     int  dy,
				     bool immediate,
				     bool sync)
{
    GroupPendingMoves *move;

    GROUP_SCREEN (screen);

    move = (GroupPendingMoves *) malloc (sizeof (GroupPendingMoves));
    if (!move)
	return;

    move->w  = window;
    move->dx = dx;
    move->dy = dy;

    move->immediate = immediate;
    move->sync      = sync;
    move->next      = NULL;

    if (gs->mPendingMoves)
    {
	GroupPendingMoves *temp;
	for (temp = gs->mPendingMoves; temp->next; temp = temp->next);

	temp->next = move;
    }
    else
	gs->mPendingMoves = move;

    if (!gs->mDequeueTimeoutHandle.active ())
    {
	gs->mDequeueTimeoutHandle.start ();
    }
}

void
GroupScreen::groupDequeueSyncs (GroupPendingSyncs *syncs)
{
    GroupPendingSyncs *sync;

    while (syncs)
    {
	sync = syncs;
	syncs = sync->next;
	
	GROUP_WINDOW (sync->w);
	if (gw->mNeedsPosSync)
	{
	    sync->w->syncPosition ();
	    gw->mNeedsPosSync = FALSE;
	}

	free (sync);
    }

}

void
GroupScreen::groupDequeueMoveNotifies ()
{
    GroupPendingMoves *move;
    GroupPendingSyncs *syncs = NULL, *sync;

    mQueued = TRUE;

    while (mPendingMoves)
    {
	move = mPendingMoves;
	mPendingMoves = move->next;

	move->w->move (move->dx, move->dy, move->immediate);
	if (move->sync)
	{
	    sync = (GroupPendingSyncs *) malloc (sizeof (GroupPendingSyncs));
	    if (sync)
	    {
		GROUP_WINDOW (move->w);

		gw->mNeedsPosSync = TRUE;
		sync->w          = move->w;
		sync->next       = syncs;
		syncs            = sync;
	    }
	}
	free (move);
    }

    if (syncs)
    {
	groupDequeueSyncs (syncs);
    }

    mQueued = FALSE;
}

void
GroupWindow::groupEnqueueGrabNotify (int          x,
				     int          y,
				     unsigned int state,
				     unsigned int mask)
{
    GroupPendingGrabs *grab;
    
    GROUP_SCREEN (screen);

    grab = (GroupPendingGrabs *) malloc (sizeof (GroupPendingGrabs));
    if (!grab)
	return;

    grab->w = window;
    grab->x = x;
    grab->y = y;

    grab->state = state;
    grab->mask  = mask;
    grab->next  = NULL;

    if (gs->mPendingGrabs)
    {
	GroupPendingGrabs *temp;
	for (temp = gs->mPendingGrabs; temp->next; temp = temp->next);

	temp->next = grab;
    }
    else
	gs->mPendingGrabs = grab;

    if (!gs->mDequeueTimeoutHandle.active ())
    {
	gs->mDequeueTimeoutHandle.start ();
    }
}

void
GroupScreen::groupDequeueGrabNotifies ()
{
    GroupPendingGrabs *grab;

    mQueued = TRUE;

    while (mPendingGrabs)
    {
	grab = mPendingGrabs;
	mPendingGrabs = mPendingGrabs->next;

	grab->w->grabNotify (grab->x, grab->y,
			     grab->state, grab->mask);

	free (grab);
    }

    mQueued = FALSE;
}

void
GroupWindow::groupEnqueueUngrabNotify ()
{
    GroupPendingUngrabs *ungrab;

    GROUP_SCREEN (screen);

    ungrab = (GroupPendingUngrabs *) malloc (sizeof (GroupPendingUngrabs));

    if (!ungrab)
	return;

    ungrab->w    = window;
    ungrab->next = NULL;

    if (gs->mPendingUngrabs)
    {
	GroupPendingUngrabs *temp;
	for (temp = gs->mPendingUngrabs; temp->next; temp = temp->next);

	temp->next = ungrab;
    }
    else
	gs->mPendingUngrabs = ungrab;

    if (!gs->mDequeueTimeoutHandle.active ())
    {
	gs->mDequeueTimeoutHandle.start ();
    }
}

void
GroupScreen::groupDequeueUngrabNotifies ()
{
    GroupPendingUngrabs *ungrab;

    mQueued = TRUE;

    while (mPendingUngrabs)
    {
	ungrab = mPendingUngrabs;
	mPendingUngrabs = mPendingUngrabs->next;

	ungrab->w->ungrabNotify ();

	free (ungrab);
    }

    mQueued = FALSE;
}

bool
GroupScreen::groupDequeueTimer ()
{
    groupDequeueMoveNotifies ();
    groupDequeueGrabNotifies ();
    groupDequeueUngrabNotifies ();

    return false;
}
