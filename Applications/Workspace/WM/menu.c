/*  Generic menu, used for root menu, application menus etc.
 *
 *  Workspace window manager
 *
 *  Copyright (c) 2015-2021 Sergii Stoian
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 1998-2003 Dan Pascu
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

#include "WM.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <ctype.h>

#include <core/WMcore.h>
#include <core/util.h>
#include <core/log_utils.h>
#include <core/string_utils.h>

#include <core/wevent.h>
#include <core/wcolor.h>
#include <core/drawing.h>

#include "GNUstep.h"
#include "WM.h"
#include "wcore.h"
#include "framewin.h"
#include "menu.h"
#include "actions.h"
#include "winmenu.h"
#include "stacking.h"
#include "xrandr.h"
#include "desktop.h"
#include "switchmenu.h"
#include "moveres.h"
#include "defaults.h"
#include "xmodifier.h"

/* do not divide main menu and submenu in different tiers, opposed to OpenStep */
#undef SINGLE_MENULEVEL

/* delays in ms for menu item selection hysteresis */
#define MENU_SELECT_DELAY       200

/* delays in ms for jumpback of scrolled menus */
#define MENU_JUMP_BACK_DELAY    400

/*menu scrolling */
#define MENU_SCROLL_STEPS_UF	14
#define MENU_SCROLL_DELAY_UF	1

#define MENU_SCROLL_STEPS_F	10
#define MENU_SCROLL_DELAY_F	5

#define MENU_SCROLL_STEPS_M	6
#define MENU_SCROLL_DELAY_M	5

#define MENU_SCROLL_STEPS_S	4
#define MENU_SCROLL_DELAY_S	6

#define MENU_SCROLL_STEPS_US	1
#define MENU_SCROLL_DELAY_US	8

#define MENU_SCROLL_STEP  menuScrollParameters[(int)wPreferences.menu_scroll_speed].steps
#define MENU_SCROLL_DELAY menuScrollParameters[(int)wPreferences.menu_scroll_speed].delay

/***** Local Stuff ******/

static struct {
  int steps;
  int delay;
} menuScrollParameters[5] = {
                             {
                              MENU_SCROLL_STEPS_UF, MENU_SCROLL_DELAY_UF}, {
                                                                            MENU_SCROLL_STEPS_F, MENU_SCROLL_DELAY_F}, {
                                                                                                                        MENU_SCROLL_STEPS_M, MENU_SCROLL_DELAY_M}, {
                                                                                                                                                                    MENU_SCROLL_STEPS_S, MENU_SCROLL_DELAY_S}, {
                                                                                                                                                                                                                MENU_SCROLL_STEPS_US, MENU_SCROLL_DELAY_US}};

static void menuMouseDown(WObjDescriptor *desc, XEvent *event);
static void menuExpose(WObjDescriptor *desc, XEvent *event);
static void menuTitleDoubleClick(WCoreWindow *sender, void *data, XEvent *event);
static void menuTitleMouseDown(WCoreWindow *sender, void *data, XEvent *event);
static void menuCloseClick(WCoreWindow *sender, void *data, XEvent *event);
static void updateTexture(WMenu *menu);
static void closeCascade(WMenu *menu);

/****** Notification Observers ******/

static void appearanceObserver(CFNotificationCenterRef center,
                               void *observedMenu, // observer
                               CFNotificationName name,
                               const void *settingsFlags, // object
                               CFDictionaryRef userInfo)
{
  WMenu *menu = (WMenu *)observedMenu;
  /* uintptr_t flags = (uintptr_t)WMGetNotificationClientData(notif); */
  uintptr_t flags = (uintptr_t)settingsFlags;

  if (!menu->flags.realized)
    return;

  if (CFStringCompare(name, WMDidChangeMenuAppearanceSettings, 0) == 0) {
    if (flags & WFontSettings) {
      menu->flags.realized = 0;
      wMenuRealize(menu);
    }
    if (flags & WTextureSettings) {
      if (!menu->flags.brother)
        updateTexture(menu);
    }
    if (flags & (WTextureSettings | WColorSettings)) {
      wMenuPaint(menu);
    }
  } else if (menu->flags.titled) {

    if (flags & WFontSettings) {
      menu->flags.realized = 0;
      wMenuRealize(menu);
    }
    if (flags & WTextureSettings) {
      menu->frame->flags.need_texture_remake = 1;
    }
    if (flags & (WColorSettings | WTextureSettings)) {
      wFrameWindowPaint(menu->frame);
    }
  }
}

/************************************/

/*
 *----------------------------------------------------------------------
 * wMenuCreate--
 * 	Creates a new empty menu with the specified title. If main_menu
 * is True, the created menu will be a main menu, which has some special
 * properties such as being placed over other normal menus.
 * 	If title is NULL, the menu will have no titlebar.
 *
 * Returns:
 * 	The created menu.
 *----------------------------------------------------------------------
 */
WMenu *wMenuCreate(WScreen *screen, const char *title, int main_menu)
{
  WMenu *menu;
  static int brother = 0;
  int window_flags;

  menu = wmalloc(sizeof(WMenu));
  menu->flags.app_menu = main_menu;
  
  window_flags = WFF_SINGLE_STATE | WFF_BORDER;
  if (title) {
    window_flags |= WFF_TITLEBAR | WFF_RIGHT_BUTTON;
    menu->flags.titled = 1;
  }
  menu->frame = wFrameWindowCreate(screen,
                                   (main_menu ? NSMainMenuWindowLevel : NSSubmenuWindowLevel),
                                   8, 2, 1, 1, &wPreferences.menu_title_clearance,
                                   &wPreferences.menu_title_min_height,
                                   &wPreferences.menu_title_max_height,
                                   window_flags,
                                   screen->menu_title_texture, NULL,
                                   screen->menu_title_color, &screen->menu_title_font,
                                   screen->w_depth, screen->w_visual, screen->w_colormap);

  menu->frame->core->descriptor.parent = menu;
  menu->frame->core->descriptor.parent_type = WCLASS_MENU;
  menu->frame->core->descriptor.handle_mousedown = menuMouseDown;

  wFrameWindowHideButton(menu->frame, WFF_RIGHT_BUTTON);

  if (title) {
    menu->frame->title = wstrdup(title);
  }

  menu->frame->flags.justification = WTJ_LEFT;
  menu->frame->rbutton_image = screen->b_pixmaps[WBUT_CLOSE];

  menu->items_count = 0;
  menu->allocated_items = 0;
  menu->selected_item_index = -1;
  menu->items = NULL;

  menu->frame_x = screen->app_menu_x;
  menu->frame_y = screen->app_menu_y;

  menu->frame->child = menu;

  menu->flags.lowered = 0;

  /* create borders */
  if (title) {
    /* setup object descriptors */
    menu->frame->on_mousedown_titlebar = menuTitleMouseDown;
    menu->frame->on_dblclick_titlebar = menuTitleDoubleClick;
  }

  menu->frame->on_click_right = menuCloseClick;

  menu->menu = wCoreCreate(menu->frame->core, 0, menu->frame->top_width, menu->frame->core->width, 10);

  menu->menu->descriptor.parent = menu;
  menu->menu->descriptor.parent_type = WCLASS_MENU;
  menu->menu->descriptor.handle_expose = menuExpose;
  menu->menu->descriptor.handle_mousedown = menuMouseDown;

  menu->menu_texture_data = None;

  XMapWindow(dpy, menu->menu->window);

  XFlush(dpy);

  if (!brother) {
    brother = 1;
    menu->brother = wMenuCreate(screen, title, main_menu);
    brother = 0;
    menu->brother->flags.brother = 1;
    menu->brother->brother = menu;
  }

  if (screen->notificationCenter) {
    CFNotificationCenterAddObserver(screen->notificationCenter, menu, appearanceObserver,
                                    WMDidChangeMenuAppearanceSettings, NULL,
                                    CFNotificationSuspensionBehaviorDeliverImmediately);
    CFNotificationCenterAddObserver(screen->notificationCenter, menu, appearanceObserver,
                                    WMDidChangeMenuTitleAppearanceSettings, NULL,
                                    CFNotificationSuspensionBehaviorDeliverImmediately);
  }
  
  return menu;
}

WMenu *wMenuCreateForApp(WScreen *screen, const char *title, int main_menu)
{
  WMenu *menu;

  menu = wMenuCreate(screen, title, main_menu);
  if (!menu)
    return NULL;
  menu->flags.app_menu = 1;
  menu->brother->flags.app_menu = 1;

  return menu;
}


static void insertItem(WMenu *menu, WMenuItem *item, int index)
{
  int i;

  for (i = menu->items_count - 1; i >= index; i--) {
    menu->items[i]->index++;
    menu->items[i + 1] = menu->items[i];
  }
  menu->items[index] = item;
}

