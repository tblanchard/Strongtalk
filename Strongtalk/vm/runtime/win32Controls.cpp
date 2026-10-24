/* Copyright (c) 2006, Sun Microsystems, Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
	  disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Sun Microsystems nor the names of its contributors may be used to endorse or promote products derived
	  from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT
NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE


*/

// The predefined window classes the image uses (BUTTON, LISTBOX) and the scroll bars of
// windows (WS_VSCROLL, WS_HSCROLL), drawn by the emulation (see win32GUI.hpp). The image
// subclasses the controls (GWL_WNDPROC) and owner-draws the list box items.

#include "platform.hpp"
#if defined(__APPLE__)

#include "win32GUI.hpp"
#include "win32Shim.hpp"

namespace w32 {

enum {
  WM_ENABLE = 0x000A, WM_SETTEXT = 0x000C, WM_GETTEXT = 0x000D, WM_GETTEXTLENGTH = 0x000E, WM_PAINT = 0x000F,
  WM_ERASEBKGND = 0x0014, WM_SETFOCUS = 0x0007, WM_KILLFOCUS = 0x0008, WM_SIZE = 0x0005, WM_SETFONT = 0x0030,
  WM_GETFONT = 0x0031, WM_DRAWITEM = 0x002B, WM_MEASUREITEM = 0x002C, WM_KEYDOWN = 0x0100, WM_CHAR = 0x0102,
  WM_COMMAND = 0x0111, WM_HSCROLL = 0x0114, WM_VSCROLL = 0x0115, WM_CTLCOLORBTN = 0x0135, WM_MOUSEMOVE = 0x0200,
  WM_LBUTTONDOWN = 0x0201, WM_LBUTTONUP = 0x0202, WM_LBUTTONDBLCLK = 0x0203, WM_MOUSEWHEEL = 0x020A,
  WM_CAPTURECHANGED = 0x0215, WM_GETDLGCODE = 0x0087
};
enum { BM_GETCHECK = 0xF0, BM_SETCHECK = 0xF1, BM_GETSTATE = 0xF2, BM_SETSTATE = 0xF3, BM_SETSTYLE = 0xF4,
       BM_CLICK = 0xF5, BM_GETIMAGE = 0xF6, BM_SETIMAGE = 0xF7 };
enum { BS_PUSHBUTTON = 0, BS_DEFPUSHBUTTON = 1, BS_CHECKBOX = 2, BS_AUTOCHECKBOX = 3, BS_RADIOBUTTON = 4,
       BS_3STATE = 5, BS_AUTO3STATE = 6, BS_GROUPBOX = 7, BS_AUTORADIOBUTTON = 9, BS_OWNERDRAW = 0xB,
       BS_TYPEMASK = 0xF, BS_BITMAP = 0x80, BS_ICON = 0x40 };
enum { BN_CLICKED = 0 };
enum { LB_ADDSTRING = 0x180, LB_INSERTSTRING = 0x181, LB_DELETESTRING = 0x182, LB_SELITEMRANGEEX = 0x183,
       LB_RESETCONTENT = 0x184, LB_SETSEL = 0x185, LB_SETCURSEL = 0x186, LB_GETSEL = 0x187, LB_GETCURSEL = 0x188,
       LB_GETTEXT = 0x189, LB_GETTEXTLEN = 0x18A, LB_GETCOUNT = 0x18B, LB_GETTOPINDEX = 0x18E,
       LB_GETSELCOUNT = 0x190, LB_GETSELITEMS = 0x191, LB_SETTOPINDEX = 0x197, LB_GETITEMRECT = 0x198,
       LB_GETITEMDATA = 0x199, LB_SETITEMDATA = 0x19A, LB_SELITEMRANGE = 0x19B, LB_SETCARETINDEX = 0x19E,
       LB_GETCARETINDEX = 0x19F, LB_SETITEMHEIGHT = 0x1A0, LB_GETITEMHEIGHT = 0x1A1, LB_SETCOUNT = 0x1A7,
       LB_ITEMFROMPOINT = 0x1A9 };
enum { LBS_NOTIFY = 0x1, LBS_MULTIPLESEL = 0x8, LBS_OWNERDRAWFIXED = 0x10, LBS_OWNERDRAWVARIABLE = 0x20,
       LBS_HASSTRINGS = 0x40, LBS_NOINTEGRALHEIGHT = 0x100, LBS_EXTENDEDSEL = 0x800, LBS_NODATA = 0x2000 };
enum { LBN_SELCHANGE = 1, LBN_DBLCLK = 2, LBN_SETFOCUS = 4, LBN_KILLFOCUS = 5 };
const word LB_ERR = -1;
enum { ODT_LISTBOX = 2, ODT_BUTTON = 4, ODA_DRAWENTIRE = 1, ODS_SELECTED = 1, ODS_DISABLED = 4, ODS_FOCUS = 0x10 };
enum { MK_SHIFT = 0x4, MK_CONTROL = 0x8 };
enum { SB_HORZ = 0, SB_VERT = 1, SB_CTL = 2, SB_BOTH = 3 };
enum { SB_LINEUP = 0, SB_LINEDOWN = 1, SB_PAGEUP = 2, SB_PAGEDOWN = 3, SB_THUMBPOSITION = 4, SB_THUMBTRACK = 5,
       SB_TOP = 6, SB_BOTTOM = 7, SB_ENDSCROLL = 8 };
enum { SIF_RANGE = 1, SIF_PAGE = 2, SIF_POS = 4, SIF_DISABLENOSCROLL = 8, SIF_TRACKPOS = 0x10 };
enum { VK_PRIOR = 0x21, VK_NEXT = 0x22, VK_END = 0x23, VK_HOME = 0x24, VK_UP = 0x26, VK_DOWN = 0x28, VK_SPACE = 0x20,
       VK_RETURN = 0x0D };

const int scroll_bar_size = 15;

static inline word make_wparam(int lo, int hi) { return (word) (uint32_t) ((lo & 0xFFFF) | ((hi & 0xFFFF) << 16)); }
static inline int lo16(word v) { return (int16_t) (v & 0xFFFF); }
static inline int hi16(word v) { return (int16_t) ((v >> 16) & 0xFFFF); }

static void notify_parent(Window* w, int code) {
  if (w->parent == NULL) return;
  send_message(w->parent->handle, WM_COMMAND, make_wparam((int) w->id, code), w->handle);
}

static Font* control_font(word f) {
  Font* font = lookup_as<Font>(f, K_FONT);
  return font ? font : default_gui_font();
}

// ---------------------------------------------------------------------------
// Buttons

struct Button {
  word font;
  word image;		// BM_SETIMAGE bitmap
  int  check;
  bool pressed;		// drawn pressed (mouse down inside)
  bool tracking;	// mouse down in the button
};

static Button* button_of(Window* w) {
  if (w->control == NULL) {
    Button* b = new Button();
    b->font = 0; b->image = 0; b->check = 0; b->pressed = false; b->tracking = false;
    w->control = b;
  }
  return (Button*) w->control;
}

static void paint_button(Window* w) {
  Button* b = button_of(w);
  word hdc = gdi_window_dc(w);
  RECT r = client_rect(w);
  int type = w->style & BS_TYPEMASK;
  bool enabled = w->enabled;
  COLORREF face = 0xF0F0F0;
  if (w->parent != NULL) {	// the parent may give the background (WM_CTLCOLORBTN answers a brush)
    word brush = send_message(w->parent->handle, WM_CTLCOLORBTN, hdc, w->handle);
    (void) brush;
  }
  Font* font = control_font(b->font);
  if (type == BS_CHECKBOX || type == BS_AUTOCHECKBOX || type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON) {
    gdi_fill_rect(hdc, r, face);
    int size = 13, y = (r.bottom - size) / 2;
    RECT box = make_rect(1, y, 1 + size, y + size);
    gdi_fill_rect(hdc, box, b->pressed ? 0xE0E0E0 : 0xFFFFFF);
    gdi_frame_rect(hdc, box, enabled ? 0x707070 : 0xB0B0B0);
    if (b->check) gdi_fill_rect(hdc, make_rect(box.left + 3, box.top + 3, box.right - 3, box.bottom - 3), enabled ? 0xD77800 : 0xB0B0B0);
    gdi_draw_text(hdc, box.right + 5, (r.bottom - font->height) / 2, w->text.c_str(), (int) w->text.size(),
                  enabled ? 0x000000 : 0x6D6D6D, font);
    return;
  }
  if (type == BS_GROUPBOX) {
    gdi_frame_rect(hdc, make_rect(0, font->height / 2, r.right, r.bottom), 0xC0C0C0);
    gdi_draw_text(hdc, 8, 0, w->text.c_str(), (int) w->text.size(), 0x000000, font);
    return;
  }
  // push buttons
  bool down = b->pressed || b->check;
  gdi_fill_rect(hdc, r, down ? 0xD8D8D8 : face);
  gdi_frame_rect(hdc, r, enabled ? (type == BS_DEFPUSHBUTTON ? 0xD77800 : 0xA0A0A0) : 0xC8C8C8);
  if (!down) gdi_fill_rect(hdc, make_rect(1, 1, r.right - 1, 2), 0xFFFFFF);
  int shift = down ? 1 : 0;
  Bitmap* image = (w->style & BS_BITMAP) ? lookup_as<Bitmap>(b->image, K_BITMAP) : NULL;
  if (image != NULL) {
    gdi_draw_bitmap(hdc, image, (r.right - image->width) / 2 + shift, (r.bottom - image->height) / 2 + shift);
    return;
  }
  std::string label;
  for (size_t i = 0; i < w->text.size(); i++) {	// without the mnemonic marks
    if (w->text[i] == '&') { if (i + 1 < w->text.size() && w->text[i + 1] == '&') { label += '&'; i++; } continue; }
    label += w->text[i];
  }
  int tw = gdi_text_width(font, label.c_str(), (int) label.size());
  gdi_draw_text(hdc, (r.right - tw) / 2 + shift, (r.bottom - font->height) / 2 + shift, label.c_str(), (int) label.size(),
                enabled ? 0x000000 : 0x6D6D6D, font);
}

static void button_clicked(Window* w) {
  Button* b = button_of(w);
  int type = w->style & BS_TYPEMASK;
  if (type == BS_AUTOCHECKBOX) b->check = !b->check;
  if (type == BS_AUTORADIOBUTTON) b->check = 1;
  invalidate(w, NULL, false);
  notify_parent(w, BN_CLICKED);
}

word button_wndproc(word hwnd, word msg, word wparam, word lparam) {
  Window* w = window(hwnd);
  if (w == NULL) return 0;
  Button* b = button_of(w);
  switch (msg) {
    case WM_PAINT: {
      uint32_t ps[16];
      extern word BeginPaint_user(word, word);
      extern word EndPaint_user(word, word);
      BeginPaint_user(hwnd, (word) ps);
      paint_button(w);
      EndPaint_user(hwnd, (word) ps);
      return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
      extern word SetCapture_user(word);
      b->tracking = true;
      b->pressed = true;
      SetCapture_user(hwnd);
      invalidate(w, NULL, false);
      return 0;
    }
    case WM_MOUSEMOVE:
      if (b->tracking) {
        bool inside = rect_contains(client_rect(w), lo16(lparam), hi16(lparam));
        if (inside != b->pressed) { b->pressed = inside; invalidate(w, NULL, false); }
      }
      return 0;
    case WM_LBUTTONUP:
      if (b->tracking) {
        extern word ReleaseCapture_user();
        bool inside = rect_contains(client_rect(w), lo16(lparam), hi16(lparam));
        b->tracking = false;
        b->pressed = false;
        ReleaseCapture_user();
        invalidate(w, NULL, false);
        if (inside && window(hwnd) != NULL) button_clicked(w);
      }
      return 0;
    case WM_CAPTURECHANGED:
      if (b->tracking && (word) lparam != hwnd) { b->tracking = false; b->pressed = false; invalidate(w, NULL, false); }
      return 0;
    case WM_KEYDOWN:
      if (wparam == VK_SPACE || wparam == VK_RETURN) button_clicked(w);
      return 0;
    case BM_SETIMAGE: { word old = b->image; b->image = lparam; invalidate(w, NULL, false); return old; }
    case BM_GETIMAGE: return b->image;
    case BM_SETCHECK: b->check = (int) wparam; invalidate(w, NULL, false); return 0;
    case BM_GETCHECK: return b->check;
    case BM_SETSTATE: b->pressed = wparam != 0; invalidate(w, NULL, false); return 0;
    case BM_GETSTATE: return (b->check ? 3 : 0) | (b->pressed ? 4 : 0);
    case BM_SETSTYLE: w->style = (w->style & ~0xFFFF) | (uint32_t) (wparam & 0xFFFF); if (lparam) invalidate(w, NULL, false); return 0;
    case BM_CLICK:    button_clicked(w); return 0;
    case WM_SETFONT:  b->font = wparam; if (lparam) invalidate(w, NULL, false); return 0;
    case WM_GETFONT:  return b->font;
    case WM_SETTEXT:  w->text = lparam ? (const char*) lparam : ""; invalidate(w, NULL, false); return 1;
    case WM_ENABLE:   invalidate(w, NULL, false); return 0;
    case WM_SETFOCUS: case WM_KILLFOCUS: return 0;
    case 0x0002: /* WM_DESTROY */ return 0;
    case 0x0082: /* WM_NCDESTROY */ delete b; w->control = NULL; return 0;
  }
  return def_window_proc(hwnd, msg, wparam, lparam);
}

// ---------------------------------------------------------------------------
// List boxes

struct ListBox {
  std::vector<std::string> strings;	// LBS_HASSTRINGS / not owner-drawn
  std::vector<word>        data;
  std::vector<bool>        selected;
  std::vector<int>         heights;	// LBS_OWNERDRAWVARIABLE
  int  count;
  int  top;				// the first visible item
  int  cur;				// the current selection (single selection) / caret
  int  anchor;				// LBS_EXTENDEDSEL
  int  item_height;			// fixed height (0 = not measured yet)
  word font;
  bool focus;
};

static ListBox* listbox_of(Window* w) {
  if (w->control == NULL) {
    ListBox* l = new ListBox();
    l->count = 0; l->top = 0; l->cur = -1; l->anchor = -1; l->item_height = 0; l->font = 0; l->focus = false;
    w->control = l;
  }
  return (ListBox*) w->control;
}

static bool multiple(Window* w) { return (w->style & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL)) != 0; }
static bool owner_drawn(Window* w) { return (w->style & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) != 0; }
static bool variable(Window* w) { return (w->style & LBS_OWNERDRAWVARIABLE) != 0; }

