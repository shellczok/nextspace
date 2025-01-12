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


#ifndef __WORKSPACE_WM_PROPERTIES__
#define __WORKSPACE_WM_PROPERTIES__

#include <X11/Xutil.h>
#include "GNUstep.h"
#include "window.h"

unsigned char* PropGetCheckProperty(Window window, Atom hint, Atom type,
                                    int format, int count, int *retCount);

int PropGetWindowState(Window window);

int PropGetNormalHints(Window window, XSizeHints *size_hints, int *pre_iccm);
void PropGetProtocols(Window window, WProtocols *prots);
int PropGetWMClass(Window window, char **wm_class, char **wm_instance);
int PropGetGNUstepWMAttr(Window window, GNUstepWMAttributes **attr);

void PropSetWMakerProtocols(Window root);
void PropCleanUp(Window root);
void PropSetIconTileHint(WScreen *scr, RImage *image);

Window PropGetClientLeader(Window window);

#endif /* __WORKSPACE_WM_PROPERTIES__ */