WMenuItem *wMenuItemInsert(WMenu *menu, int index, const char *text,
                            void (*callback) (WMenu *menu, WMenuItem *item),
                            void *clientdata)
{
  WMenuItem *item;

  menu->flags.realized = 0;
  menu->brother->flags.realized = 0;

  /* reallocate array if it's too small */
  if (menu->items_count >= menu->allocated_items) {
    void *tmp;

    tmp = wrealloc(menu->items, sizeof(WMenuItem) * (menu->allocated_items + 5));

    menu->items = tmp;
    menu->allocated_items += 5;

    menu->brother->items = tmp;
    menu->brother->allocated_items = menu->allocated_items;
  }
  item = wmalloc(sizeof(WMenuItem));
  item->menu = menu;
  item->flags.enabled = 1;
  item->text = wstrdup(text);
  item->submenu_index = -1;
  item->clientdata = clientdata;
  item->callback = callback;
  item->shortcut = NULL;
  if (index < 0 || index >= menu->items_count) {
    item->index = menu->items_count;
    menu->items[menu->items_count] = item;
  } else {
    item->index = index;
    insertItem(menu, item, index);
  }

  menu->items_count++;
  menu->brother->items_count = menu->items_count;

  return item;
}

void wMenuItemRemove(WMenu *menu, int index)
{
  int i;

  if (menu->flags.brother) {
    wMenuItemRemove(menu->brother, index);
    return;
  }

  if (index >= menu->items_count)
    return;

  /* destroy cascade menu */
  wMenuItemRemoveSubmenu(menu, menu->items[index]);

  /* destroy unshared data */

  if (menu->items[index]->text) {
    wfree(menu->items[index]->text);
  }
  if (menu->items[index]->rtext) {
    wfree(menu->items[index]->rtext);
  }
  if (menu->items[index]->free_cdata && menu->items[index]->clientdata) {
    (*menu->items[index]->free_cdata) (menu->items[index]->clientdata);
  }
  if (menu->items[index]->shortcut) {
    wfree(menu->items[index]->shortcut);
  }
  wfree(menu->items[index]);

  for (i = index; i < menu->items_count - 1; i++) {
    menu->items[i + 1]->index--;
    menu->items[i] = menu->items[i + 1];
  }
  menu->items_count--;
  menu->brother->items_count--;
}

/* `shortcut_desc` should be in the format like: "Alt+h", "Control+Alt+h" etc.
   NOTE: "Alt+H" and "Alt+Shift+h" are equivalent. */
void wMenuItemSetShortcut(WMenuItem *item, const char *shortcut_desc)
{
  KeySym ksym;
  char *k, *b;
  char buf[MAX_SHORTCUT_LENGTH];

  item->shortcut = wmalloc(sizeof(WShortKey));

  wstrlcpy(buf, shortcut_desc, MAX_SHORTCUT_LENGTH);

  b = (char *)buf;

  /* get modifiers */
  item->shortcut->modifier = 0;
  while ((k = strchr(b, '+')) != NULL) {
    int mod;
    *k = 0;
    mod = wXModifierFromKey(b);
    if (mod < 0) {
      WMLogWarning(_("Failed to get modifier for shortcut \"%s\""), shortcut_desc);
      wfree(item->shortcut);
      item->shortcut = NULL;
      break;
    }
    item->shortcut->modifier |= mod;
    b = k + 1;
  }

  /* get key */
  if (item->shortcut) {
    if (isupper(b[0])) {
      item->shortcut->modifier |= wXModifierFromKey("Shift");
    }
    ksym = XStringToKeysym(b);

    if (ksym == NoSymbol) {
      WMLogWarning(_("Failed to get KeySym for shortcut \"%s\""), shortcut_desc);
      wfree(item->shortcut);
      item->shortcut = NULL;
    } else {
      item->shortcut->keycode = XKeysymToKeycode(dpy, ksym);
      if (item->shortcut->keycode == 0) {
        WMLogWarning(_("Failed to get Keycode for shortcut \"%s\""), shortcut_desc);
        wfree(item->shortcut);
        item->shortcut = NULL;
      } else {
        if (item->shortcut->modifier & wXModifierFromKey("Shift")) {
          b[0] = toupper(b[0]);
        }
        item->rtext = wstrdup(b);
      }
    }
  }
}

void wMenuItemSetSubmenu(WMenu *menu, WMenuItem *item, WMenu *submenu)
{
  WMenu *brother = menu->brother;
  int i, done;

  assert(menu->flags.brother == 0);

  if (item->submenu_index >= 0) {
    menu->flags.realized = 0;
    brother->flags.realized = 0;
  }

  submenu->parent = menu;

  submenu->brother->parent = brother;

  done = 0;
  for (i = 0; i < menu->submenus_count; i++) {
    if (menu->submenus[i] == NULL) {
      menu->submenus[i] = submenu;
      brother->submenus[i] = submenu->brother;
      done = 1;
      item->submenu_index = i;
      break;
    }
  }
  if (!done) {
    item->submenu_index = menu->submenus_count;

    menu->submenus = wrealloc(menu->submenus, sizeof(menu->submenus[0]) * (menu->submenus_count + 1));
    menu->submenus[menu->submenus_count++] = submenu;

    brother->submenus = wrealloc(brother->submenus, sizeof(brother->submenus[0]) * (brother->submenus_count + 1));
    brother->submenus[brother->submenus_count++] = submenu->brother;
  }

  if (menu->flags.lowered) {

    submenu->flags.lowered = 1;
    ChangeStackingLevel(submenu->frame->core, NSNormalWindowLevel);

    submenu->brother->flags.lowered = 1;
    ChangeStackingLevel(submenu->brother->frame->core, NSNormalWindowLevel);
  }

  if (!menu->flags.realized)
    wMenuRealize(menu);
}

void wMenuItemRemoveSubmenu(WMenu *menu, WMenuItem *item)
{
  assert(menu->flags.brother == 0);

  /* destroy cascade menu */
  if (item->submenu_index >= 0 && menu->submenus && menu->submenus[item->submenu_index] != NULL) {

    wMenuDestroy(menu->submenus[item->submenu_index], True);

    menu->submenus[item->submenu_index] = NULL;
    menu->brother->submenus[item->submenu_index] = NULL;

    item->submenu_index = -1;
  }
}

#define F_NORMAL	0
#define F_TOP		1
#define F_BOTTOM	2
#define F_NONE		3
static void drawFrame(WScreen *scr, Drawable win, int y, int w, int h, int type)
{
  XSegment segs[2];
  int i;

  i = 0;
  segs[i].x1 = segs[i].x2 = w - 1;
  segs[i].y1 = y;
  segs[i].y2 = y + h - 1;
  i++;
  if (type != F_TOP && type != F_NONE) {
    segs[i].x1 = 1;
    segs[i].y1 = segs[i].y2 = y + h - 2;
    segs[i].x2 = w - 1;
    i++;
  }
  XDrawSegments(dpy, win, scr->menu_item_auxtexture->dim_gc, segs, i);

  i = 0;
  segs[i].x1 = 0;
  segs[i].y1 = y;
  segs[i].x2 = 0;
  segs[i].y2 = y + h - 1;
  i++;
  if (type != F_BOTTOM && type != F_NONE) {
    segs[i].x1 = 0;
    segs[i].y1 = y;
    segs[i].x2 = w - 1;
    segs[i].y2 = y;
    i++;
  }
  XDrawSegments(dpy, win, scr->menu_item_auxtexture->light_gc, segs, i);

  if (type != F_TOP && type != F_NONE)
    XDrawLine(dpy, win, scr->menu_item_auxtexture->dark_gc, 0, y + h - 1, w - 1, y + h - 1);
}