// MEASUREITEMSTRUCT: CtlType, CtlID, itemID, itemWidth, itemHeight, itemData
static int measure_item(Window* w, int item) {
  ListBox* l = listbox_of(w);
  uint32_t mis[6] = { ODT_LISTBOX, (uint32_t) w->id, (uint32_t) item, (uint32_t) client_rect(w).right, 0, 0 };
  Font* font = control_font(l->font);
  mis[4] = font->height;
  if (item >= 0 && item < (int) l->data.size()) mis[5] = (uint32_t) l->data[item];
  if (w->parent != NULL && owner_drawn(w)) send_message(w->parent->handle, WM_MEASUREITEM, w->id, (word) mis);
  return std::max(1, (int) mis[4]);
}

static int item_height(Window* w, int item) {
  ListBox* l = listbox_of(w);
  if (variable(w)) {
    if (item < 0 || item >= l->count) return 16;
    if ((int) l->heights.size() < l->count) l->heights.resize(l->count, 0);
    if (l->heights[item] == 0) l->heights[item] = measure_item(w, item);
    return l->heights[item];
  }
  if (l->item_height == 0) l->item_height = measure_item(w, 0);
  return l->item_height;
}

static int item_top(Window* w, int item) {	// relative to the top item
  ListBox* l = listbox_of(w);
  if (!variable(w)) return (item - l->top) * item_height(w, 0);
  int y = 0;
  if (item >= l->top) for (int i = l->top; i < item; i++) y += item_height(w, i);
  else                for (int i = item; i < l->top; i++) y -= item_height(w, i);
  return y;
}

