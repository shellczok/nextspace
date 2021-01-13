/*
 *  Workspace window manager
 *  Copyright (c) 2015- Sergii Stoian
 *
 *  Window Maker window manager
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2013 Window Maker Team
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

#ifndef __WORKSPACE_WM_SWITCHMENU__
#define __WORKSPACE_WM_SWITCHMENU__

void UpdateSwitchMenu(WScreen *scr, WWindow *wwin, int action);
void OpenSwitchMenu(WScreen *scr, int x, int y, int keyboard);
void InitializeSwitchMenu(WScreen *scr);

#endif /* __WORKSPACE_WM_SWITCHMENU__ */