void wMenuItemPaint(WMenu *menu, int item_index, int selected)
{
  WScreen *scr = menu->frame->screen_ptr;
  Window win = menu->menu->window;
  WMenuItem *item = menu->items[item_index];
  GC light, dim, dark;
  WMColor *color;
  int x, y, w, h, tw;
  int type;

  if (!menu->flags.realized)
    return;
  h = menu->item_height;
  w = menu->menu->width;
  y = item_index * h;

  light = scr->menu_item_auxtexture->light_gc;
  dim = scr->menu_item_auxtexture->dim_gc;
  dark = scr->menu_item_auxtexture->dark_gc;

  if (wPreferences.menu_style == MS_FLAT && menu->items_count > 1) {
    if (item_index == 0)
      type = F_TOP;
    else if (item_index == menu->items_count - 1)
      type = F_BOTTOM;
    else
      type = F_NONE;
  } else {
    type = F_NORMAL;
  }

  /* paint background */
  if (selected) {
    XFillRectangle(dpy, win, WMColorGC(scr->select_color), 1, y + 1, w - 2, h - 3);
    if (scr->menu_item_texture->any.type == WTEX_SOLID)
      drawFrame(scr, win, y, w, h, type);
  } else {
    if (scr->menu_item_texture->any.type == WTEX_SOLID) {
      XClearArea(dpy, win, 0, y + 1, w - 1, h - 3, False);
      /* draw the frame */
      drawFrame(scr, win, y, w, h, type);
    } else {
      XClearArea(dpy, win, 0, y, w, h, False);
    }
  }

  if (selected) {
    if (item->flags.enabled)
      color = scr->select_text_color;
    else
      color = scr->dtext_color;
  } else if (!item->flags.enabled) {
    color = scr->dtext_color;
  } else {
    color = scr->mtext_color;
  }
  /* draw text */
  x = 5;
  if (item->flags.indicator)
    x += MENU_INDICATOR_SPACE + 2;

  WMDrawString(scr->wmscreen, win, color, scr->menu_item_font,
               x, 3 + y + wPreferences.menu_text_clearance, item->text, strlen(item->text));

  if (item->submenu_index >= 0) {
    /* draw the cascade indicator */
    XDrawLine(dpy, win, dim, w - 11, y + 6, w - 6, y + h / 2 - 1);
    XDrawLine(dpy, win, light, w - 11, y + h - 8, w - 6, y + h / 2 - 1);
    XDrawLine(dpy, win, dark, w - 12, y + 6, w - 12, y + h - 8);
  }

  /* draw indicator */
  if (item->flags.indicator && item->flags.indicator_on) {
    int iw, ih;
    WPixmap *indicator;

    switch (item->flags.indicator_type) {
    case MI_CHECK:
      indicator = scr->menu_check_indicator;
      break;
    case MI_MINIWINDOW:
      indicator = scr->menu_mini_indicator;
      break;
    case MI_HIDDEN:
      indicator = scr->menu_hide_indicator;
      break;
    case MI_SHADED:
      indicator = scr->menu_shade_indicator;
      break;
    case MI_DIAMOND:
    default:
      indicator = scr->menu_radio_indicator;
      break;
    }

    iw = indicator->width;
    ih = indicator->height;
    XSetClipMask(dpy, scr->copy_gc, indicator->mask);
    XSetClipOrigin(dpy, scr->copy_gc, 5, y + (h - ih) / 2);
    if (selected) {
      if (item->flags.enabled) {
        XSetForeground(dpy, scr->copy_gc, WMColorPixel(scr->select_text_color));
      } else {
        XSetForeground(dpy, scr->copy_gc, WMColorPixel(scr->dtext_color));
      }
    } else {
      if (item->flags.enabled) {
        XSetForeground(dpy, scr->copy_gc, WMColorPixel(scr->mtext_color));
      } else {
        XSetForeground(dpy, scr->copy_gc, WMColorPixel(scr->dtext_color));
      }
    }
    XFillRectangle(dpy, win, scr->copy_gc, 5, y + (h - ih) / 2, iw, ih);
    /*
      XCopyArea(dpy, indicator->image, win, scr->copy_gc, 0, 0,
      iw, ih, 5, y+(h-ih)/2);
    */
    XSetClipOrigin(dpy, scr->copy_gc, 0, 0);
  }

  /* draw right text */

  if (item->rtext && item->submenu_index < 0) {
    tw = WMWidthOfString(scr->menu_item_font, item->rtext, strlen(item->rtext));

    WMDrawString(scr->wmscreen, win, color, scr->menu_item_font, w - 6 - tw,
                 y + 3 + wPreferences.menu_text_clearance, item->rtext, strlen(item->rtext));
  }
}

void wMenuItemSelect(WMenu *menu, int item_index)
{
  WMenuItem *item;
  WMenu *submenu;
  int old_item_index;

  if (menu->items == NULL)
    return;

  if (item_index >= menu->items_count)
    return;

  old_item_index = menu->selected_item_index;
  menu->selected_item_index = item_index;

  if (old_item_index != item_index) {
    /* unselect previous item */
    if (old_item_index >= 0) {
      wMenuItemPaint(menu, old_item_index, False);
      item = menu->items[old_item_index];
      /* unmap cascade */
      if (item->submenu_index >= 0 && menu->submenus) {
        if (!menu->submenus[item->submenu_index]->flags.tornoff) {
          wMenuItemSelect(menu->submenus[item->submenu_index], -1);
          wMenuUnmap(menu->submenus[item->submenu_index]);
        }
      }
    }

    if (item_index < 0) {
      menu->selected_item_index = -1;
      return;
    }
    item = menu->items[item_index];

    if (item->flags.enabled && item->submenu_index >= 0 && menu->submenus) {
      submenu = menu->submenus[item->submenu_index];
      if (submenu && submenu->flags.brother) {
        submenu = submenu->brother;
      }
      if (item->callback) {
        /* Only call the callback if the submenu is not yet mapped. */
        if (menu->flags.brother) {
          if (!submenu || !submenu->flags.mapped) {
            (*item->callback) (menu->brother, item);
          }
        } else if (!submenu || !submenu->flags.tornoff) {
          (*item->callback) (menu, item);
        }
      }
      /* the submenu menu might have changed */
      submenu = menu->submenus[item->submenu_index];

      /* map submenu */
      if (!submenu->flags.mapped) {
        int x, y;

        x = menu->frame_x + MENU_WIDTH(menu);

        if (wPreferences.align_menus) {
          y = menu->frame_y;
        } else {
          y = menu->frame_y + menu->item_height * item_index;
          if (menu->flags.titled) {
            y += menu->frame->top_width;
          }
          if (menu->submenus[item->submenu_index]->flags.titled) {
            y -= menu->submenus[item->submenu_index]->frame->top_width;
          }
        }

        wMenuMapAt(menu->submenus[item->submenu_index], x, y, False);
        menu->submenus[item->submenu_index]->parent = menu;
      } else {
        return;
      }
    }
    wMenuItemPaint(menu, item_index, True);
  }
}


static Pixmap renderTexture(WMenu *menu)
{
  RImage *img;
  Pixmap pix;
  int i;
  RColor light;
  RColor dark;
  RColor mid;
  WScreen *scr = menu->menu->screen_ptr;
  WTexture *texture = scr->menu_item_texture;

  if (wPreferences.menu_style == MS_NORMAL) {
    img = wTextureRenderImage(texture, menu->menu->width, menu->item_height, WREL_MENUENTRY);
  } else {
    img = wTextureRenderImage(texture, menu->menu->width, menu->menu->height + 1, WREL_MENUENTRY);
  }
  if (!img) {
    WMLogWarning(_("could not render texture: %s"), RMessageForError(RErrorCode));

    return None;
  }

  if (wPreferences.menu_style == MS_SINGLE_TEXTURE) {
    light.alpha = 0;
    light.red = light.green = light.blue = 80;

    dark.alpha = 255;
    dark.red = dark.green = dark.blue = 0;

    mid.alpha = 0;
    mid.red = mid.green = mid.blue = 40;

    for (i = 1; i < menu->items_count; i++) {
      ROperateLine(img, RSubtractOperation, 0, i *menu->item_height - 2,
                   menu->menu->width - 1, i *menu->item_height - 2, &mid);

      RDrawLine(img, 0, i *menu->item_height - 1,
                menu->menu->width - 1, i *menu->item_height - 1, &dark);

      ROperateLine(img, RAddOperation, 0, i *menu->item_height,
                   menu->menu->width - 1, i *menu->item_height, &light);
    }
  }
  if (!RConvertImage(scr->rcontext, img, &pix)) {
    WMLogWarning(_("error rendering image:%s"), RMessageForError(RErrorCode));
  }
  RReleaseImage(img);

  return pix;
}

static void updateTexture(WMenu *menu)
{
  WScreen *scr = menu->menu->screen_ptr;

  /* setup background texture */
  if (scr->menu_item_texture->any.type != WTEX_SOLID) {
    if (!menu->flags.brother) {
      FREE_PIXMAP(menu->menu_texture_data);

      menu->menu_texture_data = renderTexture(menu);

      XSetWindowBackgroundPixmap(dpy, menu->menu->window, menu->menu_texture_data);
      XClearWindow(dpy, menu->menu->window);

      XSetWindowBackgroundPixmap(dpy, menu->brother->menu->window, menu->menu_texture_data);
      XClearWindow(dpy, menu->brother->menu->window);
    }
  } else {
    XSetWindowBackground(dpy, menu->menu->window, scr->menu_item_texture->any.color.pixel);
    XClearWindow(dpy, menu->menu->window);
  }
}

