/**
 *
 * Compiz group plugin
 *
 * tab.c
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
 * groupGetCurrentMousePosition
 *
 * Description:
 * Return the current function of the pointer at the given screen.
 * The position is queried trough XQueryPointer directly from the xserver.
 *
 */
bool
GroupScreen::groupGetCurrentMousePosition (int &x, int &y)
{
    MousePoller poller;
    CompPoint pos = poller.getCurrentPosition ();
    
    x = pos.x ();
    y = pos.y ();

    return (x != 0 && y != 0);
}

/*
 * groupGetClippingRegion
 *
 * Description:
 * This function returns a clipping region which is used to clip
 * several events involving window stack such as hover detection
 * in the tab bar or Drag'n'Drop. It creates the clipping region
 * with getting the region of every window above the given window
 * and then adds this region to the clipping region using
 * XUnionRectWithRegion. w->region won't work since it doesn't include
 * the window decoration.
 *
 */
CompRegion
GroupWindow::groupGetClippingRegion ()
{
    CompWindow *cw;
    CompRegion     clip;

    for (cw = window->next; cw; cw = cw->next)
    {
	if (!cw->invisible () && !(cw->state () & CompWindowStateHiddenMask))
	{
	    CompRect rect;
	    CompRegion     buf;

	    rect = CompRect (WIN_REAL_X (cw),
			     WIN_REAL_Y (cw),
			     WIN_REAL_WIDTH (cw),
			     WIN_REAL_HEIGHT (cw));

	    buf = buf.united (rect);
	    clip = buf.united (clip);
	}
    }

    return clip;
}


/*
 * groupClearWindowInputShape
 *
 */
void
GroupWindow::groupClearWindowInputShape (GroupWindowHideInfo *hideInfo)
{
    XRectangle  *rects;
    int         count = 0, ordering;

    rects = XShapeGetRectangles (screen->dpy (), window->id (), ShapeInput,
				 &count, &ordering);

    if (count == 0)
	return;

    /* check if the returned shape exactly matches the window shape -
       if that is true, the window currently has no set input shape */
    if ((count == 1) &&
	(rects[0].x == -window->serverGeometry ().border ()) &&
	(rects[0].y == -window->serverGeometry ().border ()) &&
	(rects[0].width == (window->serverWidth () +
			    window->serverGeometry ().border ())) &&
	(rects[0].height == (window->serverHeight () +
			     window->serverGeometry ().border ())))
    {
	count = 0;
    }

    if (hideInfo->mInputRects)
	XFree (hideInfo->mInputRects);

    hideInfo->mInputRects = rects;
    hideInfo->mNInputRects = count;
    hideInfo->mInputRectOrdering = ordering;

    XShapeSelectInput (screen->dpy (), hideInfo->mShapeWindow, NoEventMask);

    XShapeCombineRectangles (screen->dpy (), hideInfo->mShapeWindow, ShapeInput, 0, 0,
			     NULL, 0, ShapeSet, 0);

    XShapeSelectInput (screen->dpy (), hideInfo->mShapeWindow, ShapeNotify);
}

/*
 * groupSetWindowVisibility
 *
 */
void
GroupWindow::groupSetWindowVisibility (bool visible)
{
    if (!visible && !mWindowHideInfo)
    {
	GroupWindowHideInfo *info;

	mWindowHideInfo = info = (GroupWindowHideInfo *) malloc (sizeof (GroupWindowHideInfo));
	if (!mWindowHideInfo)
	    return;

	info->mInputRects = NULL;
	info->mNInputRects = 0;
	info->mShapeMask = XShapeInputSelected (screen->dpy (), window->id ());

	/* We are a reparenting window manager now, which means that we either
	 * shape the frame window, or if it does not exist, shape the window
	 */
	
	if (window->frame ())
	{
	    info->mShapeWindow = window->frame ();
	}
	else
	    info->mShapeWindow = window->id ();

	groupClearWindowInputShape (info);

	info->mSkipState = window->state () & (CompWindowStateSkipPagerMask |
				              CompWindowStateSkipTaskbarMask);

	window->changeState (window->state () |
			   CompWindowStateSkipPagerMask |
			   CompWindowStateSkipTaskbarMask);
    }
    else if (visible && mWindowHideInfo)
    {
	GroupWindowHideInfo *info = mWindowHideInfo;

	if (info->mNInputRects)
	{
	    XShapeCombineRectangles (screen->dpy (), info->mShapeWindow, ShapeInput, 
				     0, 0, info->mInputRects, info->mNInputRects,
				     ShapeSet, info->mInputRectOrdering);
	}
	else
	{
	    XShapeCombineMask (screen->dpy (), info->mShapeWindow, ShapeInput,
			       0, 0, None, ShapeSet);
	}

	if (info->mInputRects)
	    XFree (info->mInputRects);

	XShapeSelectInput (screen->dpy (), info->mShapeWindow, info->mShapeMask);

	window->changeState ((window->state () &
				 ~(CompWindowStateSkipPagerMask |
				   CompWindowStateSkipTaskbarMask)) |
			      info->mSkipState);

	free (info);
	mWindowHideInfo = NULL;
    }
}

/*
 * groupTabBarTimeout
 *
 * Description:
 * This function is called when the time expired (== timeout).
 * We use this to realize a delay with the bar hiding after tab change.
 * groupHandleAnimation sets up a timer after the animation has finished.
 * This function itself basically just sets the tab bar to a PaintOff status
 * through calling groupSetTabBarVisibility.
 * The PERMANENT mask allows you to force hiding even of
 * PaintPermanentOn tab bars.
 *
 */
bool
GroupScreen::groupTabBarTimeout (GroupSelection *group)
{
    groupTabSetVisibility (group, false, PERMANENT);

    return false;	/* This will free the timer. */
}

/*
 * groupShowDelayTimeout
 *
 */
bool
GroupScreen::groupShowDelayTimeout (GroupSelection *group)
{
    int            mouseX, mouseY;
    CompWindow     *topTab;

    if (!HAS_TOP_WIN (group))
    {
	mShowDelayTimeoutHandle.stop ();
	return false;	/* This will free the timer. */
    }

    topTab = TOP_TAB (group);

    groupGetCurrentMousePosition (mouseX, mouseY);

    groupRecalcTabBarPos (group, mouseX, WIN_REAL_X (topTab),
			  WIN_REAL_X (topTab) + WIN_REAL_WIDTH (topTab));

    groupTabSetVisibility (group, true, 0);

    mShowDelayTimeoutHandle.stop ();
    return false;	/* This will free the timer. */
}

/*
 * groupTabSetVisibility
 *
 * Description:
 * This function is used to set the visibility of the tab bar.
 * The "visibility" is indicated through the PaintState, which
 * can be PaintOn, PaintOff, PaintFadeIn, PaintFadeOut
 * and PaintPermantOn.
 * Currently the mask paramater is mostely used for the PERMANENT mask.
 * This mask affects how the visible parameter is handled, for example if
 * visibule is set to true and the mask to PERMANENT state it will set
 * PaintPermanentOn state for the tab bar. When visibile is false, mask 0
 * and the current state of the tab bar is PaintPermanentOn it won't do
 * anything because its not strong enough to disable a
 * Permanent-State, for those you need the mask.
 *
 */
void
GroupScreen::groupTabSetVisibility (GroupSelection *group,
				    bool           visible,
				    unsigned int   mask)
{
    GroupTabBar *bar;
    CompWindow  *topTab;
    PaintState  oldState;

    if (!group || !group->mWindows.size () || !group->mTabBar || !HAS_TOP_WIN (group))
	return;

    bar = group->mTabBar;
    topTab = TOP_TAB (group);
    oldState = bar->mState;

    /* hide tab bars for invisible top windows */
    if ((topTab->state () & CompWindowStateHiddenMask) || topTab->invisible ())
    {
	bar->mState = PaintOff;
	groupSwitchTopTabInput (group, true);
    }
    else if (visible && bar->mState != PaintPermanentOn && (mask & PERMANENT))
    {
	bar->mState = PaintPermanentOn;
	groupSwitchTopTabInput (group, false);
    }
    else if (visible && bar->mState == PaintPermanentOn && !(mask & PERMANENT))
    {
	bar->mState = PaintOn;
    }
    else if (visible && (bar->mState == PaintOff || bar->mState == PaintFadeOut))
    {
	if (optionGetBarAnimations ())
	{
	    bar->mBgAnimation = AnimationReflex;
	    bar->mBgAnimationTime = optionGetReflexTime () * 1000.0;
	}
	bar->mState = PaintFadeIn;
	groupSwitchTopTabInput (group, false);
    }
    else if (!visible &&
	     (bar->mState != PaintPermanentOn || (mask & PERMANENT)) &&
	     (bar->mState == PaintOn || bar->mState == PaintPermanentOn ||
	      bar->mState == PaintFadeIn))
    {
	bar->mState = PaintFadeOut;
	groupSwitchTopTabInput (group, true);
    }

    if (bar->mState == PaintFadeIn || bar->mState == PaintFadeOut)
	bar->mAnimationTime = (optionGetFadeTime () * 1000) - bar->mAnimationTime;

    if (bar->mState != oldState)
	groupDamageTabBarRegion (group);
}

/*
 * groupGetDrawOffsetForSlot
 *
 * Description:
 * Its used when the draggedSlot is dragged to another viewport.
 * It calculates a correct offset to the real slot position.
 *
 */
