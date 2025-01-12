/*
 *  Workspace window manager
 *  Copyright (c) 2015-2021 Sergii Stoian
 *
 *  Window Maker window manager
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2014 Window Maker Team
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
 *  with this program; if not, write to the Free Software Foundation
 */

#ifndef __WORKSPACE_WM_XDND__
#define __WORKSPACE_WM_XDND__

#include <X11/Xlib.h>

void wXDNDInitializeAtoms(void);
Bool wXDNDProcessSelection(XEvent *event);
Bool wXDNDProcessClientMessage(XClientMessageEvent *event);
void wXDNDMakeAwareness(Window window);

#ifndef XDND_VERSION
#define XDND_VERSION 3L
#endif

#define XDND_ENTER_SOURCE_WIN(e)	((e)->data.l[0])
#define XDND_ENTER_THREE_TYPES(e)	(((e)->data.l[1] & 0x1UL) == 0)
#define XDND_ENTER_TYPE(e, i)		((e)->data.l[2 + i])	/* i => (0, 1, 2) */

/* XdndPosition */
#define XDND_POSITION_SOURCE_WIN(e)     ((e)->data.l[0])

/* XdndStatus */
#define XDND_STATUS_TARGET_WIN(e)	((e)->xclient.data.l[0])
#define XDND_STATUS_WILL_ACCEPT_SET(e, b) (e)->xclient.data.l[1] = ((e)->xclient.data.l[1] & ~0x1UL) | (((b) == 0) ? 0 : 0x1UL)
#define XDND_STATUS_WANT_POSITION_SET(e, b) (e)->xclient.data.l[1] = ((e)->xclient.data.l[1] & ~0x2UL) | (((b) == 0) ? 0 : 0x2UL)
#define XDND_STATUS_RECT_SET(e, x, y, w, h)	{(e)->xclient.data.l[2] = ((x) << 16) | ((y) & 0xFFFFUL); (e)->xclient.data.l[3] = ((w) << 16) | ((h) & 0xFFFFUL); }
#define XDND_STATUS_ACTION(e)		((e)->xclient.data.l[4])

/* XdndDrop */
#define XDND_DROP_SOURCE_WIN(e)         ((e)->data.l[0])

/* XdndFinished */
#define XDND_FINISHED_TARGET_WIN(e)	((e)->xclient.data.l[0])

#endif /* __WORKSPACE_WM_XDND__ */