void wMenuRealize(WMenu *menu)
{
  int i;
  int width, rwidth, mrwidth, mwidth;
  int theight, twidth, eheight;
  WScreen *scr = menu->frame->screen_ptr;
  static int brother_done = 0;
  int flags;

  if (!brother_done) {
    brother_done = 1;
    wMenuRealize(menu->brother);
    brother_done = 0;
  }

  flags = WFF_SINGLE_STATE | WFF_BORDER;
  if (menu->flags.titled)
    flags |= WFF_TITLEBAR | WFF_RIGHT_BUTTON;

  wFrameWindowUpdateBorders(menu->frame, flags);

  if (menu->flags.titled) {
    twidth = WMWidthOfString(scr->menu_title_font, menu->frame->title, strlen(menu->frame->title));
    theight = menu->frame->top_width;
    twidth += theight + (wPreferences.titlebar_style == TS_NEW ? 16 : 8);
  } else {
    twidth = 0;
    theight = 0;
  }
  eheight = WMFontHeight(scr->menu_item_font) + 6 + wPreferences.menu_text_clearance * 2;
  menu->item_height = eheight;
  mrwidth = 0;
  mwidth = 0;
  for (i = 0; i < menu->items_count; i++) {
    char *text;

    /* search widest text */
    text = menu->items[i]->text;
    width = WMWidthOfString(scr->menu_item_font, text, strlen(text)) + 10;

    if (menu->items[i]->flags.indicator) {
      width += MENU_INDICATOR_SPACE;
    }

    if (width > mwidth)
      mwidth = width;

    /* search widest text on right */
    text = menu->items[i]->rtext;
    if (text)
      rwidth = WMWidthOfString(scr->menu_item_font, text, strlen(text))
        + 10;
    else if (menu->items[i]->submenu_index >= 0)
      rwidth = 16;
    else
      rwidth = 4;

    if (rwidth > mrwidth)
      mrwidth = rwidth;
  }
  mwidth += mrwidth;

  if (mwidth < twidth)
    mwidth = twidth;

  wCoreConfigure(menu->menu, 0, theight, mwidth, menu->items_count * eheight - 1);

  wFrameWindowResize(menu->frame, mwidth, menu->items_count * eheight - 1
                     + menu->frame->top_width + menu->frame->bottom_width);

  updateTexture(menu);

  menu->flags.realized = 1;

  if (menu->flags.mapped)
    wMenuPaint(menu);
  if (menu->brother->flags.mapped)
    wMenuPaint(menu->brother);
}

void wMenuDestroy(WMenu *menu, int recurse)
{
  int i;

  if (menu->frame->screen_ptr->notificationCenter) {
    CFNotificationCenterRemoveObserver(menu->frame->screen_ptr->notificationCenter,
                                       menu, WMDidChangeMenuAppearanceSettings, NULL);
    CFNotificationCenterRemoveObserver(menu->frame->screen_ptr->notificationCenter,
                                       menu, WMDidChangeMenuTitleAppearanceSettings, NULL);
  }
  /* remove any pending timers */
  if (menu->timer) {
    WMDeleteTimerHandler(menu->timer);
  }
  menu->timer = NULL;

  /* call destroy handler */
  if (menu->on_destroy) {
    (*menu->on_destroy) (menu);
  }
  /* Destroy items if this menu own them. If this is the "brother" menu,
   * leave them alone as it is shared by them.
   */
  if (!menu->flags.brother) {
    for (i = 0; i < menu->items_count; i++) {
      if (menu->items[i]->text) {
        wfree(menu->items[i]->text);
      }
      if (menu->items[i]->rtext) {
        wfree(menu->items[i]->rtext);
      }
      if (menu->items[i]->free_cdata && menu->items[i]->clientdata) {
        (*menu->items[i]->free_cdata) (menu->items[i]->clientdata);
      }
      if (menu->items[i]->shortcut) {
        wfree(menu->items[i]->shortcut);
      }
      wfree(menu->items[i]);
    }

    if (recurse) {
      for (i = 0; i < menu->submenus_count; i++) {
        if (menu->submenus[i]) {
          if (menu->submenus[i]->flags.brother)
            wMenuDestroy(menu->submenus[i]->brother, recurse);
          else
            wMenuDestroy(menu->submenus[i], recurse);
        }
      }
    }

    if (menu->items) {
      wfree(menu->items);
    }
  }

  FREE_PIXMAP(menu->menu_texture_data);

  if (menu->submenus) {
    wfree(menu->submenus);
  }
  wCoreDestroy(menu->menu);
  wFrameWindowDestroy(menu->frame);

  /* destroy copy of this menu */
  if (!menu->flags.brother && menu->brother) {
    wMenuDestroy(menu->brother, False);
  }
  wfree(menu);
}

static void move_menus(WMenu *menu, int x, int y)
{
  while (menu->parent) {
    menu = menu->parent;
    x -= MENU_WIDTH(menu);
    if (!wPreferences.align_menus && menu->selected_item_index >= 0) {
      y -= menu->selected_item_index *menu->item_height;
    }
  }
  wMenuMove(menu, x, y, True);
}

static void makeVisible(WMenu *menu)
{
  WScreen *scr = menu->frame->screen_ptr;
  int x1, y1, x2, y2, new_x, new_y;
  WMRect rect = wGetRectForHead(scr, wGetHeadForPointerLocation(scr));

  if (menu->items_count < 0)
    return;

  x1 = menu->frame_x;
  y1 = menu->frame_y + menu->frame->top_width + menu->selected_item_index *menu->item_height;
  x2 = x1 + MENU_WIDTH(menu);
  y2 = y1 + menu->item_height;

  new_x = x1;
  new_y = y1;

  if (x1 < rect.pos.x) {
    new_x = rect.pos.x;
  } else if (x2 >= rect.pos.x + rect.size.width) {
    new_x = rect.pos.x + rect.size.width - MENU_WIDTH(menu) - 1;
  }

  if (y1 < rect.pos.y) {
    new_y = rect.pos.y;
  } else if (y2 >= rect.pos.y + rect.size.height) {
    new_y = rect.pos.y + rect.size.height - menu->item_height - 1;
  }

  new_y = new_y - menu->frame->top_width - menu->selected_item_index *menu->item_height;
  move_menus(menu, new_x, new_y);
}

static int check_key(WMenu *menu, XKeyEvent * event)
{
  int i, ch, s;
  char buffer[32];

  if (XLookupString(event, buffer, 32, NULL, NULL) < 1)
    return -1;

  ch = toupper(buffer[0]);

  s = (menu->selected_item_index >= 0 ? menu->selected_item_index + 1 : 0);

 again:
  for (i = s; i < menu->items_count; i++) {
    if (ch == toupper(menu->items[i]->text[0])) {
      return i;
    }
  }
  /* no match. Retry from start, if previous started from a selected item */
  if (s != 0) {
    s = 0;
    goto again;
  }
  return -1;
}

static int keyboardMenu(WMenu *menu)
{
  XEvent event;
  KeySym ksym = NoSymbol;
  int done = 0;
  int index;
  WMenuItem *item;
  int old_pos_x = menu->frame_x;
  int old_pos_y = menu->frame_y;
  int new_x = old_pos_x, new_y = old_pos_y;
  WMRect rect = wGetRectForHead(menu->frame->screen_ptr,
                                wGetHeadForPointerLocation(menu->frame->screen_ptr));

  XGrabKeyboard(dpy, menu->frame->core->window, True, GrabModeAsync, GrabModeAsync, CurrentTime);

  if (menu->frame_y + menu->frame->top_width >= rect.pos.y + rect.size.height)
    new_y = rect.pos.y + rect.size.height - menu->frame->top_width;

  if (menu->frame_x + MENU_WIDTH(menu) >= rect.pos.x + rect.size.width)
    new_x = rect.pos.x + rect.size.width - MENU_WIDTH(menu) - 1;

  move_menus(menu, new_x, new_y);

  while (!done && menu->flags.mapped) {
    XAllowEvents(dpy, AsyncKeyboard, CurrentTime);
    WMMaskEvent(dpy, ExposureMask | ButtonMotionMask | ButtonPressMask
                | ButtonReleaseMask | KeyPressMask | KeyReleaseMask | SubstructureNotifyMask, &event);

    switch (event.type) {
    case KeyPress:
      ksym = XLookupKeysym(&event.xkey, 0);
      if (wPreferences.vi_key_menus) {
        switch (ksym) {
        case XK_h:
          ksym = XK_Left;
          break;

        case XK_j:
          ksym = XK_Down;
          break;

        case XK_k:
          ksym = XK_Up;
          break;

        case XK_l:
          ksym = XK_Right;
          break;

        }
      }
      switch (ksym) {
      case XK_Escape:
        done = 1;
        break;

      case XK_Home:
#ifdef XK_KP_Home
      case XK_KP_Home:
#endif
        wMenuItemSelect(menu, 0);
        makeVisible(menu);
        break;

      case XK_End:
#ifdef XK_KP_End
      case XK_KP_End:
#endif
        wMenuItemSelect(menu, menu->items_count - 1);
        makeVisible(menu);
        break;

      case XK_Up:
#ifdef XK_KP_Up
      case XK_KP_Up:
#endif
        if (menu->selected_item_index <= 0)
          wMenuItemSelect(menu, menu->items_count - 1);
        else
          wMenuItemSelect(menu, menu->selected_item_index - 1);
        makeVisible(menu);
        break;

      case XK_Down:
#ifdef XK_KP_Down
      case XK_KP_Down:
#endif
        if (menu->selected_item_index < 0)
          wMenuItemSelect(menu, 0);
        else if (menu->selected_item_index == menu->items_count - 1)
          wMenuItemSelect(menu, 0);
        else if (menu->selected_item_index < menu->items_count - 1)
          wMenuItemSelect(menu, menu->selected_item_index + 1);
        makeVisible(menu);
        break;

      case XK_Right:
#ifdef XK_KP_Right
      case XK_KP_Right:
#endif
        if (menu->selected_item_index >= 0) {
          WMenuItem *item;
          item = menu->items[menu->selected_item_index];

          if (item->submenu_index >= 0 && menu->submenus
              && menu->submenus[item->submenu_index]->items_count > 0) {

            XUngrabKeyboard(dpy, CurrentTime);

            wMenuItemSelect(menu->submenus[item->submenu_index], 0);
            if (!keyboardMenu(menu->submenus[item->submenu_index]))
              done = 1;

            XGrabKeyboard(dpy, menu->frame->core->window, True,
                          GrabModeAsync, GrabModeAsync, CurrentTime);
          }
        }
        break;

      case XK_Left:
#ifdef XK_KP_Left
      case XK_KP_Left:
#endif
        if (menu->parent != NULL && menu->parent->selected_item_index >= 0) {
          wMenuItemSelect(menu, -1);
          move_menus(menu, old_pos_x, old_pos_y);
          return True;
        }
        break;

      case XK_Return:
#ifdef XK_KP_Enter
      case XK_KP_Enter:
#endif
        done = 2;
        break;

      default:
        index = check_key(menu, &event.xkey);
        if (index >= 0) {
          wMenuItemSelect(menu, index);
        }
      }
      break;

    default:
      if (event.type == ButtonPress)
        done = 1;

      WMHandleEvent(&event);
    }
  }

  XUngrabKeyboard(dpy, CurrentTime);

  if (done == 2 && menu->selected_item_index >= 0) {
    item = menu->items[menu->selected_item_index];
  } else {
    item = NULL;
  }

  if (item && item->callback != NULL && item->flags.enabled && item->submenu_index < 0) {
    wMenuItemSelect(menu, -1);

    if (!menu->flags.tornoff) {
      wMenuUnmap(menu);
      move_menus(menu, old_pos_x, old_pos_y);
    }
    closeCascade(menu);

    (*item->callback) (menu, item);
  } else {
    if (!menu->flags.tornoff) {
      wMenuUnmap(menu);
      move_menus(menu, old_pos_x, old_pos_y);
    }
    wMenuItemSelect(menu, -1);
  }

  /* returns True if returning from a submenu to a parent menu,
   * False if exiting from menu */
  return False;
}

