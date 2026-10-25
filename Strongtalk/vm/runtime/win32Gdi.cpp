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

// GDI32 emulation on CoreGraphics and CoreText, see win32GUI.hpp.
//
// Bitmaps (and the surfaces of top-level windows) have gdi_scale device pixels per logical
// pixel; their contexts have logical coordinates with y down. Shapes are drawn without
// antialiasing so that they cover whole logical pixels, as in GDI; text is antialiased.
// Character advances are rounded to whole pixels: GetCharWidth32A, GetCharWidthFloatA,
// GetTextExtentPoint32A and TextOutA agree (the image positions the characters itself).
// Raster operations (BitBlt, SetPixelV, DrawFocusRect) work on the pixels directly.

#include "platform.hpp"
#if defined(__APPLE__)

#include "win32GUI.hpp"
#include "win32Shim.hpp"
#include <map>
#include <math.h>

namespace w32 {

// ---------------------------------------------------------------------------
// Colors

static COLORREF sys_color(int index) {	// GetSysColor
  switch (index) {
    case 0:  return 0xC8C8C8;	// COLOR_SCROLLBAR
    case 1:  return 0x808000;	// COLOR_BACKGROUND
    case 2:  return 0xD77800;	// COLOR_ACTIVECAPTION
    case 4:  return 0xF0F0F0;	// COLOR_MENU
    case 5:  return 0xFFFFFF;	// COLOR_WINDOW
    case 6:  return 0x646464;	// COLOR_WINDOWFRAME
    case 7:  return 0x000000;	// COLOR_MENUTEXT
    case 8:  return 0x000000;	// COLOR_WINDOWTEXT
    case 9:  return 0xFFFFFF;	// COLOR_CAPTIONTEXT
    case 12: return 0xABABAB;	// COLOR_APPWORKSPACE
    case 13: return 0xD77800;	// COLOR_HIGHLIGHT
    case 14: return 0xFFFFFF;	// COLOR_HIGHLIGHTTEXT
    case 15: return 0xF0F0F0;	// COLOR_BTNFACE
    case 16: return 0xA0A0A0;	// COLOR_BTNSHADOW
    case 17: return 0x6D6D6D;	// COLOR_GRAYTEXT
    case 18: return 0x000000;	// COLOR_BTNTEXT
    case 20: return 0xFFFFFF;	// COLOR_BTNHIGHLIGHT
    case 21: return 0x696969;	// COLOR_3DDKSHADOW
    case 22: return 0xE3E3E3;	// COLOR_3DLIGHT
  }
  return 0xFFFFFF;
}

// 0x00BBGGRR (also PALETTERGB 0x02BBGGRR) -> 0xXXRRGGBB
static inline uint32_t pixel_of(COLORREF c) {
  if ((c >> 24) == 1) {		// PALETTEINDEX: the 16 VGA colors of the default palette
    static const uint32_t vga[16] = { 0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080, 0x008080, 0xC0C0C0,
                                      0x808080, 0xFF0000, 0x00FF00, 0xFFFF00, 0x0000FF, 0xFF00FF, 0x00FFFF, 0xFFFFFF };
    return vga[c & 15];
  }
  return ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);
}

static inline COLORREF colorref_of(uint32_t p) {
  return ((p & 0xFF) << 16) | (p & 0xFF00) | ((p >> 16) & 0xFF);
}

static void set_fill(CGContextRef ctx, COLORREF c) {
  uint32_t p = pixel_of(c);
  CGContextSetRGBFillColor(ctx, ((p >> 16) & 0xFF) / 255.0, ((p >> 8) & 0xFF) / 255.0, (p & 0xFF) / 255.0, 1.0);
}

// ---------------------------------------------------------------------------
// Bitmaps

static CGColorSpaceRef device_rgb = NULL;

static CGContextRef create_context(int w, int h, uint32_t*& pixels, size_t& row_words) {
  int pw = std::max(1, w) * gdi_scale, ph = std::max(1, h) * gdi_scale;
  row_words = (size_t) pw;
  pixels = (uint32_t*) calloc((size_t) pw * ph, sizeof(uint32_t));
  CGContextRef ctx = CGBitmapContextCreate(pixels, pw, ph, 8, row_words * 4, device_rgb,
                                           kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
  CGContextTranslateCTM(ctx, 0, ph);
  CGContextScaleCTM(ctx, gdi_scale, -gdi_scale);
  CGContextSetShouldAntialias(ctx, false);
  CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);
  return ctx;
}

Bitmap::Bitmap(int w, int h, int bpp) : Obj(K_BITMAP), width(w), height(h), bits_pixel(bpp) {
  ctx = create_context(w, h, pixels, row_words);
  for (size_t i = 0; i < row_words * std::max(1, h) * gdi_scale; i++) pixels[i] = 0xFFFFFFFF;	// white
}

Bitmap::~Bitmap() {
  CGContextRelease(ctx);
  free(pixels);
}

void Bitmap::resize(int w, int h) {
  uint32_t* np;
  size_t nrow;
  CGContextRef nctx = create_context(w, h, np, nrow);
  int ph = std::max(1, h) * gdi_scale;
  for (size_t i = 0; i < nrow * ph; i++) np[i] = 0xFFFFFFFF;
  int cw = std::min(std::max(1, w), std::max(1, width)) * gdi_scale, ch = std::min(std::max(1, h), std::max(1, height)) * gdi_scale;
  for (int y = 0; y < ch; y++) memcpy(np + y * nrow, pixels + y * row_words, cw * 4);
  CGContextRelease(ctx);
  free(pixels);
  ctx = nctx; pixels = np; row_words = nrow; width = w; height = h;
}

// ---------------------------------------------------------------------------
// Fonts

static CTFontRef ui_font(CGFloat size, bool bold, bool fixed) {
  if (fixed) {
    CTFontRef f = CTFontCreateWithName(CFSTR("Menlo-Regular"), size, NULL);
    if (bold) {
      CTFontRef b = CTFontCreateCopyWithSymbolicTraits(f, size, NULL, kCTFontTraitBold, kCTFontTraitBold);
      if (b) { CFRelease(f); f = b; }
    }
    return f;
  }
  return CTFontCreateUIFontForLanguage(bold ? kCTFontUIFontEmphasizedSystem : kCTFontUIFontSystem, size, NULL);
}