static int visible_items(Window* w) {	// fully visible from the top item
  ListBox* l = listbox_of(w);
  int h = client_rect(w).bottom, n = 0, y = 0;
  for (int i = l->top; i < l->count; i++) {
    y += item_height(w, i);
    if (y > h) break;
    n++;
  }
  return std::max(1, n);
}

static int max_top(Window* w) {
  ListBox* l = listbox_of(w);
  int h = client_rect(w).bottom, y = 0;
  for (int i = l->count - 1; i >= 0; i--) {
    y += item_height(w, i);
    if (y > h) return std::min(l->count - 1, i + 1);
  }
  return 0;
}

extern void set_scroll_bar(Window* w, bool vertical, int min, int max, int page, int pos, bool redraw);
static void scroll_bar_visibility(Window* w, bool vertical, bool visible);

static void update_listbox_scroll(Window* w) {
  ListBox* l = listbox_of(w);
  if (!(w->style & 0x00200000) && !w->vscroll.visible) {	// no WS_VSCROLL
    l->top = std::max(0, std::min(l->top, max_top(w)));
    return;
  }
  // the scroll bar is shown when the items do not fit (as the Win32 list box)
  bool needed = max_top(w) > 0;
  if (needed != w->vscroll.visible) {
    scroll_bar_visibility(w, true, needed);	// changes the client area (and sends WM_SIZE)
    w->style |= 0x00200000;
  }
  l->top = std::max(0, std::min(l->top, max_top(w)));
  int page = visible_items(w);
  set_scroll_bar(w, true, 0, std::max(0, l->count - 1), page, l->top, true);
}