void
GroupScreen::groupGetDrawOffsetForSlot (GroupTabBarSlot *slot,
					int &hoffset,
					int &voffset)
{
    CompWindow *w, *topTab;
    int        x, y, vx, vy;
    CompPoint  vp;
    CompWindow::Geometry winGeometry;

    if (!slot || !slot->mWindow)
	return;

    w = slot->mWindow;

    GROUP_WINDOW (w);

    if (slot != mDraggedSlot)
    {
	hoffset = 0;
	voffset = 0;

	return;
    }

    if (HAS_TOP_WIN (gw->mGroup))
	topTab = TOP_TAB (gw->mGroup);
    else if (HAS_PREV_TOP_WIN (gw->mGroup))
	topTab = PREV_TOP_TAB (gw->mGroup);
    else
    {
	hoffset = 0;
	voffset = 0;
	return;
    }

    x = WIN_CENTER_X (topTab) - WIN_WIDTH (w) / 2;
    y = WIN_CENTER_Y (topTab) - WIN_HEIGHT (w) / 2;

    winGeometry = CompWindow::Geometry (x, y, w->serverWidth (),
					      w->serverHeight (),
					w->serverGeometry ().border ());

    screen->viewportForGeometry (winGeometry, vp);

    vx = vp.x ();
    vy = vp.y ();

    hoffset = ((screen->vp ().x () - vx) % screen->vpSize ().width ()) * screen->width ();

    voffset = ((screen->vp ().y () - vy) % screen->vpSize ().height ()) * screen->height ();
}

/*
 * groupHandleHoverDetection
 *
 * Description:
 * This function is called from groupPreparePaintScreen to handle
 * the hover detection. This is needed for the text showing up,
 * when you hover a thumb on the thumb bar.
 *
 * FIXME: we should better have a timer for that ...
 */
void
GroupScreen::groupHandleHoverDetection (GroupSelection *group)
{
    GroupTabBar *bar = group->mTabBar;
    CompWindow  *topTab = TOP_TAB (group);
    int         mouseX, mouseY;
    bool        mouseOnScreen, inLastSlot;

    /* first get the current mouse position */
    mouseOnScreen = groupGetCurrentMousePosition (mouseX, mouseY);

    if (!mouseOnScreen)
	return;

    /* then check if the mouse is in the last hovered slot --
       this saves a lot of CPU usage */
    inLastSlot = bar->mHoveredSlot &&
		 bar->mHoveredSlot->mRegion.contains (CompPoint (mouseX,
								 mouseY));

    if (!inLastSlot)
    {
	CompRegion      clip;
	GroupTabBarSlot *slot;

	bar->mHoveredSlot = NULL;
	clip = GroupWindow::get (topTab)->groupGetClippingRegion ();

	foreach (slot, bar->mSlots)
	{
	    /* We need to clip the slot region with the clip region first.
	       This is needed to respect the window stack, so if a window
	       covers a port of that slot, this part won't be used
	       for in-slot-detection. */
	    CompRegion reg = slot->mRegion.subtracted (clip);

	    if (reg.contains (CompPoint (mouseX, mouseY)))
	    {
		bar->mHoveredSlot = slot;
		break;
	    }
	}

	if (bar->mTextLayer)
	{
	    /* trigger a FadeOut of the text */
	    if ((bar->mHoveredSlot != bar->mTextSlot) &&
		(bar->mTextLayer->mState == PaintFadeIn ||
		 bar->mTextLayer->mState == PaintOn))
	    {
		bar->mTextLayer->mAnimationTime =
		    (optionGetFadeTextTime () * 1000) -
		    bar->mTextLayer->mAnimationTime;
		bar->mTextLayer->mState = PaintFadeOut;
	    }

	    /* or trigger a FadeIn of the text */
	    else if (bar->mTextLayer->mState == PaintFadeOut &&
		     bar->mHoveredSlot == bar->mTextSlot && bar->mHoveredSlot)
	    {
		bar->mTextLayer->mAnimationTime =
		    (optionGetFadeTextTime () * 1000) -
		    bar->mTextLayer->mAnimationTime;
		bar->mTextLayer->mState = PaintFadeIn;
	    }
	}
    }
}

/*
 * groupHandleTabBarFade
 *
 * Description:
 * This function is called from groupPreparePaintScreen
 * to handle the tab bar fade. It checks the animationTime and updates it,
 * so we can calculate the alpha of the tab bar in the painting code with it.
 *
 */
void
GroupScreen::groupHandleTabBarFade (GroupSelection *group,
				    int		   msSinceLastPaint)
{
    GroupTabBar *bar = group->mTabBar;

    bar->mAnimationTime -= msSinceLastPaint;

    if (bar->mAnimationTime < 0)
	bar->mAnimationTime = 0;

    /* Fade finished */
    if (bar->mAnimationTime == 0)
    {
	if (bar->mState == PaintFadeIn)
	{
	    bar->mState = PaintOn;
	}
	else if (bar->mState == PaintFadeOut)
	{
	    bar->mState = PaintOff;

	    if (bar->mTextLayer)
	    {
		/* Tab-bar is no longer painted, clean up
		   text animation variables. */
		bar->mTextLayer->mAnimationTime = 0;
		bar->mTextLayer->mState = PaintOff;
		bar->mTextSlot = bar->mHoveredSlot = NULL;

		group->renderWindowTitle ();
	    }
	}
    }
}

/*
 * groupHandleTextFade
 *
 * Description:
 * This function is called from groupPreparePaintScreen
 * to handle the text fade. It checks the animationTime and updates it,
 * so we can calculate the alpha of the text in the painting code with it.
 *
 */
void
GroupScreen::groupHandleTextFade (GroupSelection *group,
				  int		 msSinceLastPaint)
{
    GroupTabBar     *bar = group->mTabBar;
    GroupCairoLayer *textLayer = bar->mTextLayer;

    /* Fade in progress... */
    if ((textLayer->mState == PaintFadeIn || textLayer->mState == PaintFadeOut) &&
	textLayer->mAnimationTime > 0)
    {
	textLayer->mAnimationTime -= msSinceLastPaint;

	if (textLayer->mAnimationTime < 0)
	    textLayer->mAnimationTime = 0;

	/* Fade has finished. */
	if (textLayer->mAnimationTime == 0)
	{
	    if (textLayer->mState == PaintFadeIn)
		textLayer->mState = PaintOn;

	    else if (textLayer->mState == PaintFadeOut)
		textLayer->mState = PaintOff;
	}
    }

    if (textLayer->mState == PaintOff && bar->mHoveredSlot)
    {
	/* Start text animation for the new hovered slot. */
	bar->mTextSlot = bar->mHoveredSlot;
	textLayer->mState = PaintFadeIn;
	textLayer->mAnimationTime =
	    (optionGetFadeTextTime () * 1000);

	group->renderWindowTitle ();
    }

    else if (textLayer->mState == PaintOff && bar->mTextSlot)
    {
	/* Clean Up. */
	bar->mTextSlot = NULL;
	group->renderWindowTitle ();
    }
}

/*
 * groupHandleTabBarAnimation
 *
 * Description: Handles the different animations for the tab bar defined in
 * GroupAnimationType. Basically that means this function updates
 * tabBar->animation->time as well as checking if the animation is already
 * finished.
 *
 */
void
GroupScreen::groupHandleTabBarAnimation (GroupSelection *group,
			    		 int            msSinceLastPaint)
{
    GroupTabBar *bar = group->mTabBar;

    bar->mBgAnimationTime -= msSinceLastPaint;

    if (bar->mBgAnimationTime <= 0)
    {
	bar->mBgAnimationTime = 0;
	bar->mBgAnimation = AnimationNone;

	group->renderTabBarBackground ();
    }
}
/*
 * groupTabChangeActivateEvent
 *
 * Description: Creates a compiz event to let other plugins know about
 * the starting and ending point of the tab changing animation
 */
void
GroupScreen::groupTabChangeActivateEvent (bool activating)
{
    CompOption::Vector o;

    CompOption opt ("root", CompOption::TypeInt);
    opt.value ().set ((int) screen->root ());

    o.push_back (opt);

    CompOption opt2 ("active", CompOption::TypeBool);
    opt2.value ().set (activating);

    o.push_back (opt2);

    screen->handleCompizEvent ("group", "tabChangeActivate", o);
}

/*
 * groupHandleAnimation
 *
 * Description:
 * This function handles the change animation. It's called
 * from groupHandleChanges. Don't let the changeState
 * confuse you, PaintFadeIn equals with the start of the
 * rotate animation and PaintFadeOut is the end of these
 * animation.
 *
 */
void
GroupScreen::groupHandleAnimation (GroupSelection *group)
{
    if (group->mChangeState == TabChangeOldOut)
    {
	CompWindow      *top = TOP_TAB (group);
	bool            activate;

	/* recalc here is needed (for y value)! */
	groupRecalcTabBarPos (group,
			      (group->mTabBar->mRegion.boundingRect ().x1 () +
			       group->mTabBar->mRegion.boundingRect ().x2 ()) / 2,
			      WIN_REAL_X (top),
			      WIN_REAL_X (top) + WIN_REAL_WIDTH (top));

	group->mChangeAnimationTime += optionGetChangeAnimationTime () * 500;

	if (group->mChangeAnimationTime <= 0)
	    group->mChangeAnimationTime = 0;

	group->mChangeState = TabChangeNewIn;

	activate = !group->mCheckFocusAfterTabChange;
	if (!activate)
	{
/*
	    CompFocusResult focus;
	    focus    = allowWindowFocus (top, NO_FOCUS_MASK, s->x, s->y, 0);
	    activate = focus == CompFocusAllowed;
*/
	}

	if (activate)
	    top->activate ();

	group->mCheckFocusAfterTabChange = false;
    }

    if (group->mChangeState == TabChangeNewIn &&
	group->mChangeAnimationTime <= 0)
    {
	int oldChangeAnimationTime = group->mChangeAnimationTime;

	groupTabChangeActivateEvent (false);

	if (group->mPrevTopTab)
	    GroupWindow::get (PREV_TOP_TAB (group))->groupSetWindowVisibility (false);

	group->mPrevTopTab = group->mTopTab;
	group->mChangeState = NoTabChange;

	if (group->mNextTopTab)
	{
	    GroupTabBarSlot *next = group->mNextTopTab;
	    group->mNextTopTab = NULL;

	    groupChangeTab (next, group->mNextDirection);

	    if (group->mChangeState == TabChangeOldOut)
	    {
		/* If a new animation was started. */
		group->mChangeAnimationTime += oldChangeAnimationTime;
	    }
	}

	if (group->mChangeAnimationTime <= 0)
	{
	    group->mChangeAnimationTime = 0;
	}
	else if (optionGetVisibilityTime () != 0.0f &&
		 group->mChangeState == NoTabChange)
	{
	    groupTabSetVisibility (group, true,
				   PERMANENT | SHOW_BAR_INSTANTLY_MASK);

	    if (group->mTabBar->mTimeoutHandle.active ())
		group->mTabBar->mTimeoutHandle.stop ();

	    group->mTabBar->mTimeoutHandle.setTimes (optionGetVisibilityTime () * 1000,
						   optionGetVisibilityTime () * 1200);

	    group->mTabBar->mTimeoutHandle.setCallback (boost::bind (&GroupScreen::groupTabBarTimeout, this, group));

	    group->mTabBar->mTimeoutHandle.start ();
	}
    }
}