void wMenuMapAt(WMenu *menu, int x, int y, int keyboard)
{
  if (!menu->flags.realized) {
    wMenuRealize(menu);
  }

  if (!menu->flags.mapped) {
    menu->frame_x = x;
    menu->frame_y = y;
    wMenuMap(menu);
  } else {
    wMenuItemSelect(menu, 0);
  }

  if (keyboard)
    keyboardMenu(menu);
}

void wMenuMap(WMenu *menu)
{
  if (!menu->flags.realized) {
    wMenuRealize(menu);
  }
  XMoveWindow(dpy, menu->frame->core->window, menu->frame_x, menu->frame_y);
  XMapWindow(dpy, menu->frame->core->window);
  wRaiseFrame(menu->frame->core);
  menu->flags.mapped = 1;
}

void wMenuUnmap(WMenu *menu)
{
  int i;

  XUnmapWindow(dpy, menu->frame->core->window);
  menu->flags.mapped = 0;

  for (i = 0; i < menu->submenus_count; i++) {
    if (menu->submenus[i] != NULL && menu->submenus[i]->flags.mapped
        && !menu->submenus[i]->flags.tornoff) {
      wMenuUnmap(menu->submenus[i]);
    }
  }
  /* menu->selected_item_index = -1; */
}

void wMenuPaint(WMenu *menu)
{
  int i;

  if (!menu->flags.mapped) {
    return;
  }

  /* paint entries */
  for (i = 0; i < menu->items_count; i++) {
    wMenuItemPaint(menu, i, i == menu->selected_item_index);
  }
}

void wMenuSetEnabled(WMenu *menu, int index, int enable)
{
  if (index >= menu->items_count)
    return;
  menu->items[index]->flags.enabled = enable;
  wMenuItemPaint(menu, index, index == menu->selected_item_index);
  wMenuItemPaint(menu->brother, index, index == menu->selected_item_index);
}

void wMenuItemSetEnabled(WMenu *menu, WMenuItem *item, Bool enable)
{
  if (!menu || !item) {
    return;
  }
  
  for (int i = 0; i < menu->items_count; i++) {
    if (menu->items[i] == item) {
      menu->items[i]->flags.enabled = enable;
      wMenuItemPaint(menu, i, i == menu->selected_item_index);
      wMenuItemPaint(menu->brother, i, i == menu->selected_item_index);
    }
  }
}

static WMenu *findMenu(WScreen *scr, int *x_ret, int *y_ret)
{
  WMenu *menu;
  WObjDescriptor *desc;
  Window root_ret, win, junk_win;
  int x, y, wx, wy;
  unsigned int mask;

  *x_ret = -1;
  *y_ret = -1;

  XQueryPointer(dpy, scr->root_win, &root_ret, &win, &x, &y, &wx, &wy, &mask);

  if (win == None) {
    return NULL;
  }
  if (XFindContext(dpy, win, w_global.context.client_win, (XPointer *) & desc) == XCNOENT) {
    return NULL;
  }
  if (desc->parent_type == WCLASS_MENU) {
    menu = (WMenu *) desc->parent;
    XTranslateCoordinates(dpy, root_ret, menu->menu->window, wx, wy, x_ret, y_ret, &junk_win);
    return menu;
  }
  return NULL;
}

static void closeCascade(WMenu *menu)
{
  WMenu *parent = menu->parent;

  if (menu->flags.brother || (!menu->flags.tornoff && (!menu->flags.app_menu || menu->parent))) {
    wMenuItemSelect(menu, -1);
    XSync(dpy, 0);
    wMenuUnmap(menu);
    while (parent != NULL
           && (parent->parent != NULL || !parent->flags.app_menu || parent->flags.brother)
           && !parent->flags.tornoff) {
      wMenuItemSelect(parent, -1);
      wMenuUnmap(parent);
      parent = parent->parent;
    }
    if (parent) {
      wMenuItemSelect(parent, -1);
    }
  }
}

#define getItemIndexAt(menu, x, y)   ((y)<0 ? -1 : (y)/(menu->item_height))

static WMenu *parentMenu(WMenu *menu)
{
  WMenu *parent;
  WMenuItem *item;

  if (menu->flags.tornoff)
    return menu;

  while (menu->parent && menu->parent->flags.mapped) {
    parent = menu->parent;
    if (parent->selected_item_index < 0)
      break;
    item = parent->items[parent->selected_item_index];
    if (!item->flags.enabled || item->submenu_index < 0 || !parent->submenus ||
        parent->submenus[item->submenu_index] != menu)
      break;
    menu = parent;
    if (menu->flags.tornoff)
      break;
  }

  return menu;
}

/*
 * Will raise the passed menu, if submenu = 0
 * If submenu > 0 will also raise all mapped submenus
 * until the first buttoned one
 * If submenu < 0 will also raise all mapped parent menus
 * until the first buttoned one
 */

static void raiseMenus(WMenu *menu, int submenus)
{
  WMenu *submenu;
  int i;

  if (!menu)
    return;

  wRaiseFrame(menu->frame->core);

  if (submenus > 0 && menu->selected_item_index >= 0) {
    i = menu->items[menu->selected_item_index]->submenu_index;
    if (i >= 0 && menu->submenus) {
      submenu = menu->submenus[i];
      if (submenu->flags.mapped && !submenu->flags.tornoff)
        raiseMenus(submenu, submenus);
    }
  }
  if (submenus < 0 && !menu->flags.tornoff && menu->parent && menu->parent->flags.mapped)
    raiseMenus(menu->parent, submenus);
}

WMenu *wMenuUnderPointer(WScreen *screen)
{
  WObjDescriptor *desc;
  Window root_ret, win;
  int dummy;
  unsigned int mask;

  XQueryPointer(dpy, screen->root_win, &root_ret, &win, &dummy, &dummy, &dummy, &dummy, &mask);

  if (win == None)
    return NULL;

  if (XFindContext(dpy, win, w_global.context.client_win, (XPointer *) & desc) == XCNOENT)
    return NULL;

  if (desc->parent_type == WCLASS_MENU)
    return (WMenu *) desc->parent;
  return NULL;
}

static void getPointerPosition(WScreen *scr, int *x, int *y)
{
  Window root_ret, win;
  int wx, wy;
  unsigned int mask;

  XQueryPointer(dpy, scr->root_win, &root_ret, &win, x, y, &wx, &wy, &mask);
}