static void ensure_visible(Window* w, int item) {
  ListBox* l = listbox_of(w);
  if (item < 0 || item >= l->count) return;
  if (item < l->top) l->top = item;
  else {
    int h = client_rect(w).bottom;
    while (l->top < item && item_top(w, item) + item_height(w, item) > h) l->top++;
  }
  update_listbox_scroll(w);
  invalidate(w, NULL, true);
}

static int item_at(Window* w, int y) {
  ListBox* l = listbox_of(w);
  int top = 0;
  for (int i = l->top; i < l->count; i++) {
    int h = item_height(w, i);
    if (y >= top && y < top + h) return i;
    top += h;
  }
  return -1;
}

// DRAWITEMSTRUCT: CtlType, CtlID, itemID, itemAction, itemState, hwndItem, hDC, rcItem, itemData
static void paint_listbox(Window* w) {
  ListBox* l = listbox_of(w);
  word hdc = gdi_window_dc(w);
  RECT c = client_rect(w);
  int y = 0;
  Font* font = control_font(l->font);
  for (int i = l->top; i < l->count && y < c.bottom; i++) {
    int h = item_height(w, i);
    RECT r = make_rect(0, y, c.right, y + h);
    bool sel = i < (int) l->selected.size() && l->selected[i];
    if (owner_drawn(w) && w->parent != NULL) {
      uint32_t dis[12];
      dis[0] = ODT_LISTBOX; dis[1] = (uint32_t) w->id; dis[2] = (uint32_t) i; dis[3] = ODA_DRAWENTIRE;
      dis[4] = (sel ? ODS_SELECTED : 0) | (w->enabled ? 0 : ODS_DISABLED) | (l->focus && i == l->cur ? ODS_FOCUS : 0);
      dis[5] = (uint32_t) w->handle; dis[6] = (uint32_t) hdc;
      memcpy(&dis[7], &r, sizeof r);
      dis[11] = i < (int) l->data.size() ? (uint32_t) l->data[i] : 0;
      send_message(w->parent->handle, WM_DRAWITEM, w->id, (word) dis);
    } else {
      gdi_fill_rect(hdc, r, sel ? 0xD77800 : 0xFFFFFF);
      const std::string& s = i < (int) l->strings.size() ? l->strings[i] : std::string();
      gdi_draw_text(hdc, 2, y + (h - font->height) / 2, s.c_str(), (int) s.size(), sel ? 0xFFFFFF : 0x000000, font);
    }
    y += h;
  }
  if (y < c.bottom) gdi_fill_rect(hdc, make_rect(0, y, c.right, c.bottom), 0xFFFFFF);
}

static void select_single(Window* w, int item, bool notify) {
  ListBox* l = listbox_of(w);
  if ((int) l->selected.size() < l->count) l->selected.resize(l->count, false);
  for (int i = 0; i < l->count; i++) l->selected[i] = i == item;
  l->cur = item;
  ensure_visible(w, item);
  invalidate(w, NULL, true);
  if (notify && (w->style & LBS_NOTIFY)) notify_parent(w, LBN_SELCHANGE);
}

static void click_item(Window* w, int item, word keys) {
  ListBox* l = listbox_of(w);
  if (item < 0) return;
  if ((int) l->selected.size() < l->count) l->selected.resize(l->count, false);
  if (w->style & LBS_MULTIPLESEL) {
    l->selected[item] = !l->selected[item];
    l->cur = item;
  } else if (w->style & LBS_EXTENDEDSEL) {
    if ((keys & MK_SHIFT) && l->anchor >= 0) {
      if (!(keys & MK_CONTROL)) for (int i = 0; i < l->count; i++) l->selected[i] = false;
      for (int i = std::min(l->anchor, item); i <= std::max(l->anchor, item); i++) l->selected[i] = true;
    } else if (keys & MK_CONTROL) {
      l->selected[item] = !l->selected[item];
      l->anchor = item;
    } else {
      for (int i = 0; i < l->count; i++) l->selected[i] = i == item;
      l->anchor = item;
    }
    l->cur = item;
  } else {
    select_single(w, item, false);
  }
  ensure_visible(w, item);
  invalidate(w, NULL, true);
  if (w->style & LBS_NOTIFY) notify_parent(w, LBN_SELCHANGE);
}