/* adjust velocity for each animation step (adapted from the scale plugin) */
int
GroupWindow::adjustTabVelocity ()
{
    float dx, dy, adjust, amount;
    float x1, y1;

    x1 = mDestination.x;
    y1 = mDestination.y;

    dx = x1 - (mOrgPos.x + mTx);
    adjust = dx * 0.15f;
    amount = fabs (dx) * 1.5f;
    if (amount < 0.5f)
	amount = 0.5f;
    else if (amount > 5.0f)
	amount = 5.0f;

    mXVelocity = (amount * mXVelocity + adjust) / (amount + 1.0f);

    dy = y1 - (mOrgPos.y + mTy);
    adjust = dy * 0.15f;
    amount = fabs (dy) * 1.5f;
    if (amount < 0.5f)
	amount = 0.5f;
    else if (amount > 5.0f)
	amount = 5.0f;

    mYVelocity = (amount * mYVelocity + adjust) / (amount + 1.0f);

    if (fabs (dx) < 0.1f && fabs (mXVelocity) < 0.2f &&
	fabs (dy) < 0.1f && fabs (mYVelocity) < 0.2f)
    {
	mXVelocity = mYVelocity = 0.0f;
	mTx = x1 - window->serverX ();
	mTy = y1 - window->serverY ();

	return 0;
    }
    return 1;
}

void
GroupScreen::finishTabbing (GroupSelection *group)
{
    group->mTabbingState = NoTabbing;
    groupTabChangeActivateEvent (false);

    if (group->mTabBar)
    {
	/* tabbing case - hide all non-toptab windows */
	GroupTabBarSlot *slot;

	foreach (slot, group->mTabBar->mSlots)
	{
	    CompWindow *w = slot->mWindow;
	    if (!w)
		continue;

	    GROUP_WINDOW (w);

	    if (slot == group->mTopTab || (gw->mAnimateState & IS_UNGROUPING))
		continue;

	    gw->groupSetWindowVisibility (false);
	}
	group->mPrevTopTab = group->mTopTab;
    }

    for (CompWindowList::iterator it = group->mWindows.begin ();
	 it != group->mWindows.end ();
	 it++)
    {
	CompWindow *w = *it;
	GROUP_WINDOW (w);

	/* move window to target position */
	mQueued = true;
	w->move (gw->mDestination.x - WIN_X (w),
		 gw->mDestination.y - WIN_Y (w), true);
	mQueued = false;
	w->syncPosition ();

	if (group->mUngroupState == UngroupSingle &&
	    (gw->mAnimateState & IS_UNGROUPING))
	{
	    /* Possibility of stack breakage here, stop here */
	    gw->groupRemoveWindowFromGroup ();
	    it = group->mWindows.end ();	    
	}

	gw->mAnimateState = 0;
	gw->mTx = gw->mTy = gw->mXVelocity = gw->mYVelocity = 0.0f;
    }

    if (group->mUngroupState == UngroupAll)
	group->fini ();
    else
	group->mUngroupState = UngroupNone;
}

/*
 * groupDrawTabAnimation
 *
 * Description:
 * This function is called from groupPreparePaintScreen, to move
 * all the animated windows, with the required animation step.
 * The function goes through all grouped animated windows, calculates
 * the required step using adjustTabVelocity, moves the window,
 * and then checks if the animation is finished for that window.
 *
 */
void
GroupScreen::drawTabAnimation (GroupSelection *group,
			       int	      msSinceLastPaint)
{
    int        steps;
    float      amount, chunk;
    bool       doTabbing;

    amount = msSinceLastPaint * 0.05f * optionGetTabbingSpeed ();
    steps = amount / (0.5f * optionGetTabbingTimestep ());
    if (!steps)
	steps = 1;
    chunk = amount / (float)steps;

    while (steps--)
    {
	doTabbing = false;

	foreach (CompWindow *cw, group->mWindows)
	{
	    if (!cw)
		continue;

	    GROUP_WINDOW (cw);

	    if (!(gw->mAnimateState & IS_ANIMATED))
		continue;

	    if (!gw->adjustTabVelocity ())
	    {
		gw->mAnimateState |= FINISHED_ANIMATION;
		gw->mAnimateState &= ~IS_ANIMATED;
	    }

	    gw->mTx += gw->mXVelocity * chunk;
	    gw->mTy += gw->mYVelocity * chunk;

	    doTabbing |= (gw->mAnimateState & IS_ANIMATED);
	}

	if (!doTabbing)
	{
	    /* tabbing animation finished */
	    finishTabbing (group);
	    break;
	}
    }
}

/*
 * groupUpdateTabBars
 *
 * Description:
 * This function is responsible for showing / unshowing the tab-bars,
 * when the title-bars / tab-bars are hovered.
 * The function is called whenever a new window is entered,
 * checks if the entered window is a window frame (and if the title
 * bar part of that frame was hovered) or if it was the input
 * prevention window of a tab bar, and sets tab-bar visibility
 * according to that.
 *
 */
void
GroupScreen::groupUpdateTabBars (Window enteredWin)
{
    CompWindow     *w = NULL;
    GroupSelection *hoveredGroup = NULL;

    /* do nothing if the screen is grabbed, as the frame might be drawn
       transformed */
    if (!screen->otherGrabExist ("group", "group-drag", NULL))
    {
	/* first check if the entered window is a frame */
	foreach (w, screen->windows ())
	{
	    if (w->frame () == enteredWin)
		break;
	}
    }

    if (w)
    {
	/* is the window the entered frame belongs to inside
	   a tabbed group? if no, it's not interesting for us */
	GROUP_WINDOW (w);

	if (gw->mGroup && gw->mGroup->mTabBar)
	{
	    int mouseX, mouseY;
	    /* it is grouped and tabbed, so now we have to
	       check if we hovered the title bar or the frame */
	    if (groupGetCurrentMousePosition (mouseX, mouseY))
	    {
		CompRect rect;
		CompRegion     reg;

		rect = CompRect (WIN_X (w) - w->input ().left,
				 WIN_Y (w) - w->input ().top,
				 WIN_WIDTH (w) + w->input ().right,
				 WIN_Y (w) - (WIN_Y (w) - w->input ().top));

		reg = reg.united (rect);

		if (reg.contains (CompPoint (mouseX, mouseY)))
		{
		    hoveredGroup = gw->mGroup;
		}
	    }
	}
    }

    /* if we didn't hover a title bar, check if we hovered
       a tab bar (means: input prevention window) */
    if (!hoveredGroup)
    {
	GroupSelection *group;

	foreach (group, mGroups)
	{
	    if (group->mInputPrevention == enteredWin)
	    {
		/* only accept it if the IPW is mapped */
		if (group->mIpwMapped)
		{
		    hoveredGroup = group;
		    break;
		}
	    }
	}
    }

    /* if we found a hovered tab bar different than the last one
       (or left a tab bar), hide the old one */
    if (mLastHoveredGroup && (hoveredGroup != mLastHoveredGroup))
	groupTabSetVisibility (mLastHoveredGroup, false, 0);

    /* if we entered a tab bar (or title bar), show the tab bar */
    if (hoveredGroup && HAS_TOP_WIN (hoveredGroup) &&
	!TOP_TAB (hoveredGroup)->grabbed ())
    {	    
	GroupTabBar *bar = hoveredGroup->mTabBar;

	if (bar && ((bar->mState == PaintOff) || (bar->mState == PaintFadeOut)))
	{
	    int showDelayTime = optionGetTabbarShowDelay () * 1000;

	    /* Show the tab-bar after a delay,
	       only if the tab-bar wasn't fading out. */
	    if (showDelayTime > 0 && (bar->mState == PaintOff))
	    {
		if (mShowDelayTimeoutHandle.active ())
		    mShowDelayTimeoutHandle.stop ();

		mShowDelayTimeoutHandle.setTimes (showDelayTime, showDelayTime * 1.2);

		mShowDelayTimeoutHandle.setCallback (boost::bind (&GroupScreen::groupShowDelayTimeout, this, hoveredGroup));
		mShowDelayTimeoutHandle.start ();
	    }
	    else
		groupShowDelayTimeout (hoveredGroup);
	}
    }

    mLastHoveredGroup = hoveredGroup;
}



/*
 * groupGetConstrainRegion
 *
 */
CompRegion
GroupScreen::groupGetConstrainRegion ()
{
    CompRegion     region;
    CompRect       r;

    for (unsigned int i = 0;i < screen->outputDevs ().size (); i++)
	region = CompRegion (screen->outputDevs ()[i]).united (region);

    foreach (CompWindow *w, screen->windows ())
    {
	if (!w->mapNum ())
	    continue;

	if (w->struts ())
	{
	    r = CompRect (w->struts ()->top.x,
			  w->struts ()->top.y,
			  w->struts ()->top.width,
			  w->struts ()->top.height);

	    region = region.subtracted (r);

	    r = CompRect (w->struts ()->bottom.x,
			  w->struts ()->bottom.y,
			  w->struts ()->bottom.width,
			  w->struts ()->bottom.height);

	    region = region.subtracted (r);

	    r = CompRect (w->struts ()->left.x,
			  w->struts ()->left.y,
			  w->struts ()->left.width,
			  w->struts ()->left.height);

	    region = region.subtracted (r);

	    r = CompRect (w->struts ()->right.x,
			  w->struts ()->right.y,
			  w->struts ()->right.width,
			  w->struts ()->right.height);

	    region = region.subtracted (r);
	}
    }

    return region;
}

/*
 * groupConstrainMovement
 *
 */