static void getScrollAmount(WMenu *menu, int *hamount, int *vamount)
{
  WScreen *scr = menu->menu->screen_ptr;
  int menuX1 = menu->frame_x;
  int menuY1 = menu->frame_y;
  int menuX2 = menu->frame_x + MENU_WIDTH(menu);
  int menuY2 = menu->frame_y + MENU_HEIGHT(menu);
  int xroot, yroot;
  WMRect rect = wGetRectForHead(scr, wGetHeadForPointerLocation(scr));

  *hamount = 0;
  *vamount = 0;

  getPointerPosition(scr, &xroot, &yroot);

  if (xroot <= (rect.pos.x + 1) && menuX1 < rect.pos.x) {
    /* scroll to the right */
    *hamount = WMIN(MENU_SCROLL_STEP, abs(menuX1));
  } else if (xroot >= (rect.pos.x + rect.size.width - 2) &&
             menuX2 > (rect.pos.x + rect.size.width - 1)) {
    /* scroll to the left */
    *hamount = WMIN(MENU_SCROLL_STEP, abs((int)(menuX2 - rect.pos.x - rect.size.width - 1)));
    if (*hamount == 0) {
      *hamount = 1;
    }
    *hamount = -*hamount;
  }

  if (yroot <= (rect.pos.y + 1) && menuY1 < rect.pos.y) {
    /* scroll down */
    *vamount = WMIN(MENU_SCROLL_STEP, abs(menuY1));
  } else if (yroot >= (rect.pos.y + rect.size.height - 2) &&
             menuY2 > (rect.pos.y + rect.size.height - 1)) {
    /* scroll up */
    *vamount = WMIN(MENU_SCROLL_STEP, abs((int)(menuY2 - rect.pos.y - rect.size.height - 2)));
    *vamount = -*vamount;
  }
}

static void dragScrollMenuCallback(CFRunLoopTimerRef timer, void *data)
{
  WMenu *menu = (WMenu *) data;
  WScreen *scr = menu->menu->screen_ptr;
  WMenu *parent = parentMenu(menu);
  int hamount, vamount;
  int x, y;
  int new_selected_item_index;

  getScrollAmount(menu, &hamount, &vamount);

  if (hamount != 0 || vamount != 0) {
    wMenuMove(parent, parent->frame_x + hamount, parent->frame_y + vamount, True);
    if (findMenu(scr, &x, &y)) {
      new_selected_item_index = getItemIndexAt(menu, x, y);
      wMenuItemSelect(menu, new_selected_item_index);
    } else {
      /* Pointer fell outside of menu. If the selected item is
       * not a submenu, unselect it */
      if (menu->selected_item_index >= 0 && menu->items[menu->selected_item_index]->submenu_index < 0)
        wMenuItemSelect(menu, -1);
      new_selected_item_index = 0;
    }

    /* paranoid check */
    if (new_selected_item_index >= 0) {
      /* keep scrolling */
      menu->timer = WMAddTimerHandler(MENU_SCROLL_DELAY, 0, dragScrollMenuCallback, menu);
    } else {
      if (menu->timer)
        WMDeleteTimerHandler(menu->timer);
      menu->timer = NULL;
    }
  } else {
    /* don't need to scroll anymore */
    if (menu->timer) {
      WMDeleteTimerHandler(menu->timer);
    }
    menu->timer = NULL;
    if (findMenu(scr, &x, &y)) {
      new_selected_item_index = getItemIndexAt(menu, x, y);
      wMenuItemSelect(menu, new_selected_item_index);
    }
  }
}

static void trackMenuMouse(WMenu *menu);

Bool isMenuContainsSubmenu(WMenu *menu, WMenu *submenu)
{
  WMenu *smenu;
  Bool result = False;

  for (int i = 0; i < menu->submenus_count; i++) {
    smenu = menu->submenus[i];
    if (smenu == submenu || smenu->brother == submenu) {
      result = True;
      break;
    } else if (smenu->submenus_count > 0) {
      result = isMenuContainsSubmenu(smenu, submenu);
      if (result)
        break;
    }
  }
  return result;
}

static void slideMenuCallback(CFRunLoopTimerRef timer, void *data)
{
  WMenu *menu = (WMenu *)data;
  WScreen *scr = menu->menu->screen_ptr;
  WMRect rect = wGetRectForHead(scr, wGetHeadForPointerLocation(scr));
  WMenu *parent = parentMenu(menu);
  
  if ((menu->frame_y + MENU_HEIGHT(menu)) > rect.size.height) {
    wMenuMove(parent, parent->frame_x, parent->frame_y - 1, True);
  } else {
    /* don't need to scroll anymore */
    if (menu->timer) {
      WMDeleteTimerHandler(menu->timer);
    }
    menu->timer = NULL;
    trackMenuMouse(menu);
  }
}

static void slideMenuBackCallback(CFRunLoopTimerRef timer, void *data)
{
  WMenu *menu = (WMenu *)data;
  WMenu *parent = parentMenu(menu);
  
  if (menu->old_frame_y > 0 && menu->frame_y != menu->old_frame_y) {
    wMenuMove(parent, parent->frame_x, parent->frame_y + 1, True);
  } else {
    /* don't need to scroll anymore */
    if (menu->timer) {
      WMDeleteTimerHandler(menu->timer);
    }
    menu->timer = NULL;
    menu->old_frame_y = -1;
  }
}

static void trackMenuMouse(WMenu *menu)
{
  WMenu *smenu;
  WMenu *omenu = parentMenu(menu);
  WScreen *scr = menu->frame->screen_ptr;
  int done = 0;
  XEvent ev;
  Bool should_slide_back = True;

  if (omenu->timer) {
    WMDeleteTimerHandler(omenu->timer);
  }
  
  raiseMenus(omenu, True);

  /* WMLogInfo("Entering wMenuScroll event loop."); */
  while (!done) {
    WMNextEvent(dpy, &ev);
    switch (ev.type)
      {
      case LeaveNotify:
        {
          smenu = wMenuUnderPointer(scr);
          if (smenu == NULL || ((isMenuContainsSubmenu(menu, smenu) == False) && smenu != menu)) {
            done = 1;
          }
        }
        break;
      case ButtonPress:
        {
          Bool click_on_title = False;
          /* True if we push on title, or drag the omenu to other position */
          click_on_title = ev.xbutton.x_root >= omenu->frame_x &&
            ev.xbutton.x_root <= omenu->frame_x + MENU_WIDTH(omenu) &&
            ev.xbutton.y_root >= omenu->frame_y &&
            ev.xbutton.y_root <= omenu->frame_y + omenu->frame->top_width;
          WMHandleEvent(&ev);
          smenu = wMenuUnderPointer(scr);
          if (smenu == NULL || (smenu->flags.tornoff && smenu != omenu)) {
            done = 1;
          } else if (smenu == omenu && click_on_title) {
            should_slide_back = False;
            done = 1;
          }
        }
        break;
      case KeyPress:
        done = 1;
      default:
        WMHandleEvent(&ev);
        break;
      }
  }

  /* WMLogInfo("Exiting wMenuScroll event loop."); */
  if (should_slide_back) {
    menu->timer = WMAddTimerHandler(0, MENU_SCROLL_DELAY, slideMenuBackCallback, menu);
  }
}

void wMenuSlideIfNeeded(WMenu *menu)
{
  WMRect rect = wGetRectForHead(menu->menu->screen_ptr, wGetHeadForPointerLocation(menu->menu->screen_ptr));
  
  if (menu->timer == NULL && (menu->frame_y + MENU_HEIGHT(menu)) > rect.size.height) {
    menu->old_frame_x = menu->frame_x;
    menu->old_frame_y = menu->frame_y;
    menu->timer = WMAddTimerHandler(0, MENU_SCROLL_DELAY, slideMenuCallback, menu);
  }
}


static void menuExpose(WObjDescriptor *desc, XEvent *event)
{
  wMenuPaint(desc->parent);
}

static WMenu *getSelectedLeafMenu(WMenu *menu)
{
  WMenuItem *selected_item = NULL;
  WMenu *selected_menu = NULL;

  if (menu->selected_item_index >= 0) {
    selected_item = menu->items[menu->selected_item_index];
  }

  if (selected_item == NULL) { // no selected item in this menu
    return menu->parent;
  } else if (selected_item->submenu_index >= 0) { // going deeper
    selected_menu = getSelectedLeafMenu(menu->submenus[selected_item->submenu_index]);
  } else {
    selected_menu = menu;
  }

  return selected_menu;
}