static void set_count(Window* w, int n) {
  ListBox* l = listbox_of(w);
  l->count = n;
  l->selected.resize(n, false);
  if (!(w->style & LBS_NODATA) || (w->style & LBS_HASSTRINGS)) { l->strings.resize(n); }
  l->data.resize(n, 0);
  if (variable(w)) l->heights.resize(n, 0);
  if (l->cur >= n) l->cur = -1;
  if (l->anchor >= n) l->anchor = -1;
  update_listbox_scroll(w);
  invalidate(w, NULL, true);
}

word listbox_wndproc(word hwnd, word msg, word wparam, word lparam) {
  Window* w = window(hwnd);
  if (w == NULL) return 0;
  ListBox* l = listbox_of(w);
  switch (msg) {
    case WM_PAINT: {
      uint32_t ps[16];
      extern word BeginPaint_user(word, word);
      extern word EndPaint_user(word, word);
      BeginPaint_user(hwnd, (word) ps);
      paint_listbox(w);
      EndPaint_user(hwnd, (word) ps);
      return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE:
      update_listbox_scroll(w);
      invalidate(w, NULL, true);
      return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
      extern word SetFocus_user(word);
      if (!l->focus) SetFocus_user(hwnd);
      if (window(hwnd) == NULL) return 0;
      int item = item_at(w, hi16(lparam));
      if (msg == WM_LBUTTONDBLCLK) {
        if (item >= 0 && (w->style & LBS_NOTIFY)) notify_parent(w, LBN_DBLCLK);
        return 0;
      }
      click_item(w, item, wparam);
      return 0;
    }
    case WM_MOUSEWHEEL: {
      int notches = hi16(wparam) / 120;
      l->top = std::max(0, std::min(max_top(w), l->top - 3 * notches));
      update_listbox_scroll(w);
      invalidate(w, NULL, true);
      return 0;
    }
    case WM_VSCROLL: {
      int code = lo16(wparam), page = visible_items(w);
      switch (code) {
        case SB_LINEUP:        l->top--; break;
        case SB_LINEDOWN:      l->top++; break;
        case SB_PAGEUP:        l->top -= page; break;
        case SB_PAGEDOWN:      l->top += page; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: l->top = (uint16_t) hi16(wparam); break;
        case SB_TOP:           l->top = 0; break;
        case SB_BOTTOM:        l->top = l->count; break;
        default:               return 0;
      }
      update_listbox_scroll(w);
      invalidate(w, NULL, true);
      return 0;
    }
    case WM_KEYDOWN: {
      int item = l->cur, page = visible_items(w);
      switch (wparam) {
        case VK_UP:    item = std::max(0, item - 1); break;
        case VK_DOWN:  item = std::min(l->count - 1, item + 1); break;
        case VK_PRIOR: item = std::max(0, item - page); break;
        case VK_NEXT:  item = std::min(l->count - 1, item + page); break;
        case VK_HOME:  item = 0; break;
        case VK_END:   item = l->count - 1; break;
        default:       return 0;
      }
      if (item >= 0 && item < l->count) {
        if (multiple(w)) click_item(w, item, 0); else select_single(w, item, true);
      }
      return 0;
    }
    case WM_SETFOCUS:  l->focus = true;  invalidate(w, NULL, false); if (w->style & LBS_NOTIFY) notify_parent(w, LBN_SETFOCUS); return 0;
    case WM_KILLFOCUS: l->focus = false; invalidate(w, NULL, false); if (w->style & LBS_NOTIFY) notify_parent(w, LBN_KILLFOCUS); return 0;
    case WM_SETFONT:   l->font = wparam; l->item_height = 0; if (lparam) invalidate(w, NULL, true); return 0;
    case WM_GETFONT:   return l->font;
    case WM_GETDLGCODE: return 0x1;	// DLGC_WANTARROWS
    case LB_SETCOUNT:  set_count(w, (int) wparam); return 0;
    case LB_GETCOUNT:  return l->count;
    case LB_RESETCONTENT:
      l->strings.clear(); l->data.clear(); l->selected.clear(); l->heights.clear();
      l->count = 0; l->top = 0; l->cur = -1; l->anchor = -1;
      update_listbox_scroll(w);
      invalidate(w, NULL, true);
      return 0;
    case LB_ADDSTRING:
    case LB_INSERTSTRING: {
      int index = msg == LB_ADDSTRING ? l->count : ((int32_t) wparam < 0 ? l->count : std::min((int) wparam, l->count));
      std::string s = (!owner_drawn(w) || (w->style & LBS_HASSTRINGS)) && lparam ? (const char*) lparam : "";
      word data = owner_drawn(w) && !(w->style & LBS_HASSTRINGS) ? lparam : 0;
      if ((int) l->strings.size() < l->count) l->strings.resize(l->count);
      l->strings.insert(l->strings.begin() + index, s);
      l->data.resize(l->count, 0);
      l->data.insert(l->data.begin() + index, data);
      l->selected.resize(l->count, false);
      l->selected.insert(l->selected.begin() + index, false);
      if (variable(w)) { l->heights.resize(l->count, 0); l->heights.insert(l->heights.begin() + index, 0); }
      l->count++;
      update_listbox_scroll(w);
      invalidate(w, NULL, true);
      return index;
    }
    case LB_DELETESTRING: {
      int index = (int) wparam;
      if (index < 0 || index >= l->count) return LB_ERR;
      if (index < (int) l->strings.size()) l->strings.erase(l->strings.begin() + index);
      if (index < (int) l->data.size()) l->data.erase(l->data.begin() + index);
      if (index < (int) l->selected.size()) l->selected.erase(l->selected.begin() + index);
      if (index < (int) l->heights.size()) l->heights.erase(l->heights.begin() + index);
      l->count--;
      if (l->cur >= l->count) l->cur = l->count - 1;
      update_listbox_scroll(w);
      invalidate(w, NULL, true);
      return l->count;
    }
    case LB_GETTEXT: {
      int index = (int) wparam;
      if (index < 0 || index >= (int) l->strings.size()) return LB_ERR;
      strcpy((char*) lparam, l->strings[index].c_str());
      return (word) l->strings[index].size();
    }
    case LB_GETTEXTLEN: {
      int index = (int) wparam;
      if (index < 0 || index >= (int) l->strings.size()) return LB_ERR;
      return (word) l->strings[index].size();
    }
    case LB_SETITEMDATA:
      if ((int) wparam < 0 || (int) wparam >= l->count) return LB_ERR;
      l->data.resize(l->count, 0);
      l->data[wparam] = lparam;
      return 0;
    case LB_GETITEMDATA:
      if ((int) wparam < 0 || (int) wparam >= (int) l->data.size()) return LB_ERR;
      return l->data[wparam];
    case LB_SETCURSEL:
      if (multiple(w)) return LB_ERR;
      if ((int32_t) wparam < 0) {
        l->selected.assign(l->count, false);
        l->cur = -1;
        invalidate(w, NULL, true);
        return LB_ERR;
      }
      if ((int) wparam >= l->count) return LB_ERR;
      select_single(w, (int) wparam, false);
      return wparam;
    case LB_GETCURSEL:
      if (multiple(w)) return l->cur;
      return l->cur >= 0 && l->cur < l->count && l->cur < (int) l->selected.size() && l->selected[l->cur] ? l->cur : LB_ERR;
    case LB_SETSEL: {	// wparam: select, lparam: index (-1 = all)
      if (!multiple(w)) return LB_ERR;
      l->selected.resize(l->count, false);
      int index = (int32_t) lparam;
      if (index == -1) { for (int i = 0; i < l->count; i++) l->selected[i] = wparam != 0; }
      else if (index >= 0 && index < l->count) { l->selected[index] = wparam != 0; l->cur = index; }
      else return LB_ERR;
      invalidate(w, NULL, true);
      return 0;
    }
    case LB_GETSEL: {
      int index = (int) wparam;
      if (index < 0 || index >= l->count) return LB_ERR;
      return index < (int) l->selected.size() && l->selected[index] ? 1 : 0;
    }
    case LB_SELITEMRANGE: {	// wparam: select, lparam: first | last << 16
      if (!multiple(w)) return LB_ERR;
      l->selected.resize(l->count, false);
      int first = (uint16_t) lo16(lparam), last = (uint16_t) hi16(lparam);
      for (int i = std::min(first, last); i <= std::max(first, last) && i < l->count; i++) l->selected[i] = wparam != 0;
      invalidate(w, NULL, true);
      return 0;
    }
    case LB_GETSELCOUNT: {
      if (!multiple(w)) return LB_ERR;
      int n = 0;
      for (int i = 0; i < l->count && i < (int) l->selected.size(); i++) if (l->selected[i]) n++;
      return n;
    }
    case LB_GETSELITEMS: {	// wparam: max, lparam: int buffer
      if (!multiple(w)) return LB_ERR;
      int n = 0;
      int32_t* items = (int32_t*) lparam;
      for (int i = 0; i < l->count && i < (int) l->selected.size() && n < (int) wparam; i++) if (l->selected[i]) items[n++] = i;
      return n;
    }
    case LB_GETTOPINDEX: return l->top;
    case LB_SETTOPINDEX:
      if ((int) wparam < 0 || (int) wparam >= std::max(1, l->count)) return LB_ERR;
      l->top = (int) wparam;
      update_listbox_scroll(w);
      invalidate(w, NULL, true);
      return 0;
    case LB_SETCARETINDEX: l->cur = (int) wparam; ensure_visible(w, l->cur); return 0;
    case LB_GETCARETINDEX: return l->cur;
    case LB_SETITEMHEIGHT:
      if (variable(w)) { if ((int) wparam >= 0 && (int) wparam < l->count) { l->heights.resize(l->count, 0); l->heights[wparam] = (int) lparam & 0xFFFF; } }
      else l->item_height = (int) lparam & 0xFFFF;
      update_listbox_scroll(w);
      invalidate(w, NULL, true);
      return 0;
    case LB_GETITEMHEIGHT: return item_height(w, (int) wparam);
    case LB_GETITEMRECT: {
      int index = (int) wparam;
      if (index < 0 || index >= std::max(1, l->count)) return LB_ERR;
      int y = item_top(w, index);
      RECT r = make_rect(0, y, client_rect(w).right, y + item_height(w, index));
      memcpy((void*) lparam, &r, sizeof r);
      return 1;
    }
    case LB_ITEMFROMPOINT: {
      int item = item_at(w, hi16(lparam));
      return item < 0 ? make_lparam(std::max(0, l->count - 1), 1) : item;
    }
    case 0x0082: /* WM_NCDESTROY */ delete l; w->control = NULL; return 0;
  }
  return def_window_proc(hwnd, msg, wparam, lparam);
}