bool
GroupWindow::groupConstrainMovement (CompRegion constrainRegion,
				     int        dx,
				     int        dy,
				     int        &new_dx,
				     int        &new_dy)
{
    int status, xStatus;
    int origDx = dx, origDy = dy;
    int x, y, width, height;
    CompWindow *w = window;

    if (!mGroup)
	return false;

    if (!dx && !dy)
	return false;

    x = mOrgPos.x - w->input ().left + dx;
    y = mOrgPos.y - w->input ().top + dy;
    width = WIN_REAL_WIDTH (w);
    height = WIN_REAL_HEIGHT (w);

    status = constrainRegion.contains (CompRect (x, y, width, height));

    xStatus = status;
    while (dx && (xStatus != RectangleIn))
    {
	xStatus = constrainRegion.contains (CompRect (x, y - dy, width, height));

	if (xStatus != RectangleIn)
	    dx += (dx < 0) ? 1 : -1;

	x = mOrgPos.x - w->input ().left + dx;
    }

    while (dy && (status != RectangleIn))
    {
	status = constrainRegion.contains (CompRect (x, y, width, height));

	if (status != RectangleIn)
	    dy += (dy < 0) ? 1 : -1;

	y = mOrgPos.y - w->input ().top + dy;
    }

    new_dx = dx;

    new_dy = dy;

    return ((dx != origDx) || (dy != origDy));
}

/*
 * groupApplyConstraining
 *
 */
void
GroupScreen::groupApplyConstraining (GroupSelection *group,
				     CompRegion	    constrainRegion,
				     Window	    constrainedWindow,
				     int	    dx,
				     int	    dy)
{
    if (!dx && !dy)
	return;

    foreach (CompWindow *w, group->mWindows)
    {
	GROUP_WINDOW (w);

	/* ignore certain windows: we don't want to apply the constraining
	   results on the constrained window itself, nor do we want to
	   change the target position of unamimated windows and of
	   windows which already are constrained */
	if (w->id () == constrainedWindow)
	    continue;

	if (!(gw->mAnimateState & IS_ANIMATED))
	    continue;

	if (gw->mAnimateState & DONT_CONSTRAIN)
	    continue;

	if (!(gw->mAnimateState & CONSTRAINED_X))
	{
	    int dummy;
	    gw->mAnimateState |= IS_ANIMATED;

	    /* applying the constraining result of another window
	       might move the window offscreen, too, so check
	       if this is not the case */
	    if (gw->groupConstrainMovement (constrainRegion, dx, 0, dx, dummy))
		gw->mAnimateState |= CONSTRAINED_X;

	    gw->mDestination.x += dx;
	}

	if (!(gw->mAnimateState & CONSTRAINED_Y))
	{
	    int dummy;
	    gw->mAnimateState |= IS_ANIMATED;

	    /* analog to X case */
	    if (gw->groupConstrainMovement (constrainRegion, 0, dy, dummy, dy))
		gw->mAnimateState |= CONSTRAINED_Y;

	    gw->mDestination.y += dy;
	}
    }
}

/*
 * groupStartTabbingAnimation
 *
 */
void
GroupScreen::groupStartTabbingAnimation (GroupSelection *group,
			    		 bool           tab)
{
    int        dx, dy;
    int        constrainStatus;

    if (!group || (group->mTabbingState != NoTabbing))
	return;

    group->mTabbingState = (tab) ? Tabbing : Untabbing;
    groupTabChangeActivateEvent (true);

    if (!tab)
    {
	/* we need to set up the X/Y constraining on untabbing */
	CompRegion constrainRegion = groupGetConstrainRegion ();
	bool   constrainedWindows = true;

	/* reset all flags */
	foreach (CompWindow *cw, group->mWindows)
	{
	    GROUP_WINDOW (cw);
	    gw->mAnimateState &= ~(CONSTRAINED_X | CONSTRAINED_Y |
				  DONT_CONSTRAIN);
	}

	/* as we apply the constraining in a flat loop,
	   we may need to run multiple times through this
	   loop until all constraining dependencies are met */
	while (constrainedWindows)
	{
	    constrainedWindows = false;
	    /* loop through all windows and try to constrain their
	       animation path (going from gw->mOrgPos to
	       gw->mDestination) to the active screen area */
	    foreach (CompWindow *w, group->mWindows)
	    {
		GroupWindow *gw = GroupWindow::get (w);
		CompRect   statusRect (gw->mOrgPos.x - w->input ().left,
				       gw->mOrgPos.y - w->input ().top,
				       WIN_REAL_WIDTH (w),
				       WIN_REAL_HEIGHT (w));

		/* ignore windows which aren't animated and/or
		   already are at the edge of the screen area */
		if (!(gw->mAnimateState & IS_ANIMATED))
		    continue;

		if (gw->mAnimateState & DONT_CONSTRAIN)
		    continue;

		/* is the original position inside the screen area? */
		constrainStatus = constrainRegion.contains (statusRect);

		/* constrain the movement */
		if (gw->groupConstrainMovement (constrainRegion,
					    gw->mDestination.x - gw->mOrgPos.x,
					    gw->mDestination.y - gw->mOrgPos.y,
					    dx, dy))
		{
		    /* handle the case where the window is outside the screen
		       area on its whole animation path */
		    if (constrainStatus != RectangleIn && !dx && !dy)
		    {
			gw->mAnimateState |= DONT_CONSTRAIN;
			gw->mAnimateState |= CONSTRAINED_X | CONSTRAINED_Y;

			/* use the original position as last resort */
			gw->mDestination.x = gw->mMainTabOffset.x;
			gw->mDestination.y = gw->mMainTabOffset.y;
		    }
		    else
		    {
			/* if we found a valid target position, apply
			   the change also to other windows to retain
			   the distance between the windows */
			groupApplyConstraining (group, constrainRegion, w->id (),
						dx - gw->mDestination.x +
						gw->mOrgPos.x,
						dy - gw->mDestination.y +
						gw->mOrgPos.y);

			/* if we hit constraints, adjust the mask and the
			   target position accordingly */
			if (dx != (gw->mDestination.x - gw->mOrgPos.x))
			{
			    gw->mAnimateState |= CONSTRAINED_X;
			    gw->mDestination.x = gw->mOrgPos.x + dx;
			}

			if (dy != (gw->mDestination.y - gw->mOrgPos.y))
			{
			    gw->mAnimateState |= CONSTRAINED_Y;
			    gw->mDestination.y = gw->mOrgPos.y + dy;
			}

			constrainedWindows = true;
		    }
		}
	    }
	}
    }
}

/*
 * GroupSelection::tabGroup
 *
 */
void
GroupSelection::tabGroup (CompWindow *main)
{
    GroupTabBarSlot *slot;
    int             width, height;
    int             space, thumbSize;

    GROUP_WINDOW (main);
    GROUP_SCREEN (screen);

    if (mTabBar)
	return;

    if (!screen->XShape ())
    {
	compLogMessage ("group", CompLogLevelError,
			"No X shape extension! Tabbing disabled.");
	return;
    }

    gs->groupInitTabBar (this, main);
    if (!mTabBar)
	return;

    mTabbingState = NoTabbing;
    /* Slot is initialized after groupInitTabBar(group); */
    gs->groupChangeTab (gw->mSlot, RotateUncertain);
    gs->groupRecalcTabBarPos (gw->mGroup, WIN_CENTER_X (main),
			  WIN_X (main), WIN_X (main) + WIN_WIDTH (main));

    width = mTabBar->mRegion.boundingRect ().x2 () -
	    mTabBar->mRegion.boundingRect ().x1 ();
    height = mTabBar->mRegion.boundingRect ().y2 () -
	     mTabBar->mRegion.boundingRect ().y1 ();

    mTabBar->mTextLayer = gs->groupCreateCairoLayer (width, height);
    if (mTabBar->mTextLayer)
    {
	GroupCairoLayer *layer;

	layer = mTabBar->mTextLayer;
	layer->mState = PaintOff;
	layer->mAnimationTime = 0;
	renderWindowTitle ();
    }
    if (mTabBar->mTextLayer)
    {
	GroupCairoLayer *layer;

	layer = mTabBar->mTextLayer;
	layer->mAnimationTime = gs->optionGetFadeTextTime () * 1000;
	layer->mState = PaintFadeIn;
    }

    /* we need a buffer for DnD here */
    space = gs->optionGetThumbSpace ();
    thumbSize = gs->optionGetThumbSize ();
    mTabBar->mBgLayer = gs->groupCreateCairoLayer (width + space + thumbSize,
						    height);
    if (mTabBar->mBgLayer)
    {
	mTabBar->mBgLayer->mState = PaintOn;
	mTabBar->mBgLayer->mAnimationTime = 0;
	renderTabBarBackground ();
    }

    width = mTopTab->mRegion.boundingRect ().x2 () -
	    mTopTab->mRegion.boundingRect ().x1 ();
    height = mTopTab->mRegion.boundingRect ().y2 () -
	     mTopTab->mRegion.boundingRect ().y1 ();

    mTabBar->mSelectionLayer = gs->groupCreateCairoLayer (width, height);
    if (mTabBar->mSelectionLayer)
    {
	mTabBar->mSelectionLayer->mState = PaintOn;
	mTabBar->mSelectionLayer->mAnimationTime = 0;
	renderTopTabHighlight ();
    }

    if (!HAS_TOP_WIN (this))
	return;

    foreach (slot, mTabBar->mSlots)
    {
	CompWindow *cw = slot->mWindow;

	GROUP_WINDOW (cw);

	if (gw->mAnimateState & (IS_ANIMATED | FINISHED_ANIMATION))
	    cw->move (gw->mDestination.x - WIN_X (cw),
		      gw->mDestination.y - WIN_Y (cw), true);

	/* center the window to the main window */
	gw->mDestination.x = WIN_CENTER_X (main) - (WIN_WIDTH (cw) / 2);
	gw->mDestination.y = WIN_CENTER_Y (main) - (WIN_HEIGHT (cw) / 2);

	/* Distance from destination. */
	gw->mMainTabOffset.x = WIN_X (cw) - gw->mDestination.x;
	gw->mMainTabOffset.y = WIN_Y (cw) - gw->mDestination.y;

	if (gw->mTx || gw->mTy)
	{
	    gw->mTx -= (WIN_X (cw) - gw->mOrgPos.x);
	    gw->mTy -= (WIN_Y (cw) - gw->mOrgPos.y);
	}

	gw->mOrgPos.x = WIN_X (cw);
	gw->mOrgPos.y = WIN_Y (cw);

	gw->mAnimateState = IS_ANIMATED;
	gw->mXVelocity = gw->mYVelocity = 0.0f;
    }

    gs->groupStartTabbingAnimation (this, true);
}