static void menuMouseDown(WObjDescriptor *desc, XEvent *event)
{
  WMenu *event_menu = desc->parent; // menu where button press has happend
  WMenu *mouse_menu = NULL;         // menu where button release has happend
  WMenuItem *item = NULL;
  WScreen *scr = event_menu->frame->screen_ptr;
  XEvent ev;
  int done = 0;
  int item_index = -1;
  int old_selected_item_index = event_menu->selected_item_index; // selected item before click
  int x, y;
  BOOL first_run = true;

  wRaiseFrame(event_menu->frame->core);

  // it's dirty but force processing of first ButtonPress in event loop below
  ev.type = event->type;

  while (!done) {
    if (first_run == false) {
      XAllowEvents(dpy, AsyncPointer | SyncPointer, CurrentTime);
      WMMaskEvent(dpy, ExposureMask | ButtonMotionMask | ButtonReleaseMask | ButtonPressMask, &ev);
    } else {
      first_run = false;
    }

    mouse_menu = findMenu(scr, &x, &y);
    
    switch (ev.type) {
    case MotionNotify:
      {
        // This part should detect these cases:
        // - mouse moved completely out of menu
        // - mouse moved inside menu (highlight/unhighlight items, open/close submenus)
        // - mouse moved from submenu to menu and vice versa

        /* WMLogInfo("MotionNotify: mouse_menu: %s event_menu: %s", */
        /*           mouse_menu ? mouse_menu->frame->title : "NONE", */
        /*           event_menu ? event_menu->frame->title : "NONE"); */

        if (mouse_menu == NULL) { /* mouse moved out of menu */
          /* mouse could leave menu:
             1. from opened submenu - deselect item in submenu, submenu stays opened
             2. from menu with submenu opened - deselect item in submenu, submenu stays opened
             3. from menu with plain item selected - deselect item in menu
          */

          /* WMLogInfo("Mouse moved out of menu mouse: %s event: %s", */
          /*           mouse_menu ? mouse_menu->frame->title : "NONE", */
          /*           event_menu ? event_menu->frame->title : "NONE"); */
          /* WMLogInfo("Event menu selected item: %i", event_menu->selected_item_index); */

          if (event_menu != NULL) {
            WMenu *main_menu, *submenu;
            /* depending on where click was performed, item may be an item in parent
               menu (click on parent) or item in opened submenu (click on submenu) */
            //item = getSelectedLeafItem(event_menu); // TODO: remove
            /* always go to toplevel menu because event_menu could be a submenu
               of deselected item */
            main_menu = event_menu;
            while (main_menu->parent) {
              main_menu = main_menu->parent;
            }
            /* WMLogInfo("Main menu: %s", main_menu ? main_menu->frame->title : "NONE"); */
            submenu = getSelectedLeafMenu(main_menu);
            /* WMLogInfo("Submenu: %s for item %s", */
            /*           submenu ? submenu->frame->title : "NONE", */
            /*           item ? item->text : "NONE"); */
            // Menu is attached and selected item has no submenu
            if (submenu && submenu->flags.mapped && !submenu->flags.tornoff &&
                submenu->selected_item_index >= 0 &&
                submenu->items[submenu->selected_item_index]->submenu_index < 0) {
              /* deselect item in opened submenu */
              /* WMLogInfo("Deselect item in submenu %s", submenu->frame->title); */
              wMenuItemSelect(submenu, -1);
            }
          }
          break;
        }
        
        dragScrollMenuCallback(NULL, mouse_menu);
        
        item_index = getItemIndexAt(mouse_menu, x, y);
        /* WMLogInfo("Mouse on item %i in %s", item_index, mouse_menu->frame->title); */
        if (item_index >= 0) {
          item = mouse_menu->items[item_index];
          if (/*entry->flags.enabled && */item->submenu_index >= 0 && mouse_menu->submenus) {
            WMenu *submenu = mouse_menu->submenus[item->submenu_index];
            if (submenu->flags.mapped && !submenu->flags.tornoff) {
              wMenuItemSelect(submenu, -1);
            } else {
              wMenuItemSelect(mouse_menu, item_index);
            }
          } else {
            wMenuItemSelect(mouse_menu, item_index);
          }
        } else {
          wMenuItemSelect(mouse_menu, -1);
        }
      }
      break;

    case ButtonPress:
      {
        item_index = getItemIndexAt(event_menu, x, y);
        if (item_index < 0) {
          item_index = event_menu->selected_item_index;
        }
        if (item_index >= 0) {
          item = event_menu->items[item_index];
        }
        
        /* WMLogInfo("[ButtonPress] event menu: %s, mouse menu: %s", */
        /*           event_menu ? event_menu->frame->title : "NULL", */
        /*           mouse_menu ? mouse_menu->frame->title : "NULL"); */
        if (item_index >= 0) {
          wMenuItemSelect(event_menu, item_index);
        }
      }
      break;

    case ButtonRelease:
      {
        /* Button release posisions:
           1. On plain menu item: item->submenu_index < 0, smenu
           2. On item with submenu opened: item->submenu_index >=0, smenu
           3. On menu tiitlebar: smenu, 
           3. Out of menu: menu->selected_item_index == -1, !smenu
        */
        /* WMLogInfo("[ButtonRelease] event menu: %s, mouse menu: %s", */
        /*           event_menu ? event_menu->frame->title : "NULL", */
        /*           mouse_menu ? mouse_menu->frame->title : "NULL"); */
        if (ev.xbutton.button == event->xbutton.button) {
          if (mouse_menu) { // on menu
            /* WMLogInfo("ButtonRelease: on menu"); */
            if (mouse_menu->selected_item_index >= 0) {
              item = mouse_menu->items[mouse_menu->selected_item_index];
              if (item->submenu_index < 0) {
                /* WMLogInfo("ButtonRelease: on plain item"); */
                if (item->callback != NULL && item->flags.enabled) {
                  (*item->callback) (event_menu, item);
                }
                wMenuItemSelect(mouse_menu, -1);
                if (mouse_menu != event_menu || !mouse_menu->parent) {
                  closeCascade(mouse_menu);
                }
              } else if (item->submenu_index >= 0 && mouse_menu->submenus) {
                WMenu *submenu = mouse_menu->submenus[item->submenu_index];
                /* WMLogInfo("ButtonRelease: on submenu. Selected item: %s", item->text); */
                if (submenu && submenu->flags.mapped && !submenu->flags.tornoff) {
                  if (event_menu->flags.brother) {
                    /* submenu of main menu copy */
                    closeCascade(event_menu);
                  } else if ((mouse_menu == event_menu &&
                              mouse_menu->selected_item_index == old_selected_item_index) ||
                             submenu->brother->flags.mapped) {
                    /* selected with last click or tornoff submenu is visible */
                    wMenuItemSelect(mouse_menu, -1);
                  }
                } else if (!mouse_menu->flags.app_menu || mouse_menu->flags.brother) {
                  wMenuItemSelect(mouse_menu, -1);
                  wMenuUnmap(mouse_menu);
                }
              }
            } else { //...without selection - probably on menu titlebar
              if (!mouse_menu->flags.app_menu || mouse_menu->flags.brother) {
                closeCascade(mouse_menu);
              }
              wMenuItemSelect(mouse_menu, -1);
            }
          } else if (event_menu) { // outside of menu
            /* WMLogInfo("ButtonRelease: out of menu %s", event_menu->frame->title); */
            if (!event_menu->flags.app_menu || event_menu->flags.brother) {
              closeCascade(event_menu);
            }
            wMenuItemSelect(event_menu, -1);
          }
          done = 1;
        }
      }
      break;

    case Expose:
      WMHandleEvent(&ev);
      break;
    }
  }

  XSync(dpy, 0);
  XFlush(dpy);
  
  if (event_menu && event_menu->timer) {
    WMDeleteTimerHandler(event_menu->timer);
    event_menu->timer = NULL;
  }  
  if (mouse_menu && mouse_menu->timer) {
    WMDeleteTimerHandler(mouse_menu->timer);
    mouse_menu->timer = NULL;
  }  
}

void wMenuMove(WMenu *menu, int x, int y, int submenus)
{
  WMenu *submenu;
  int i;

  if (!menu)
    return;

  menu->frame_x = x;
  menu->frame_y = y;
  XMoveWindow(dpy, menu->frame->core->window, x, y);

  if (submenus > 0 && menu->selected_item_index >= 0) {
    i = menu->items[menu->selected_item_index]->submenu_index;

    if (i >= 0 && menu->submenus) {
      submenu = menu->submenus[i];
      if (submenu->flags.mapped && !submenu->flags.tornoff) {
        if (wPreferences.align_menus) {
          wMenuMove(submenu, x + MENU_WIDTH(menu), y, submenus);
        } else {
          wMenuMove(submenu, x + MENU_WIDTH(menu),
                    y + submenu->item_height *menu->selected_item_index, submenus);
        }
      }
    }
  }
  if (submenus < 0 && menu->parent != NULL && menu->parent->flags.mapped && !menu->parent->flags.tornoff) {
    if (wPreferences.align_menus) {
      wMenuMove(menu->parent, x - MENU_WIDTH(menu->parent), y, submenus);
    } else {
      wMenuMove(menu->parent, x - MENU_WIDTH(menu->parent), menu->frame_y
                - menu->parent->item_height *menu->parent->selected_item_index, submenus);
    }
  }
}

static void changeMenuLevels(WMenu *menu, int lower)
{
  int i;

  if (!lower) {
    ChangeStackingLevel(menu->frame->core, (!menu->parent ? NSMainMenuWindowLevel : NSSubmenuWindowLevel));
    wRaiseFrame(menu->frame->core);
    menu->flags.lowered = 0;
  } else {
    ChangeStackingLevel(menu->frame->core, NSNormalWindowLevel);
    wLowerFrame(menu->frame->core);
    menu->flags.lowered = 1;
  }
  for (i = 0; i < menu->submenus_count; i++) {
    if (menu->submenus[i]
        && !menu->submenus[i]->flags.tornoff && menu->submenus[i]->flags.lowered != lower) {
      changeMenuLevels(menu->submenus[i], lower);
    }
  }
}

static void menuTitleDoubleClick(WCoreWindow *sender, void *data, XEvent *event)
{
  WMenu *menu = data;
  int lower;

  /* Parameter not used, but tell the compiler that it is ok */
  (void) sender;

  if (event->xbutton.state & wPreferences.cmd_modifier_mask) {
    if (menu->flags.lowered) {
      lower = 0;
    } else {
      lower = 1;
    }
    changeMenuLevels(menu, lower);
  }
}