// ---------------------------------------------------------------------------
// Scroll bars

static ScrollBar& bar_of(Window* w, bool vertical) { return vertical ? w->vscroll : w->hscroll; }

// the track of a scroll bar (window coordinates)
static RECT track_rect(Window* w, bool vertical) {
  RECT c = client_rect_in_window(w);
  if (vertical) return make_rect(c.right, c.top, c.right + scroll_bar_size, c.bottom);
  return make_rect(c.left, c.bottom, c.right, c.bottom + scroll_bar_size);
}

static int track_length(Window* w, bool vertical) {
  RECT t = track_rect(w, vertical);
  return vertical ? t.bottom - t.top : t.right - t.left;
}

// the thumb: offset and length along the track
static void thumb(Window* w, bool vertical, int pos, int& offset, int& length) {
  ScrollBar& s = bar_of(w, vertical);
  int len = track_length(w, vertical);
  int range = s.max - s.min + 1;
  if (range <= 0 || (s.page > 0 && s.page >= range)) { offset = 0; length = len; return; }
  length = s.page > 0 ? std::max(32, (int) ((int64_t) len * s.page / range)) : 32;
  length = std::min(length, len);
  int span = range - std::max(1, s.page);
  int p = std::max(0, std::min(span, pos - s.min));
  offset = span > 0 ? (int) ((int64_t) (len - length) * p / span) : 0;
}