/*
 * GroupSelection::untabGroup
 *
 */
void
GroupSelection::untabGroup ()
{
    int             oldX, oldY;
    CompWindow      *prevTopTab;
    GroupTabBarSlot *slot;
    
    GROUP_SCREEN (screen);

    if (!HAS_TOP_WIN (this))
	return;

    if (mPrevTopTab)
	prevTopTab = PREV_TOP_TAB (this);
    else
    {
	/* If prevTopTab isn't set, we have no choice but using topTab.
	   It happens when there is still animation, which
	   means the tab wasn't changed anyway. */
	prevTopTab = TOP_TAB (this);
    }

    mLastTopTab = TOP_TAB (this);
    mTopTab = NULL;

    foreach (slot, mTabBar->mSlots)
    {
	CompWindow *cw = slot->mWindow;

	GROUP_WINDOW (cw);

	if (gw->mAnimateState & (IS_ANIMATED | FINISHED_ANIMATION))
	{
	    gs->mQueued = true;
	    cw->move(gw->mDestination.x - WIN_X (cw),
		     gw->mDestination.y - WIN_Y (cw), true);
	    gs->mQueued = false;
	}

	gw->groupSetWindowVisibility (true);

	/* save the old original position - we might need it
	   if constraining fails */
	oldX = gw->mOrgPos.x;
	oldY = gw->mOrgPos.y;

	gw->mOrgPos.x = WIN_CENTER_X (prevTopTab) - WIN_WIDTH (cw) / 2;
	gw->mOrgPos.y = WIN_CENTER_Y (prevTopTab) - WIN_HEIGHT (cw) / 2;

	gw->mDestination.x = gw->mOrgPos.x + gw->mMainTabOffset.x;
	gw->mDestination.y = gw->mOrgPos.y + gw->mMainTabOffset.y;

	if (gw->mTx || gw->mTy)
	{
	    gw->mTx -= (gw->mOrgPos.x - oldX);
	    gw->mTy -= (gw->mOrgPos.y - oldY);
	}

	gw->mMainTabOffset.x = oldX;
	gw->mMainTabOffset.y = oldY;

	gw->mAnimateState = IS_ANIMATED;
	gw->mXVelocity = gw->mYVelocity = 0.0f;
    }

    mTabbingState = NoTabbing;
    gs->groupStartTabbingAnimation (this, false);

    gs->groupDeleteTabBar (this);
    mChangeAnimationTime = 0;
    mChangeState = NoTabChange;
    mNextTopTab = NULL;
    mPrevTopTab = NULL;

    gs->cScreen->damageScreen ();
}

/*
 * groupChangeTab
 *
 */
bool
GroupScreen::groupChangeTab (GroupTabBarSlot             *topTab,
			     ChangeTabAnimationDirection direction)
{
    CompWindow     *w, *oldTopTab;
    GroupSelection *group;

    if (!topTab)
	return true;

    w = topTab->mWindow;

    GROUP_WINDOW (w);

    group = gw->mGroup;

    if (!group || group->mTabbingState != NoTabbing)
	return true;

    if (group->mChangeState == NoTabChange && group->mTopTab == topTab)
	return true;

    if (group->mChangeState != NoTabChange && group->mNextTopTab == topTab)
	return true;

    oldTopTab = group->mTopTab ? group->mTopTab->mWindow : NULL;

    if (group->mChangeState != NoTabChange)
	group->mNextDirection = direction;
    else if (direction == RotateLeft)
	group->mChangeAnimationDirection = 1;
    else if (direction == RotateRight)
	group->mChangeAnimationDirection = -1;
    else
    {
	int             distanceOld = 0, distanceNew = 0;
	GroupTabBarSlot::List::iterator it = group->mTabBar->mSlots.begin ();

	if (group->mTopTab)
	    for (; (*it) && ((*it) != group->mTopTab);
		 it++, distanceOld++);

	for (it = group->mTabBar->mSlots.begin (); (*it) && ((*it) != topTab);
	     it++, distanceNew++);

	if (distanceNew < distanceOld)
	    group->mChangeAnimationDirection = 1;   /*left */
	else
	    group->mChangeAnimationDirection = -1;  /* right */

	/* check if the opposite direction is shorter */
	if (abs (distanceNew - distanceOld) > ((int) group->mTabBar->mSlots.size () / 2))
	    group->mChangeAnimationDirection *= -1;
    }

    if (group->mChangeState != NoTabChange)
    {
	if (group->mPrevTopTab == topTab)
	{
	    /* Reverse animation. */
	    GroupTabBarSlot *tmp = group->mTopTab;
	    group->mTopTab = group->mPrevTopTab;
	    group->mPrevTopTab = tmp;

	    group->mChangeAnimationDirection *= -1;
	    group->mChangeAnimationTime =
		optionGetChangeAnimationTime () * 500 -
		group->mChangeAnimationTime;
	    group->mChangeState = (group->mChangeState == TabChangeOldOut) ?
		TabChangeNewIn : TabChangeOldOut;

	    group->mNextTopTab = NULL;
	}
	else
	    group->mNextTopTab = topTab;
    }
    else
    {
	group->mTopTab = topTab;

	group->renderWindowTitle ();
	group->renderTopTabHighlight ();
	if (oldTopTab)
	    CompositeWindow::get (oldTopTab)->addDamage ();
	CompositeWindow::get (w)->addDamage ();
    }

    if (topTab != group->mNextTopTab)
    {
	gw->groupSetWindowVisibility (true);
	if (oldTopTab)
	{
	    int dx, dy;

	    dx = WIN_CENTER_X (oldTopTab) - WIN_CENTER_X (w);
	    dy = WIN_CENTER_Y (oldTopTab) - WIN_CENTER_Y (w);

	    mQueued = true;
	    w->move (dx, dy, false);
	    w->syncPosition ();
	    mQueued = false;
	}

	if (HAS_PREV_TOP_WIN (group))
	{
	    /* we use only the half time here -
	       the second half will be PaintFadeOut */
	    group->mChangeAnimationTime =
		optionGetChangeAnimationTime () * 500;
	    groupTabChangeActivateEvent (true);
	    group->mChangeState = TabChangeOldOut;
	}
	else
	{
	    bool activate;

	    /* No window to do animation with. */
	    if (HAS_TOP_WIN (group))
		group->mPrevTopTab = group->mTopTab;
	    else
		group->mPrevTopTab = NULL;

	    activate = !group->mCheckFocusAfterTabChange;
	    if (!activate)
	    {
		/*
		CompFocusResult focus;

		focus    = allowWindowFocus (w, NO_FOCUS_MASK, s->x, s->y, 0);
		activate = focus == CompFocusAllowed;
		*/
	    }

	    if (activate)
		w->activate ();

	    group->mCheckFocusAfterTabChange = false;
	}
    }

    return true;
}

/*
 * groupRecalcSlotPos
 *
 */
void
GroupScreen::groupRecalcSlotPos (GroupTabBarSlot *slot,
				 int		 slotPos)
{
    GroupSelection *group;
    CompRect       box;
    int            space, thumbSize;

    GROUP_WINDOW (slot->mWindow);
    group = gw->mGroup;

    if (!HAS_TOP_WIN (group) || !group->mTabBar)
	return;

    space = optionGetThumbSpace ();
    thumbSize = optionGetThumbSize ();

    slot->mRegion = emptyRegion;

    box.setX (space + ((thumbSize + space) * slotPos));
    box.setY (space);

    box.setWidth (thumbSize);
    box.setHeight (thumbSize);

    slot->mRegion = CompRegion (box);
}

/*
 * groupRecalcTabBarPos
 *
 */
void
GroupScreen::groupRecalcTabBarPos (GroupSelection *group,
				   int		  middleX,
				   int		  minX1,
				   int		  maxX2)
{
    GroupTabBarSlot *slot;
    GroupTabBar     *bar;
    CompWindow      *topTab;
    bool            isDraggedSlotGroup = false;
    int             space, barWidth;
    int             thumbSize;
    int             tabsWidth = 0, tabsHeight = 0;
    int             currentSlot;
    CompRect	    box;

    if (!HAS_TOP_WIN (group) || !group->mTabBar)
	return;

    bar = group->mTabBar;
    topTab = TOP_TAB (group);
    space = optionGetThumbSpace ();

    /* calculate the space which the tabs need */
    foreach (slot, bar->mSlots)
    {
	if (slot == mDraggedSlot && mDragged)
	{
	    isDraggedSlotGroup = true;
	    continue;
	}

	tabsWidth += (slot->mRegion.boundingRect ().x2 () - slot->mRegion.boundingRect ().x1 ());
	if ((slot->mRegion.boundingRect ().y2 () - slot->mRegion.boundingRect ().y1 ()) > tabsHeight)
	    tabsHeight = slot->mRegion.boundingRect ().y2 () - slot->mRegion.boundingRect ().y1 ();
    }

    /* just a little work-a-round for first call
       FIXME: remove this! */
    thumbSize = optionGetThumbSize ();
    if (bar->mSlots.size () && tabsWidth <= 0)
    {
	/* first call */
	tabsWidth = thumbSize * bar->mSlots.size ();

	if (bar->mSlots.size () && tabsHeight < thumbSize)
	{
	    /* we need to do the standard height too */
	    tabsHeight = thumbSize;
	}

	if (isDraggedSlotGroup)
	    tabsWidth -= thumbSize;
    }

    barWidth = space * (bar->mSlots.size () + 1) + tabsWidth;

    if (isDraggedSlotGroup)
    {
	/* 1 tab is missing, so we have 1 less border */
	barWidth -= space;
    }

    if (maxX2 - minX1 < barWidth)
	box.setX ((maxX2 + minX1) / 2 - barWidth / 2);
    else if (middleX - barWidth / 2 < minX1)
	box.setX (minX1);
    else if (middleX + barWidth / 2 > maxX2)
	box.setX (maxX2 - barWidth);
    else
	box.setX (middleX - barWidth / 2);

    box.setY (WIN_Y (topTab));
    box.setWidth (barWidth);
    box.setHeight (space * 2 + tabsHeight);

    resizeTabBarRegion (group, box, true);

    /* recalc every slot region */
    currentSlot = 0;
    foreach (slot, bar->mSlots)
    {
	if (slot == mDraggedSlot && mDragged)
	    continue;

	groupRecalcSlotPos (slot, currentSlot);
	slot->mRegion.translate (bar->mRegion.boundingRect ().x1 (),
				 bar->mRegion.boundingRect ().y1 ());

	slot->mSpringX = (slot->mRegion.boundingRect ().x1 () +
			 slot->mRegion.boundingRect ().x2 ()) / 2;
	slot->mSpeed = 0;
	slot->mMsSinceLastMove = 0;

	currentSlot++;
    }

    bar->mLeftSpringX = box.x ();
    bar->mRightSpringX = box.x () + box.width ();

    bar->mRightSpeed = 0;
    bar->mLeftSpeed = 0;

    bar->mRightMsSinceLastMove = 0;
    bar->mLeftMsSinceLastMove = 0;
}