static CTFontRef family_font(const char* family, CGFloat size, bool bold, bool italic) {
  CFStringRef name = CFStringCreateWithCString(NULL, family, kCFStringEncodingWindowsLatin1);
  CFStringRef keys[] = { kCTFontFamilyNameAttribute };
  CFTypeRef values[] = { name };
  CFDictionaryRef attrs = CFDictionaryCreate(NULL, (const void**) keys, (const void**) values, 1,
                                             &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CTFontDescriptorRef desc = CTFontDescriptorCreateWithAttributes(attrs);
  CTFontDescriptorRef match = CTFontDescriptorCreateMatchingFontDescriptor(desc, NULL);
  CTFontRef font = NULL;
  if (match != NULL) {
    font = CTFontCreateWithFontDescriptor(match, size, NULL);
    CFStringRef got = (CFStringRef) CTFontCopyAttribute(font, kCTFontFamilyNameAttribute);
    if (got == NULL || CFStringCompare(got, name, kCFCompareCaseInsensitive) != kCFCompareEqualTo) { CFRelease(font); font = NULL; }
    if (got) CFRelease(got);
    CFRelease(match);
  }
  CFRelease(desc); CFRelease(attrs); CFRelease(name);
  if (font == NULL) return NULL;
  CTFontSymbolicTraits traits = (bold ? kCTFontTraitBold : 0) | (italic ? kCTFontTraitItalic : 0);
  if (traits) {
    CTFontRef t = CTFontCreateCopyWithSymbolicTraits(font, size, NULL, traits, traits);
    if (t) { CFRelease(font); font = t; }
  }
  return font;
}

static const struct { const char* win; const char* mac; } face_map[] = {
  { "Lucida Sans",          "Lucida Grande" },
  { "Lucida Sans Unicode",  "Lucida Grande" },
  { "Lucida Console",       "Menlo" },
  { "Courier",              "Courier New" },
  { "Fixedsys",             "Menlo" },
  { "Terminal",             "Menlo" },
  { "Consolas",             "Menlo" },
  { "Times Roman",          "Times New Roman" },
  { "Times",                "Times New Roman" },
  { "Helv",                 "Helvetica" },
  { NULL, NULL }
};

static bool is_ui_face(const char* face) {
  static const char* ui[] = { "", "MS Sans Serif", "MS Shell Dlg", "MS Shell Dlg 2", "Microsoft Sans Serif",
                              "System", "Segoe UI", "MS Serif", NULL };
  for (int i = 0; ui[i]; i++) if (strcasecmp(face, ui[i]) == 0) return true;
  return false;
}

// LOGFONTA (32-bit, 60 bytes)
struct LOGFONTA {
  int32_t height, width, escapement, orientation, weight;
  uint8_t italic, underline, strike_out, char_set, out_precision, clip_precision, quality, pitch_and_family;
  char    face_name[32];
};

static std::map<Font*, LOGFONTA> font_descriptions;	// GetObjectA

Font::Font(const void* logfont) : Obj(K_FONT) {
  LOGFONTA lf;
  memcpy(&lf, logfont, sizeof lf);
  lf.face_name[31] = '\0';
  font_descriptions[this] = lf;
  bool bold = lf.weight >= 600;
  italic = lf.italic != 0;
  underline = lf.underline != 0;
  strikeout = lf.strike_out != 0;
  weight = lf.weight ? lf.weight : 400;
  char_set = lf.char_set;
  bool want_fixed = (lf.pitch_and_family & 3) == 1 || (lf.pitch_and_family & 0xF0) == 0x30;	// FIXED_PITCH, FF_MODERN
  const char* face = lf.face_name;
  for (int i = 0; face_map[i].win; i++) if (strcasecmp(face, face_map[i].win) == 0) { face = face_map[i].mac; break; }
  // the em size: lfHeight < 0 is the character height, > 0 the cell height (ascent + descent)
  CGFloat em = lf.height < 0 ? -lf.height : (lf.height == 0 ? 13 : lf.height);
  bool symbol_face = strcasecmp(face, "Symbol") == 0;
  CTFontRef probe = is_ui_face(face) || symbol_face ? ui_font(100, bold, want_fixed) : family_font(face, 100, bold, italic);
  if (probe == NULL) probe = ui_font(100, bold, want_fixed);
  if (lf.height > 0) em = lf.height * 100 / (CTFontGetAscent(probe) + CTFontGetDescent(probe));
  ct = CTFontCreateCopyWithAttributes(probe, em, NULL, NULL);
  CFRelease(probe);
  if (italic && !(CTFontGetSymbolicTraits(ct) & kCTFontTraitItalic)) {
    CTFontRef t = CTFontCreateCopyWithSymbolicTraits(ct, em, NULL, kCTFontTraitItalic, kCTFontTraitItalic);
    if (t) { CFRelease(ct); ct = t; }
  }
  fixed_pitch = (CTFontGetSymbolicTraits(ct) & kCTFontTraitMonoSpace) != 0;
  ascent  = (int) ceil(CTFontGetAscent(ct));
  descent = (int) ceil(CTFontGetDescent(ct));
  height  = ascent + descent;
  internal_leading = std::max(0, height - (int) lround(em));
  external_leading = (int) lround(CTFontGetLeading(ct));
  pitch_and_family = lf.pitch_and_family;
  // the characters of Windows-1252 (of the Symbol encoding for the Symbol font)
  bool symbol = strcasecmp(lf.face_name, "Symbol") == 0 || lf.char_set == 2;	// SYMBOL_CHARSET
  static const uint16_t symbol_encoding[224] = {	// Adobe Symbol encoding of 0x20..0xFF
      0x0020, 0x0021, 0x2200, 0x0023, 0x2203, 0x0025, 0x0026, 0x220B, 0x0028, 0x0029, 0x2217, 0x002B, 0x002C, 0x2212, 0x002E, 0x002F,
      0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037, 0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
      0x2245, 0x0391, 0x0392, 0x03A7, 0x0394, 0x0395, 0x03A6, 0x0393, 0x0397, 0x0399, 0x03D1, 0x039A, 0x039B, 0x039C, 0x039D, 0x039F,
      0x03A0, 0x0398, 0x03A1, 0x03A3, 0x03A4, 0x03A5, 0x03C2, 0x03A9, 0x039E, 0x03A8, 0x0396, 0x005B, 0x2234, 0x005D, 0x22A5, 0x005F,
      0xF8E5, 0x03B1, 0x03B2, 0x03C7, 0x03B4, 0x03B5, 0x03C6, 0x03B3, 0x03B7, 0x03B9, 0x03D5, 0x03BA, 0x03BB, 0x03BC, 0x03BD, 0x03BF,
      0x03C0, 0x03B8, 0x03C1, 0x03C3, 0x03C4, 0x03C5, 0x03D6, 0x03C9, 0x03BE, 0x03C8, 0x03B6, 0x007B, 0x007C, 0x007D, 0x223C, 0x0020,
      0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020,
      0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020,
      0x20AC, 0x03D2, 0x2032, 0x2264, 0x2044, 0x221E, 0x0192, 0x2663, 0x2666, 0x2665, 0x2660, 0x2194, 0x2190, 0x2191, 0x2192, 0x2193,
      0x00B0, 0x00B1, 0x2033, 0x2265, 0x00D7, 0x221D, 0x2202, 0x2022, 0x00F7, 0x2260, 0x2261, 0x2248, 0x2026, 0x23D0, 0x23AF, 0x21B5,
      0x2135, 0x2111, 0x211C, 0x2118, 0x2297, 0x2295, 0x2205, 0x2229, 0x222A, 0x2283, 0x2287, 0x2284, 0x2282, 0x2286, 0x2208, 0x2209,
      0x2220, 0x2207, 0x00AE, 0x00A9, 0x2122, 0x220F, 0x221A, 0x22C5, 0x00AC, 0x2227, 0x2228, 0x21D4, 0x21D0, 0x21D1, 0x21D2, 0x21D3,
      0x25CA, 0x2329, 0x00AE, 0x00A9, 0x2122, 0x2211, 0x239B, 0x239C, 0x239D, 0x23A1, 0x23A2, 0x23A3, 0x23A7, 0x23A8, 0x23A9, 0x23AA,
      0x0020, 0x232A, 0x222B, 0x2320, 0x23AE, 0x2321, 0x239E, 0x239F, 0x23A0, 0x23A4, 0x23A5, 0x23A6, 0x23AB, 0x23AC, 0x23AD, 0x0020,
    };
  UniChar chars[256];
  for (int c = 0; c < 256; c++) {
    static const uint16_t high[32] = {
      0x20AC, 0x0020, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0020, 0x017D, 0x0020,
      0x0020, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0020, 0x017E, 0x0178 };
    chars[c] = c < 32 ? 0x20 : (c >= 0x80 && c < 0xA0 ? high[c - 0x80] : c);
    if (symbol && c >= 0x20) chars[c] = symbol_encoding[c - 0x20];
  }
  CTFontGetGlyphsForCharacters(ct, chars, glyph, 256);
  CGSize adv[256];
  CTFontGetAdvancesForGlyphs(ct, kCTFontOrientationHorizontal, glyph, adv, 256);
  max_width = 0;
  for (int c = 0; c < 256; c++) {
    advance[c] = c < 32 ? 0 : (float) floor(adv[c].width + 0.5);
    max_width = std::max(max_width, (int) advance[c]);
  }
  ave_width = (int) advance['x'];
}

Font::~Font() {
  font_descriptions.erase(this);
  CFRelease(ct);
}

static Font* make_font(const char* face, int height, int weight, int pitch_and_family) {
  LOGFONTA lf;
  memset(&lf, 0, sizeof lf);
  lf.height = height;
  lf.weight = weight;
  lf.pitch_and_family = pitch_and_family;
  strlcpy(lf.face_name, face, sizeof lf.face_name);
  Font* f = new Font(&lf);
  f->stock = true;
  return f;
}

Font* default_gui_font() {
  static Font* f = NULL;
  if (f == NULL) f = make_font("MS Shell Dlg", -12, 400, 0);
  return f;
}

int gdi_text_width(Font* font, const char* s, int n) {
  int w = 0;
  for (int i = 0; i < n; i++) w += (int) font->advance[(unsigned char) s[i]];
  return w;
}

// ---------------------------------------------------------------------------
// Device contexts

enum { TRANSPARENT = 1, OPAQUE = 2 };
enum { TA_UPDATECP = 1, TA_RIGHT = 2, TA_CENTER = 6, TA_BOTTOM = 8, TA_BASELINE = 24 };
enum { PS_SOLID = 0, PS_DASH = 1, PS_DOT = 2, PS_DASHDOT = 3, PS_DASHDOTDOT = 4, PS_NULL = 5, PS_INSIDEFRAME = 6 };
enum { BS_SOLID = 0, BS_NULL = 1 };

struct Palette : Obj {
  Palette() : Obj(K_PALETTE) {}
};

struct DCState {
  Pen*     pen;
  Brush*   brush;
  Font*    font;
  Bitmap*  bitmap;
  COLORREF text_color, bk_color;
  int      bk_mode;
  uint32_t text_align;
  POINT    pos;
};

struct DC : Obj {
  enum Type { MEMORY, WINDOW, NONCLIENT, SCREEN } type;
  HANDLE   hwnd;		// WINDOW, NONCLIENT
  bool     paint_clip_set;
  RECT     paint_clip;		// client coordinates
  DCState  s;
  std::vector<DCState> saved;
  Palette* palette;
  DC(Type t, HANDLE w);
  ~DC();
};

// The objects of a DC state are counted (Obj::selected) so that DeleteObject of a selected pen,
// brush or font (as Win32 allows) defers freeing it and DCs never hold dangling pointers.
static void hold(Obj* o) { if (o) o->selected++; }
static void let_go(Obj* o) {
  if (o && --o->selected == 0 && o->deleted) delete o;
}
static void hold_state(DCState& s)   { hold(s.pen); hold(s.brush); hold(s.font); hold(s.bitmap); }
static void let_go_state(DCState& s) { let_go(s.pen); let_go(s.brush); let_go(s.font); let_go(s.bitmap); }

DC::~DC() {
  let_go_state(s);
  for (DCState& x : saved) let_go_state(x);
}

static Pen*     stock_pens[3];		// WHITE_PEN, BLACK_PEN, NULL_PEN
static Brush*   stock_brushes[6];	// WHITE_BRUSH .. NULL_BRUSH
static Font*    stock_fonts[8];		// OEM_FIXED_FONT (10) .. DEFAULT_GUI_FONT (17)
static Bitmap*  stock_bitmap;		// the 1x1 bitmap of new memory DCs
static Palette* stock_palette;
static Bitmap*  scratch_bitmap;		// what screen DCs draw into

DC::DC(Type t, HANDLE w) : Obj(K_DC), type(t), hwnd(w), paint_clip_set(false), palette(stock_palette) {
  paint_clip = make_rect(0, 0, 0, 0);
  s.pen = stock_pens[1];
  s.brush = stock_brushes[0];
  s.font = stock_fonts[13 - 10];	// SYSTEM_FONT
  s.bitmap = t == MEMORY ? stock_bitmap : NULL;
  hold_state(s);
  s.text_color = 0x000000;
  s.bk_color = 0xFFFFFF;
  s.bk_mode = OPAQUE;
  s.text_align = 0;
  s.pos.x = s.pos.y = 0;
}

static DC* dc_of(word hdc) { return lookup_as<DC>(hdc, K_DC); }

// Where dc draws (gui_lock held). false if it draws nothing.
static bool target_of(DC* dc, DrawTarget& t) {
  t.top = NULL;
  t.clip.clear();
  switch (dc->type) {
    case DC::MEMORY:
      t.bitmap = dc->s.bitmap;
      t.ox = t.oy = 0;
      t.clip.push_back(make_rect(0, 0, t.bitmap->width, t.bitmap->height));
      return true;
    case DC::SCREEN:
      t.bitmap = scratch_bitmap;
      t.ox = t.oy = 0;
      return false;
    case DC::WINDOW: {
      Window* w = window(dc->hwnd);
      if (w == NULL) return false;
      window_draw_target(w, dc->paint_clip_set ? &dc->paint_clip : NULL, t);
      return t.bitmap != NULL && !t.clip.empty();
    }
    case DC::NONCLIENT: {
      Window* w = window(dc->hwnd);
      if (w == NULL) return false;
      window_nonclient_target(w, t);
      return t.bitmap != NULL && !t.clip.empty();
    }
  }
  return false;
}

static void clip_context(CGContextRef ctx, const DrawTarget& t) {
  std::vector<CGRect> rects;
  for (const RECT& r : t.clip) rects.push_back(CGRectMake(r.left, r.top, r.right - r.left, r.bottom - r.top));
  CGContextClipToRects(ctx, rects.data(), rects.size());
  CGContextTranslateCTM(ctx, t.ox, t.oy);
}

static RECT bounds_of(const DrawTarget& t, const RECT& r_dc) {	// the drawn area in bitmap coordinates
  RECT r = rect_offset(r_dc, t.ox, t.oy);
  RECT u = make_rect(0, 0, 0, 0);
  for (const RECT& c : t.clip) u = rect_union(u, rect_intersection(r, c));
  return u;
}

static void drawn(const DrawTarget& t, const RECT& r_dc) {
  if (t.top != NULL) surface_changed(t.top, bounds_of(t, r_dc));
}

// A drawing operation on a DC: resolves the target, clips, calls draw(ctx), shows the result
template <class F> static bool draw_on(word hdc, const RECT& bounds_dc, F draw) {
  GUILocker l;
  DC* dc = dc_of(hdc);
  if (dc == NULL) return false;
  DrawTarget t;
  if (!target_of(dc, t)) return true;
  CGContextRef ctx = t.bitmap->ctx;
  CGContextSaveGState(ctx);
  clip_context(ctx, t);
  draw(ctx, dc);
  CGContextRestoreGState(ctx);
  drawn(t, bounds_dc);
  return true;
}

// Pixel operations: f(uint32_t& pixel) for the device pixels of r (DC coordinates), clipped
template <class F> static void for_pixels(const DrawTarget& t, const RECT& r_dc, F f) {
  RECT r = rect_offset(r_dc, t.ox, t.oy);
  int S = gdi_scale;
  for (const RECT& c : t.clip) {
    RECT a = rect_intersection(r, c);
    a = rect_intersection(a, make_rect(0, 0, t.bitmap->width, t.bitmap->height));
    if (rect_empty(a)) continue;
    for (int y = a.top * S; y < a.bottom * S; y++) {
      uint32_t* row = t.bitmap->pixels + y * t.bitmap->row_words;
      for (int x = a.left * S; x < a.right * S; x++) f(row[x], x / S, y / S);
    }
  }
}

word gdi_window_dc(Window* w) {
  GUILocker l;
  if (w->dc == 0) w->dc = (new DC(DC::WINDOW, w->handle))->handle;
  return w->dc;
}

void gdi_dc_set_paint_clip(word hdc, const RECT* clip) {
  GUILocker l;
  DC* dc = dc_of(hdc);
  if (dc == NULL) return;
  dc->paint_clip_set = clip != NULL;
  if (clip) dc->paint_clip = *clip;
}

static std::map<HANDLE, DC*> nonclient_dcs;

word gdi_window_nonclient_dc(Window* w) {
  GUILocker l;
  DC*& dc = nonclient_dcs[w->handle];
  if (dc == NULL) dc = new DC(DC::NONCLIENT, w->handle);
  return dc->handle;
}

void gdi_destroy_window_dc(Window* w) {
  GUILocker l;
  if (w->dc) { delete dc_of(w->dc); w->dc = 0; }
  auto i = nonclient_dcs.find(w->handle);
  if (i != nonclient_dcs.end()) { delete i->second; nonclient_dcs.erase(i); }
}

word gdi_screen_dc() {
  static DC* dc = NULL;
  GUILocker l;
  if (dc == NULL) { dc = new DC(DC::SCREEN, 0); dc->stock = true; }
  return dc->handle;
}

static word CreateDCA(word driver, word device, word output, word devmode) {
  return (new DC(DC::SCREEN, 0))->handle;
}

static word CreateCompatibleDC(word hdc) {
  return (new DC(DC::MEMORY, 0))->handle;
}

static word DeleteDC(word hdc) {
  GUILocker l;
  DC* dc = dc_of(hdc);
  if (dc == NULL || dc->stock || dc->type == DC::WINDOW || dc->type == DC::NONCLIENT) return dc != NULL;
  delete dc;
  return 1;
}

static word SaveDC(word hdc) {
  GUILocker l;
  DC* dc = dc_of(hdc);
  if (dc == NULL) return 0;
  dc->saved.push_back(dc->s);
  hold_state(dc->s);
  return (word) dc->saved.size();
}

static word RestoreDC(word hdc, word level) {
  GUILocker l;
  DC* dc = dc_of(hdc);
  if (dc == NULL) return 0;
  int n = (int) dc->saved.size();
  int target = (int32_t) level < 0 ? n + (int32_t) level : (int32_t) level - 1;	// index of the state to restore
  if (target < 0 || target >= n) return 0;
  let_go_state(dc->s);		// the restored state's hold moves from saved[target] to s
  dc->s = dc->saved[target];
  for (int i = target + 1; i < n; i++) let_go_state(dc->saved[i]);
  dc->saved.resize(target);
  return 1;
}

// ---------------------------------------------------------------------------
// Objects

static word CreatePen(word style, word width, word color) {
  return (new Pen((int) style & 0xF, std::max(1, (int) width), (COLORREF) color))->handle;
}

static word CreateSolidBrush(word color) {
  return (new Brush(BS_SOLID, (COLORREF) color))->handle;
}

static word CreateFontIndirectA(word logfont) {
  if (logfont == 0) return 0;
  GUILocker l;
  return (new Font((const void*) logfont))->handle;
}

static word CreateCompatibleBitmap(word hdc, word w, word h) {
  return (new Bitmap(std::max(1, (int) w), std::max(1, (int) h), 32))->handle;
}

static word CreatePalette(word logpalette) { return (new Palette())->handle; }

static word GetStockObject(word index) {
  switch ((int) index) {
    case 0: case 1: case 2: case 3: case 4: case 5: return stock_brushes[index]->handle;
    case 6: case 7: case 8:                         return stock_pens[index - 6]->handle;
    case 10: case 11: case 12: case 13: case 14: case 16: case 17: return stock_fonts[index - 10]->handle;
    case 15: return stock_palette->handle;
  }
  return 0;
}

static word DeleteObject(word h) {
  GUILocker l;
  Obj* o = lookup(h, K_ANY);
  if (o == NULL || o->deleted) return 0;
  if (o->stock) return 1;
  switch (o->kind) {
    case K_PEN: case K_BRUSH: case K_FONT: case K_PALETTE:
      if (o->selected > 0) { o->deleted = true; return 1; }	// freed when deselected (lookup fails meanwhile)
      delete o;
      return 1;
    case K_BITMAP:
      if (o->selected > 0) return 0;
      delete o;
      return 1;
    default:
      return 0;
  }
}

static word SelectObject(word hdc, word h) {
  GUILocker l;
  DC* dc = dc_of(hdc);
  Obj* o = lookup(h, K_ANY);
  if (dc == NULL || o == NULL) return 0;
  Obj** slot;
  switch (o->kind) {
    case K_PEN:   slot = (Obj**) &dc->s.pen;   break;
    case K_BRUSH: slot = (Obj**) &dc->s.brush; break;
    case K_FONT:  slot = (Obj**) &dc->s.font;  break;
    case K_BITMAP:
      if (dc->type != DC::MEMORY) return 0;
      slot = (Obj**) &dc->s.bitmap;
      break;
    default: return 0;
  }
  Obj* previous = *slot;
  word old = previous->handle;
  hold(o);
  *slot = o;
  let_go(previous);		// frees it if it was deleted while selected
  return old;
}

static word SelectPalette(word hdc, word palette, word background) {
  GUILocker l;
  DC* dc = dc_of(hdc);
  Palette* p = lookup_as<Palette>(palette, K_PALETTE);
  if (dc == NULL || p == NULL) return 0;
  word old = dc->palette ? dc->palette->handle : 0;
  dc->palette = p;
  return old;
}

static word RealizePalette(word hdc)  { return 0; }
static word UnrealizeObject(word h)   { return 1; }
static word GdiFlush()                { return 1; }

static word GetObjectA(word h, word size, word buffer) {
  GUILocker l;
  Obj* o = lookup(h, K_ANY);
  if (o == NULL) return 0;
  uint8_t data[64];
  int n = 0;
  memset(data, 0, sizeof data);
  switch (o->kind) {
    case K_BITMAP: {		// BITMAP: bmType, bmWidth, bmHeight, bmWidthBytes, bmPlanes, bmBitsPixel, bmBits
      Bitmap* b = (Bitmap*) o;
      int32_t* f = (int32_t*) data;
      f[1] = b->width; f[2] = b->height;
      f[3] = ((b->width * b->bits_pixel + 15) / 16) * 2;
      ((uint16_t*) data)[8] = 1;
      ((uint16_t*) data)[9] = (uint16_t) b->bits_pixel;
      n = 24;
      break;
    }
    case K_FONT: {
      auto i = font_descriptions.find((Font*) o);
      if (i == font_descriptions.end()) return 0;
      memcpy(data, &i->second, sizeof(LOGFONTA));
      n = sizeof(LOGFONTA);
      break;
    }
    case K_PEN: {		// LOGPEN: style, POINT width, color
      Pen* p = (Pen*) o;
      int32_t* f = (int32_t*) data;
      f[0] = p->style; f[1] = p->width; f[3] = (int32_t) p->color;
      n = 16;
      break;
    }
    case K_BRUSH: {		// LOGBRUSH: style, color, hatch
      Brush* b = (Brush*) o;
      int32_t* f = (int32_t*) data;
      f[0] = b->style; f[1] = (int32_t) b->color;
      n = 12;
      break;
    }
    default:
      return 0;
  }
  if (buffer == 0) return n;
  n = std::min(n, (int) size);
  memcpy((void*) buffer, data, n);
  return n;
}

static word GetDeviceCaps(word hdc, word index) {
  extern int win32_screen_width();
  extern int win32_screen_height();
  switch ((int) index) {
    case 2:   return 1;				// TECHNOLOGY: DT_RASDISPLAY
    case 4:   return win32_screen_width() * 254 / 960;	// HORZSIZE (mm)
    case 6:   return win32_screen_height() * 254 / 960;	// VERTSIZE
    case 8:   return win32_screen_width();	// HORZRES
    case 10:  return win32_screen_height();	// VERTRES
    case 12:  return 32;			// BITSPIXEL
    case 14:  return 1;				// PLANES
    case 24:  return -1;			// NUMCOLORS
    case 38:  return 0x1 | 0x8 | 0x80 | 0x200 | 0x800;	// RASTERCAPS (no RC_PALETTE)
    case 88:  case 90: return 96;		// LOGPIXELSX, LOGPIXELSY
    case 104: return 0;				// SIZEPALETTE
    case 106: return 0;				// NUMRESERVED
    case 108: return 24;			// COLORRES
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Attributes

static word SetBkMode(word hdc, word mode) {
  GUILocker l;
  DC* dc = dc_of(hdc);
  if (dc == NULL) return 0;
  int old = dc->s.bk_mode;
  dc->s.bk_mode = (int) mode;
  return old;
}

static word SetTextAlign(word hdc, word align) {
  GUILocker l;
  DC* dc = dc_of(hdc);
  if (dc == NULL) return (word) (int32_t) 0xFFFFFFFF;	// GDI_ERROR
  uint32_t old = dc->s.text_align;
  dc->s.text_align = (uint32_t) align;
  return old;
}

static word SetTextColor(word hdc, word color) {
  GUILocker l;
  DC* dc = dc_of(hdc);
  if (dc == NULL) return (word) (int32_t) 0xFFFFFFFF;	// CLR_INVALID
  COLORREF old = dc->s.text_color;
  dc->s.text_color = (COLORREF) color;
  return old;
}

// ---------------------------------------------------------------------------
// Text

// TEXTMETRICA (32-bit)
static word GetTextMetricsA(word hdc, word tm) {
  GUILocker l;
  DC* dc = dc_of(hdc);
  if (dc == NULL) return 0;
  Font* f = dc->s.font;
  uint8_t* p = (uint8_t*) tm;
  memset(p, 0, 56);
  int32_t* i = (int32_t*) p;
  i[0] = f->height; i[1] = f->ascent; i[2] = f->descent; i[3] = f->internal_leading; i[4] = f->external_leading;
  i[5] = f->ave_width; i[6] = f->max_width; i[7] = f->weight; i[8] = 0; i[9] = 96; i[10] = 96;
  p[44] = 32; p[45] = 255; p[46] = '?'; p[47] = ' ';
  p[48] = f->italic; p[49] = f->underline; p[50] = f->strikeout;
  p[51] = (uint8_t) ((f->fixed_pitch ? 0 : 1) | 2 | 4 | (f->pitch_and_family & 0xF0));	// TMPF_FIXED_PITCH means variable
  p[52] = (uint8_t) f->char_set;
  return 1;
}

static word GetTextExtentPoint32A(word hdc, word str, word count, word size) {
  GUILocker l;
  DC* dc = dc_of(hdc);
  if (dc == NULL) return 0;
  int32_t* s = (int32_t*) size;
  s[0] = gdi_text_width(dc->s.font, (const char*) str, (int) count);
  s[1] = dc->s.font->height;
  return 1;
}

static word GetCharWidth32A(word hdc, word first, word last, word buffer) {
  GUILocker l;
  DC* dc = dc_of(hdc);
  if (dc == NULL || last < first) return 0;
  int32_t* b = (int32_t*) buffer;
  for (word c = first; c <= last; c++) b[c - first] = (int32_t) dc->s.font->advance[c & 0xFF];
  return 1;
}

static word GetCharWidthFloatA(word hdc, word first, word last, word buffer) {
  GUILocker l;
  DC* dc = dc_of(hdc);
  if (dc == NULL || last < first) return 0;
  float* b = (float*) buffer;
  for (word c = first; c <= last; c++) b[c - first] = dc->s.font->advance[c & 0xFF];
  return 1;
}

// draws s at x with baseline y (DC coordinates) in ctx (clipped, translated)
static void draw_glyphs(CGContextRef ctx, Font* f, double x, double baseline, const char* s, int n, COLORREF color) {
  if (n <= 0) return;
  std::vector<CGGlyph> glyphs(n);
  std::vector<CGPoint> positions(n);
  double cx = x;
  for (int i = 0; i < n; i++) {
    unsigned char c = (unsigned char) s[i];
    glyphs[i] = f->glyph[c];
    positions[i] = CGPointMake(cx, 0);
    cx += f->advance[c];
  }
  CGContextSaveGState(ctx);
  set_fill(ctx, color);
  CGContextSetShouldAntialias(ctx, true);
  CGContextTranslateCTM(ctx, 0, baseline);
  CGContextScaleCTM(ctx, 1, -1);
  CGContextSetTextMatrix(ctx, CGAffineTransformIdentity);
  CTFontDrawGlyphs(f->ct, glyphs.data(), positions.data(), n, ctx);
  CGContextRestoreGState(ctx);
  if (f->underline || f->strikeout) {
    set_fill(ctx, color);
    int w = (int) (cx - x);
    if (f->underline) CGContextFillRect(ctx, CGRectMake(x, baseline + 1, w, 1));
    if (f->strikeout) CGContextFillRect(ctx, CGRectMake(x, baseline - f->ascent / 3, w, 1));
  }
}

static word TextOutA(word hdc, word x, word y, word str, word count) {
  int n = (int) count;
  const char* s = (const char*) str;
  RECT bounds;
  int left, top, baseline;
  {
    GUILocker l;
    DC* dc = dc_of(hdc);
    if (dc == NULL) return 0;
    Font* f = dc->s.font;
    int px = (int32_t) x, py = (int32_t) y;
    if (dc->s.text_align & TA_UPDATECP) { px = dc->s.pos.x; py = dc->s.pos.y; }
    int width = gdi_text_width(f, s, n);
    uint32_t h = dc->s.text_align & 6, v = dc->s.text_align & 24;
    left = h == TA_CENTER ? px - width / 2 : (h == TA_RIGHT ? px - width : px);
    top  = v == TA_BASELINE ? py - f->ascent : (v == TA_BOTTOM ? py - f->height : py);
    baseline = top + f->ascent;
    bounds = make_rect(left, top, left + width, top + f->height);
    if (dc->s.text_align & TA_UPDATECP) dc->s.pos.x = h == TA_RIGHT ? left : left + width;
  }
  return draw_on(hdc, bounds, [&](CGContextRef ctx, DC* dc) {
    if (dc->s.bk_mode == OPAQUE) {
      set_fill(ctx, dc->s.bk_color);
      CGContextFillRect(ctx, CGRectMake(bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top));
    }
    draw_glyphs(ctx, dc->s.font, left, baseline, s, n, dc->s.text_color);
  });
}

void gdi_draw_text(word hdc, int x, int y, const char* s, int n, COLORREF color, Font* font) {
  RECT bounds = make_rect(x, y, x + gdi_text_width(font, s, n), y + font->height);
  draw_on(hdc, bounds, [&](CGContextRef ctx, DC* dc) {
    draw_glyphs(ctx, font, x, y + font->ascent, s, n, color);
  });
}

// ---------------------------------------------------------------------------
// Shapes

static void fill(CGContextRef ctx, int l, int t, int r, int b, COLORREF c) {
  if (r <= l || b <= t) return;
  set_fill(ctx, c);
  CGContextFillRect(ctx, CGRectMake(l, t, r - l, b - t));
}

// a line of the pen from (x0, y0) to (x1, y1), without the last point (as GDI)
static void pen_line(CGContextRef ctx, Pen* pen, int x0, int y0, int x1, int y1) {
  if (pen->style == PS_NULL) return;
  int w = pen->width;
  int o = (w - 1) / 2;
  set_fill(ctx, pen->color);
  bool dotted = pen->style == PS_DOT || pen->style == PS_DASH || pen->style == PS_DASHDOT || pen->style == PS_DASHDOTDOT;
  if (!dotted && (y0 == y1 || x0 == x1)) {	// the common cases: one rectangle
    if (y0 == y1) {
      int a = std::min(x0, x1 + (x1 < x0 ? 1 : 0)), b = std::max(x0 + 1, x1 + (x1 < x0 ? 1 : 0));
      if (x1 > x0) { a = x0; b = x1; } else if (x1 < x0) { a = x1 + 1; b = x0 + 1; } else return;
      CGContextFillRect(ctx, CGRectMake(a, y0 - o, b - a, w));
    } else {
      int a, b;
      if (y1 > y0) { a = y0; b = y1; } else if (y1 < y0) { a = y1 + 1; b = y0 + 1; } else return;
      CGContextFillRect(ctx, CGRectMake(x0 - o, a, w, b - a));
    }
    return;
  }
  // Bresenham
  int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int err = dx + dy, x = x0, y = y0, i = 0;
  int period = pen->style == PS_DOT ? 2 : 6;
  while (x != x1 || y != y1) {
    if (!dotted || (i % period) < period / 2) CGContextFillRect(ctx, CGRectMake(x - o, y - o, w, w));
    i++;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x += sx; }
    if (e2 <= dx) { err += dx; y += sy; }
  }
}

static word MoveToEx(word hdc, word x, word y, word old) {
  GUILocker l;
  DC* dc = dc_of(hdc);
  if (dc == NULL) return 0;
  if (old) { POINT* p = (POINT*) old; *p = dc->s.pos; }
  dc->s.pos.x = (int32_t) x;
  dc->s.pos.y = (int32_t) y;
  return 1;
}

static word LineTo(word hdc, word x, word y) {
  POINT from;
  int w;
  {
    GUILocker l;
    DC* dc = dc_of(hdc);
    if (dc == NULL) return 0;
    from = dc->s.pos;
    w = dc->s.pen->width;
    dc->s.pos.x = (int32_t) x;
    dc->s.pos.y = (int32_t) y;
  }
  int x1 = (int32_t) x, y1 = (int32_t) y;
  RECT bounds = make_rect(std::min(from.x, x1) - w, std::min(from.y, y1) - w, std::max(from.x, x1) + w + 1, std::max(from.y, y1) + w + 1);
  return draw_on(hdc, bounds, [&](CGContextRef ctx, DC* dc) { pen_line(ctx, dc->s.pen, from.x, from.y, x1, y1); });
}

static word Rectangle(word hdc, word l, word t, word r, word b) {
  int L = (int32_t) l, T = (int32_t) t, R = (int32_t) r, B = (int32_t) b;
  if (R < L) std::swap(L, R);
  if (B < T) std::swap(T, B);
  return draw_on(hdc, make_rect(L, T, R, B), [&](CGContextRef ctx, DC* dc) {
    Pen* pen = dc->s.pen;
    Brush* brush = dc->s.brush;
    if (pen->style == PS_NULL) {	// one pixel smaller (as GDI)
      if (brush->style != BS_NULL) fill(ctx, L, T, R - 1, B - 1, brush->color);
      return;
    }
    int w = pen->width;
    if (brush->style != BS_NULL) fill(ctx, L + w, T + w, R - w, B - w, brush->color);
    if (pen->style == PS_SOLID || pen->style == PS_INSIDEFRAME) {
      fill(ctx, L, T, R, T + w, pen->color);
      fill(ctx, L, B - w, R, B, pen->color);
      fill(ctx, L, T + w, L + w, B - w, pen->color);
      fill(ctx, R - w, T + w, R, B - w, pen->color);
    } else {
      pen_line(ctx, pen, L, T, R - 1, T);
      pen_line(ctx, pen, R - 1, T, R - 1, B - 1);
      pen_line(ctx, pen, R - 1, B - 1, L, B - 1);
      pen_line(ctx, pen, L, B - 1, L, T);
    }
  });
}

static COLORREF brush_color(word hbrush, bool& ok) {
  ok = true;
  if (hbrush > 0 && hbrush <= 31) return sys_color((int) hbrush - 1);	// COLOR_x + 1
  Brush* b = lookup_as<Brush>(hbrush, K_BRUSH);
  if (b == NULL || b->style == BS_NULL) { ok = false; return 0; }
  return b->color;
}

word FillRect_user(word hdc, word rect, word hbrush) {
  RECT r;
  memcpy(&r, (void*) rect, sizeof r);
  // GDI fills inverted rectangles too (as PatBlt with negative extents): the image's
  // ValueBar has its bars grow up from the baseline (bottom < top)
  if (r.right < r.left) std::swap(r.left, r.right);
  if (r.bottom < r.top) std::swap(r.top, r.bottom);
  bool ok;
  COLORREF c;
  { GUILocker l; c = brush_color(hbrush, ok); }
  if (!ok) return 0;
  return draw_on(hdc, r, [&](CGContextRef ctx, DC* dc) { fill(ctx, r.left, r.top, r.right, r.bottom, c); });
}

void gdi_fill_rect(word hdc, const RECT& r, COLORREF color) {
  draw_on(hdc, r, [&](CGContextRef ctx, DC* dc) { fill(ctx, r.left, r.top, r.right, r.bottom, color); });
}

void gdi_frame_rect(word hdc, const RECT& r, COLORREF color) {
  draw_on(hdc, r, [&](CGContextRef ctx, DC* dc) {
    fill(ctx, r.left, r.top, r.right, r.top + 1, color);
    fill(ctx, r.left, r.bottom - 1, r.right, r.bottom, color);
    fill(ctx, r.left, r.top + 1, r.left + 1, r.bottom - 1, color);
    fill(ctx, r.right - 1, r.top + 1, r.right, r.bottom - 1, color);
  });
}

static word SetPixelV(word hdc, word x, word y, word color) {
  int X = (int32_t) x, Y = (int32_t) y;
  return draw_on(hdc, make_rect(X, Y, X + 1, Y + 1), [&](CGContextRef ctx, DC* dc) { fill(ctx, X, Y, X + 1, Y + 1, (COLORREF) color); });
}

// XOR with a dotted pattern (every other pixel)
word DrawFocusRect_user(word hdc, word rect) {
  RECT r;
  memcpy(&r, (void*) rect, sizeof r);
  GUILocker l;
  DC* dc = dc_of(hdc);
  if (dc == NULL) return 0;
  DrawTarget t;
  if (!target_of(dc, t)) return 1;
  auto dots = [&](const RECT& edge) {
    for_pixels(t, edge, [](uint32_t& p, int x, int y) { if (((x + y) & 1) == 0) p ^= 0x00FFFFFF; });
  };
  dots(make_rect(r.left, r.top, r.right, r.top + 1));
  if (r.bottom - 1 > r.top) dots(make_rect(r.left, r.bottom - 1, r.right, r.bottom));
  dots(make_rect(r.left, r.top + 1, r.left + 1, r.bottom - 1));
  if (r.right - 1 > r.left) dots(make_rect(r.right - 1, r.top + 1, r.right, r.bottom - 1));
  drawn(t, r);
  return 1;
}

// ---------------------------------------------------------------------------
// BitBlt

enum { SRCCOPY = 0xCC0020, SRCPAINT = 0xEE0086, SRCAND = 0x8800C6, SRCINVERT = 0x660046, SRCERASE = 0x440328,
       NOTSRCCOPY = 0x330008, NOTSRCERASE = 0x1100A6, MERGECOPY = 0xC000CA, MERGEPAINT = 0xBB0226,
       PATCOPY = 0xF00021, PATPAINT = 0xFB0A09, PATINVERT = 0x5A0049, DSTINVERT = 0x550009,
       BLACKNESS = 0x000042, WHITENESS = 0xFF0062 };

// the result of the ternary raster operation rop3 (bits 16..23 of rop) for pattern, source, destination
static inline uint32_t rop3(uint32_t rop, uint32_t P, uint32_t S, uint32_t D) {
  switch (rop) {
    case SRCCOPY:     return S;
    case SRCPAINT:    return S | D;
    case SRCAND:      return S & D;
    case SRCINVERT:   return S ^ D;
    case SRCERASE:    return S & ~D;
    case NOTSRCCOPY:  return ~S;
    case NOTSRCERASE: return ~(S | D);
    case MERGECOPY:   return P & S;
    case MERGEPAINT:  return ~S | D;
    case PATCOPY:     return P;
    case PATPAINT:    return P | ~S | D;
    case PATINVERT:   return P ^ D;
    case DSTINVERT:   return ~D;
    case BLACKNESS:   return 0;
    case WHITENESS:   return 0xFFFFFFFF;
  }
  // the general case, bit by bit of the operation code
  uint32_t code = (rop >> 16) & 0xFF, r = 0;
  for (int bit = 0; bit < 24; bit++) {
    int index = (((P >> bit) & 1) << 2) | (((S >> bit) & 1) << 1) | ((D >> bit) & 1);
    r |= ((code >> index) & 1) << bit;
  }
  return r;
}

static bool uses_source(uint32_t rop) {
  uint32_t code = (rop >> 16) & 0xFF;
  return ((code >> 2) & 0x33) != (code & 0x33);
}

static word BitBlt(word hdc, word x, word y, word w, word h, word hsrc, word sx, word sy, word rop) {
  GUILocker l;
  DC* dst = dc_of(hdc);
  if (dst == NULL) return 0;
  DrawTarget dt;
  if (!target_of(dst, dt)) return 1;
  int X = (int32_t) x, Y = (int32_t) y, W = (int32_t) w, H = (int32_t) h, SX = (int32_t) sx, SY = (int32_t) sy;
  uint32_t op = (uint32_t) rop;
  DrawTarget st;
  bool has_source = uses_source(op);
  if (has_source) {
    DC* src = dc_of(hsrc);
    if (src == NULL) return 0;
    target_of(src, st);
    if (st.bitmap == NULL) return 1;
  }
  uint32_t P = 0;
  bool ok;
  COLORREF pc = brush_color(dst->s.brush->handle, ok);
  P = ok ? pixel_of(pc) : 0;
  int S = gdi_scale;
  RECT r = make_rect(X, Y, X + W, Y + H);
  // the source pixels first (the source may be the destination: scrolling)
  std::vector<uint32_t> source;
  int sw = W * S, sh = H * S;
  if (has_source && sw > 0 && sh > 0) {
    source.assign((size_t) sw * sh, 0xFFFFFFFF);
    Bitmap* sb = st.bitmap;
    int bx = (SX + st.ox) * S, by = (SY + st.oy) * S;
    for (int row = 0; row < sh; row++) {
      int yy = by + row;
      if (yy < 0 || yy >= sb->height * S) continue;
      for (int col = 0; col < sw; col++) {
        int xx = bx + col;
        if (xx < 0 || xx >= sb->width * S) continue;
        source[(size_t) row * sw + col] = sb->pixels[yy * sb->row_words + xx];
      }
    }
  }
  int ox = (X + dt.ox) * S, oy = (Y + dt.oy) * S;
  RECT dr = rect_offset(r, dt.ox, dt.oy);
  for (const RECT& c : dt.clip) {
    RECT a = rect_intersection(rect_intersection(dr, c), make_rect(0, 0, dt.bitmap->width, dt.bitmap->height));
    if (rect_empty(a)) continue;
    for (int yy = a.top * S; yy < a.bottom * S; yy++) {
      uint32_t* row = dt.bitmap->pixels + yy * dt.bitmap->row_words;
      const uint32_t* srow = has_source ? source.data() + (size_t) (yy - oy) * sw - ox : NULL;
      for (int xx = a.left * S; xx < a.right * S; xx++) {
        uint32_t s = srow ? srow[xx] : 0;
        row[xx] = (rop3(op, P, s, row[xx]) & 0x00FFFFFF) | 0xFF000000;
      }
    }
  }
  drawn(dt, r);
  return 1;
}

void gdi_draw_bitmap(word hdc, Bitmap* bm, int x, int y) {
  word mem;
  {
    GUILocker l;
    DC* dc = new DC(DC::MEMORY, 0);
    stock_bitmap->selected--;
    dc->s.bitmap = bm;
    bm->selected++;
    mem = dc->handle;
  }
  BitBlt(hdc, x, y, bm->width, bm->height, mem, 0, 0, SRCCOPY);
  GUILocker l;
  DC* dc = dc_of(mem);
  bm->selected--;
  dc->s.bitmap = NULL;
  delete dc;
}

// ---------------------------------------------------------------------------
// BMP files

static uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24); }
static uint16_t le16(const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }

Bitmap* gdi_load_bmp(const char* path) {
  FILE* f = fopen(path, "rb");
  if (f == NULL) return NULL;
  std::vector<uint8_t> d;
  uint8_t buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
  fclose(f);
  if (d.size() < 54 || d[0] != 'B' || d[1] != 'M') return NULL;
  uint32_t offset = le32(&d[10]), header = le32(&d[14]);
  int32_t w = (int32_t) le32(&d[18]), h = (int32_t) le32(&d[22]);
  int bpp = le16(&d[28]);
  uint32_t compression = le32(&d[30]);
  uint32_t colors = header >= 40 ? le32(&d[46]) : 0;
  if (compression != 0 || w <= 0 || h == 0 || (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32)) return NULL;
  bool top_down = h < 0;
  h = std::abs(h);
  if (colors == 0 && bpp <= 8) colors = 1u << bpp;
  const uint8_t* palette = &d[14 + header];
  size_t stride = ((size_t) w * bpp + 31) / 32 * 4;
  if (offset + stride * h > d.size()) return NULL;
  GUILocker l;
  Bitmap* b = new Bitmap(w, h, 32);
  int S = gdi_scale;
  for (int y = 0; y < h; y++) {
    const uint8_t* row = &d[offset + stride * (top_down ? y : h - 1 - y)];
    for (int x = 0; x < w; x++) {
      uint32_t p;
      if (bpp <= 8) {
        int index;
        if (bpp == 8)      index = row[x];
        else if (bpp == 4) index = (row[x / 2] >> ((x & 1) ? 0 : 4)) & 0xF;
        else               index = (row[x / 8] >> (7 - (x & 7))) & 1;
        const uint8_t* q = palette + 4 * std::min<uint32_t>(index, colors - 1);
        p = (q[2] << 16) | (q[1] << 8) | q[0];
      } else {
        const uint8_t* q = row + x * (bpp / 8);
        p = (q[2] << 16) | (q[1] << 8) | q[0];
      }
      for (int dy = 0; dy < S; dy++)
        for (int dx = 0; dx < S; dx++) b->pixels[(y * S + dy) * b->row_words + x * S + dx] = p | 0xFF000000;
    }
  }
  return b;
}