void paint_scroll_bars(Window* w, word hdc) {
  for (int v = 0; v < 2; v++) {
    bool vertical = v == 1;
    ScrollBar& s = bar_of(w, vertical);
    if (!s.visible) continue;
    RECT t = track_rect(w, vertical);
    gdi_fill_rect(hdc, t, 0xE6E6E6);
    gdi_fill_rect(hdc, vertical ? make_rect(t.left, t.top, t.left + 1, t.bottom) : make_rect(t.left, t.top, t.right, t.top + 1), 0xB4B4B4);
    int range = s.max - s.min + 1;
    if (s.disabled || range <= 0 || (s.page > 0 && s.page >= range)) continue;
    int offset, length;
    thumb(w, vertical, scroll_bar_tracking() == w ? s.track_pos : s.pos, offset, length);
    RECT r = vertical ? make_rect(t.left + 4, t.top + offset + 2, t.right - 3, t.top + offset + length - 2)
                      : make_rect(t.left + offset + 2, t.top + 4, t.left + offset + length - 2, t.bottom - 3);
    gdi_fill_rect(hdc, r, 0x7A7A7A);
  }
  if (w->vscroll.visible && w->hscroll.visible) {
    RECT c = client_rect_in_window(w);
    gdi_fill_rect(hdc, make_rect(c.right, c.bottom, c.right + scroll_bar_size, c.bottom + scroll_bar_size), 0xE6E6E6);
  }
}

int scroll_bar_hit(Window* w, int x, int y, bool& vertical) {
  if (w->vscroll.visible && rect_contains(track_rect(w, true), x, y))  { vertical = true;  return 7; }	// HTVSCROLL
  if (w->hscroll.visible && rect_contains(track_rect(w, false), x, y)) { vertical = false; return 6; }	// HTHSCROLL
  return 0;
}

static Window* tracking_window = NULL;	// the window whose scroll bar thumb is dragged
static bool    tracking_vertical;
static int     tracking_grab;		// the mouse offset in the thumb
static HANDLE  tracking_hwnd;

Window* scroll_bar_tracking() {
  if (tracking_window != NULL && window(tracking_hwnd) != tracking_window) tracking_window = NULL;
  return tracking_window;
}

static void send_scroll(Window* w, bool vertical, int code, int pos) {
  send_message(w->handle, vertical ? WM_VSCROLL : WM_HSCROLL, make_wparam(code, pos), 0);
}