void
GroupScreen::groupDamageTabBarRegion (GroupSelection *group)
{
    REGION reg;
    CompRegion cReg;

    reg.rects = &reg.extents;
    reg.numRects = 1;

    /* we use 15 pixels as damage buffer here, as there is a 10 pixel wide
       border around the selected slot which also needs to be damaged
       properly - however the best way would be if slot->mRegion was
       sized including the border */

#define DAMAGE_BUFFER 20

    reg.extents = group->mTabBar->mRegion.handle ()->extents;

    if (group->mTabBar->mSlots.size ())
    {
	reg.extents.x1 = MIN (reg.extents.x1,
			      group->mTabBar->mSlots.front ()->mRegion.boundingRect ().x1 ());
	reg.extents.y1 = MIN (reg.extents.y1,
			      group->mTabBar->mSlots.front ()->mRegion.boundingRect ().y1 ());
	reg.extents.x2 = MAX (reg.extents.x2,
			      group->mTabBar->mSlots.back ()->mRegion.boundingRect ().x2 ());
	reg.extents.y2 = MAX (reg.extents.y2,
			      group->mTabBar->mSlots.back ()->mRegion.boundingRect ().y2 ());
    }

    reg.extents.x1 -= DAMAGE_BUFFER;
    reg.extents.y1 -= DAMAGE_BUFFER;
    reg.extents.x2 += DAMAGE_BUFFER;
    reg.extents.y2 += DAMAGE_BUFFER;

    cReg = CompRegion (reg.extents.x1, reg.extents.y1,
		       reg.extents.x2 - reg.extents.x1,
		       reg.extents.y2 - reg.extents.y1);

    cScreen->damageRegion (cReg);
}

void
GroupScreen::groupMoveTabBarRegion (GroupSelection *group,
				    int		   dx,
				    int		   dy,
				    bool	   syncIPW)
{
    groupDamageTabBarRegion (group);

    group->mTabBar->mRegion.translate (dx, dy);

    if (syncIPW)
	XMoveWindow (screen->dpy (),
		     group->mInputPrevention,
		     group->mTabBar->mLeftSpringX,
		     group->mTabBar->mRegion.boundingRect ().y1 ());

    groupDamageTabBarRegion (group);
}

void
GroupScreen::resizeTabBarRegion (GroupSelection *group,
				 CompRect	&box,
				 bool           syncIPW)
{
    int oldWidth;

    groupDamageTabBarRegion (group);

    oldWidth = group->mTabBar->mRegion.boundingRect ().x2 () -
	group->mTabBar->mRegion.boundingRect ().x1 ();

    if (group->mTabBar->mBgLayer && oldWidth != box.width () && syncIPW)
    {
	group->mTabBar->mBgLayer =
	    groupRebuildCairoLayer (group->mTabBar->mBgLayer,
				    box.width () +
				    optionGetThumbSpace () +
				    optionGetThumbSize (),
				    box.height ());
	group->renderTabBarBackground ();

	/* invalidate old width */
	group->mTabBar->mOldWidth = 0;
    }    

    group->mTabBar->mRegion = CompRegion (box);

    if (syncIPW)
    {
	XWindowChanges xwc;

	xwc.x = box.x ();
	xwc.y = box.y ();
	xwc.width = box.width ();
	xwc.height = box.height ();

	if (!group->mIpwMapped)
	    XMapWindow (screen->dpy (), group->mInputPrevention);

	XMoveResizeWindow (screen->dpy (), group->mInputPrevention, xwc.x, xwc.y, xwc.width, xwc.height);
	
	if (!group->mIpwMapped)
	    XUnmapWindow (screen->dpy (), group->mInputPrevention);
    }

    groupDamageTabBarRegion (group);
}

/*
 * groupInsertTabBarSlotBefore
 *
 */
void
GroupScreen::groupInsertTabBarSlotBefore (GroupTabBar     *bar,
					  GroupTabBarSlot *slot,
					  GroupTabBarSlot *nextSlot)
{
    GroupTabBarSlot *prev = nextSlot->mPrev;
    GroupTabBarSlot::List::iterator pos = std::find (bar->mSlots.begin (),
						     bar->mSlots.end (),
						     nextSlot);
    CompWindow      *w = slot->mWindow;
    
    bar->mSlots.insert (pos, slot);

    GROUP_WINDOW (w);

    if (prev)
    {
	slot->mPrev = prev;
	prev->mNext = slot;
    }
    else
    {
	slot->mPrev = NULL;
    }

    slot->mNext = nextSlot;
    nextSlot->mPrev = slot;

    /* Moving bar->mRegion.boundingRect ().x1 () / x2 as minX1 / maxX2 will work,
       because the tab-bar got wider now, so it will put it in
       the average between them, which is
       (bar->mRegion.boundingRect ().x1 () + bar->mRegion.boundingRect ().x2 ()) / 2 anyway. */
    groupRecalcTabBarPos (gw->mGroup,
			  (bar->mRegion.boundingRect ().x1 () +
			   bar->mRegion.boundingRect ().x2 ()) / 2,
			  bar->mRegion.boundingRect ().x1 (), bar->mRegion.boundingRect ().x2 ());
}

/*
 * groupInsertTabBarSlotAfter
 *
 */
void
GroupScreen::groupInsertTabBarSlotAfter (GroupTabBar     *bar,
					 GroupTabBarSlot *slot,
					 GroupTabBarSlot *prevSlot)
{
    GroupTabBarSlot *next = prevSlot->mNext;
    GroupTabBarSlot::List::iterator pos = std::find (bar->mSlots.begin (),
						     bar->mSlots.end (),
						     next);
    CompWindow      *w = slot->mWindow;

    bar->mSlots.insert (pos, slot);

    GROUP_WINDOW (w);

    if (next)
    {
	slot->mNext = next;
	next->mPrev = slot;
    }
    else
    {
	slot->mNext = NULL;
    }

    slot->mPrev = prevSlot;
    prevSlot->mNext = slot;

    /* Moving bar->mRegion.boundingRect ().x1 () / x2 as minX1 / maxX2 will work,
       because the tab-bar got wider now, so it will put it in the
       average between them, which is
       (bar->mRegion.boundingRect ().x1 () + bar->mRegion.boundingRect ().x2 ()) / 2 anyway. */
    groupRecalcTabBarPos (gw->mGroup,
			  (bar->mRegion.boundingRect ().x1 () +
			   bar->mRegion.boundingRect ().x2 ()) / 2,
			  bar->mRegion.boundingRect ().x1 (), bar->mRegion.boundingRect ().x2 ());
}

/*
 * groupInsertTabBarSlot
 *
 */
void
GroupScreen::groupInsertTabBarSlot (GroupTabBar     *bar,
				    GroupTabBarSlot *slot)
{
    CompWindow *w = slot->mWindow;

    GROUP_WINDOW (w);

    if (bar->mSlots.size ())
    {
	bar->mSlots.back ()->mNext = slot;
	slot->mPrev = bar->mSlots.back ();
	slot->mNext = NULL;
    }
    else
    {
	slot->mPrev = NULL;
	slot->mNext = NULL;
    }

    bar->mSlots.push_back (slot);

    /* Moving bar->mRegion.boundingRect ().x1 () / x2 as minX1 / maxX2 will work,
       because the tab-bar got wider now, so it will put it in
       the average between them, which is
       (bar->mRegion.boundingRect ().x1 () + bar->mRegion.boundingRect ().x2 ()) / 2 anyway. */
    groupRecalcTabBarPos (gw->mGroup,
			  (bar->mRegion.boundingRect ().x1 () +
			   bar->mRegion.boundingRect ().x2 ()) / 2,
			  bar->mRegion.boundingRect ().x1 (), bar->mRegion.boundingRect ().x2 ());
}

/*
 * groupUnhookTabBarSlot
 *
 */