// ---------------------------------------------------------------------------
// Initialization

void gdi_initialize(int scale) {
  device_rgb = CGColorSpaceCreateDeviceRGB();
  stock_brushes[0] = new Brush(BS_SOLID, 0xFFFFFF);
  stock_brushes[1] = new Brush(BS_SOLID, 0xC0C0C0);
  stock_brushes[2] = new Brush(BS_SOLID, 0x808080);
  stock_brushes[3] = new Brush(BS_SOLID, 0x404040);
  stock_brushes[4] = new Brush(BS_SOLID, 0x000000);
  stock_brushes[5] = new Brush(BS_NULL, 0);
  stock_pens[0] = new Pen(PS_SOLID, 1, 0xFFFFFF);
  stock_pens[1] = new Pen(PS_SOLID, 1, 0x000000);
  stock_pens[2] = new Pen(PS_NULL, 1, 0);
  for (Brush* b : stock_brushes) b->stock = true;
  for (Pen* p : stock_pens) p->stock = true;
  Font* fixed = make_font("Menlo", -12, 400, 0x31);	// FIXED_PITCH | FF_MODERN
  Font* var   = make_font("MS Sans Serif", -12, 400, 0x22);
  stock_fonts[0] = fixed;					// OEM_FIXED_FONT
  stock_fonts[1] = fixed;					// ANSI_FIXED_FONT
  stock_fonts[2] = var;						// ANSI_VAR_FONT
  stock_fonts[3] = make_font("System", -13, 700, 0x22);	// SYSTEM_FONT
  stock_fonts[4] = var;						// DEVICE_DEFAULT_FONT
  stock_fonts[5] = var;
  stock_fonts[6] = fixed;					// SYSTEM_FIXED_FONT
  stock_fonts[7] = default_gui_font();				// DEFAULT_GUI_FONT
  stock_bitmap = new Bitmap(1, 1, 1);
  stock_bitmap->stock = true;
  stock_palette = new Palette();
  stock_palette->stock = true;
  scratch_bitmap = new Bitmap(1, 1, 32);
  scratch_bitmap->stock = true;
}

} // namespace w32

#define SHIM(f) { #f, (dll_func) &w32::f }
const ShimFunction win32_gdi32_functions[] = {
  SHIM(BitBlt), SHIM(CreateCompatibleBitmap), SHIM(CreateCompatibleDC), SHIM(CreateDCA), SHIM(CreateFontIndirectA),
  SHIM(CreatePalette), SHIM(CreatePen), SHIM(CreateSolidBrush), SHIM(DeleteDC), SHIM(DeleteObject), SHIM(GdiFlush),
  SHIM(GetCharWidth32A), SHIM(GetCharWidthFloatA), SHIM(GetDeviceCaps), SHIM(GetObjectA), SHIM(GetStockObject),
  SHIM(GetTextExtentPoint32A), SHIM(GetTextMetricsA), SHIM(LineTo), SHIM(MoveToEx), SHIM(RealizePalette),
  SHIM(Rectangle), SHIM(RestoreDC), SHIM(SaveDC), SHIM(SelectObject), SHIM(SelectPalette), SHIM(SetBkMode),
  SHIM(SetPixelV), SHIM(SetTextAlign), SHIM(SetTextColor), SHIM(TextOutA), SHIM(UnrealizeObject),
  { NULL, NULL }
};
#undef SHIM

#endif // __APPLE__