// mouse messages in the scroll bars (window coordinates); called without gui_lock
void scroll_bar_mouse(Window* w, word msg, int x, int y) {
  extern word SetCapture_user(word);
  extern word ReleaseCapture_user();
  HANDLE hwnd = w->handle;
  if (scroll_bar_tracking() == w) {
    ScrollBar& s = bar_of(w, tracking_vertical);
    RECT t = track_rect(w, tracking_vertical);
    int len = track_length(w, tracking_vertical);
    int offset, length;
    thumb(w, tracking_vertical, s.pos, offset, length);
    int along = (tracking_vertical ? y - t.top : x - t.left) - tracking_grab;
    int range = s.max - s.min + 1, span = range - std::max(1, s.page);
    int pos = len > length ? s.min + (int) ((int64_t) along * span / (len - length)) : s.min;
    pos = std::max(s.min, std::min(s.min + std::max(0, span), pos));
    if (msg == WM_MOUSEMOVE) {
      if (pos != s.track_pos) {
        s.track_pos = pos;
        send_scroll(w, tracking_vertical, SB_THUMBTRACK, pos);
        if (window(hwnd)) paint_nonclient(w);
      }
    } else if (msg == WM_LBUTTONUP) {
      tracking_window = NULL;
      ReleaseCapture_user();
      send_scroll(w, tracking_vertical, SB_THUMBPOSITION, s.track_pos);
      if (window(hwnd)) send_scroll(w, tracking_vertical, SB_ENDSCROLL, 0);
      if (window(hwnd)) paint_nonclient(w);
    }
    return;
  }
  if (msg != WM_LBUTTONDOWN && msg != WM_LBUTTONDBLCLK) return;
  bool vertical;
  if (scroll_bar_hit(w, x, y, vertical) == 0) return;
  ScrollBar& s = bar_of(w, vertical);
  int range = s.max - s.min + 1;
  if (s.disabled || range <= 0 || (s.page > 0 && s.page >= range)) return;
  RECT t = track_rect(w, vertical);
  int along = vertical ? y - t.top : x - t.left;
  int offset, length;
  thumb(w, vertical, s.pos, offset, length);
  if (along < offset)                { send_scroll(w, vertical, SB_PAGEUP, 0);   if (window(hwnd)) send_scroll(w, vertical, SB_ENDSCROLL, 0); }
  else if (along >= offset + length) { send_scroll(w, vertical, SB_PAGEDOWN, 0); if (window(hwnd)) send_scroll(w, vertical, SB_ENDSCROLL, 0); }
  else {
    tracking_window = w;
    tracking_hwnd = hwnd;
    tracking_vertical = vertical;
    tracking_grab = along - offset;
    s.track_pos = s.pos;
    SetCapture_user(hwnd);
  }
}

// sets a scroll bar; shows or hides it as needed (WM_SIZE when the client area changes)
void set_scroll_bar(Window* w, bool vertical, int min, int max, int page, int pos, bool redraw) {
  ScrollBar& s = bar_of(w, vertical);
  s.min = min; s.max = max; s.page = page; s.pos = pos;
  if (redraw) paint_nonclient(w);
}

static void scroll_bar_visibility(Window* w, bool vertical, bool visible) {
  ScrollBar& s = bar_of(w, vertical);
  if (s.visible == visible) return;
  s.visible = visible;
  if (visible) w->style |= vertical ? 0x00200000 : 0x00100000;
  else if (w->cls == NULL || strcasecmp(w->cls->name.c_str(), "ListBox") != 0) w->style &= ~(vertical ? 0x00200000 : 0x00100000);
  RECT c = client_rect(w);
  invalidate(w, NULL, true);
  if (w->parent != NULL) { RECT r = w->rect; invalidate(w->parent, &r, true); }
  send_message(w->handle, WM_SIZE, 0, make_lparam(c.right, c.bottom));
}

// SCROLLINFO: cbSize, fMask, nMin, nMax, nPage, nPos, nTrackPos
word SetScrollInfo_user(word hwnd, word bar, word info, word redraw) {
  Window* w = window(hwnd);
  if (w == NULL || (bar != SB_HORZ && bar != SB_VERT)) return 0;
  bool vertical = bar == SB_VERT;
  ScrollBar& s = bar_of(w, vertical);
  const int32_t* f = (const int32_t*) info;
  uint32_t mask = (uint32_t) f[1];
  if (mask & SIF_RANGE) { s.min = f[2]; s.max = f[3]; }
  if (mask & SIF_PAGE)  s.page = std::max(0, f[4]);
  if (mask & SIF_POS)   s.pos = f[5];
  int range = s.max - s.min + 1;
  s.pos = std::max(s.min, std::min(s.pos, s.max - std::max(0, s.page - 1)));
  bool needed = range > 0 && (s.page == 0 || s.page < range);
  if (mask & SIF_DISABLENOSCROLL) { s.disabled = !needed; needed = true; }
  else s.disabled = false;
  if (needed != s.visible) scroll_bar_visibility(w, vertical, needed);
  else if (redraw) paint_nonclient(w);
  return s.pos;
}

word GetScrollInfo_user(word hwnd, word bar, word info) {
  Window* w = window(hwnd);
  if (w == NULL || (bar != SB_HORZ && bar != SB_VERT)) return 0;
  ScrollBar& s = bar_of(w, bar == SB_VERT);
  int32_t* f = (int32_t*) info;
  uint32_t mask = (uint32_t) f[1];
  if (mask & SIF_RANGE)    { f[2] = s.min; f[3] = s.max; }
  if (mask & SIF_PAGE)     f[4] = s.page;
  if (mask & SIF_POS)      f[5] = s.pos;
  if (mask & SIF_TRACKPOS) f[6] = scroll_bar_tracking() == w ? s.track_pos : s.pos;
  return 1;
}

word ShowScrollBar_user(word hwnd, word bar, word show) {
  Window* w = window(hwnd);
  if (w == NULL) return 0;
  if (bar == SB_VERT || bar == SB_BOTH) scroll_bar_visibility(w, true, show != 0);
  if (bar == SB_HORZ || bar == SB_BOTH) scroll_bar_visibility(w, false, show != 0);
  return 1;
}

// ---------------------------------------------------------------------------

void register_control_classes() {
  extern void add_builtin_class(const char* name, uint32_t style, word wndproc);
  add_builtin_class("Button",  0x0008, (word) &button_wndproc);		// CS_DBLCLKS (the image expects them)
  add_builtin_class("ListBox", 0x0008, (word) &listbox_wndproc);
}

} // namespace w32

#endif // __APPLE__