void
GroupScreen::groupUnhookTabBarSlot (GroupTabBar     *bar,
				    GroupTabBarSlot *slot,
				    bool            temporary)
{
    GroupTabBarSlot *tempSlot;
    GroupTabBarSlot *prev = slot->mPrev;
    GroupTabBarSlot *next = slot->mNext;
    CompWindow      *w = slot->mWindow;
    GroupSelection  *group;

    GROUP_WINDOW (w);

    group = gw->mGroup;

    /* check if slot is not already unhooked */
    foreach (tempSlot, bar->mSlots)
	if (tempSlot == slot)
	    break;

    if (!tempSlot)
	return;

    if (prev)
	prev->mNext = next;

    if (next)
	next->mPrev = prev;

    slot->mPrev = NULL;
    slot->mNext = NULL;
    
    bar->mSlots.remove (slot);

    if (!temporary)
    {
	if (IS_PREV_TOP_TAB (w, group))
	    group->mPrevTopTab = NULL;
	if (IS_TOP_TAB (w, group))
	{
	    group->mTopTab = NULL;

	    if (next)
		groupChangeTab (next, RotateRight);
	    else if (prev)
		groupChangeTab (prev, RotateLeft);

	    if (optionGetUntabOnClose ())
		group->untabGroup ();
	}
    }

    if (slot == bar->mHoveredSlot)
	bar->mHoveredSlot = NULL;

    if (slot == bar->mTextSlot)
    {
	bar->mTextSlot = NULL;

	if (bar->mTextLayer)
	{
	    if (bar->mTextLayer->mState == PaintFadeIn ||
		bar->mTextLayer->mState == PaintOn)
	    {
		bar->mTextLayer->mAnimationTime =
		    (optionGetFadeTextTime () * 1000) -
		    bar->mTextLayer->mAnimationTime;
		bar->mTextLayer->mState = PaintFadeOut;
	    }
	}
    }

    /* Moving bar->mRegion.boundingRect ().x1 () / x2 as minX1 / maxX2 will work,
       because the tab-bar got thiner now, so
       (bar->mRegion.boundingRect ().x1 () + bar->mRegion.boundingRect ().x2 ()) / 2
       Won't cause the new x1 / x2 to be outside the original region. */
    groupRecalcTabBarPos (group,
			  (bar->mRegion.boundingRect ().x1 () +
			   bar->mRegion.boundingRect ().x2 ()) / 2,
			  bar->mRegion.boundingRect ().x1 (),
			  bar->mRegion.boundingRect ().x2 ());
}

/*
 * groupDeleteTabBarSlot
 *
 */
void
GroupScreen::groupDeleteTabBarSlot (GroupTabBar     *bar,
				    GroupTabBarSlot *slot)
{
    CompWindow *w = slot->mWindow;

    GROUP_WINDOW (w);

    groupUnhookTabBarSlot (bar, slot, false);

    slot->mRegion = CompRegion ();

    if (slot == mDraggedSlot)
    {
	mDraggedSlot = NULL;
	mDragged = false;

	if (mGrabState == ScreenGrabTabDrag)
	    groupGrabScreen (ScreenGrabNone);
    }

    gw->mSlot = NULL;
    gw->groupUpdateWindowProperty ();
    delete slot;
}

/*
 * groupCreateSlot
 *
 */
void
GroupScreen::groupCreateSlot (GroupSelection *group,
			      CompWindow      *w)
{
    GroupTabBarSlot *slot;

    GROUP_WINDOW (w);

    if (!group->mTabBar)
	return;

    slot = new GroupTabBarSlot ();
    if (!slot)
        return;

    slot->mWindow = w;

    groupInsertTabBarSlot (group->mTabBar, slot);
    gw->mSlot = slot;
    gw->groupUpdateWindowProperty ();
}

#define SPRING_K     GroupScreen::get (screen)->optionGetDragSpringK()
#define FRICTION     GroupScreen::get (screen)->optionGetDragFriction()
#define SIZE	     GroupScreen::get (screen)->optionGetThumbSize()
#define BORDER	     GroupScreen::get (screen)->optionGetBorderRadius()
#define Y_START_MOVE GroupScreen::get (screen)->optionGetDragYDistance()
#define SPEED_LIMIT  GroupScreen::get (screen)->optionGetDragSpeedLimit()

/*
 * groupSpringForce
 *
 */
static inline int
groupSpringForce (CompScreen *s,
		  int        centerX,
		  int        springX)
{
    /* Each slot has a spring attached to it, starting at springX,
       and ending at the center of the slot (centerX).
       The spring will cause the slot to move, using the
       well-known physical formula F = k * dl... */
    return -SPRING_K * (centerX - springX);
}

/*
 * groupDraggedSlotForce
 *
 */
static int
groupDraggedSlotForce (CompScreen *s,
		       int        distanceX,
		       int        distanceY)
{
    /* The dragged slot will make the slot move, to get
       DnD animations (slots will make room for the newly inserted slot).
       As the dragged slot is closer to the slot, it will put
       more force on the slot, causing it to make room for the dragged slot...
       But if the dragged slot gets too close to the slot, they are
       going to be reordered soon, so the force will get lower.

       If the dragged slot is in the other side of the slot,
       it will have to make force in the opposite direction.

       So we the needed funtion is an odd function that goes
       up at first, and down after that.
       Sinus is a function like that... :)

       The maximum is got when x = (x1 + x2) / 2,
       in this case: x = SIZE + BORDER.
       Because of that, for x = SIZE + BORDER,
       we get a force of SPRING_K * (SIZE + BORDER) / 2.
       That equals to the force we get from the the spring.
       This way, the slot won't move when its distance from
       the dragged slot is SIZE + BORDER (which is the default
       distance between slots).
       */

    /* The maximum value */
    float a = SPRING_K * (SIZE + BORDER) / 2;
    /* This will make distanceX == 2 * (SIZE + BORDER) to get 0,
       and distanceX == (SIZE + BORDER) to get the maximum. */
    float b = PI /  (2 * SIZE + 2 * BORDER);

    /* If there is some distance between the slots in the y axis,
       the slot should get less force... For this, we change max
       to a lower value, using a simple linear function. */

    if (distanceY < Y_START_MOVE)
	a *= 1.0f - (float)distanceY / Y_START_MOVE;
    else
	a = 0;

    if (abs (distanceX) < 2 * (SIZE + BORDER))
	return a * sin (b * distanceX);
    else
	return 0;
}

/*
 * groupApplyFriction
 *
 */
static inline void
groupApplyFriction (CompScreen *s,
		    int        *speed)
{
    if (abs (*speed) < FRICTION)
	*speed = 0;
    else if (*speed > 0)
	*speed -= FRICTION;
    else if (*speed < 0)
	*speed += FRICTION;
}

/*
 * groupApplySpeedLimit
 *
 */
static inline void
groupApplySpeedLimit (CompScreen *s,
		      int        *speed)
{
    if (*speed > SPEED_LIMIT)
	*speed = SPEED_LIMIT;
    else if (*speed < -SPEED_LIMIT)
	*speed = -SPEED_LIMIT;
}

/*
 * groupApplyForces
 *
 */
void
GroupScreen::groupApplyForces (GroupTabBar     *bar,
			       GroupTabBarSlot *draggedSlot)
{
    GroupTabBarSlot *slot, *slot2;
    int             centerX, centerY;
    int             draggedCenterX, draggedCenterY;

    if (draggedSlot)
    {
	int vx, vy;

	groupGetDrawOffsetForSlot (draggedSlot, vx, vy);

	draggedCenterX = ((draggedSlot->mRegion.boundingRect ().x1 () +
			   draggedSlot->mRegion.boundingRect ().x2 ()) / 2) + vx;
	draggedCenterY = ((draggedSlot->mRegion.boundingRect ().y1 () +
			   draggedSlot->mRegion.boundingRect ().y2 ()) / 2) + vy;
    }
    else
    {
	draggedCenterX = 0;
	draggedCenterY = 0;
    }

    bar->mLeftSpeed += groupSpringForce(screen,
				       bar->mRegion.boundingRect ().x1 (),
				       bar->mLeftSpringX);
    bar->mRightSpeed += groupSpringForce(screen,
					bar->mRegion.boundingRect ().x2 (),
					bar->mRightSpringX);

    if (draggedSlot)
    {
	int leftForce, rightForce;

	leftForce = groupDraggedSlotForce(screen,
					  bar->mRegion.boundingRect ().x1 () -
					  SIZE / 2 - draggedCenterX,
					  abs ((bar->mRegion.boundingRect ().y1 () +
						bar->mRegion.boundingRect ().y2 ()) / 2 -
					       draggedCenterY));

	rightForce = groupDraggedSlotForce (screen,
					    bar->mRegion.boundingRect ().x2 () +
					    SIZE / 2 - draggedCenterX,
					    abs ((bar->mRegion.boundingRect ().y1 () +
						  bar->mRegion.boundingRect ().y2 ()) / 2 -
						 draggedCenterY));

	if (leftForce < 0)
	    bar->mLeftSpeed += leftForce;
	if (rightForce > 0)
	    bar->mRightSpeed += rightForce;
    }

    foreach (slot, bar->mSlots)
    {
	centerX = (slot->mRegion.boundingRect ().x1 () + slot->mRegion.boundingRect ().x2 ()) / 2;
	centerY = (slot->mRegion.boundingRect ().y1 () + slot->mRegion.boundingRect ().y2 ()) / 2;

	slot->mSpeed += groupSpringForce (screen, centerX, slot->mSpringX);

	if (draggedSlot && draggedSlot != slot)
	{
	    int draggedSlotForce;
	    draggedSlotForce =
		groupDraggedSlotForce(screen, centerX - draggedCenterX,
				      abs (centerY - draggedCenterY));

	    slot->mSpeed += draggedSlotForce;
	    slot2 = NULL;

	    if (draggedSlotForce < 0)
	    {
		slot2 = slot->mPrev;
		bar->mLeftSpeed += draggedSlotForce;
	    }
	    else if (draggedSlotForce > 0)
	    {
		slot2 = slot->mNext;
		bar->mRightSpeed += draggedSlotForce;
	    }

	    while (slot2)
	    {
		if (slot2 != draggedSlot)
		    slot2->mSpeed += draggedSlotForce;

		slot2 = (draggedSlotForce < 0) ? slot2->mPrev : slot2->mNext;
	    }
	}
    }

    foreach (slot, bar->mSlots)
    {
	groupApplyFriction (screen, &slot->mSpeed);
	groupApplySpeedLimit (screen, &slot->mSpeed);
    }

    groupApplyFriction (screen, &bar->mLeftSpeed);
    groupApplySpeedLimit (screen, &bar->mLeftSpeed);

    groupApplyFriction (screen, &bar->mRightSpeed);
    groupApplySpeedLimit (screen, &bar->mRightSpeed);
}

/*
 * groupApplySpeeds
 *
 */
