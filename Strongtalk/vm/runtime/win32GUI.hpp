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

// Internal interface of the Win32 GUI emulation (USER32, GDI32, COMDLG32) on macOS:
//   win32User.mm     - threads (the VM runs off the main thread, which runs AppKit), handles,
//                      windows, message queues, input, menus, cursors, clipboard, dialogs
//   win32Gdi.cpp     - device contexts, bitmaps, pens, brushes, fonts, drawing (CoreGraphics/CoreText)
//   win32Controls.cpp- the predefined window classes BUTTON and LISTBOX, scroll bars
//
// The image sees 32-bit Win32: handles are 32-bit values (never in the ExternalWords handle
// range), structures have the 32-bit layouts, and pointers stored in them are external words.
// Windows live in a tree: top-level windows are NSWindows showing a backing bitmap (the
// "surface", logical pixels x gdi_scale); child windows are drawn into their top-level surface.

#ifndef WIN32GUI_HPP
#define WIN32GUI_HPP

#ifndef _WIN32

#include <stdint.h>
#include <pthread.h>
#include <vector>
#include <string>
#include <deque>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>

namespace w32 {

typedef intptr_t word;		// DLL arguments and results
typedef uint32_t DWORD;
typedef uint32_t HANDLE;	// all handles are 32-bit values
typedef uint32_t COLORREF;	// 0x00BBGGRR

struct RECT  { int32_t left, top, right, bottom; };
struct POINT { int32_t x, y; };

inline bool rect_empty(const RECT& r)                  { return r.right <= r.left || r.bottom <= r.top; }
inline RECT make_rect(int l, int t, int r, int b)      { RECT x = { l, t, r, b }; return x; }
RECT rect_union(const RECT& a, const RECT& b);
RECT rect_intersection(const RECT& a, const RECT& b);
inline RECT rect_offset(const RECT& r, int dx, int dy) { return make_rect(r.left + dx, r.top + dy, r.right + dx, r.bottom + dy); }
inline bool rect_contains(const RECT& r, int x, int y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
// rects -= r (each rect replaced by the up to 4 parts outside r)
void rects_subtract(std::vector<RECT>& rects, const RECT& r);

inline word make_lparam(int lo, int hi) { return (word) (uint32_t) ((lo & 0xFFFF) | ((hi & 0xFFFF) << 16)); }

// The pointer represented by a 32-bit structure field / callback word (ExternalWords::decode)
void*    external_pointer(uint32_t w);
uint32_t external_word(void* p);

// ---------------------------------------------------------------------------
// Handles

enum ObjKind { K_ANY = 0, K_WINDOW, K_MENU, K_DC, K_BITMAP, K_PEN, K_BRUSH, K_FONT, K_PALETTE, K_CURSOR, K_ICON };

struct Obj {
  ObjKind kind;
  HANDLE  handle;
  bool    stock;	// stock objects are never deleted
  Obj(ObjKind k);
  virtual ~Obj();
};

Obj* lookup(word handle, ObjKind kind);		// NULL if not a live object of that kind
template <class T> T* lookup_as(word h, ObjKind k) { return (T*) lookup(h, k); }

// The GUI state is shared by the VM threads and the main (AppKit) thread: gui_lock protects
// it (recursive). Never wait for the main thread while holding it.
void gui_lock();
void gui_unlock();
struct GUILocker { GUILocker() { gui_lock(); } ~GUILocker() { gui_unlock(); } };

extern bool TraceGUI;		// $STRONGTALK_TRACE_GUI: trace calls of unimplemented functions

// ---------------------------------------------------------------------------
// GDI objects (win32Gdi.cpp)

extern int gdi_scale;		// device pixels per logical pixel (2 on Retina displays)

struct Bitmap : Obj {
  int          width, height;	// logical pixels
  int          bits_pixel;	// as created: 1 (monochrome) or 32
  CGContextRef ctx;		// device pixels (width * gdi_scale), y down, logical coordinates
  uint32_t*    pixels;		// 0xXXRRGGBB
  size_t       row_words;	// uint32_t per row
  int          selected;	// number of DCs it is selected into
  Bitmap(int w, int h, int bpp);
  ~Bitmap();
  void resize(int w, int h);	// keeps the overlapping contents
};

struct Pen : Obj {
  int      style;		// PS_SOLID, PS_DASH, PS_DOT, PS_NULL, ...
  int      width;
  COLORREF color;
  Pen(int s, int w, COLORREF c) : Obj(K_PEN), style(s), width(w), color(c) {}
};

struct Brush : Obj {
  int      style;		// BS_SOLID, BS_NULL
  COLORREF color;
  Brush(int s, COLORREF c) : Obj(K_BRUSH), style(s), color(c) {}
};

struct Font : Obj {
  CTFontRef ct;
  int       height, ascent, descent, internal_leading, external_leading;
  int       ave_width, max_width, weight;
  bool      italic, underline, strikeout, fixed_pitch;
  int       pitch_and_family, char_set;
  float     advance[256];	// rounded advances of the characters 0..255 (Windows-1252)
  CGGlyph   glyph[256];
  Font(const void* logfont);	// LOGFONTA
  ~Font();
};

Font* default_gui_font();	// for the predefined controls

// Where a DC draws: a bitmap with an origin and a clip (logical surface coordinates)
struct Window;
struct DrawTarget {
  Bitmap*           bitmap;
  int               ox, oy;	// DC coordinate (0, 0) in the bitmap
  std::vector<RECT> clip;	// in bitmap coordinates; empty = draw nothing
  Window*           top;	// the top-level window owning the surface (window DCs), or NULL
};

// Window DCs: every window has one DC (GetDC and BeginPaint answer it, as with CS_OWNDC)
word    gdi_window_dc(Window* w);
void    gdi_dc_set_paint_clip(word hdc, const RECT* clip);	// client coordinates; NULL = whole client area
void    gdi_destroy_window_dc(Window* w);
Bitmap* gdi_load_bmp(const char* posix_path);			// NULL if not a BMP file
void    gdi_initialize(int scale);				// at GUI start

// Drawing helpers for the controls (DC coordinates)
void gdi_fill_rect(word hdc, const RECT& r, COLORREF color);
void gdi_frame_rect(word hdc, const RECT& r, COLORREF color);
void gdi_draw_text(word hdc, int x, int y, const char* s, int n, COLORREF color, Font* font);	// y = top
int  gdi_text_width(Font* font, const char* s, int n);
void gdi_draw_bitmap(word hdc, Bitmap* bm, int x, int y);

// ---------------------------------------------------------------------------
// Windows and messages (win32User.mm)

struct WndClass {
  std::string name;
  uint32_t    style;
  word        wndproc;
  word        cursor, icon, background;
};

typedef word (*WNDPROC)(word hwnd, word msg, word wparam, word lparam);

struct ScrollBar {
  bool visible;
  int  min, max, page, pos, track_pos;
  bool disabled;
};

struct Queue;
struct Surface;

struct Window : Obj {
  WndClass*            cls;
  word                 wndproc;
  uint32_t             style, ex_style;
  Window*              parent;
  std::vector<Window*> children;	// z-order: first is on top
  RECT                 rect;		// window rect: in parent client coordinates (screen for top-level)
  std::string          text;
  word                 id;		// child window id (the hMenu argument)
  word                 user_data;
  word                 menu;		// top-level menu bar
  bool                 visible, enabled, destroyed;
  bool                 size_sent;	// WM_SIZE / WM_MOVE of the first show
  RECT                 update;		// invalid client area (empty = valid)
  bool                 erase;		// background erase pending (WM_ERASEBKGND)
  ScrollBar            vscroll, hscroll;
  Queue*               queue;		// of the thread that created it
  void*                control;		// state of a predefined control (win32Controls.cpp)
  // top-level windows
  void*                nswindow;	// NSWindow (retained), main thread
  Bitmap*              surface;		// client area contents
  RECT                 dirty;		// surface area to show
  bool                 display_pending;
  POINT                min_track;	// WM_GETMINMAXINFO
  word                 dc;		// the window's own DC (CS_OWNDC semantics for all windows)
  word                 last_focus;	// top-level: the focus window to restore on activation
  Window(WndClass* c);
  ~Window();
};

Window* window(word hwnd);			// NULL if not a live window
Window* top_level(Window* w);
RECT    client_rect(Window* w);			// in client coordinates
RECT    client_rect_in_window(Window* w);	// client area in window coordinates (inside borders, scroll bars)
POINT   client_origin_in_top(Window* w);	// client (0, 0) in the top-level surface
bool    is_visible(Window* w);			// it and all ancestors visible
void    invalidate(Window* w, const RECT* r, bool erase);	// client coordinates, NULL = all
void    validate(Window* w);

// The DrawTarget of a window DC: the client area (clipped to paint_clip, client coordinates,
// as by BeginPaint), minus the visible children (WS_CLIPCHILDREN) and the siblings above
void    window_draw_target(Window* w, const RECT* paint_clip, DrawTarget& target);
void    window_nonclient_target(Window* w, DrawTarget& target);
// a window DC drew into r (surface coordinates): show it
void    surface_changed(Window* top, const RECT& r);

word    send_message(word hwnd, word msg, word wparam, word lparam);
bool    post_message(word hwnd, word msg, word wparam, word lparam);
word    def_window_proc(word hwnd, word msg, word wparam, word lparam);

// non-client area painting (borders, scroll bars) after the window painted its client area
void    paint_nonclient(Window* w);

// The predefined window classes (win32Controls.cpp)
void    register_control_classes();
word    button_wndproc(word hwnd, word msg, word wparam, word lparam);
word    listbox_wndproc(word hwnd, word msg, word wparam, word lparam);
// scroll bars: hit testing and mouse tracking in the non-client area
int     scroll_bar_hit(Window* w, int x, int y, bool& vertical);	// window coordinates; 0 = none
void    scroll_bar_mouse(Window* w, word msg, int x, int y);		// window coordinates
Window* scroll_bar_tracking();						// the window whose scroll bar is dragged
void    paint_scroll_bars(Window* w, word hdc);			// hdc: window DC with window coordinates
extern const int scroll_bar_size;

// Windows-1252 <-> UTF-8 (the image's strings are Windows-1252)
std::string ansi_to_utf8(const char* s, int n = -1);
std::string utf8_to_ansi(const char* s);

} // namespace w32

#endif // _WIN32
#endif // WIN32GUI_HPP
