/*
 *  Workspace window manager
 *  Copyright (c) 2015-2021 Sergii Stoian
 *
 *  Window Maker window manager
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __WORKSPACE_WM_STACKING__
#define __WORKSPACE_WM_STACKING__

#include "wcore.h"

void wRaiseFrame(WCoreWindow *frame);
void wLowerFrame(WCoreWindow *frame);
void wRaiseLowerFrame(WCoreWindow *frame);
void AddToStackList(WCoreWindow *frame);
void MoveInStackListAbove(WCoreWindow *next, WCoreWindow *frame);
void MoveInStackListUnder(WCoreWindow *prev, WCoreWindow *frame);
void RemoveFromStackList(WCoreWindow *frame);
void ChangeStackingLevel(WCoreWindow *frame, int new_level);
void RemakeStackList(WScreen *scr);
void CommitStacking(WScreen *scr);
void CommitStackingForFrame(WCoreWindow *frame);
void CommitStackingForWindow(WCoreWindow * frame);

#endif /* __WORKSPACE_WM_STACKING__ */