void
GroupScreen::groupApplySpeeds (GroupSelection *group,
			       int            msSinceLastRepaint)
{
    GroupTabBar     *bar = group->mTabBar;
    GroupTabBarSlot *slot;
    int             move;
    CompRect	    box;
    bool            updateTabBar = false;

    box.setX (bar->mRegion.boundingRect ().x1 ());
    box.setY (bar->mRegion.boundingRect ().y1 ());
    box.setWidth (bar->mRegion.boundingRect ().x2 () - bar->mRegion.boundingRect ().x1 ());
    box.setHeight (bar->mRegion.boundingRect ().y2 () - bar->mRegion.boundingRect ().y1 ());

    bar->mLeftMsSinceLastMove += msSinceLastRepaint;
    bar->mRightMsSinceLastMove += msSinceLastRepaint;

    /* Left */
    move = bar->mLeftSpeed * bar->mLeftMsSinceLastMove / 1000;
    if (move)
    {
	box.setX (box.x () + move);
	box.setWidth (box.width () - move);

	bar->mLeftMsSinceLastMove = 0;
	updateTabBar = true;
    }
    else if (bar->mLeftSpeed == 0 &&
	     bar->mRegion.boundingRect ().x1 () != bar->mLeftSpringX &&
	     (SPRING_K * abs (bar->mRegion.boundingRect ().x1 () - bar->mLeftSpringX) <
	      FRICTION))
    {
	/* Friction is preventing from the left border to get
	   to its original position. */
	box.setX (box.x () + bar->mLeftSpringX - bar->mRegion.boundingRect ().x1 ());
	box.setWidth (box.width () - bar->mLeftSpringX - bar->mRegion.boundingRect ().x1 ());

	bar->mLeftMsSinceLastMove = 0;
	updateTabBar = true;
    }
    else if (bar->mLeftSpeed == 0)
	bar->mLeftMsSinceLastMove = 0;

    /* Right */
    move = bar->mRightSpeed * bar->mRightMsSinceLastMove / 1000;
    if (move)
    {
	box.setWidth (box.width () + move);

	bar->mRightMsSinceLastMove = 0;
	updateTabBar = true;
    }
    else if (bar->mRightSpeed == 0 &&
	     bar->mRegion.boundingRect ().x2 () != bar->mRightSpringX &&
	     (SPRING_K * abs (bar->mRegion.boundingRect ().x2 () - bar->mRightSpringX) <
	      FRICTION))
    {
	/* Friction is preventing from the right border to get
	   to its original position. */
	box.setWidth (box.width () + bar->mLeftSpringX - bar->mRegion.boundingRect ().x1 ());

	bar->mLeftMsSinceLastMove = 0;
	updateTabBar = true;
    }
    else if (bar->mRightSpeed == 0)
	bar->mRightMsSinceLastMove = 0;

    if (updateTabBar)
	resizeTabBarRegion (group, box, false);

    foreach (slot, bar->mSlots)
    {
	int slotCenter;

	slot->mMsSinceLastMove += msSinceLastRepaint;
	move = slot->mSpeed * slot->mMsSinceLastMove / 1000;
	slotCenter = (slot->mRegion.boundingRect ().x1 () +
		      slot->mRegion.boundingRect ().x2 ()) / 2;

	if (move)
	{
	    slot->mRegion.translate (move, 0);
	    slot->mMsSinceLastMove = 0;
	}
	else if (slot->mSpeed == 0 &&
		 slotCenter != slot->mSpringX &&
		 SPRING_K * abs (slotCenter - slot->mSpringX) < FRICTION)
	{
	    /* Friction is preventing from the slot to get
	       to its original position. */

	    slot->mRegion.translate (slot->mSpringX - slotCenter, 0);
	    slot->mMsSinceLastMove = 0;
	}
	else if (slot->mSpeed == 0)
	    slot->mMsSinceLastMove = 0;
    }
}

/*
 * groupInitTabBar
 *
 */
void
GroupScreen::groupInitTabBar (GroupSelection *group,
			      CompWindow     *topTab)
{
    GroupTabBar *bar;

    if (group->mTabBar)
	return;

    bar = new GroupTabBar ();
    if (!bar)
	return;

    bar->mSlots.clear ();
    bar->mSlots.size ();
    bar->mBgAnimation = AnimationNone;
    bar->mBgAnimationTime = 0;
    bar->mState = PaintOff;
    bar->mAnimationTime = 0;
    bar->mTextLayer = NULL;
    bar->mBgLayer = NULL;
    bar->mSelectionLayer = NULL;
    bar->mHoveredSlot = NULL;
    bar->mTextSlot = NULL;
    bar->mOldWidth = 0;
    group->mTabBar = bar;

    foreach (CompWindow *cw, group->mWindows)
	groupCreateSlot (group, cw);

    group->createInputPreventionWindow ();

    groupRecalcTabBarPos (group, WIN_CENTER_X (topTab),
			  WIN_X (topTab), WIN_X (topTab) + WIN_WIDTH (topTab));
}

/*
 * groupDeleteTabBar
 *
 */
void
GroupScreen::groupDeleteTabBar (GroupSelection *group)
{
    GroupTabBar *bar = group->mTabBar;

    groupDestroyCairoLayer (bar->mTextLayer);
    groupDestroyCairoLayer (bar->mBgLayer);
    groupDestroyCairoLayer (bar->mSelectionLayer);

    group->destroyInputPreventionWindow ();

    if (bar->mTimeoutHandle.active ())
	bar->mTimeoutHandle.stop ();

    while (bar->mSlots.size ())
	groupDeleteTabBarSlot (bar, bar->mSlots.front ());

    delete bar;
    group->mTabBar = NULL;
}

/*
 * groupInitTab
 *
 */
bool
GroupScreen::groupInitTab (CompAction         *action,
			   CompAction::State  state,
			   CompOption::Vector options)
{
    Window     xid;
    CompWindow *w;
    bool       allowUntab = true;

    xid = CompOption::getIntOptionNamed (options, "window", 0);
    w   = screen->findWindow (xid);
    if (!w)
	return true;

    GROUP_WINDOW (w);

    if (gw->mInSelection)
    {
	groupGroupWindows (action, state, options);
	/* If the window was selected, we don't want to
	   untab the group, because the user probably
	   wanted to tab the selected windows. */
	allowUntab = false;
    }

    if (!gw->mGroup)
	return true;

    if (!gw->mGroup->mTabBar)
	gw->mGroup->tabGroup (w);
    else if (allowUntab)
	gw->mGroup->untabGroup ();

    cScreen->damageScreen ();

    return true;
}

/*
 * groupChangeTabLeft
 *
 */
bool
GroupScreen::groupChangeTabLeft (CompAction          *action,
				 CompAction::State   state,
				 CompOption::Vector  options)
{
    Window     xid;
    CompWindow *w, *topTab;

    xid = CompOption::getIntOptionNamed (options, "window", 0);
    w   = topTab = screen->findWindow (xid);
    if (!w)
	return true;

    GROUP_WINDOW (w);

    if (!gw->mSlot || !gw->mGroup)
	return true;

    if (gw->mGroup->mNextTopTab)
	topTab = NEXT_TOP_TAB (gw->mGroup);
    else if (gw->mGroup->mTopTab)
    {
	/* If there are no tabbing animations,
	   topTab is never NULL. */
	topTab = TOP_TAB (gw->mGroup);
    }

    gw = GroupWindow::get (topTab);

    if (gw->mSlot->mPrev)
	return groupChangeTab (gw->mSlot->mPrev, RotateLeft);
    else
	return groupChangeTab (gw->mGroup->mTabBar->mSlots.back (), RotateLeft);
}

/*
 * groupChangeTabRight
 *
 */
bool
GroupScreen::groupChangeTabRight (CompAction         *action,
				  CompAction::State  state,
				  CompOption::Vector options)
{
    Window     xid;
    CompWindow *w, *topTab;

    xid = CompOption::getIntOptionNamed (options, "window", 0);
    w   = topTab = screen->findWindow (xid);
    if (!w)
	return true;

    GROUP_WINDOW (w);

    if (!gw->mSlot || !gw->mGroup)
	return true;

    if (gw->mGroup->mNextTopTab)
	topTab = NEXT_TOP_TAB (gw->mGroup);
    else if (gw->mGroup->mTopTab)
    {
	/* If there are no tabbing animations,
	   topTab is never NULL. */
	topTab = TOP_TAB (gw->mGroup);
    }

    gw = GroupWindow::get (topTab);

    if (gw->mSlot->mNext)
	return groupChangeTab (gw->mSlot->mNext, RotateRight);
    else
	return groupChangeTab (gw->mGroup->mTabBar->mSlots.front (), RotateRight);
}

/*
 * groupSwitchTopTabInput
 *
 */
void
GroupScreen::groupSwitchTopTabInput (GroupSelection *group,
				     bool	    enable)
{
    if (!group->mTabBar || !HAS_TOP_WIN (group))
	return;

    if (!group->mInputPrevention)
	group->createInputPreventionWindow ();

    if (!enable)
    {
	XMapWindow (screen->dpy (),
		    group->mInputPrevention);
		    
    }
    else
    {
	XUnmapWindow (screen->dpy (),
		      group->mInputPrevention);
    }

    group->mIpwMapped = !enable;
}

/*
 * GroupSelection::createInputPreventionWindow
 *
 */
void
GroupSelection::createInputPreventionWindow ()
{	
    if (!mInputPrevention)
    {
	XSetWindowAttributes attrib;
	attrib.override_redirect = true;

	mInputPrevention = 
	    XCreateWindow (screen->dpy (),
			   screen->root (), -100, -100, 1, 1, 0,
			   CopyFromParent, InputOnly,
			   CopyFromParent, CWOverrideRedirect, &attrib);
	
	mIpwMapped = false;
    }
}

/*
 * GroupSelection::destroyInputPreventionWindow
 *
 */
void
GroupSelection::destroyInputPreventionWindow ()
{
    if (mInputPrevention)
    {
	XDestroyWindow (screen->dpy (),
			mInputPrevention);

	mInputPrevention = None;
	mIpwMapped = true;
    }
}