static void menuTitleMouseDown(WCoreWindow *sender, void *data, XEvent *event)
{
  WMenu *menu = data;
  WMenu *tmp;
  XEvent ev;
  int x = menu->frame_x, old_x = x, y = menu->frame_y, old_y = y;
  int dx = event->xbutton.x_root, dy = event->xbutton.y_root;
  int i, lower;
  Bool started;
  WMRect head_rect;

  /* can't touch the menu copy */
  if (menu->flags.brother)
    return;

  if (event->xbutton.button != Button1 && event->xbutton.button != Button2)
    return;

  if (event->xbutton.state & wPreferences.cmd_modifier_mask) {
    wLowerFrame(menu->frame->core);
    lower = 1;
  } else {
    wRaiseFrame(menu->frame->core);
    lower = 0;
  }
  tmp = menu;

  /* lower/raise all submenus */
  while (1) {
    if (tmp->selected_item_index >= 0 && tmp->submenus &&
        tmp->items[tmp->selected_item_index]->submenu_index >= 0) {
      tmp = tmp->submenus[tmp->items[tmp->selected_item_index]->submenu_index];
      if (!tmp || !tmp->flags.mapped)
        break;
      if (lower)
        wLowerFrame(tmp->frame->core);
      else
        wRaiseFrame(tmp->frame->core);
    } else {
      break;
    }
  }

  started = False;
  head_rect = wGetRectForHead(menu->frame->screen_ptr,
                              wGetHeadForPointerLocation(menu->frame->screen_ptr));
  while (1) {
    WMMaskEvent(dpy, ButtonMotionMask | ButtonReleaseMask | ButtonPressMask | ExposureMask, &ev);
    switch (ev.type) {
    case MotionNotify:
      if (started) {
        x += ev.xmotion.x_root - dx;
        x = (x < 0) ? 0 : x;
        if ((x + MENU_WIDTH(menu)) > head_rect.size.width) {
          x = head_rect.size.width - MENU_WIDTH(menu);
        }
        y += ev.xmotion.y_root - dy;
        y = (y < 0) ? 0 : y;
        if ((y + menu->item_height) > head_rect.size.height) {
          y = head_rect.size.height - menu->item_height;
        }
        dx = ev.xmotion.x_root;
        dy = ev.xmotion.y_root;
        wMenuMove(menu, x, y, True);
      } else if (abs(ev.xmotion.x_root - dx) > MOVE_THRESHOLD ||
                 abs(ev.xmotion.y_root - dy) > MOVE_THRESHOLD) {
        started = True;
        XGrabPointer(dpy, menu->frame->titlebar->window, False,
                     ButtonMotionMask | ButtonReleaseMask | ButtonPressMask,
                     GrabModeAsync, GrabModeAsync, None,
                     wPreferences.cursor[WCUR_MOVE], CurrentTime);
      }
      break;

    case ButtonPress:
      break;

    case ButtonRelease:
      if (ev.xbutton.button != event->xbutton.button) {
        break;
      }
      /* tear off the menu if it's a root menu or a cascade application menu */
      if (!menu->flags.tornoff && !menu->flags.brother &&
          (!menu->flags.app_menu || menu->parent != NULL) &&
          (abs(old_x - x) > 5 || abs(old_y - y) > 5)) {
        menu->flags.tornoff = 1;
        wFrameWindowShowButton(menu->frame, WFF_RIGHT_BUTTON);
        if (menu->parent) {
          /* turn off selected menu item in parent menu */
          wMenuItemSelect(menu->parent, -1);
          /* make parent map the copy in place of the original */
          for (i = 0; i < menu->parent->submenus_count; i++) {
            if (menu->parent->submenus[i] == menu) {
              menu->parent->submenus[i] = menu->brother;
            }
          }
        }
      }
      XUngrabPointer(dpy, CurrentTime);
      return;

    default:
      WMHandleEvent(&ev);
      break;
    }
  }
}

/*
 *----------------------------------------------------------------------
 * menuCloseClick()
 * 	Handles mouse click on the close button of menus. The menu is
 *      closed when the button is clicked.
 *
 * Side effects:
 * 	The closed menu is reinserted at it's parent menus cascade list.
 *----------------------------------------------------------------------
 */
static void menuCloseClick(WCoreWindow *sender, void *data, XEvent *event)
{
  WMenu *menu = (WMenu *)data;
  WMenu *parent = menu->parent;
  int i;

  if (parent) {
    for (i = 0; i < parent->submenus_count; i++) {
      /* find the item that points to the copy */
      if (parent->submenus[i] == menu->brother) {
        /* if this function was called menu has title and close button on it */
        wFrameWindowHideButton(menu->frame, WFF_RIGHT_BUTTON);
        menu->flags.tornoff = 0;
        /* make it point to the original */
        parent->submenus[i] = menu;
        break;
      }
    }
  } else {
    wFrameWindowHideButton(menu->frame, WFF_RIGHT_BUTTON);
    menu->flags.tornoff = 0;
    wMenuUnmap(menu);
  }
  closeCascade(menu);
}

static void saveMenuInfo(CFMutableDictionaryRef dict, WMenu *menu, CFTypeRef key)
{
  CFStringRef value;
  CFMutableArrayRef list;
  
  value = CFStringCreateWithFormat(kCFAllocatorDefault, 0, CFSTR("%i,%i"),
                                   menu->frame_x, menu->frame_y);
  list = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
  CFArrayAppendValue(list, value);
  if (menu->flags.lowered) {
    CFArrayAppendValue(list, CFSTR("lowered"));
  }
  CFDictionarySetValue(dict, key, list);
  
  CFRelease(value);
  CFRelease(list);
}

void wMenuSaveState(WScreen *scr)
{
  CFMutableDictionaryRef menus;

  menus = CFDictionaryCreateMutable(kCFAllocatorDefault, 9, &kCFTypeDictionaryKeyCallBacks,
                                    &kCFTypeDictionaryValueCallBacks);

  if (scr->switch_menu && scr->switch_menu->flags.tornoff) {
    saveMenuInfo(menus, scr->switch_menu, CFSTR("SwitchMenu"));
  }
  
  CFDictionarySetValue(scr->session_state, CFSTR("Menus"), menus);
  
  CFRelease(menus);
}

static Bool getMenuInfo(CFTypeRef info, int *x, int *y, Bool *lowered)
{
  CFTypeRef pos;
  const char *f;

  *lowered = False;

  if (CFGetTypeID(info) == CFArrayGetTypeID()) {
    CFTypeRef flags;
    pos = CFArrayGetValueAtIndex(info, 0);
    if (CFArrayGetCount(info) > 1) {
      flags = CFArrayGetValueAtIndex(info, 1);
      if (flags != NULL
          && (CFGetTypeID(flags) == CFStringGetTypeID())
          && (f = CFStringGetCStringPtr(flags, kCFStringEncodingUTF8)) != NULL
          && strcmp(f, "lowered") == 0) {
        *lowered = True;
      }
    }
  } else {
    pos = info;
  }

  if (pos != NULL && (CFGetTypeID(pos) == CFStringGetTypeID())) {
    if (sscanf(CFStringGetCStringPtr(pos, kCFStringEncodingUTF8), "%i,%i", x, y) != 2) {
      WMLogWarning(_("bad value in menus state info: %s"), "Position");
    }
  } else {
    WMLogWarning(_("bad value in menus state info: %s"), "(position, flags...)");
    return False;
  }

  return True;
}

static int restoreMenu(WScreen *scr, CFTypeRef menu)
{
  int x, y;
  Bool lowered = False;
  WMenu *pmenu = NULL;

  if (!menu)
    return False;

  if (!getMenuInfo(menu, &x, &y, &lowered))
    return False;

  OpenSwitchMenu(scr, x, y, False);
  pmenu = scr->switch_menu;

  if (pmenu) {
    int width = MENU_WIDTH(pmenu);
    int height = MENU_HEIGHT(pmenu);
    WMRect rect = wGetRectForHead(scr, wGetHeadForPointerLocation(scr));

    if (lowered) {
      changeMenuLevels(pmenu, True);
    }

    if (x < rect.pos.x - width)
      x = rect.pos.x;
    if (x > rect.pos.x + rect.size.width)
      x = rect.pos.x + rect.size.width - width;
    if (y < rect.pos.y)
      y = rect.pos.y;
    if (y > rect.pos.y + rect.size.height)
      y = rect.pos.y + rect.size.height - height;

    wMenuMove(pmenu, x, y, True);
    pmenu->flags.tornoff = 1;
    wFrameWindowShowButton(pmenu->frame, WFF_RIGHT_BUTTON);
    return True;
  }
  return False;
}

void wMenuRestoreState(WScreen *scr)
{
  CFTypeRef menus, menu;

  if (!scr->session_state) {
    return;
  }

  menus = CFDictionaryGetValue(scr->session_state, CFSTR("Menus"));
  if (!menus)
    return;

  /* restore menus */
  menu = CFDictionaryGetValue(menus, CFSTR("SwitchMenu"));

  restoreMenu(scr, menu);
}
