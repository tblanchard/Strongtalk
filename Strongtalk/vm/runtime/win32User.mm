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

// USER32 (and COMDLG32) emulation on AppKit, see win32GUI.hpp.
//
// Threads: main() runs the VM on a VM thread (win32_gui_main); the main thread starts AppKit
// when the image creates its first window. Every VM thread that creates windows has a
// message queue (as in Win32): AppKit events are posted to the queue of the window's
// thread, GetMessageA (an asynchronous DLL call) waits for them, and window procedures run
// on the window's thread in DispatchMessageA / SendMessageA. When the main thread needs an
// answer from a window procedure (WM_INITMENUPOPUP) it sends the message to the window's
// thread, which handles it while it waits (in GetMessageA, or for the main thread).
// Rules: no window procedure is called with gui_lock held, and VM threads never block on
// the main thread without handling sent messages.

#ifndef _WIN32

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#include <objc/runtime.h>
#include <ImageIO/ImageIO.h>
#include "win32GUI.hpp"
#include "win32Shim.hpp"
#include <mach/mach_time.h>
#include <map>
#include <unordered_map>
#include <functional>

extern "C" uint32_t win32_external_word(void* p);	// proxy_prims.cpp
extern "C" void*    win32_external_pointer(uint32_t w);
extern "C" void     win32_dump_processes(const char* path);	// process.cpp

namespace w32 {

bool TraceGUI = getenv("STRONGTALK_TRACE_GUI") != NULL;
static bool TraceMessages = getenv("STRONGTALK_TRACE_MESSAGES") != NULL;
// GUI test scripts run invisibly (no Dock icon, transparent windows that ignore the mouse), so that
// they do not get in the way of a user; STRONGTALK_GUI_VISIBLE=1 shows them
static bool HiddenGUI = getenv("STRONGTALK_GUI_SCRIPT") != NULL && getenv("STRONGTALK_GUI_VISIBLE") == NULL;

// ---------------------------------------------------------------------------
// Win32 constants

enum {
  WM_NULL = 0x0000, WM_CREATE = 0x0001, WM_DESTROY = 0x0002, WM_MOVE = 0x0003, WM_SIZE = 0x0005,
  WM_ACTIVATE = 0x0006, WM_SETFOCUS = 0x0007, WM_KILLFOCUS = 0x0008, WM_ENABLE = 0x000A,
  WM_SETTEXT = 0x000C, WM_PAINT = 0x000F, WM_CLOSE = 0x0010, WM_QUIT = 0x0012, WM_ERASEBKGND = 0x0014,
  WM_SHOWWINDOW = 0x0018, WM_SETCURSOR = 0x0020, WM_MOUSEACTIVATE = 0x0021, WM_GETMINMAXINFO = 0x0024,
  WM_NCCREATE = 0x0081, WM_NCDESTROY = 0x0082, WM_NCCALCSIZE = 0x0083, WM_NCHITTEST = 0x0084,
  WM_KEYDOWN = 0x0100, WM_KEYUP = 0x0101, WM_CHAR = 0x0102, WM_SYSKEYDOWN = 0x0104, WM_SYSKEYUP = 0x0105,
  WM_SYSCHAR = 0x0106, WM_COMMAND = 0x0111, WM_SYSCOMMAND = 0x0112, WM_TIMER = 0x0113,
  WM_HSCROLL = 0x0114, WM_VSCROLL = 0x0115, WM_INITMENU = 0x0116, WM_INITMENUPOPUP = 0x0117,
  WM_MENUSELECT = 0x011F, WM_MOUSEFIRST = 0x0200, WM_MOUSEMOVE = 0x0200, WM_LBUTTONDOWN = 0x0201,
  WM_LBUTTONUP = 0x0202, WM_LBUTTONDBLCLK = 0x0203, WM_RBUTTONDOWN = 0x0204, WM_RBUTTONUP = 0x0205,
  WM_RBUTTONDBLCLK = 0x0206, WM_MBUTTONDOWN = 0x0207, WM_MBUTTONUP = 0x0208, WM_MBUTTONDBLCLK = 0x0209,
  WM_MOUSEWHEEL = 0x020A, WM_MOUSELAST = 0x020A, WM_CAPTURECHANGED = 0x0215
};

enum {
  WS_OVERLAPPED = 0x00000000, WS_POPUP = 0x80000000, WS_CHILD = 0x40000000, WS_MINIMIZE = 0x20000000,
  WS_VISIBLE = 0x10000000, WS_DISABLED = 0x08000000, WS_CLIPSIBLINGS = 0x04000000,
  WS_CLIPCHILDREN = 0x02000000, WS_MAXIMIZE = 0x01000000, WS_CAPTION = 0x00C00000,
  WS_BORDER = 0x00800000, WS_DLGFRAME = 0x00400000, WS_VSCROLL = 0x00200000, WS_HSCROLL = 0x00100000,
  WS_SYSMENU = 0x00080000, WS_THICKFRAME = 0x00040000, WS_MINIMIZEBOX = 0x00020000, WS_MAXIMIZEBOX = 0x00010000,
  WS_EX_DLGMODALFRAME = 0x00000001, WS_EX_TOPMOST = 0x00000008, WS_EX_TOOLWINDOW = 0x00000080,
  WS_EX_CLIENTEDGE = 0x00000200, WS_EX_STATICEDGE = 0x00020000
};

enum { CS_DBLCLKS = 0x0008 };
enum { MK_LBUTTON = 0x0001, MK_RBUTTON = 0x0002, MK_SHIFT = 0x0004, MK_CONTROL = 0x0008, MK_MBUTTON = 0x0010 };
enum { HTERROR = -2, HTNOWHERE = 0, HTCLIENT = 1, HTHSCROLL = 6, HTVSCROLL = 7, HTBORDER = 18 };
enum { SW_HIDE = 0, SW_SHOWNORMAL = 1, SW_SHOWMINIMIZED = 2, SW_SHOWMAXIMIZED = 3, SW_SHOWNOACTIVATE = 4,
       SW_SHOW = 5, SW_MINIMIZE = 6, SW_SHOWMINNOACTIVE = 7, SW_SHOWNA = 8, SW_RESTORE = 9 };
enum { GWL_WNDPROC = -4, GWL_HINSTANCE = -6, GWL_HWNDPARENT = -8, GWL_STYLE = -16, GWL_EXSTYLE = -20,
       GWL_USERDATA = -21, GWL_ID = -12 };
enum { PM_NOREMOVE = 0, PM_REMOVE = 1 };
enum { VK_LBUTTON = 0x01, VK_RBUTTON = 0x02, VK_MBUTTON = 0x04, VK_BACK = 0x08, VK_TAB = 0x09,
       VK_RETURN = 0x0D, VK_SHIFT = 0x10, VK_CONTROL = 0x11, VK_MENU = 0x12, VK_CAPITAL = 0x14,
       VK_ESCAPE = 0x1B, VK_SPACE = 0x20, VK_PRIOR = 0x21, VK_NEXT = 0x22, VK_END = 0x23, VK_HOME = 0x24,
       VK_LEFT = 0x25, VK_UP = 0x26, VK_RIGHT = 0x27, VK_DOWN = 0x28, VK_INSERT = 0x2D, VK_DELETE = 0x2E,
       VK_F1 = 0x70 };
enum { SB_LINEUP = 0, SB_LINEDOWN = 1 };
enum { SIZE_RESTORED = 0, SIZE_MINIMIZED = 1 };
const word CW_USEDEFAULT = (word) (int32_t) 0x80000000;

// Messages of the emulation (in the range reserved by Win32)
enum { WM_SHIM_SENT = 0x7F00 };

// ---------------------------------------------------------------------------
// Rectangles

RECT rect_union(const RECT& a, const RECT& b) {
  if (rect_empty(a)) return b;
  if (rect_empty(b)) return a;
  return make_rect(std::min(a.left, b.left), std::min(a.top, b.top), std::max(a.right, b.right), std::max(a.bottom, b.bottom));
}

RECT rect_intersection(const RECT& a, const RECT& b) {
  RECT r = make_rect(std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right), std::min(a.bottom, b.bottom));
  if (rect_empty(r)) return make_rect(0, 0, 0, 0);
  return r;
}

void rects_subtract(std::vector<RECT>& rects, const RECT& r) {
  std::vector<RECT> result;
  for (const RECT& a : rects) {
    RECT i = rect_intersection(a, r);
    if (rect_empty(i)) { result.push_back(a); continue; }
    if (a.top < i.top)       result.push_back(make_rect(a.left, a.top, a.right, i.top));
    if (i.bottom < a.bottom) result.push_back(make_rect(a.left, i.bottom, a.right, a.bottom));
    if (a.left < i.left)     result.push_back(make_rect(a.left, i.top, i.left, i.bottom));
    if (i.right < a.right)   result.push_back(make_rect(i.right, i.top, a.right, i.bottom));
  }
  rects.swap(result);
}

void*    external_pointer(uint32_t w) { return win32_external_pointer(w); }
uint32_t external_word(void* p)       { return win32_external_word(p); }

// A word as a 32-bit structure field: as is if it fits, else as an external word
static uint32_t pack_word(word v) {
  if (v == (word) (int32_t) v || v == (word) (uint32_t) v) return (uint32_t) v;
  return external_word((void*) v);
}

static inline int32_t lo16(word v) { return (int16_t) (v & 0xFFFF); }
static inline int32_t hi16(word v) { return (int16_t) ((v >> 16) & 0xFFFF); }

static uint32_t tick_count() {
  static mach_timebase_info_data_t tb;
  if (tb.denom == 0) mach_timebase_info(&tb);
  return (uint32_t) (mach_absolute_time() * tb.numer / tb.denom / 1000000);
}

// ---------------------------------------------------------------------------
// The lock and the handle table

static pthread_mutex_t gui_mutex;
static thread_local int lock_depth = 0;

static void init_gui_mutex() {
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&gui_mutex, &attr);
}
static pthread_once_t gui_mutex_once = PTHREAD_ONCE_INIT;

void gui_lock()   { pthread_once(&gui_mutex_once, init_gui_mutex); pthread_mutex_lock(&gui_mutex); lock_depth++; }
void gui_unlock() { lock_depth--; pthread_mutex_unlock(&gui_mutex); }

// waits with gui_lock held once (pthread_cond_wait releases a recursive mutex only once)
static void gui_wait(pthread_cond_t* cond, int timeout_ms = -1) {
  if (lock_depth != 1) fprintf(stderr, "Strongtalk GUI: waiting with the GUI lock held %d times\n", lock_depth);
  int depth = lock_depth;
  if (timeout_ms < 0) {
    pthread_cond_wait(cond, &gui_mutex);
  } else {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += timeout_ms / 1000;
    ts.tv_nsec += (long) (timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
    pthread_cond_timedwait(cond, &gui_mutex, &ts);
  }
  lock_depth = depth;
}

// Handles are not reused (as Win32, which does not reuse them soon): the image keeps handles of
// destroyed objects for a while (e.g. its HWND -> Window registry), and a recycled one would
// reach the wrong object.
static const HANDLE handle_base  = 0x00010000;	// below the ExternalWords range (0xA0000000)
static const HANDLE handle_limit = 0x9FFF0000;
static std::unordered_map<HANDLE, Obj*> handles;
static HANDLE next_handle = handle_base;

Obj::Obj(ObjKind k) : kind(k), handle(0), stock(false) {
  GUILocker l;
  do {
    handle = next_handle;
    next_handle += 4;
    if (next_handle >= handle_limit) next_handle = handle_base;	// after 670 million objects
  } while (handles.count(handle) != 0);
  handles[handle] = this;
}

Obj::~Obj() {
  GUILocker l;
  auto i = handles.find(handle);
  if (i != handles.end() && i->second == this) handles.erase(i);
}

Obj* lookup(word h, ObjKind kind) {
  GUILocker l;
  if (h < (word) handle_base || h >= (word) handle_limit || (h & 3) != 0) return NULL;
  auto i = handles.find((HANDLE) h);
  if (i == handles.end()) return NULL;
  Obj* o = i->second;
  if (kind != K_ANY && o->kind != kind) return NULL;
  return o;
}

// ---------------------------------------------------------------------------
// Windows-1252

static const uint16_t cp1252_high[32] = {	// 0x80..0x9F
  0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
  0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178
};

static uint32_t cp1252_to_unicode(unsigned char c) { return c >= 0x80 && c < 0xA0 ? cp1252_high[c - 0x80] : c; }

static int unicode_to_cp1252(uint32_t u) {	// -1 if not representable
  if (u < 0x80 || (u >= 0xA0 && u < 0x100)) return (int) u;
  for (int i = 0; i < 32; i++) if (cp1252_high[i] == u && u != 0xFFFD) return 0x80 + i;
  return -1;
}

std::string ansi_to_utf8(const char* s, int n) {
  std::string out;
  if (s == NULL) return out;
  if (n < 0) n = (int) strlen(s);
  for (int i = 0; i < n; i++) {
    uint32_t u = cp1252_to_unicode((unsigned char) s[i]);
    if (u < 0x80)       { out += (char) u; }
    else if (u < 0x800) { out += (char) (0xC0 | (u >> 6)); out += (char) (0x80 | (u & 0x3F)); }
    else                { out += (char) (0xE0 | (u >> 12)); out += (char) (0x80 | ((u >> 6) & 0x3F)); out += (char) (0x80 | (u & 0x3F)); }
  }
  return out;
}

std::string utf8_to_ansi(const char* s) {
  std::string out;
  NSString* str = [NSString stringWithUTF8String: s];
  for (NSUInteger i = 0; i < str.length; i++) {
    int c = unicode_to_cp1252([str characterAtIndex: i]);
    out += (char) (c < 0 ? '?' : c);
  }
  return out;
}

static NSString* ns_string(const char* ansi, int n = -1) {
  return [NSString stringWithUTF8String: ansi_to_utf8(ansi, n).c_str()];
}

// Win32 menu and button texts: '&' marks the mnemonic ("&&" is '&'); a tab separates the accelerator
static NSString* menu_title(const std::string& text) {
  std::string s;
  for (size_t i = 0; i < text.size(); i++) {
    char c = text[i];
    if (c == '\t') break;
    if (c == '&') { if (i + 1 < text.size() && text[i + 1] == '&') { s += '&'; i++; } continue; }
    s += c;
  }
  return ns_string(s.c_str());
}

// ---------------------------------------------------------------------------
// Threads: the VM thread and AppKit

static int    (*vm_main_function)(int, char**);
static int    vm_argc;
static char** vm_argv;

static pthread_mutex_t start_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  start_cond  = PTHREAD_COND_INITIALIZER;
static bool            gui_requested = false;
static volatile bool   gui_ready     = false;

int gdi_scale = 2;
static int screen_width = 1440, screen_height = 900;	// main display, logical pixels

int win32_screen_width()  { return screen_width; }
int win32_screen_height() { return screen_height; }

} // namespace w32
void win32_gui_exit(int code);
namespace w32 {

static void* vm_thread_start(void* arg) {
  win32_gui_exit(vm_main_function(vm_argc, vm_argv));
  return NULL;
}

static void run_app();

// Starts AppKit on the main thread (once); answers when it is running
static void ensure_gui() {
  if (gui_ready) return;
  pthread_mutex_lock(&start_mutex);
  gui_requested = true;
  pthread_cond_broadcast(&start_cond);
  while (!gui_ready) pthread_cond_wait(&start_cond, &start_mutex);
  pthread_mutex_unlock(&start_mutex);
}

static bool on_main_thread() { return pthread_main_np() != 0; }

} // namespace w32

// Ends the process. With AppKit running, exit() would run the static destructors while the
// main thread still uses the GUI state: flush and _exit instead.
void win32_gui_exit(int code) {
  fflush(NULL);
  if (w32::gui_ready) _exit(code);
  exit(code);
}

int win32_gui_main(int argc, char** argv, int (*vm_main)(int argc, char** argv)) {
  using namespace w32;
  vm_main_function = vm_main;
  vm_argc = argc;
  vm_argv = argv;
  // the display scale (without AppKit: command line runs do not start it)
  CGDirectDisplayID display = CGMainDisplayID();
  CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display);
  if (mode != NULL) {
    size_t pw = CGDisplayModeGetPixelWidth(mode), w = CGDisplayModeGetWidth(mode);
    if (w > 0) gdi_scale = std::max(1, (int) ((pw + w / 2) / w));
    screen_width  = (int) w;
    screen_height = (int) CGDisplayModeGetHeight(mode);
    CGDisplayModeRelease(mode);
  }
  if (getenv("STRONGTALK_GUI_SCALE") != NULL) gdi_scale = std::max(1, atoi(getenv("STRONGTALK_GUI_SCALE")));
  gdi_initialize(gdi_scale);
  register_control_classes();

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 64 * 1024 * 1024);	// as large as the main thread's
  pthread_t vm_thread;
  if (pthread_create(&vm_thread, &attr, &vm_thread_start, NULL) != 0) {
    fprintf(stderr, "Strongtalk: cannot create the VM thread\n");
    return 1;
  }
  pthread_mutex_lock(&start_mutex);
  while (!gui_requested) pthread_cond_wait(&start_cond, &start_mutex);
  pthread_mutex_unlock(&start_mutex);
  run_app();
  return 0;
}

namespace w32 {

// ---------------------------------------------------------------------------
// Message queues

struct QMessage {
  word     hwnd, message, wparam, lparam;
  uint32_t time;
  POINT    pt;
  uint32_t modifiers;	// MK_ flags and key state at the time of the event
  std::u16string chars;	// WM_KEYDOWN: the characters it produces (TranslateMessage)
};

struct SentMessage {
  word msg_hwnd, msg, wparam, lparam;
  word result;
  bool done, abandoned;
};

struct Timer {
  word     hwnd, id;
  uint32_t interval, due;
  word     proc;
};

enum { KS_SHIFT = 1, KS_CONTROL = 2, KS_ALT = 4, KS_LBUTTON = 8, KS_RBUTTON = 16, KS_MBUTTON = 32, KS_CAPS = 64 };

struct Queue {
  uint32_t                  thread_id;	// as GetCurrentThreadId (Process>>win32Handle)
  pthread_cond_t            cond;
  std::deque<QMessage>      messages;
  std::deque<SentMessage*>  sent;
  std::vector<Timer>        timers;
  bool                      quit;
  word                      quit_code;
  uint32_t                  key_state;	// KS_ flags as of the last message retrieved
  word                      focus;	// the focus window of the thread (as in Win32)
  bool                      waiting;	// in GetMessageA
  QMessage                  last;	// the last message retrieved
  Queue(uint32_t id) : thread_id(id), quit(false), quit_code(0), key_state(0), focus(0), waiting(false) {
    pthread_cond_init(&cond, NULL);
  }
};

static std::vector<Queue*> queues;
static thread_local Queue* current_queue = NULL;
static pthread_cond_t sent_done_cond = PTHREAD_COND_INITIALIZER;

static uint32_t current_thread_id() {
  uint64_t id;
  pthread_threadid_np(NULL, &id);
  return (uint32_t) id;
}

// the queue of a thread (created when first used, possibly by a PostThreadMessageA to it)
static Queue* queue_of(uint32_t thread_id) {
  GUILocker l;
  for (Queue* q : queues) if (q->thread_id == thread_id) return q;
  Queue* q = new Queue(thread_id);
  queues.push_back(q);
  return q;
}

static Queue* my_queue() {
  if (current_queue == NULL) current_queue = queue_of(current_thread_id());
  return current_queue;
}

static uint32_t current_modifiers = 0;	// KS_ flags, updated by the main thread

static POINT cursor_pos = { 0, 0 };	// screen coordinates

static void enqueue(Queue* q, const QMessage& m, bool front = false) {
  // coalesce mouse moves (as Win32)
  if (!front && m.message == WM_MOUSEMOVE && !q->messages.empty()) {
    QMessage& last = q->messages.back();
    if (last.message == WM_MOUSEMOVE && last.hwnd == m.hwnd) { last = m; return; }
  }
  if (front) q->messages.push_front(m); else q->messages.push_back(m);
  pthread_cond_broadcast(&q->cond);
}

static QMessage make_message(word hwnd, word msg, word wparam, word lparam) {
  QMessage m;
  m.hwnd = hwnd; m.message = msg; m.wparam = wparam; m.lparam = lparam;
  m.time = tick_count();
  m.pt = cursor_pos;
  m.modifiers = current_modifiers;
  return m;
}

bool post_message(word hwnd, word msg, word wparam, word lparam) {
  GUILocker l;
  Window* w = window(hwnd);
  if (w == NULL || w->queue == NULL) return false;
  enqueue(w->queue, make_message(hwnd, msg, wparam, lparam));
  return true;
}

// Handles the messages sent by other threads (called with gui_lock held once)
static void handle_sent_messages(Queue* q) {
  while (!q->sent.empty()) {
    SentMessage* s = q->sent.front();
    q->sent.pop_front();
    gui_unlock();
    word result = send_message(s->msg_hwnd, s->msg, s->wparam, s->lparam);
    gui_lock();
    if (s->abandoned) { delete s; continue; }
    s->result = result;
    s->done = true;
    pthread_cond_broadcast(&sent_done_cond);
  }
}

// From the main thread: sends msg to the thread of hwnd and waits (at most timeout_ms)
static word send_from_main(word hwnd, word msg, word wparam, word lparam, int timeout_ms, bool* ok = NULL) {
  GUILocker l;
  Window* w = window(hwnd);
  if (ok) *ok = false;
  if (w == NULL || w->queue == NULL) return 0;
  SentMessage* s = new SentMessage();
  s->msg_hwnd = hwnd; s->msg = msg; s->wparam = wparam; s->lparam = lparam;
  s->result = 0; s->done = false; s->abandoned = false;
  w->queue->sent.push_back(s);
  pthread_cond_broadcast(&w->queue->cond);
  uint32_t end = tick_count() + timeout_ms;
  while (!s->done && (int32_t) (end - tick_count()) > 0) gui_wait(&sent_done_cond, (int) (end - tick_count()));
  if (!s->done) {
    if (TraceGUI) fprintf(stderr, "Strongtalk GUI: no answer to message 0x%lx for window 0x%lx\n", (long) msg, (long) hwnd);
    s->abandoned = true;
    return 0;
  }
  word result = s->result;
  if (ok) *ok = true;
  delete s;
  return result;
}

// Runs block on the main thread and waits for it, handling sent messages meanwhile
static void run_on_main_sync(void (^block)(void)) {
  if (on_main_thread()) { block(); return; }
  ensure_gui();
  Queue* q = my_queue();
  __block bool done = false;
  dispatch_async(dispatch_get_main_queue(), ^{
    block();
    GUILocker l;
    done = true;
    pthread_cond_broadcast(&q->cond);
  });
  GUILocker l;
  while (!done) {
    handle_sent_messages(q);
    if (!done) gui_wait(&q->cond);
  }
}

static void run_on_main(void (^block)(void)) {
  if (!gui_ready) return;
  dispatch_async(dispatch_get_main_queue(), block);
}

// ---------------------------------------------------------------------------
// Window classes

static std::vector<WndClass*> classes;

static WndClass* find_class(const char* name) {
  GUILocker l;
  for (WndClass* c : classes) if (strcasecmp(c->name.c_str(), name) == 0) return c;
  return NULL;
}

static WndClass* add_class(const char* name, uint32_t style, word wndproc, word cursor, word background) {
  GUILocker l;
  WndClass* c = find_class(name);
  if (c == NULL) { c = new WndClass(); classes.push_back(c); }
  c->name = name; c->style = style; c->wndproc = wndproc; c->cursor = cursor; c->icon = 0; c->background = background;
  return c;
}

// WNDCLASSA (32-bit): style, lpfnWndProc, cbClsExtra, cbWndExtra, hInstance, hIcon, hCursor,
// hbrBackground, lpszMenuName, lpszClassName
static word RegisterClassA(word wndclass) {
  uint32_t* f = (uint32_t*) wndclass;
  const char* name = (const char*) external_pointer(f[9]);
  if (name == NULL) return 0;
  WndClass* c = add_class(name, f[0], (word) external_pointer(f[1]), (word) external_pointer(f[6]), (word) external_pointer(f[7]));
  c->icon = (word) external_pointer(f[5]);
  static uint16_t atom = 0xC000;
  return atom++;
}

// ---------------------------------------------------------------------------
// Windows

static std::vector<Window*> top_levels;
static word capture_hwnd = 0;

Window::Window(WndClass* c) : Obj(K_WINDOW), cls(c), wndproc(c ? c->wndproc : 0), style(0), ex_style(0),
  parent(NULL), id(0), user_data(0), menu(0), visible(false), enabled(true), destroyed(false), erase(false),
  size_sent(false), last_focus(0), queue(NULL), control(NULL), nswindow(NULL), surface(NULL), display_pending(false), dc(0) {
  rect = update = dirty = make_rect(0, 0, 0, 0);
  memset(&vscroll, 0, sizeof vscroll);
  memset(&hscroll, 0, sizeof hscroll);
  vscroll.max = hscroll.max = 100;
  min_track.x = min_track.y = 0;
}

Window::~Window() {}

Window* window(word hwnd) { return lookup_as<Window>(hwnd, K_WINDOW); }

Window* top_level(Window* w) {
  while (w != NULL && w->parent != NULL) w = w->parent;
  return w;
}

static int border_width(Window* w) {
  if (w->parent == NULL) return 0;		// top-level windows: AppKit draws the frame
  if (w->ex_style & WS_EX_CLIENTEDGE) return 2;
  if (w->style & WS_BORDER) return 1;
  return 0;
}

RECT client_rect_in_window(Window* w) {
  int b = border_width(w);
  RECT r = make_rect(b, b, w->rect.right - w->rect.left - b, w->rect.bottom - w->rect.top - b);
  if (w->vscroll.visible) r.right  -= scroll_bar_size;
  if (w->hscroll.visible) r.bottom -= scroll_bar_size;
  if (r.right < r.left) r.right = r.left;
  if (r.bottom < r.top) r.bottom = r.top;
  return r;
}

RECT client_rect(Window* w) {
  RECT c = client_rect_in_window(w);
  return make_rect(0, 0, c.right - c.left, c.bottom - c.top);
}

// window (0, 0) in the top-level surface
static POINT window_origin_in_top(Window* w) {
  POINT p = { 0, 0 };
  while (w->parent != NULL) {
    RECT c = client_rect_in_window(w->parent);
    p.x += w->rect.left + c.left;
    p.y += w->rect.top + c.top;
    w = w->parent;
  }
  return p;
}

POINT client_origin_in_top(Window* w) {
  POINT p = window_origin_in_top(w);
  RECT c = client_rect_in_window(w);
  p.x += c.left;
  p.y += c.top;
  return p;
}

bool is_visible(Window* w) {
  for (; w != NULL; w = w->parent) if (!w->visible || w->destroyed) return false;
  return true;
}

void window_draw_target(Window* w, const RECT* paint_clip, DrawTarget& t) {
  GUILocker l;
  Window* top = top_level(w);
  t.top = top;
  t.bitmap = top->surface;
  POINT o = client_origin_in_top(w);
  t.ox = o.x; t.oy = o.y;
  t.clip.clear();
  if (t.bitmap == NULL || !is_visible(w)) return;
  RECT area = rect_offset(client_rect(w), o.x, o.y);
  if (paint_clip) area = rect_intersection(area, rect_offset(*paint_clip, o.x, o.y));
  // clipped by the client areas of the ancestors
  for (Window* a = w->parent; a != NULL; a = a->parent) {
    POINT ao = client_origin_in_top(a);
    area = rect_intersection(area, rect_offset(client_rect(a), ao.x, ao.y));
  }
  if (rect_empty(area)) return;
  t.clip.push_back(area);
  // minus the visible children (WS_CLIPCHILDREN) and the siblings above it (WS_CLIPSIBLINGS)
  for (Window* c : w->children) {
    if (!c->visible || c->destroyed) continue;
    POINT co = window_origin_in_top(c);
    rects_subtract(t.clip, make_rect(co.x, co.y, co.x + c->rect.right - c->rect.left, co.y + c->rect.bottom - c->rect.top));
  }
  for (Window* a = w; a->parent != NULL; a = a->parent) {
    for (Window* s : a->parent->children) {
      if (s == a) break;	// the rest are below
      if (!s->visible || s->destroyed) continue;
      POINT so = window_origin_in_top(s);
      rects_subtract(t.clip, make_rect(so.x, so.y, so.x + s->rect.right - s->rect.left, so.y + s->rect.bottom - s->rect.top));
    }
  }
}

// The non-client area of a child window (borders, scroll bars): window coordinates
void window_nonclient_target(Window* w, DrawTarget& t) {
  GUILocker l;
  Window* top = top_level(w);
  t.top = top;
  t.bitmap = top->surface;
  POINT o = window_origin_in_top(w);
  t.ox = o.x; t.oy = o.y;
  t.clip.clear();
  if (t.bitmap == NULL || !is_visible(w)) return;
  RECT area = make_rect(o.x, o.y, o.x + w->rect.right - w->rect.left, o.y + w->rect.bottom - w->rect.top);
  for (Window* a = w->parent; a != NULL; a = a->parent) {
    POINT ao = client_origin_in_top(a);
    area = rect_intersection(area, rect_offset(client_rect(a), ao.x, ao.y));
  }
  if (rect_empty(area)) return;
  t.clip.push_back(area);
  rects_subtract(t.clip, rect_offset(client_rect_in_window(w), o.x, o.y));
}

void invalidate(Window* w, const RECT* r, bool erase) {
  GUILocker l;
  RECT c = client_rect(w);
  RECT area = r == NULL ? c : rect_intersection(*r, c);
  if (rect_empty(area)) return;
  w->update = rect_union(w->update, area);
  if (erase) w->erase = true;
  if (w->queue != NULL) pthread_cond_broadcast(&w->queue->cond);
}

static void invalidate_tree(Window* w) {
  invalidate(w, NULL, true);
  for (Window* c : w->children) invalidate_tree(c);
}

void validate(Window* w) {
  GUILocker l;
  w->update = make_rect(0, 0, 0, 0);
  w->erase = false;
}

// window coordinates of a point in screen coordinates
static POINT screen_to_window(Window* w, POINT p) {
  Window* top = top_level(w);
  POINT o = window_origin_in_top(w);
  POINT r = { p.x - top->rect.left - o.x, p.y - top->rect.top - o.y };
  return r;
}

static POINT client_to_screen(Window* w, POINT p) {
  Window* top = top_level(w);
  POINT o = client_origin_in_top(w);
  POINT r = { p.x + o.x + top->rect.left, p.y + o.y + top->rect.top };
  return r;
}

// the deepest visible, enabled window containing p (top-level surface coordinates)
static Window* child_at(Window* w, int x, int y) {
  // children are clipped to the client area (they never cover the scroll bars)
  POINT co = client_origin_in_top(w);
  if (!rect_contains(rect_offset(client_rect(w), co.x, co.y), x, y)) return w;
  for (Window* c : w->children) {
    if (!c->visible || c->destroyed) continue;
    POINT o = window_origin_in_top(c);
    if (rect_contains(make_rect(o.x, o.y, o.x + c->rect.right - c->rect.left, o.y + c->rect.bottom - c->rect.top), x, y)) {
      return child_at(c, x, y);
    }
  }
  return w;
}

// ---------------------------------------------------------------------------
// Sending messages

word send_message(word hwnd, word msg, word wparam, word lparam) {
  word proc;
  {
    GUILocker l;
    Window* w = window(hwnd);
    if (w == NULL) return 0;
    proc = w->wndproc;
    if (w->queue != NULL && current_queue != NULL && w->queue != current_queue) {
      // a window of another thread: its procedure must run there, but the sender holds the VM
      // (a synchronous DLL call), so the message is posted
      if (TraceGUI) fprintf(stderr, "Strongtalk GUI: message 0x%lx to a window of another thread posted\n", (long) msg);
      enqueue(w->queue, make_message(hwnd, msg, wparam, lparam));
      return 0;
    }
  }
  if (TraceMessages) fprintf(stderr, "[send 0x%lx 0x%04lx 0x%lx 0x%lx]\n", (long) hwnd, (long) msg, (long) wparam, (long) lparam);
  if (proc == 0) return def_window_proc(hwnd, msg, wparam, lparam);
  return ((WNDPROC) proc)(hwnd, msg, wparam, lparam);
}

static word SendMessageA(word hwnd, word msg, word wparam, word lparam) {
  return send_message(hwnd, msg & 0xFFFFFFFF, wparam, lparam);
}

static word CallWindowProcA(word proc, word hwnd, word msg, word wparam, word lparam) {
  if (proc == 0) return def_window_proc(hwnd, msg, wparam, lparam);
  return ((WNDPROC) proc)(hwnd, msg & 0xFFFFFFFF, wparam, lparam);
}

static word PostMessageA(word hwnd, word msg, word wparam, word lparam) {
  return post_message(hwnd, msg & 0xFFFFFFFF, wparam, lparam);
}

static word PostThreadMessageA(word thread_id, word msg, word wparam, word lparam) {
  GUILocker l;
  enqueue(queue_of((uint32_t) thread_id), make_message(0, msg & 0xFFFFFFFF, wparam, lparam));
  return 1;
}

static word PostQuitMessage(word code) {
  Queue* q = my_queue();
  GUILocker l;
  q->quit = true;
  q->quit_code = code;
  pthread_cond_broadcast(&q->cond);
  return 0;
}

// ---------------------------------------------------------------------------
// Focus and capture

// SetFocus: the focus is per thread; windows of other threads cannot get it
static word set_focus(word hwnd) {
  Queue* q = my_queue();
  word old;
  {
    GUILocker l;
    Window* w = window(hwnd);
    if (hwnd != 0 && (w == NULL || w->queue != q)) return 0;
    old = window(q->focus) ? q->focus : 0;
    if (old == hwnd) return old;
    q->focus = hwnd;
    if (w != NULL) top_level(w)->last_focus = hwnd;
  }
  if (old != 0 && window(old) != NULL) send_message(old, WM_KILLFOCUS, hwnd, 0);
  if (hwnd != 0 && window(hwnd) != NULL) send_message(hwnd, WM_SETFOCUS, old, 0);
  return old;
}

static word SetFocus(word hwnd) { return set_focus(hwnd); }
static word GetFocus()          { Queue* q = my_queue(); GUILocker l; return window(q->focus) ? q->focus : 0; }

static word SetCapture(word hwnd) {
  word old;
  { GUILocker l; old = capture_hwnd; capture_hwnd = hwnd; }
  if (old != 0 && old != hwnd && window(old) != NULL) send_message(old, WM_CAPTURECHANGED, 0, hwnd);
  return old;
}

static word ReleaseCapture() {
  word old;
  { GUILocker l; old = capture_hwnd; capture_hwnd = 0; }
  if (old != 0 && window(old) != NULL) send_message(old, WM_CAPTURECHANGED, 0, 0);
  return 1;
}

// ---------------------------------------------------------------------------
// Cocoa views

} // namespace w32

using namespace w32;

@interface STView : NSView {
@public
  HANDLE          hwnd;
  NSTrackingArea* tracking;
  uint32_t        last_flags;
}
@end

@interface STWindowDelegate : NSObject <NSWindowDelegate> {
@public
  HANDLE hwnd;
}
@end

@interface STAppDelegate : NSObject <NSApplicationDelegate>
@end

@interface STMenuTarget : NSObject <NSMenuDelegate>
@end

namespace w32 {

static STMenuTarget* menu_target = nil;

// Win32 screen coordinates (y down from the top of the main display) <-> AppKit (y up)
static int main_screen_height() {
  NSScreen* s = [NSScreen screens].firstObject;
  return s ? (int) s.frame.size.height : screen_height;
}

static NSRect cocoa_rect(const RECT& r) {
  int h = main_screen_height();
  return NSMakeRect(r.left, h - r.bottom, r.right - r.left, r.bottom - r.top);
}

static RECT win32_rect(NSRect r) {
  int h = main_screen_height();
  return make_rect((int) r.origin.x, (int) (h - r.origin.y - r.size.height),
                   (int) (r.origin.x + r.size.width), (int) (h - r.origin.y));
}

static NSWindow* ns_window(Window* w) { return w->nswindow ? (__bridge NSWindow*) w->nswindow : nil; }

// Shows the surface of top (main thread)
static void display_surface(HANDLE hwnd) {
  CGImageRef image = NULL;
  NSWindow* nsw = nil;
  {
    GUILocker l;
    Window* top = window(hwnd);
    if (top == NULL || top->surface == NULL) return;
    top->display_pending = false;
    top->dirty = make_rect(0, 0, 0, 0);
    image = CGBitmapContextCreateImage(top->surface->ctx);
    nsw = ns_window(top);
  }
  if (nsw != nil) {
    CALayer* layer = nsw.contentView.layer;
    [CATransaction begin];
    [CATransaction setDisableActions: YES];
    layer.contents = (__bridge id) image;
    [CATransaction commit];
  }
  CGImageRelease(image);
}

void surface_changed(Window* top, const RECT& r) {
  GUILocker l;
  if (top == NULL || rect_empty(r)) return;
  top->dirty = rect_union(top->dirty, r);
  if (top->display_pending || top->nswindow == NULL) return;
  top->display_pending = true;
  HANDLE h = top->handle;
  run_on_main(^{ display_surface(h); });
}

static void create_ns_window(Window* w) {
  NSWindowStyleMask mask = 0;
  if ((w->style & WS_CAPTION) == WS_CAPTION) mask |= NSWindowStyleMaskTitled;
  if (w->style & WS_SYSMENU)                 mask |= NSWindowStyleMaskClosable;
  if (w->style & WS_MINIMIZEBOX)             mask |= NSWindowStyleMaskMiniaturizable;
  if (w->style & WS_THICKFRAME)              mask |= NSWindowStyleMaskResizable;
  if (mask == 0) mask = NSWindowStyleMaskBorderless;
  else           mask |= NSWindowStyleMaskTitled;
  RECT r = w->rect;
  HANDLE hwnd = w->handle;
  NSString* title = ns_string(w->text.c_str());
  bool popup = (w->style & WS_POPUP) && !(w->style & WS_CAPTION);
  __block void* result = NULL;
  run_on_main_sync(^{
    NSRect content = cocoa_rect(r);
    NSWindow* nsw = [[NSWindow alloc] initWithContentRect: content styleMask: mask backing: NSBackingStoreBuffered defer: NO];
    nsw.releasedWhenClosed = NO;
    nsw.title = title;
    nsw.acceptsMouseMovedEvents = YES;
    nsw.tabbingMode = NSWindowTabbingModeDisallowed;
    if (popup) nsw.level = NSPopUpMenuWindowLevel;
    STView* view = [[STView alloc] initWithFrame: NSMakeRect(0, 0, content.size.width, content.size.height)];
    view->hwnd = hwnd;
    view.wantsLayer = YES;
    view.layer.contentsGravity = kCAGravityTopLeft;
    view.layer.contentsScale = gdi_scale;
    view.layer.backgroundColor = CGColorGetConstantColor(kCGColorWhite);
    view.layer.magnificationFilter = kCAFilterNearest;
    nsw.contentView = view;
    if (HiddenGUI) { nsw.alphaValue = 0; nsw.ignoresMouseEvents = YES; }
    STWindowDelegate* d = [[STWindowDelegate alloc] init];
    d->hwnd = hwnd;
    nsw.delegate = d;
    objc_setAssociatedObject(nsw, "delegate", d, OBJC_ASSOCIATION_RETAIN);
    [nsw makeFirstResponder: view];
    result = (__bridge_retained void*) nsw;
  });
  GUILocker l;
  w->nswindow = result;
}

// The window rect of a top-level window from its NSWindow (main thread)
static void top_level_moved(HANDLE hwnd, NSWindow* nsw) {
  RECT r = win32_rect([nsw contentRectForFrameRect: nsw.frame]);
  bool resized;
  {
    GUILocker l;
    Window* w = window(hwnd);
    if (w == NULL) return;
    resized = r.right - r.left != w->rect.right - w->rect.left || r.bottom - r.top != w->rect.bottom - w->rect.top;
    bool moved = r.left != w->rect.left || r.top != w->rect.top;
    if (!resized && !moved) return;
    w->rect = r;
    if (resized) {
      if (w->surface) w->surface->resize(r.right - r.left, r.bottom - r.top);
      invalidate_tree(w);
    }
  }
  if (resized) post_message(hwnd, WM_SIZE, SIZE_RESTORED, make_lparam(r.right - r.left, r.bottom - r.top));
  post_message(hwnd, WM_MOVE, 0, make_lparam(r.left, r.top));
  display_surface(hwnd);
}

// ---------------------------------------------------------------------------
// CreateWindowExA

static struct {				// CREATESTRUCTA (32-bit)
  uint32_t create_params, instance, menu, parent;
  int32_t  cy, cx, y, x;
  uint32_t style, name, class_name, ex_style;
} create_struct;

static int32_t minmax_info[10];		// MINMAXINFO

static void query_min_track(Window* w) {
  memset(minmax_info, 0, sizeof minmax_info);
  minmax_info[2] = screen_width; minmax_info[3] = screen_height;	// ptMaxSize
  minmax_info[8] = screen_width * 4; minmax_info[9] = screen_height * 4;	// ptMaxTrackSize
  send_message(w->handle, WM_GETMINMAXINFO, 0, (word) minmax_info);
  w->min_track.x = minmax_info[6];
  w->min_track.y = minmax_info[7];
  if (w->nswindow != NULL) {
    HANDLE h = w->handle;
    NSSize min = NSMakeSize(std::max(1, (int) minmax_info[6]), std::max(1, (int) minmax_info[7]));
    run_on_main(^{
      GUILocker l;
      Window* top = window(h);
      if (top != NULL && top->nswindow != NULL) ns_window(top).contentMinSize = min;
    });
  }
}

static void show_window(Window* w, word cmd);

static word CreateWindowExA(word ex_style, word class_name, word window_name, word style,
                            word x, word y, word width, word height, word parent, word menu, word instance, word param) {
  const char* cname = class_name < 0x10000 ? NULL : (const char*) class_name;
  WndClass* cls = cname ? find_class(cname) : NULL;
  if (cls == NULL) {
    if (TraceGUI) fprintf(stderr, "Strongtalk GUI: CreateWindowExA: unknown class %s\n", cname ? cname : "(atom)");
    return 0;
  }
  Window* p = window(parent);
  uint32_t st = (uint32_t) style;
  if (!(st & WS_CHILD)) p = NULL;		// an owned top-level window
  if (p == NULL) ensure_gui();

  Window* w = new Window(cls);
  Queue* q = my_queue();
  {
    GUILocker l;
    w->style = st & ~WS_VISIBLE;
    w->ex_style = (uint32_t) ex_style;
    w->text = window_name ? (const char*) window_name : "";
    w->queue = q;
    w->enabled = !(st & WS_DISABLED);
    w->vscroll.visible = (st & WS_VSCROLL) != 0;
    w->hscroll.visible = (st & WS_HSCROLL) != 0;
    int ix = (int32_t) x, iy = (int32_t) y, iw = (int32_t) width, ih = (int32_t) height;
    if (p != NULL) {
      w->parent = p;
      w->id = menu;
      p->children.insert(p->children.begin(), w);
      if (x == CW_USEDEFAULT || (int32_t) x == (int32_t) 0x80000000) ix = iy = 0;
      if ((int32_t) width == (int32_t) 0x80000000) iw = ih = 0;
      w->rect = make_rect(ix, iy, ix + iw, iy + ih);
    } else {
      w->menu = menu;
      static int cascade = 0;
      if ((int32_t) x == (int32_t) 0x80000000) {
        ix = 60 + 24 * (cascade % 10);
        iy = 60 + 24 * (cascade % 10);
        cascade++;
      }
      if ((int32_t) width == (int32_t) 0x80000000 || iw <= 0) { iw = 640; ih = 480; }
      iy = std::max(iy, 30);			// below the menu bar
      w->rect = make_rect(ix, iy, ix + iw, iy + ih);
      top_levels.push_back(w);
    }
  }
  HANDLE hwnd = w->handle;
  if (p == NULL) {
    w->surface = new Bitmap(w->rect.right - w->rect.left, w->rect.bottom - w->rect.top, 32);
    create_ns_window(w);
  }
  if (TraceMessages) fprintf(stderr, "[CreateWindowExA %s -> 0x%x]\n", cls->name.c_str(), hwnd);

  // the creation messages (as Win32)
  create_struct.create_params = pack_word(param);
  create_struct.instance = pack_word(instance);
  create_struct.menu = pack_word(menu);
  create_struct.parent = pack_word(parent);
  create_struct.x = w->rect.left; create_struct.y = w->rect.top;
  create_struct.cx = w->rect.right - w->rect.left; create_struct.cy = w->rect.bottom - w->rect.top;
  create_struct.style = st; create_struct.ex_style = (uint32_t) ex_style;
  create_struct.name = pack_word(window_name);
  create_struct.class_name = pack_word(class_name);
  if (p == NULL) query_min_track(w);
  if (window(hwnd) == NULL) return 0;
  if (send_message(hwnd, WM_NCCREATE, 0, (word) &create_struct) == 0) {
    if (window(hwnd) != NULL) { extern word destroy_window(word); destroy_window(hwnd); }
    return 0;
  }
  send_message(hwnd, WM_NCCALCSIZE, 0, 0);
  if ((int32_t) send_message(hwnd, WM_CREATE, 0, (word) &create_struct) == -1) {
    if (window(hwnd) != NULL) { extern word destroy_window(word); destroy_window(hwnd); }
    return 0;
  }
  if (window(hwnd) == NULL) return 0;
  // WM_SIZE and WM_MOVE come when it is first shown (the image's WM_SIZE handler needs the handle)
  if ((st & WS_VISIBLE) && window(hwnd) != NULL) show_window(w, SW_SHOW);
  return hwnd;
}

static void destroy_tree(Window* w);

word destroy_window(word hwnd) {
  Window* w = window(hwnd);
  if (w == NULL) return 0;
  {
    GUILocker l;
    if (w->destroyed) return 1;
  }
  // DestroyWindow first takes the keyboard focus from the window and its children (the image
  // releases windows in WM_NCDESTROY and must not find a focused view then)
  word focused = 0;
  {
    GUILocker l;
    if (w->queue != NULL) {
      Window* f = window(w->queue->focus);
      for (Window* a = f; a != NULL; a = a->parent) {
        if (a == w) { focused = f->handle; w->queue->focus = 0; break; }
      }
    }
  }
  if (focused != 0) send_message(focused, WM_KILLFOCUS, 0, 0);
  if (window(hwnd) == NULL) return 1;
  send_message(hwnd, WM_DESTROY, 0, 0);
  // the children (they may destroy themselves meanwhile)
  while (true) {
    Window* c = NULL;
    {
      GUILocker l;
      for (Window* x : w->children) if (!x->destroyed) { c = x; break; }
    }
    if (c == NULL) break;
    destroy_window(c->handle);
  }
  {
    GUILocker l;
    w->destroyed = true;
    w->visible = false;
    if (w->queue != NULL && w->queue->focus == hwnd) w->queue->focus = 0;
    if (capture_hwnd == hwnd) capture_hwnd = 0;
  }
  send_message(hwnd, WM_NCDESTROY, 0, 0);
  void* nsw = NULL;
  Window* parent;
  {
    GUILocker l;
    parent = w->parent;
    if (parent != NULL) {
      auto& ch = parent->children;
      ch.erase(std::remove(ch.begin(), ch.end(), w), ch.end());
      RECT r = w->rect;
      RECT pc = client_rect(parent);
      (void) pc;
      invalidate(parent, &r, true);
    } else {
      top_levels.erase(std::remove(top_levels.begin(), top_levels.end(), w), top_levels.end());
    }
    nsw = w->nswindow;
    w->nswindow = NULL;
    // drop its messages and timers
    if (w->queue != NULL) {
      auto& m = w->queue->messages;
      m.erase(std::remove_if(m.begin(), m.end(), [hwnd](const QMessage& x) { return x.hwnd == (word) hwnd; }), m.end());
      auto& t = w->queue->timers;
      t.erase(std::remove_if(t.begin(), t.end(), [hwnd](const Timer& x) { return x.hwnd == (word) hwnd; }), t.end());
    }
  }
  gdi_destroy_window_dc(w);
  if (nsw != NULL) {
    run_on_main(^{
      NSWindow* n = (__bridge_transfer NSWindow*) nsw;
      n.delegate = nil;
      [n orderOut: nil];
      [n close];
    });
  }
  Bitmap* surface = w->surface;
  w->surface = NULL;
  delete w;			// frees the handle
  delete surface;
  return 1;
}

static word DestroyWindow(word hwnd) { return destroy_window(hwnd); }

static void show_window(Window* w, word cmd) {
  bool show = cmd != SW_HIDE;
  bool was;
  {
    GUILocker l;
    was = w->visible;
    w->visible = show;
    if (show) w->style |= WS_VISIBLE; else w->style &= ~WS_VISIBLE;
  }
  if (show != was) send_message(w->handle, WM_SHOWWINDOW, show, 0);
  if (window(w->handle) == NULL) return;
  if (show && !w->size_sent) {		// as WM_WINDOWPOSCHANGED of the first show
    w->size_sent = true;
    RECT c = client_rect(w);
    send_message(w->handle, WM_SIZE, SIZE_RESTORED, make_lparam(c.right, c.bottom));
    if (window(w->handle) == NULL) return;
    send_message(w->handle, WM_MOVE, 0, make_lparam(w->rect.left, w->rect.top));
    if (window(w->handle) == NULL) return;
  }
  if (w->parent != NULL) {
    if (show != was) {
      if (show) invalidate_tree(w);
      RECT r = w->rect;
      invalidate(w->parent, &r, true);
    }
    return;
  }
  if (show) invalidate_tree(w);
  HANDLE h = w->handle;
  bool activate = cmd != SW_SHOWNOACTIVATE && cmd != SW_SHOWNA && cmd != SW_SHOWMINNOACTIVE;
  run_on_main(^{
    GUILocker l;
    Window* top = window(h);
    if (top == NULL || top->nswindow == NULL) return;
    NSWindow* nsw = ns_window(top);
    if (!show)                                 [nsw orderOut: nil];
    else if (cmd == SW_SHOWMINIMIZED || cmd == SW_MINIMIZE || cmd == SW_SHOWMINNOACTIVE) [nsw miniaturize: nil];
    else if (activate) {
      [nsw makeKeyAndOrderFront: nil];
      if (!HiddenGUI) [NSApp activateIgnoringOtherApps: YES];
    } else                                     [nsw orderFront: nil];
  });
}

static word ShowWindow(word hwnd, word cmd) {
  Window* w = window(hwnd);
  if (w == NULL) return 0;
  bool was = w->visible;
  show_window(w, cmd & 0xFFFFFFFF);
  return was;
}

static word SetForegroundWindow(word hwnd) {
  Window* w = top_level(window(hwnd));
  if (w == NULL) return 0;
  HANDLE h = w->handle;
  run_on_main(^{
    GUILocker l;
    Window* top = window(h);
    if (top == NULL || top->nswindow == NULL || !top->visible) return;
    [ns_window(top) makeKeyAndOrderFront: nil];
    if (!HiddenGUI) [NSApp activateIgnoringOtherApps: YES];
  });
  return 1;
}

static word EnableWindow(word hwnd, word enable) {
  Window* w = window(hwnd);
  if (w == NULL) return 0;
  bool was_disabled;
  {
    GUILocker l;
    was_disabled = !w->enabled;
    w->enabled = enable != 0;
    if (enable) w->style &= ~WS_DISABLED; else w->style |= WS_DISABLED;
  }
  if (was_disabled == (enable != 0)) {
    send_message(hwnd, WM_ENABLE, enable != 0, 0);
    invalidate(w, NULL, true);
  }
  return was_disabled;
}

static word MoveWindow(word hwnd, word x, word y, word width, word height, word repaint) {
  Window* w = window(hwnd);
  if (w == NULL) return 0;
  RECT r = make_rect((int32_t) x, (int32_t) y, (int32_t) x + (int32_t) width, (int32_t) y + (int32_t) height);
  bool resized, moved;
  RECT old;
  {
    GUILocker l;
    old = w->rect;
    resized = r.right - r.left != old.right - old.left || r.bottom - r.top != old.bottom - old.top;
    moved = r.left != old.left || r.top != old.top;
    w->rect = r;
    if (w->parent == NULL && resized && w->surface) w->surface->resize(r.right - r.left, r.bottom - r.top);
  }
  if (w->parent != NULL) {
    if (resized || moved) {
      invalidate(w->parent, &old, true);
      invalidate_tree(w);
    }
  } else if (resized || moved) {
    invalidate_tree(w);
    HANDLE h = w->handle;
    run_on_main(^{
      GUILocker l;
      Window* top = window(h);
      if (top == NULL || top->nswindow == NULL) return;
      NSWindow* nsw = ns_window(top);
      [nsw setFrame: [nsw frameRectForContentRect: cocoa_rect(r)] display: YES];
    });
  }
  if (resized) {
    RECT c = client_rect(w);
    send_message(hwnd, WM_SIZE, SIZE_RESTORED, make_lparam(c.right, c.bottom));
  }
  if (moved) send_message(hwnd, WM_MOVE, 0, make_lparam(r.left, r.top));
  return 1;
}

static void write_rect(word p, const RECT& r) { memcpy((void*) p, &r, sizeof r); }

static word GetClientRect(word hwnd, word rect) {
  Window* w = window(hwnd);
  if (w == NULL) return 0;
  GUILocker l;
  write_rect(rect, client_rect(w));
  return 1;
}

static word GetWindowRect(word hwnd, word rect) {
  Window* w = window(hwnd);
  if (w == NULL) return 0;
  GUILocker l;
  POINT o = { 0, 0 };
  o = client_to_screen(w, o);
  RECT c = client_rect_in_window(w);
  RECT r = make_rect(o.x - c.left, o.y - c.top, 0, 0);
  r.right = r.left + w->rect.right - w->rect.left;
  r.bottom = r.top + w->rect.bottom - w->rect.top;
  write_rect(rect, r);
  return 1;
}

static word ClientToScreen(word hwnd, word point) {
  Window* w = window(hwnd);
  if (w == NULL) return 0;
  GUILocker l;
  POINT* p = (POINT*) point;
  *p = client_to_screen(w, *p);
  return 1;
}

static word ScreenToClient(word hwnd, word point) {
  Window* w = window(hwnd);
  if (w == NULL) return 0;
  GUILocker l;
  POINT* p = (POINT*) point;
  POINT o = { 0, 0 };
  o = client_to_screen(w, o);
  p->x -= o.x;
  p->y -= o.y;
  return 1;
}

static word AdjustWindowRect(word rect, word style, word has_menu) {
  return 1;	// top-level windows have no non-client area (AppKit draws the frame outside)
}

static word GetParent(word hwnd) {
  Window* w = window(hwnd);
  if (w == NULL || w->parent == NULL) return 0;
  return w->parent->handle;
}

static word GetWindowLongA(word hwnd, word index) {
  Window* w = window(hwnd);
  if (w == NULL) return 0;
  switch ((int32_t) index) {
    case GWL_WNDPROC:    return w->wndproc;
    case GWL_STYLE:      return w->style | (w->visible ? WS_VISIBLE : 0);
    case GWL_EXSTYLE:    return w->ex_style;
    case GWL_USERDATA:   return w->user_data;
    case GWL_ID:         return w->id;
    case GWL_HWNDPARENT: return w->parent ? w->parent->handle : 0;
    case GWL_HINSTANCE:  return 0x400000;
  }
  return 0;
}

static word SetWindowLongA(word hwnd, word index, word value) {
  Window* w = window(hwnd);
  if (w == NULL) return 0;
  GUILocker l;
  word old = 0;
  switch ((int32_t) index) {
    case GWL_WNDPROC:  old = w->wndproc;   w->wndproc = value;   break;
    case GWL_USERDATA: old = w->user_data; w->user_data = value; break;
    case GWL_ID:       old = w->id;        w->id = value;        break;
    case GWL_STYLE:
      old = w->style;
      w->style = (uint32_t) value & ~WS_VISIBLE;
      w->vscroll.visible = (w->style & WS_VSCROLL) != 0;
      w->hscroll.visible = (w->style & WS_HSCROLL) != 0;
      break;
    case GWL_EXSTYLE:  old = w->ex_style;  w->ex_style = (uint32_t) value; break;
  }
  return old;
}

static word SetWindowTextA(word hwnd, word text) {
  Window* w = window(hwnd);
  if (w == NULL) return 0;
  {
    GUILocker l;
    w->text = text ? (const char*) text : "";
  }
  if (w->parent == NULL) {
    NSString* title = ns_string(w->text.c_str());
    HANDLE h = w->handle;
    run_on_main(^{
      GUILocker l;
      Window* top = window(h);
      if (top != NULL && top->nswindow != NULL) ns_window(top).title = title;
    });
  } else {
    invalidate(w, NULL, true);
  }
  return 1;
}

static word InvalidateRect(word hwnd, word rect, word erase) {
  Window* w = window(hwnd);
  if (w == NULL) {
    if (hwnd == 0) {		// all windows
      GUILocker l;
      for (Window* t : top_levels) invalidate_tree(t);
      return 1;
    }
    return 0;
  }
  invalidate(w, rect ? (RECT*) rect : NULL, erase != 0);
  return 1;
}

// PAINTSTRUCT (32-bit): hdc, fErase, rcPaint, fRestore, fIncUpdate, rgbReserved[32]
static word BeginPaint(word hwnd, word ps) {
  Window* w = window(hwnd);
  if (w == NULL) return 0;
  RECT update;
  bool erase;
  {
    GUILocker l;
    update = w->update;
    erase = w->erase;
    validate(w);
  }
  word hdc = gdi_window_dc(w);
  gdi_dc_set_paint_clip(hdc, &update);
  if (erase && send_message(hwnd, WM_ERASEBKGND, hdc, 0) != 0) erase = false;
  uint32_t* f = (uint32_t*) ps;
  memset(f, 0, 64);
  f[0] = (uint32_t) hdc;
  f[1] = erase;
  memcpy(&f[2], &update, sizeof update);
  return hdc;
}

static word EndPaint(word hwnd, word ps) {
  Window* w = window(hwnd);
  if (w == NULL) return 0;
  gdi_dc_set_paint_clip(gdi_window_dc(w), NULL);
  paint_nonclient(w);
  return 1;
}

static word GetDC(word hwnd) {
  Window* w = window(hwnd);
  if (w == NULL) {
    extern word gdi_screen_dc();
    return hwnd == 0 ? gdi_screen_dc() : 0;
  }
  word hdc = gdi_window_dc(w);
  gdi_dc_set_paint_clip(hdc, NULL);
  return hdc;
}

static word ReleaseDC(word hwnd, word hdc) { return 1; }

// ---------------------------------------------------------------------------
// DefWindowProc

word def_window_proc(word hwnd, word msg, word wparam, word lparam) {
  Window* w = window(hwnd);
  if (w == NULL) return 0;
  switch (msg) {
    case WM_NCCREATE:   return 1;
    case WM_CLOSE:      destroy_window(hwnd); return 0;
    case WM_PAINT: {
      uint32_t ps[16];
      BeginPaint(hwnd, (word) ps);
      EndPaint(hwnd, (word) ps);
      return 0;
    }
    case WM_ERASEBKGND: {
      word brush = w->cls ? w->cls->background : 0;
      if (brush == 0) return 0;
      RECT c = client_rect(w);
      extern word FillRect_user(word hdc, word rect, word brush);
      FillRect_user(wparam, (word) &c, brush);
      return 1;
    }
    case WM_SETCURSOR: {
      // the parent first, then the class cursor in the client area
      if (w->parent != NULL && send_message(w->parent->handle, WM_SETCURSOR, wparam, lparam)) return 1;
      int hit = lo16(lparam);
      extern word SetCursor_user(word cursor);
      extern word arrow_cursor();
      if (hit == HTCLIENT && w->cls && w->cls->cursor) { SetCursor_user(w->cls->cursor); return 1; }
      if (hit != HTCLIENT) { SetCursor_user(arrow_cursor()); return 1; }
      return 0;
    }
    case WM_MOUSEWHEEL:		// to the parent (the image's scrollers handle it)
      if (w->parent != NULL) return send_message(w->parent->handle, WM_MOUSEWHEEL, wparam, lparam);
      return 0;
    case WM_MOUSEACTIVATE: return 1;
    case WM_SYSCOMMAND:    return 0;
    case WM_NCHITTEST:     return HTCLIENT;
  }
  return 0;
}

static word DefWindowProcA(word hwnd, word msg, word wparam, word lparam) {
  return def_window_proc(hwnd, msg & 0xFFFFFFFF, wparam, lparam);
}

void paint_nonclient(Window* w) {
  if (w->parent == NULL) return;
  if (!w->vscroll.visible && !w->hscroll.visible && border_width(w) == 0) return;
  extern word gdi_window_nonclient_dc(Window* w);
  word hdc = gdi_window_nonclient_dc(w);
  if (hdc == 0) return;
  int b = border_width(w);
  RECT r = make_rect(0, 0, w->rect.right - w->rect.left, w->rect.bottom - w->rect.top);
  if (b >= 1) gdi_frame_rect(hdc, r, 0x808080);
  if (b >= 2) gdi_frame_rect(hdc, make_rect(1, 1, r.right - 1, r.bottom - 1), 0xC0C0C0);
  paint_scroll_bars(w, hdc);
}

// ---------------------------------------------------------------------------
// Getting and dispatching messages

static void write_msg(word msg, const QMessage& m) {
  uint32_t* f = (uint32_t*) msg;
  f[0] = (uint32_t) m.hwnd;
  f[1] = (uint32_t) m.message;
  f[2] = pack_word(m.wparam);
  f[3] = pack_word(m.lparam);
  f[4] = m.time;
  f[5] = (uint32_t) m.pt.x;
  f[6] = (uint32_t) m.pt.y;
}

static bool in_filter(const QMessage& m, word hwnd, uint32_t min, uint32_t max) {
  if (hwnd != 0 && m.hwnd != hwnd) return false;
  if (min == 0 && max == 0) return true;
  return m.message >= min && m.message <= max;
}

static void mouse_message_target(QMessage& m);

// The next message of q (gui_lock held). false if none.
static bool next_message(Queue* q, word hwnd, uint32_t min, uint32_t max, bool remove, QMessage& out) {
  for (size_t i = 0; i < q->messages.size(); i++) {
    if (!in_filter(q->messages[i], hwnd, min, max)) continue;
    out = q->messages[i];
    if (remove) q->messages.erase(q->messages.begin() + i);
    return true;
  }
  if (q->quit && (min == 0 || (WM_QUIT >= min && WM_QUIT <= max))) {
    out = make_message(0, WM_QUIT, q->quit_code, 0);
    if (remove) q->quit = false;
    return true;
  }
  // WM_PAINT for an invalid window of this thread (parents first)
  if (min == 0 || (WM_PAINT >= min && WM_PAINT <= max)) {
    std::vector<Window*> todo(top_levels.begin(), top_levels.end());
    for (size_t i = 0; i < todo.size(); i++) {
      Window* w = todo[i];
      if (w->queue == q && !rect_empty(w->update) && is_visible(w) && (hwnd == 0 || w->handle == hwnd)) {
        out = make_message(w->handle, WM_PAINT, 0, 0);
        return true;
      }
      for (Window* c : w->children) todo.push_back(c);
    }
  }
  // timers
  uint32_t now = tick_count();
  if (min == 0 || (WM_TIMER >= min && WM_TIMER <= max)) {
    for (Timer& t : q->timers) {
      if ((int32_t) (now - t.due) >= 0 && (hwnd == 0 || t.hwnd == hwnd)) {
        out = make_message(t.hwnd, WM_TIMER, t.id, t.proc);
        if (remove) t.due = now + t.interval;
        return true;
      }
    }
  }
  return false;
}

static int next_timer_delay(Queue* q) {
  if (q->timers.empty()) return -1;
  uint32_t now = tick_count();
  int delay = INT32_MAX;
  for (Timer& t : q->timers) delay = std::min(delay, std::max(0, (int) (int32_t) (t.due - now)));
  return delay;
}

static void message_retrieved(Queue* q, const QMessage& m) {
  q->last = m;
  if (m.message >= WM_KEYDOWN && m.message <= WM_SYSCHAR) q->key_state = m.modifiers;
  else if (m.message >= WM_MOUSEFIRST && m.message <= WM_MOUSELAST) q->key_state = m.modifiers;
}

static bool handle_internal(QMessage& m);

static word GetMessageA(word msg, word hwnd, word min, word max) {
  Queue* q = my_queue();
  QMessage m;
  {
    GUILocker l;
    while (true) {
      handle_sent_messages(q);
      if (next_message(q, hwnd, (uint32_t) min, (uint32_t) max, true, m)) {
        if (handle_internal(m)) continue;
        break;
      }
      int delay = next_timer_delay(q);
      q->waiting = true;
      gui_wait(&q->cond, delay);
      q->waiting = false;
    }
    message_retrieved(q, m);
  }
  write_msg(msg, m);
  if (TraceMessages && m.message != WM_TIMER && m.message != WM_MOUSEMOVE)
    fprintf(stderr, "[get 0x%lx 0x%04lx 0x%lx 0x%lx]\n", (long) m.hwnd, (long) m.message, (long) m.wparam, (long) m.lparam);
  return m.message == WM_QUIT ? 0 : 1;
}

static word PeekMessageA(word msg, word hwnd, word min, word max, word flags) {
  Queue* q = my_queue();
  QMessage m;
  {
    GUILocker l;
    handle_sent_messages(q);
    while (true) {
      if (!next_message(q, hwnd, (uint32_t) min, (uint32_t) max, (flags & PM_REMOVE) != 0, m)) return 0;
      if ((flags & PM_REMOVE) && handle_internal(m)) continue;
      break;
    }
    if (flags & PM_REMOVE) message_retrieved(q, m);
  }
  write_msg(msg, m);
  return 1;
}

// Messages of the emulation itself, and the WM_SETCURSOR before mouse messages (gui_lock held)
static bool handle_internal(QMessage& m) {
  if (m.message >= WM_MOUSEFIRST && m.message <= WM_MOUSELAST && m.message != WM_MOUSEWHEEL) {
    Window* w = window(m.hwnd);
    if (w == NULL) return true;
    // scroll bars and borders (the non-client area of child windows)
    bool vertical;
    POINT p = { lo16(m.lparam), hi16(m.lparam) };	// client coordinates
    RECT c = client_rect_in_window(w);
    int wx = p.x + c.left, wy = p.y + c.top;
    if (scroll_bar_tracking() == w || (capture_hwnd != m.hwnd && w->parent != NULL && scroll_bar_hit(w, wx, wy, vertical) != 0)) {
      gui_unlock();
      scroll_bar_mouse(w, m.message, wx, wy);
      gui_lock();
      return true;
    }
    if (m.message == WM_MOUSEMOVE || m.message == WM_LBUTTONDOWN || m.message == WM_RBUTTONDOWN || m.message == WM_MBUTTONDOWN) {
      bool in_client = rect_contains(c, wx, wy) || capture_hwnd == m.hwnd;
      HANDLE h = (HANDLE) m.hwnd;
      word lparam = make_lparam(in_client ? HTCLIENT : HTBORDER, (int) m.message);
      gui_unlock();
      send_message(h, WM_SETCURSOR, h, lparam);
      gui_lock();
      if (window(m.hwnd) == NULL) return true;
    }
  }
  return false;
}

static word TranslateMessage(word msg) {
  Queue* q = my_queue();
  uint32_t* f = (uint32_t*) msg;
  if (f[1] != WM_KEYDOWN && f[1] != WM_SYSKEYDOWN) return 0;
  GUILocker l;
  if (q->last.message != f[1] || (uint32_t) q->last.wparam != f[2] || q->last.chars.empty()) return 0;
  std::u16string chars = q->last.chars;
  for (size_t i = chars.size(); i-- > 0; ) {
    int c = unicode_to_cp1252(chars[i]);
    if (c < 0) continue;
    QMessage m = q->last;
    m.message = f[1] == WM_SYSKEYDOWN ? WM_SYSCHAR : WM_CHAR;
    m.wparam = c;
    m.chars.clear();
    enqueue(q, m, true);
  }
  q->last.chars.clear();
  return 1;
}

static word DispatchMessageA(word msg) {
  uint32_t* f = (uint32_t*) msg;
  word hwnd = f[0], message = f[1];
  word wparam = (word) external_pointer(f[2]), lparam = (word) external_pointer(f[3]);
  if (message == WM_TIMER && lparam != 0) {	// a timer procedure
    typedef void (*TIMERPROC)(word, word, word, word);
    ((TIMERPROC) lparam)(hwnd, WM_TIMER, wparam, tick_count());
    return 0;
  }
  if (hwnd == 0) return 0;
  Window* w = window(hwnd);
  if (w == NULL) return 0;
  RECT before = w->update;
  word result = send_message(hwnd, message, wparam, lparam);
  if (message == WM_PAINT) {
    // a window that does not validate would get WM_PAINT forever
    GUILocker l;
    Window* x = window(hwnd);
    if (x != NULL && memcmp(&x->update, &before, sizeof before) == 0) validate(x);
  }
  return result;
}

// ---------------------------------------------------------------------------
// Timers

static word SetTimer(word hwnd, word id, word elapse, word proc) {
  Queue* q = my_queue();
  GUILocker l;
  Window* w = window(hwnd);
  if (hwnd != 0 && w == NULL) return 0;
  Queue* tq = w ? w->queue : q;
  static word next_id = 1;
  if (hwnd == 0) id = next_id++;
  for (Timer& t : tq->timers) {
    if (t.hwnd == hwnd && t.id == id) {
      t.interval = std::max<uint32_t>(10, (uint32_t) elapse);
      t.due = tick_count() + t.interval;
      t.proc = proc;
      return id ? id : 1;
    }
  }
  Timer t;
  t.hwnd = hwnd; t.id = id; t.interval = std::max<uint32_t>(10, (uint32_t) elapse); t.due = tick_count() + t.interval; t.proc = proc;
  tq->timers.push_back(t);
  pthread_cond_broadcast(&tq->cond);
  return id ? id : 1;
}

static word KillTimer(word hwnd, word id) {
  GUILocker l;
  for (Queue* q : queues) {
    auto& t = q->timers;
    size_t n = t.size();
    t.erase(std::remove_if(t.begin(), t.end(), [&](const Timer& x) { return x.hwnd == hwnd && x.id == id; }), t.end());
    if (t.size() != n) return 1;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Keyboard state

static word GetKeyState(word vk) {
  Queue* q = my_queue();
  uint32_t s;
  { GUILocker l; s = q->key_state; }
  bool down = false;
  switch (vk & 0xFF) {
    case VK_SHIFT:   down = (s & KS_SHIFT) != 0;   break;
    case VK_CONTROL: down = (s & KS_CONTROL) != 0; break;
    case VK_MENU:    down = (s & KS_ALT) != 0;     break;
    case VK_LBUTTON: down = (s & KS_LBUTTON) != 0; break;
    case VK_RBUTTON: down = (s & KS_RBUTTON) != 0; break;
    case VK_MBUTTON: down = (s & KS_MBUTTON) != 0; break;
    case VK_CAPITAL: return (s & KS_CAPS) ? 1 : 0;	// toggled
  }
  return down ? (word) (int16_t) 0x8000 : 0;
}

// ---------------------------------------------------------------------------
// Input from AppKit (main thread)

static uint32_t mk_flags(uint32_t ks) {
  uint32_t f = 0;
  if (ks & KS_LBUTTON) f |= MK_LBUTTON;
  if (ks & KS_RBUTTON) f |= MK_RBUTTON;
  if (ks & KS_MBUTTON) f |= MK_MBUTTON;
  if (ks & KS_SHIFT)   f |= MK_SHIFT;
  if (ks & KS_CONTROL) f |= MK_CONTROL;
  return f;
}

static uint32_t modifier_state(NSEventModifierFlags flags) {
  uint32_t s = current_modifiers & (KS_LBUTTON | KS_RBUTTON | KS_MBUTTON);
  if (flags & NSEventModifierFlagShift)                                      s |= KS_SHIFT;
  if (flags & (NSEventModifierFlagCommand | NSEventModifierFlagControl))   s |= KS_CONTROL;	// Command is Control
  if (flags & NSEventModifierFlagOption)                                     s |= KS_ALT;
  if (flags & NSEventModifierFlagCapsLock)                                   s |= KS_CAPS;
  return s;
}

// A mouse event in the view of the top-level window top: to the window under it (or the capture)
static void post_mouse(HANDLE top_hwnd, NSPoint p, word msg, word wparam_extra = 0) {
  GUILocker l;
  Window* top = window(top_hwnd);
  if (top == NULL) return;
  int x = (int) floor(p.x), y = (int) floor(p.y);
  cursor_pos.x = top->rect.left + x;
  cursor_pos.y = top->rect.top + y;
  Window* target = window(capture_hwnd);
  if (target == NULL) {
    target = child_at(top, x, y);
    // disabled windows get no input (their enabled parent does, as in Win32)
    while (target != NULL && !target->enabled && target->parent != NULL) target = target->parent;
  }
  if (target == NULL || !target->enabled) return;
  POINT o = client_origin_in_top(target);
  if (top_level(target) != top) {
    POINT sp = cursor_pos;
    POINT co = client_to_screen(target, (POINT) { 0, 0 });
    o.x = co.x - top->rect.left; o.y = co.y - top->rect.top;
    (void) sp;
  }
  word wparam = msg == WM_MOUSEWHEEL ? (wparam_extra | mk_flags(current_modifiers)) : mk_flags(current_modifiers);
  word lparam = msg == WM_MOUSEWHEEL ? make_lparam(cursor_pos.x, cursor_pos.y) : make_lparam(x - o.x, y - o.y);
  if (target->queue == NULL) return;
  enqueue(target->queue, make_message(target->handle, msg, wparam, lparam));
}

// virtual key code of a key event
static int virtual_key(NSEvent* e) {
  switch (e.keyCode) {
    case 36: case 76: return VK_RETURN;
    case 48:  return VK_TAB;
    case 49:  return VK_SPACE;
    case 51:  return VK_BACK;
    case 53:  return VK_ESCAPE;
    case 117: return VK_DELETE;
    case 114: return VK_INSERT;
    case 123: return VK_LEFT;
    case 124: return VK_RIGHT;
    case 125: return VK_DOWN;
    case 126: return VK_UP;
    case 115: return VK_HOME;
    case 119: return VK_END;
    case 116: return VK_PRIOR;
    case 121: return VK_NEXT;
    case 122: return VK_F1;      case 120: return VK_F1 + 1;  case 99:  return VK_F1 + 2;  case 118: return VK_F1 + 3;
    case 96:  return VK_F1 + 4;  case 97:  return VK_F1 + 5;  case 98:  return VK_F1 + 6;  case 100: return VK_F1 + 7;
    case 101: return VK_F1 + 8;  case 109: return VK_F1 + 9;  case 103: return VK_F1 + 10; case 111: return VK_F1 + 11;
  }
  NSString* s = e.charactersIgnoringModifiers;
  if (s.length == 0) return 0;
  unichar c = [s characterAtIndex: 0];
  if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
  if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
  switch (c) {
    case ';': case ':': return 0xBA;   case '=': case '+': return 0xBB;  case ',': case '<': return 0xBC;
    case '-': case '_': return 0xBD;   case '.': case '>': return 0xBE;  case '/': case '?': return 0xBF;
    case '`': case '~': return 0xC0;   case '[': case '{': return 0xDB;  case '\\': case '|': return 0xDC;
    case ']': case '}': return 0xDD;   case '\'': case '"': return 0xDE;
  }
  return 0;
}

// the characters a key event produces (WM_CHAR)
static std::u16string key_chars(NSEvent* e, int vk) {
  std::u16string out;
  NSEventModifierFlags f = e.modifierFlags;
  bool control = (f & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) != 0;
  switch (vk) {
    case VK_RETURN: out += (char16_t) 13; return out;
    case VK_TAB:    out += (char16_t) 9;  return out;
    case VK_BACK:   out += (char16_t) 8;  return out;
    case VK_ESCAPE: out += (char16_t) 27; return out;
  }
  if (control) {
    NSString* s = e.charactersIgnoringModifiers;
    if (s.length > 0) {
      unichar c = [s characterAtIndex: 0];
      if (c >= 'a' && c <= 'z') { out += (char16_t) (c - 'a' + 1); return out; }
      if (c >= 'A' && c <= 'Z') { out += (char16_t) (c - 'A' + 1); return out; }
      if (c == ' ') { out += (char16_t) ' '; return out; }
    }
    return out;
  }
  NSString* s = e.characters;
  for (NSUInteger i = 0; i < s.length; i++) {
    unichar c = [s characterAtIndex: i];
    if (c >= 0xF700 && c <= 0xF8FF) continue;	// function keys
    if (c == 0x7F) continue;
    out += (char16_t) c;
  }
  return out;
}

static word key_target(HANDLE top_hwnd) {
  Window* top = window(top_hwnd);
  if (top == NULL || top->queue == NULL) return 0;
  Window* f = window(top->queue->focus);
  if (f != NULL && top_level(f) == top) return f->handle;
  return top->handle;
}

static void post_key(HANDLE top_hwnd, NSEvent* e, bool down) {
  int vk = virtual_key(e);
  if (vk == 0 && !down) return;
  GUILocker l;
  current_modifiers = modifier_state(e.modifierFlags);
  Window* w = window(key_target(top_hwnd));
  if (w == NULL || w->queue == NULL || !w->enabled) return;
  Window* top = top_level(w);
  if (top != NULL && !top->enabled) return;
  word lparam = 1 | ((e.keyCode & 0xFF) << 16);
  if (vk >= VK_PRIOR && vk <= VK_DOWN) lparam |= 1 << 24;	// extended keys
  if (vk == VK_INSERT || vk == VK_DELETE) lparam |= 1 << 24;
  if (!down) lparam |= (1u << 30) | (1u << 31);
  else if (e.isARepeat) lparam |= 1 << 30;
  QMessage m = make_message(w->handle, down ? WM_KEYDOWN : WM_KEYUP, vk, (word) (uint32_t) lparam);
  if (down) m.chars = key_chars(e, vk);
  if (vk == 0) {		// a character without a virtual key (e.g. composed): only WM_CHAR
    for (char16_t c : m.chars) {
      int a = unicode_to_cp1252(c);
      if (a < 0) continue;
      QMessage cm = m;
      cm.message = WM_CHAR; cm.wparam = a; cm.chars.clear();
      enqueue(w->queue, cm);
    }
    return;
  }
  enqueue(w->queue, m);
}

static void post_modifier_keys(HANDLE top_hwnd, NSEventModifierFlags flags) {
  GUILocker l;
  uint32_t old = current_modifiers;
  current_modifiers = modifier_state(flags);
  Window* w = window(key_target(top_hwnd));
  if (w == NULL || w->queue == NULL) return;
  struct { uint32_t bit; int vk; } keys[] = { { KS_SHIFT, VK_SHIFT }, { KS_CONTROL, VK_CONTROL }, { KS_ALT, VK_MENU } };
  for (auto& k : keys) {
    bool was = (old & k.bit) != 0, is = (current_modifiers & k.bit) != 0;
    if (was == is) continue;
    word lparam = is ? 1 : (1 | (1u << 30) | (1u << 31));
    enqueue(w->queue, make_message(w->handle, is ? WM_KEYDOWN : WM_KEYUP, k.vk, (word) (uint32_t) lparam));
  }
}

// The key window changed (main thread): focus messages
static void top_level_activated(HANDLE hwnd, bool active) {
  GUILocker l;
  Window* top = window(hwnd);
  if (top == NULL || top->queue == NULL) return;
  Queue* q = top->queue;
  Window* f = window(q->focus);
  if (active) {
    if (f != NULL && top_level(f) == top) return;
    if (f != NULL) enqueue(q, make_message(f->handle, WM_KILLFOCUS, hwnd, 0));
    Window* restore = window(top->last_focus);
    if (restore == NULL || top_level(restore) != top) restore = top;
    q->focus = restore->handle;
    enqueue(q, make_message(restore->handle, WM_SETFOCUS, f ? f->handle : 0, 0));
  } else {
    if (f == NULL || top_level(f) != top) return;
    top->last_focus = f->handle;
    enqueue(q, make_message(f->handle, WM_KILLFOCUS, 0, 0));
    q->focus = 0;
  }
}

// ---------------------------------------------------------------------------
// Cursors

struct Cursor : Obj {
  int  id;		// IDC_ resource id
  Cursor(int i) : Obj(K_CURSOR), id(i) {}
};

static NSCursor* ns_cursor(int id) {
  switch (id) {
    case 32513: return [NSCursor IBeamCursor];
    case 32515: return [NSCursor crosshairCursor];
    case 32516: return [NSCursor resizeUpCursor];
    case 32644: return [NSCursor resizeLeftRightCursor];
    case 32645: return [NSCursor resizeUpDownCursor];
    case 32646: return [NSCursor openHandCursor];
    case 32648: return [NSCursor operationNotAllowedCursor];
    case 32649: return [NSCursor pointingHandCursor];
  }
  return [NSCursor arrowCursor];	// IDC_ARROW, IDC_WAIT, IDC_APPSTARTING, ...
}

static word current_cursor = 0;

word arrow_cursor() {
  static Cursor* arrow = NULL;
  if (arrow == NULL) { arrow = new Cursor(32512); arrow->stock = true; }
  return arrow->handle;
}

static word LoadCursorA(word instance, word name) {
  if (name >= 0x10000) return arrow_cursor();	// named resources: not available
  GUILocker l;
  static std::map<int, Cursor*> cursors;
  Cursor*& c = cursors[(int) name];
  if (c == NULL) { c = new Cursor((int) name); c->stock = true; }
  return c->handle;
}

word SetCursor_user(word cursor) {
  word old = current_cursor;
  Cursor* c = lookup_as<Cursor>(cursor, K_CURSOR);
  current_cursor = c ? cursor : 0;
  int id = c ? c->id : -1;
  if (old != current_cursor) {
    run_on_main(^{
      if (id < 0) [NSCursor hide];
      else { [NSCursor unhide]; [ns_cursor(id) set]; }
    });
  }
  return old;
}

static word SetCursor(word cursor) { return SetCursor_user(cursor); }

static word LoadIconA(word instance, word name) {
  static Obj* icon = NULL;
  if (icon == NULL) { icon = new Obj(K_ICON); icon->stock = true; }
  return icon->handle;
}

// LoadImageA(hinst, name, type, cx, cy, flags): bitmaps from files (LR_LOADFROMFILE)
static word LoadImageA(word instance, word name, word type, word cx, word cy, word flags) {
  enum { IMAGE_BITMAP = 0, IMAGE_ICON = 1, IMAGE_CURSOR = 2, LR_LOADFROMFILE = 0x10 };
  if (type == IMAGE_ICON)   return LoadIconA(instance, name);
  if (type == IMAGE_CURSOR) return arrow_cursor();
  if (name < 0x10000 || !(flags & LR_LOADFROMFILE)) return 0;
  char path[PATH_MAX];
  win32_posix_path((const char*) name, path, sizeof path);
  Bitmap* b = gdi_load_bmp(path);
  if (b == NULL && TraceGUI) fprintf(stderr, "Strongtalk GUI: LoadImageA: cannot load %s\n", path);
  return b ? b->handle : 0;
}

// ---------------------------------------------------------------------------
// System metrics

static word GetSystemMetrics(word index) {
  switch ((int) index) {
    case 0:  return screen_width;		// SM_CXSCREEN
    case 1:  return screen_height;		// SM_CYSCREEN
    case 2:  case 3: return scroll_bar_size;	// SM_CXVSCROLL, SM_CYHSCROLL
    case 4:  return 22;				// SM_CYCAPTION
    case 5:  case 6: return 1;			// SM_CXBORDER, SM_CYBORDER
    case 7:  case 8: return 3;			// SM_CXDLGFRAME
    case 9:  case 10: return scroll_bar_size;	// SM_CYVTHUMB, SM_CXHTHUMB
    case 11: case 12: return 32;		// SM_CXICON
    case 13: case 14: return 32;		// SM_CXCURSOR
    case 15: return 0;				// SM_CYMENU: the menu bar is not in the window
    case 16: return screen_width;		// SM_CXFULLSCREEN
    case 17: return screen_height - 30;
    case 20: case 21: return scroll_bar_size;	// SM_CYVSCROLL, SM_CXHSCROLL
    case 28: case 29: return 100;		// SM_CXMIN
    case 32: case 33: return 4;			// SM_CXFRAME
    case 36: case 37: return 4;			// SM_CXDOUBLECLK
    case 49: case 50: return 16;		// SM_CXSMICON
    case 43: return 3;				// SM_CMOUSEBUTTONS
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Menus

struct MenuItem {
  uint32_t    flags;		// MF_ flags
  word        id;		// command id
  word        submenu;		// HMENU
  std::string text;
  word        bitmap;		// MF_BITMAP
  word        checked_bitmap, unchecked_bitmap;
};

struct Menu : Obj {
  std::vector<MenuItem> items;
  bool popup;
  Menu(bool p) : Obj(K_MENU), popup(p) {}
};

enum { MF_GRAYED = 0x1, MF_DISABLED = 0x2, MF_BITMAP = 0x4, MF_CHECKED = 0x8, MF_POPUP = 0x10,
       MF_MENUBARBREAK = 0x20, MF_MENUBREAK = 0x40, MF_OWNERDRAW = 0x100, MF_SEPARATOR = 0x800,
       MF_BYPOSITION = 0x400 };

static word CreateMenu()      { return (new Menu(false))->handle; }
static word CreatePopupMenu() { return (new Menu(true))->handle; }

static word DestroyMenu(word hmenu) {
  Menu* m = lookup_as<Menu>(hmenu, K_MENU);
  if (m == NULL) return 0;
  for (MenuItem& i : m->items) if (i.flags & MF_POPUP) DestroyMenu(i.submenu);
  delete m;
  return 1;
}

static void schedule_menu_bar_update();

static MenuItem make_item(word flags, word id, word item) {
  MenuItem i;
  i.flags = (uint32_t) flags;
  i.id = (i.flags & MF_POPUP) ? 0 : id;
  i.submenu = (i.flags & MF_POPUP) ? id : 0;
  i.bitmap = 0;
  i.checked_bitmap = i.unchecked_bitmap = 0;
  if (i.flags & MF_BITMAP)                              i.bitmap = item;
  else if (!(i.flags & (MF_SEPARATOR | MF_OWNERDRAW)) && item >= 0x10000) i.text = (const char*) item;
  return i;
}

static word AppendMenuA(word hmenu, word flags, word id, word item) {
  Menu* m = lookup_as<Menu>(hmenu, K_MENU);
  if (m == NULL) return 0;
  GUILocker l;
  m->items.push_back(make_item(flags, id, item));
  schedule_menu_bar_update();
  return 1;
}

// the index of an item by command id (searching submenus) or position; menu set to its menu
static int find_item(Menu*& m, word item, bool by_position) {
  if (by_position) return item < (word) m->items.size() ? (int) item : -1;
  for (size_t i = 0; i < m->items.size(); i++) if (!(m->items[i].flags & MF_POPUP) && m->items[i].id == item) return (int) i;
  for (size_t i = 0; i < m->items.size(); i++) {
    if (!(m->items[i].flags & MF_POPUP)) continue;
    Menu* sub = lookup_as<Menu>(m->items[i].submenu, K_MENU);
    if (sub == NULL) continue;
    int index = find_item(sub, item, false);
    if (index >= 0) { m = sub; return index; }
  }
  return -1;
}

static word ModifyMenuA(word hmenu, word position, word flags, word id, word item) {
  Menu* m = lookup_as<Menu>(hmenu, K_MENU);
  if (m == NULL) return 0;
  GUILocker l;
  int index = find_item(m, position, (flags & MF_BYPOSITION) != 0);
  if (index < 0) return 0;
  MenuItem old = m->items[index];
  m->items[index] = make_item(flags & ~MF_BYPOSITION, id, item);
  m->items[index].checked_bitmap = old.checked_bitmap;
  m->items[index].unchecked_bitmap = old.unchecked_bitmap;
  schedule_menu_bar_update();
  return 1;
}

static word RemoveMenu(word hmenu, word position, word flags) {
  Menu* m = lookup_as<Menu>(hmenu, K_MENU);
  if (m == NULL) return 0;
  GUILocker l;
  int index = find_item(m, position, (flags & MF_BYPOSITION) != 0);
  if (index < 0) return 0;
  m->items.erase(m->items.begin() + index);
  schedule_menu_bar_update();
  return 1;
}

static word SetMenuItemBitmaps(word hmenu, word position, word flags, word unchecked, word checked) {
  Menu* m = lookup_as<Menu>(hmenu, K_MENU);
  if (m == NULL) return 0;
  GUILocker l;
  int index = find_item(m, position, (flags & MF_BYPOSITION) != 0);
  if (index < 0) return 0;
  m->items[index].checked_bitmap = checked;
  m->items[index].unchecked_bitmap = unchecked;
  return 1;
}

// MENUITEMINFOA (32-bit): cbSize, fMask, fType, fState, wID, hSubMenu, hbmpChecked,
// hbmpUnchecked, dwItemData, dwTypeData, cch
enum { MIIM_STATE = 0x1, MIIM_ID = 0x2, MIIM_SUBMENU = 0x4, MIIM_CHECKMARKS = 0x8, MIIM_TYPE = 0x10,
       MIIM_DATA = 0x20, MIIM_STRING = 0x40, MIIM_BITMAP = 0x80, MIIM_FTYPE = 0x100 };
enum { MFT_BITMAP = 0x4, MFT_SEPARATOR = 0x800, MFT_OWNERDRAW = 0x100, MFS_GRAYED = 0x3, MFS_CHECKED = 0x8 };

static void apply_item_info(MenuItem& i, const uint32_t* f) {
  uint32_t mask = f[1];
  if (mask & (MIIM_TYPE | MIIM_FTYPE)) {
    i.flags &= ~(MF_BITMAP | MF_SEPARATOR | MF_OWNERDRAW);
    i.flags |= f[2] & (MFT_BITMAP | MFT_SEPARATOR | MFT_OWNERDRAW);
  }
  if (mask & MIIM_STATE) {
    i.flags &= ~(MF_GRAYED | MF_DISABLED | MF_CHECKED);
    i.flags |= f[3] & (MFS_GRAYED | MFS_CHECKED);
  }
  if (mask & MIIM_ID)      i.id = f[4];
  if (mask & MIIM_SUBMENU) {
    i.submenu = f[5];
    if (i.submenu) i.flags |= MF_POPUP; else i.flags &= ~MF_POPUP;
  }
  if (mask & MIIM_CHECKMARKS) { i.checked_bitmap = f[6]; i.unchecked_bitmap = f[7]; }
  if ((mask & (MIIM_TYPE | MIIM_STRING)) && !(i.flags & (MF_BITMAP | MF_SEPARATOR | MF_OWNERDRAW))) {
    const char* s = (const char*) external_pointer(f[9]);
    i.text = s ? s : "";
  }
  if ((mask & MIIM_TYPE) && (i.flags & MF_BITMAP)) i.bitmap = f[9];
}

static word InsertMenuItemA(word hmenu, word item, word by_position, word info) {
  Menu* m = lookup_as<Menu>(hmenu, K_MENU);
  if (m == NULL) return 0;
  GUILocker l;
  MenuItem i = make_item(0, 0, 0);
  apply_item_info(i, (const uint32_t*) info);
  int index = by_position ? (int) std::min<word>(item, m->items.size()) : find_item(m, item, false);
  if (index < 0) index = (int) m->items.size();
  m->items.insert(m->items.begin() + index, i);
  schedule_menu_bar_update();
  return 1;
}

static word SetMenuItemInfoA(word hmenu, word item, word by_position, word info) {
  Menu* m = lookup_as<Menu>(hmenu, K_MENU);
  if (m == NULL) return 0;
  GUILocker l;
  int index = find_item(m, item, by_position != 0);
  if (index < 0) return 0;
  apply_item_info(m->items[index], (const uint32_t*) info);
  schedule_menu_bar_update();
  return 1;
}

static word SetMenu(word hwnd, word hmenu) {
  Window* w = window(hwnd);
  if (w == NULL) return 0;
  { GUILocker l; w->menu = hmenu; }
  schedule_menu_bar_update();
  return 1;
}

static word DrawMenuBar(word hwnd) {
  schedule_menu_bar_update();
  return 1;
}

static word menu_bar_owner = 0;	// the window whose menu bar is shown (main thread)

} // namespace w32

@interface STMenuItemInfo : NSObject {
@public
  HANDLE menu;		// the HMENU of a submenu
  word   id;
}
@end
@implementation STMenuItemInfo
@end

namespace w32 {

static NSImage* ns_image(word hbitmap) {
  Bitmap* b = lookup_as<Bitmap>(hbitmap, K_BITMAP);
  if (b == NULL) return nil;
  CGImageRef img = CGBitmapContextCreateImage(b->ctx);
  NSImage* image = [[NSImage alloc] initWithCGImage: img size: NSMakeSize(b->width, b->height)];
  CGImageRelease(img);
  return image;
}

// Fills nsmenu with the items of hmenu (main thread). owner: the window getting WM_COMMAND.
static void fill_ns_menu(NSMenu* nsmenu, word hmenu, word owner) {
  [nsmenu removeAllItems];
  nsmenu.autoenablesItems = NO;
  GUILocker l;
  Menu* m = lookup_as<Menu>(hmenu, K_MENU);
  if (m == NULL) return;
  for (MenuItem& i : m->items) {
    if (i.flags & MF_SEPARATOR) { [nsmenu addItem: [NSMenuItem separatorItem]]; continue; }
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle: menu_title(i.text) action: NULL keyEquivalent: @""];
    if (i.flags & MF_BITMAP) {
      NSImage* image = ns_image(i.bitmap);
      if (image) { item.image = image; item.title = @""; }
    }
    STMenuItemInfo* info = [[STMenuItemInfo alloc] init];
    info->id = i.id;
    info->menu = (HANDLE) owner;
    item.representedObject = info;
    item.enabled = !(i.flags & (MF_GRAYED | MF_DISABLED));
    item.state = (i.flags & MF_CHECKED) ? NSControlStateValueOn : NSControlStateValueOff;
    if (i.flags & MF_POPUP) {
      NSMenu* sub = [[NSMenu alloc] initWithTitle: item.title];
      sub.delegate = menu_target;
      STMenuItemInfo* subinfo = [[STMenuItemInfo alloc] init];
      subinfo->menu = (HANDLE) i.submenu;
      subinfo->id = owner;
      objc_setAssociatedObject(sub, "info", subinfo, OBJC_ASSOCIATION_RETAIN);
      item.submenu = sub;
      gui_unlock();
      fill_ns_menu(sub, i.submenu, owner);
      gui_lock();
    } else {
      item.target = menu_target;
      item.action = @selector(menuItemSelected:);
    }
    [nsmenu addItem: item];
  }
}

// The main menu: the application menu, then the menu bar of the key window (main thread)
static void update_main_menu() {
  NSMenu* main = [[NSMenu alloc] initWithTitle: @"Main"];
  NSMenuItem* app_item = [[NSMenuItem alloc] initWithTitle: @"Strongtalk" action: NULL keyEquivalent: @""];
  NSMenu* app_menu = [[NSMenu alloc] initWithTitle: @"Strongtalk"];
  [app_menu addItemWithTitle: @"About Strongtalk" action: @selector(orderFrontStandardAboutPanel:) keyEquivalent: @""];
  [app_menu addItem: [NSMenuItem separatorItem]];
  [app_menu addItemWithTitle: @"Hide Strongtalk" action: @selector(hide:) keyEquivalent: @"h"];
  [app_menu addItem: [NSMenuItem separatorItem]];
  NSMenuItem* dump = [app_menu addItemWithTitle: @"Dump GUI State" action: @selector(dumpGUIState:) keyEquivalent: @""];
  dump.target = menu_target;
  [app_menu addItem: [NSMenuItem separatorItem]];
  [app_menu addItemWithTitle: @"Quit Strongtalk" action: @selector(terminate:) keyEquivalent: @"q"];
  app_item.submenu = app_menu;
  [main addItem: app_item];
  word owner = 0, hmenu = 0;
  {
    GUILocker l;
    NSWindow* key = NSApp.keyWindow;
    for (Window* t : top_levels) {
      if (t->menu != 0 && ns_window(t) == key) { owner = t->handle; hmenu = t->menu; break; }
    }
    if (hmenu == 0) {	// the most recently used window with a menu bar
      Window* w = window(menu_bar_owner);
      if (w != NULL && w->menu != 0) { owner = w->handle; hmenu = w->menu; }
    }
  }
  menu_bar_owner = owner;
  if (hmenu != 0) {
    NSMenu* bar = [[NSMenu alloc] init];
    fill_ns_menu(bar, hmenu, owner);
    // the popups of the bar become top-level menus
    for (NSMenuItem* i in [bar.itemArray copy]) {
      [bar removeItem: i];
      if (i.submenu == nil) continue;	// commands in the bar itself: not supported by AppKit
      [main addItem: i];
    }
  }
  NSApp.mainMenu = main;
}

static bool menu_update_scheduled = false;

static void schedule_menu_bar_update() {
  if (!gui_ready) return;
  GUILocker l;
  if (menu_update_scheduled) return;
  menu_update_scheduled = true;
  run_on_main(^{
    { GUILocker l; menu_update_scheduled = false; }
    update_main_menu();
  });
}

static std::string scripted_popup_choice;	// the next TrackPopupMenu chooses this item (GUI scripts)

static std::string plain_text(const std::string& text) {	// without '&' and the accelerator
  std::string s;
  for (size_t i = 0; i < text.size(); i++) {
    if (text[i] == '\t') break;
    if (text[i] == '&') { if (i + 1 < text.size() && text[i + 1] == '&') { s += '&'; i++; } continue; }
    s += text[i];
  }
  return s;
}

// the command id of the item with that text in m or its submenus (0 if none); popup: its menu
static word find_item_by_text(Menu* m, const std::string& text, word* popup = NULL) {
  for (MenuItem& i : m->items) {
    if (!(i.flags & MF_POPUP) && plain_text(i.text) == text) { if (popup) *popup = m->handle; return i.id; }
  }
  for (MenuItem& i : m->items) {
    if (!(i.flags & MF_POPUP)) continue;
    Menu* sub = lookup_as<Menu>(i.submenu, K_MENU);
    if (sub == NULL) continue;
    word id = find_item_by_text(sub, text, popup);
    if (id != 0) return id;
  }
  return 0;
}

// TrackPopupMenu(hmenu, flags, x, y, reserved, hwnd, rect)
static word TrackPopupMenu(word hmenu, word flags, word x, word y, word reserved, word hwnd, word rect) {
  enum { TPM_RETURNCMD = 0x100, TPM_NONOTIFY = 0x80 };
  if (lookup_as<Menu>(hmenu, K_MENU) == NULL) return 0;
  send_message(hwnd, WM_INITMENUPOPUP, hmenu, 0);
  __block word selected = 0;
  POINT p = { (int32_t) x, (int32_t) y };
  if (!scripted_popup_choice.empty()) {	// GUI test scripts: choose without showing the menu
    std::string choice = scripted_popup_choice;
    scripted_popup_choice.clear();
    word id = 0;
    {
      GUILocker l;
      Menu* m = lookup_as<Menu>(hmenu, K_MENU);
      if (m != NULL) id = find_item_by_text(m, choice);
    }
    fprintf(stderr, "[script] popup choice \"%s\" -> %ld\n", choice.c_str(), (long) id);
    if (id != 0 && !(flags & TPM_RETURNCMD)) post_message(hwnd, WM_COMMAND, id & 0xFFFF, 0);
    return (flags & TPM_RETURNCMD) ? id : (id != 0);
  }
  run_on_main_sync(^{
    NSMenu* menu = [[NSMenu alloc] init];
    menu.delegate = menu_target;
    fill_ns_menu(menu, hmenu, 0);
    // a selection answers the item instead of posting WM_COMMAND
    objc_setAssociatedObject(menu_target, "popup_result", nil, OBJC_ASSOCIATION_RETAIN);
    NSPoint at = NSMakePoint(p.x, main_screen_height() - p.y);
    [menu popUpMenuPositioningItem: nil atLocation: at inView: nil];
    STMenuItemInfo* chosen = objc_getAssociatedObject(menu_target, "popup_result");
    if (chosen) selected = chosen->id;
  });
  if (selected != 0 && !(flags & TPM_RETURNCMD)) post_message(hwnd, WM_COMMAND, selected & 0xFFFF, 0);
  return (flags & TPM_RETURNCMD) ? selected : (selected != 0);
}

// ---------------------------------------------------------------------------
// Clipboard (CF_TEXT, as a GlobalAlloc block: malloc)

enum { CF_TEXT = 1, CF_OEMTEXT = 7 };
static word clipboard_data = 0;

static word OpenClipboard(word hwnd)  { return 1; }
static word CloseClipboard()           { return 1; }

static word EmptyClipboard() {
  run_on_main_sync(^{ [[NSPasteboard generalPasteboard] clearContents]; });
  return 1;
}

static word SetClipboardData(word format, word data) {
  if (format != CF_TEXT && format != CF_OEMTEXT) return 0;
  std::string text;
  for (const char* p = (const char*) data; p && *p; p++) if (*p != '\r') text += *p;	// CRLF -> LF
  NSString* s = ns_string(text.c_str());
  run_on_main_sync(^{
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    [pb setString: s forType: NSPasteboardTypeString];
  });
  return data;
}

static word IsClipboardFormatAvailable(word format) {
  if (format != CF_TEXT && format != CF_OEMTEXT) return 0;
  __block bool available = false;
  run_on_main_sync(^{ available = [[NSPasteboard generalPasteboard] stringForType: NSPasteboardTypeString] != nil; });
  return available;
}

static word GetClipboardData(word format) {
  if (format != CF_TEXT && format != CF_OEMTEXT) return 0;
  __block std::string text;
  run_on_main_sync(^{
    NSString* s = [[NSPasteboard generalPasteboard] stringForType: NSPasteboardTypeString];
    if (s) text = utf8_to_ansi(s.UTF8String);
  });
  std::string crlf;
  for (char c : text) { if (c == '\n') crlf += '\r'; crlf += c; }
  if (clipboard_data) free((void*) clipboard_data);	// owned by the clipboard (as in Win32)
  char* block = (char*) malloc(crlf.size() + 1);
  memcpy(block, crlf.c_str(), crlf.size() + 1);
  clipboard_data = (word) block;
  return clipboard_data;
}

// ---------------------------------------------------------------------------
// Message boxes

static std::string scripted_alert_choice;	// the next MessageBoxA answers this button (GUI scripts)

static word MessageBoxA(word hwnd, word text, word caption, word type) {
  enum { MB_OK = 0, MB_OKCANCEL = 1, MB_ABORTRETRYIGNORE = 2, MB_YESNOCANCEL = 3, MB_YESNO = 4, MB_RETRYCANCEL = 5 };
  enum { IDOK = 1, IDCANCEL = 2, IDABORT = 3, IDRETRY = 4, IDIGNORE = 5, IDYES = 6, IDNO = 7 };
  NSString* message = ns_string(text ? (const char*) text : "");
  NSString* title   = ns_string(caption ? (const char*) caption : "Strongtalk");
  int buttons = (int) (type & 0xF);
  int icon = (int) (type & 0xF0);
  int default_button = (int) ((type >> 8) & 0xF);
  __block word result = IDOK;
  if (!scripted_alert_choice.empty()) {	// GUI test scripts: answer without showing it
    std::string c = scripted_alert_choice;
    scripted_alert_choice.clear();
    static const struct { const char* name; int id; } ids[] = {
      { "OK", IDOK }, { "Cancel", IDCANCEL }, { "Abort", IDABORT }, { "Retry", IDRETRY }, { "Ignore", IDIGNORE },
      { "Yes", IDYES }, { "No", IDNO }, { NULL, 0 } };
    for (int i = 0; ids[i].name; i++) if (strcasecmp(c.c_str(), ids[i].name) == 0) result = ids[i].id;
    fprintf(stderr, "[script] message box \"%s\": %s -> %ld\n", ansi_to_utf8(text ? (const char*) text : "").c_str(), c.c_str(), (long) result);
    return result;
  }
  // one at a time: nested modal alerts would only return in reverse order
  static pthread_mutex_t alert_mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_lock(&alert_mutex);
  run_on_main_sync(^{
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = title;
    alert.informativeText = message;
    alert.alertStyle = icon == 0x10 ? NSAlertStyleCritical : (icon == 0x30 ? NSAlertStyleWarning : NSAlertStyleInformational);
    std::vector<int> ids;
    auto add = [&](NSString* t, int id) { [alert addButtonWithTitle: t]; ids.push_back(id); };
    switch (buttons) {
      case MB_OKCANCEL:         add(@"OK", IDOK); add(@"Cancel", IDCANCEL); break;
      case MB_ABORTRETRYIGNORE: add(@"Abort", IDABORT); add(@"Retry", IDRETRY); add(@"Ignore", IDIGNORE); break;
      case MB_YESNOCANCEL:      add(@"Yes", IDYES); add(@"No", IDNO); add(@"Cancel", IDCANCEL); break;
      case MB_YESNO:            add(@"Yes", IDYES); add(@"No", IDNO); break;
      case MB_RETRYCANCEL:      add(@"Retry", IDRETRY); add(@"Cancel", IDCANCEL); break;
      default:                  add(@"OK", IDOK); break;
    }
    if (default_button > 0 && default_button < (int) alert.buttons.count) {
      alert.buttons[0].keyEquivalent = @"";
      alert.buttons[default_button].keyEquivalent = @"\r";
    }
    [NSApp activateIgnoringOtherApps: YES];
    NSModalResponse r = [alert runModal];
    int index = (int) (r - NSAlertFirstButtonReturn);
    result = index >= 0 && index < (int) ids.size() ? ids[index] : IDCANCEL;
  });
  pthread_mutex_unlock(&alert_mutex);
  return result;
}

// ---------------------------------------------------------------------------
// COMDLG32: file dialogs

// OPENFILENAMEA (32-bit): lStructSize, hwndOwner, hInstance, lpstrFilter, lpstrCustomFilter,
// nMaxCustFilter, nFilterIndex, lpstrFile, nMaxFile, lpstrFileTitle, nMaxFileTitle,
// lpstrInitialDir, lpstrTitle, Flags, nFileOffset/nFileExtension, lpstrDefExt, ...
static word file_dialog(word ofn, bool save) {
  uint32_t* f = (uint32_t*) ofn;
  const char* filter      = (const char*) external_pointer(f[3]);
  char*       file        = (char*) external_pointer(f[7]);
  uint32_t    max_file    = f[8];
  char*       file_title  = (char*) external_pointer(f[9]);
  uint32_t    max_title   = f[10];
  const char* initial_dir = (const char*) external_pointer(f[11]);
  const char* title       = (const char*) external_pointer(f[12]);
  const char* def_ext     = (const char*) external_pointer(f[15]);
  if (file == NULL || max_file == 0) return 0;
  // the extensions of the selected filter ("*.st;*.txt")
  NSMutableArray<NSString*>* extensions = [NSMutableArray array];
  if (filter != NULL) {
    int index = std::max(1, (int) f[6]);
    const char* p = filter;
    for (int i = 1; *p; i++) {
      const char* pattern = p + strlen(p) + 1;
      if (i == index) {
        std::string pat = pattern;
        size_t start = 0;
        while (start <= pat.size()) {
          size_t end = pat.find(';', start);
          std::string one = pat.substr(start, end == std::string::npos ? std::string::npos : end - start);
          if (one.size() > 2 && one[0] == '*' && one[1] == '.' && one.find('*', 2) == std::string::npos)
            [extensions addObject: [NSString stringWithUTF8String: one.c_str() + 2]];
          if (end == std::string::npos) break;
          start = end + 1;
        }
      }
      if (!*pattern) break;
      p = pattern + strlen(pattern) + 1;
    }
  }
  char dir[PATH_MAX] = "";
  if (initial_dir && *initial_dir) win32_posix_path(initial_dir, dir, sizeof dir);
  char initial_file[PATH_MAX] = "";
  if (*file) {
    win32_posix_path(file, initial_file, sizeof initial_file);
    const char* slash = strrchr(initial_file, '/');
    if (slash != NULL && !*dir) { snprintf(dir, sizeof dir, "%.*s", (int) (slash - initial_file), initial_file); }
  }
  NSString* ns_title = title ? ns_string(title) : nil;
  NSString* ns_dir = *dir ? [NSString stringWithUTF8String: dir] : nil;
  NSString* ns_name = *initial_file ? [[NSString stringWithUTF8String: initial_file] lastPathComponent] : nil;
  __block std::string chosen;
  run_on_main_sync(^{
    NSSavePanel* panel = save ? [NSSavePanel savePanel] : [NSOpenPanel openPanel];
    if (ns_title) panel.title = ns_title;
    if (ns_dir) panel.directoryURL = [NSURL fileURLWithPath: ns_dir isDirectory: YES];
    if (save && ns_name) panel.nameFieldStringValue = ns_name;
    if (!save) ((NSOpenPanel*) panel).allowsMultipleSelection = NO;
    (void) extensions;
    [NSApp activateIgnoringOtherApps: YES];
    if ([panel runModal] == NSModalResponseOK) chosen = panel.URL.path.UTF8String;
  });
  if (chosen.empty()) return 0;
  if (save && def_ext && *def_ext && chosen.find('.', chosen.rfind('/') + 1) == std::string::npos) {
    chosen += "."; chosen += def_ext;
  }
  char win[PATH_MAX];
  win32_windows_path(chosen.c_str(), win, sizeof win);
  if (strlen(win) + 1 > max_file) return 0;
  strcpy(file, win);
  const char* name = strrchr(win, '\\');
  name = name ? name + 1 : win;
  if (file_title && max_title > strlen(name)) strcpy(file_title, name);
  const char* ext = strrchr(name, '.');
  uint16_t* offsets = (uint16_t*) &f[14];
  offsets[0] = (uint16_t) (name - win);
  offsets[1] = (uint16_t) (ext ? ext + 1 - win : strlen(win));
  return 1;
}

static word GetOpenFileNameA(word ofn) { return file_dialog(ofn, false); }
static word GetSaveFileNameA(word ofn) { return file_dialog(ofn, true); }
static word ChooseFontA(word cf)       { return 0; }	// cancelled

// ---------------------------------------------------------------------------
// GUI test scripts: $STRONGTALK_GUI_SCRIPT names a file of commands, run on a thread of
// their own once AppKit runs. Window arguments are substrings of window titles ("" = the
// most recently created visible window); coordinates are client coordinates.
//   wait <ms>                     sleep
//   waitfor <title> [<ms>]        wait until a visible window with that title exists
//   windows                       list the top-level windows (title, rect)
//   dump <name> [<title>]         write the window contents to <name>.png ($STRONGTALK_GUI_DUMP dir)
//   click|dblclick|rclick <title> <x> <y> [shift|ctrl]   mouse click (also into child windows)
//   drag <title> <x0> <y0> <x1> <y1>                     left button drag
//   type <title> <text>           characters (WM_KEYDOWN/WM_CHAR/WM_KEYUP), \n is Return
//   key <title> <vk> [shift|ctrl] a virtual key, e.g. key Launcher 0x28
//   menu <title> <item>           choose a menu bar item (by its text, without '&')
//   popup <title> <item>          the next popup menu (TrackPopupMenu) chooses that item
//   alert - <button>              the next message box answers that button (OK, Yes, No, ...)
//   close <title>                 WM_CLOSE
//   quit                          exit the process

static void dump_gui_state();

static Window* script_window(const std::string& title) {
  GUILocker l;
  Window* found = NULL;
  for (Window* t : top_levels) {
    if (!t->visible || t->destroyed) continue;
    if (title.empty() || title == "-" || t->text.find(title) != std::string::npos) found = t;
  }
  return found;
}

static void write_png(Window* w, const char* path) {
  CGImageRef image;
  {
    GUILocker l;
    if (w->surface == NULL) return;
    image = CGBitmapContextCreateImage(w->surface->ctx);
  }
  CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8*) path, strlen(path), false);
  CGImageDestinationRef dest = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, NULL);
  if (dest) {
    CGImageDestinationAddImage(dest, image, NULL);
    CGImageDestinationFinalize(dest);
    CFRelease(dest);
  }
  CFRelease(url);
  CGImageRelease(image);
}

// a mouse event at client point (x, y) of the window under it in top-level window top
static void script_mouse(Window* top, int x, int y, word msg, uint32_t mods, uint32_t buttons) {
  GUILocker l;
  current_modifiers = mods | buttons;
  NSPoint p = NSMakePoint(x, y);
  HANDLE h = top->handle;
  gui_unlock();
  post_mouse(h, p, msg);
  gui_lock();
}

static void script_key(Window* top, int vk, const std::u16string& chars, uint32_t mods) {
  GUILocker l;
  current_modifiers = mods;
  Window* w = window(key_target(top->handle));
  if (w == NULL || w->queue == NULL) return;
  QMessage down = make_message(w->handle, WM_KEYDOWN, vk, 1);
  down.chars = chars;
  enqueue(w->queue, down);
  enqueue(w->queue, make_message(w->handle, WM_KEYUP, vk, (word) (uint32_t) (1 | (1u << 30) | (1u << 31))));
}

static void* script_thread(void* arg) {
  const char* path = (const char*) arg;
  FILE* f = fopen(path, "r");
  if (f == NULL) { fprintf(stderr, "Strongtalk GUI script: cannot open %s\n", path); return NULL; }
  const char* dir = getenv("STRONGTALK_GUI_DUMP");
  char line[4096];
  while (fgets(line, sizeof line, f) != NULL) {
    line[strcspn(line, "\r\n")] = '\0';
    if (line[0] == '#' || line[0] == '\0') continue;
    char cmd[64] = "", a[1024] = "", b[1024] = "";
    int n1 = 0, n2 = 0, n3 = 0, n4 = 0;
    sscanf(line, "%63s", cmd);
    const char* rest = line + strlen(cmd);
    while (*rest == ' ') rest++;
    fprintf(stderr, "[script] %s\n", line);
    std::string scmd = cmd;
    if (scmd == "wait") { usleep(atoi(rest) * 1000); continue; }
    if (scmd == "quit") { win32_gui_exit(0); }
    if (scmd == "state") {			// state: Dump GUI State, and the queues to stderr
      dump_gui_state();
      FILE* st = fopen("/tmp/strongtalk-gui-state.txt", "r");
      char l2[1024];
      while (st && fgets(l2, sizeof l2, st)) { if (strstr(l2, "thread ") && strstr(l2, "messages")) fprintf(stderr, "[state]%s", l2); }
      if (st) fclose(st);
      continue;
    }
    if (scmd == "modal") {			// modal <ms>: a modal loop on the main thread (as an alert or popup menu)
      int ms = atoi(rest);
      run_on_main(^{
        NSDate* end = [NSDate dateWithTimeIntervalSinceNow: ms / 1000.0];
        while ([end timeIntervalSinceNow] > 0)
          [[NSRunLoop currentRunLoop] runMode: NSModalPanelRunLoopMode beforeDate: [NSDate dateWithTimeIntervalSinceNow: 0.05]];
      });
      continue;
    }
    if (scmd == "terminate") { run_on_main(^{ [NSApp terminate: nil]; }); continue; }	// Command-Q
    if (scmd == "windows") {
      GUILocker l;
      for (Window* t : top_levels)
        fprintf(stderr, "[window 0x%x \"%s\" %d,%d %dx%d%s]\n", t->handle, t->text.c_str(), t->rect.left, t->rect.top,
                t->rect.right - t->rect.left, t->rect.bottom - t->rect.top, t->visible ? "" : " hidden");
      continue;
    }
    // the window title (quoted if it contains blanks), then the arguments
    std::string title;
    if (*rest == '"') {
      const char* e = strchr(rest + 1, '"');
      title = std::string(rest + 1, e ? e - rest - 1 : strlen(rest + 1));
      rest = e ? e + 1 : rest + strlen(rest);
    } else {
      const char* e = strchr(rest, ' ');
      title = std::string(rest, e ? e - rest : strlen(rest));
      rest = e ? e : rest + strlen(rest);
    }
    while (*rest == ' ') rest++;
    if (scmd == "waitfor") {
      int ms = *rest ? atoi(rest) : 20000;
      for (int t = 0; t < ms && script_window(title) == NULL; t += 50) usleep(50000);
      if (script_window(title) == NULL) fprintf(stderr, "[script] no window \"%s\"\n", title.c_str());
      continue;
    }
    if (scmd == "dump") {	// dump <name> [<title>]: the name came first
      std::string name = title, wtitle = rest;
      if (wtitle.size() >= 2 && wtitle.front() == '"' && wtitle.back() == '"') wtitle = wtitle.substr(1, wtitle.size() - 2);
      Window* w = script_window(wtitle);
      if (w == NULL) { fprintf(stderr, "[script] no window \"%s\"\n", wtitle.c_str()); continue; }
      char out[PATH_MAX];
      snprintf(out, sizeof out, "%s/%s.png", dir ? dir : "/tmp", name.c_str());
      write_png(w, out);
      fprintf(stderr, "[script] wrote %s\n", out);
      continue;
    }
    Window* w = script_window(title);
    if (w == NULL) { fprintf(stderr, "[script] no window \"%s\"\n", title.c_str()); continue; }
    uint32_t mods = strstr(rest, "shift") ? KS_SHIFT : 0;
    if (strstr(rest, "ctrl")) mods |= KS_CONTROL;
    if (scmd == "click" || scmd == "dblclick" || scmd == "rclick") {
      sscanf(rest, "%d %d", &n1, &n2);
      bool right = scmd == "rclick";
      word down = right ? WM_RBUTTONDOWN : WM_LBUTTONDOWN, up = right ? WM_RBUTTONUP : WM_LBUTTONUP;
      uint32_t button = right ? KS_RBUTTON : KS_LBUTTON;
      script_mouse(w, n1, n2, WM_MOUSEMOVE, mods, 0);
      script_mouse(w, n1, n2, down, mods, button);
      script_mouse(w, n1, n2, up, mods, 0);
      if (scmd == "dblclick") {
        script_mouse(w, n1, n2, right ? WM_RBUTTONDBLCLK : WM_LBUTTONDBLCLK, mods, button);
        script_mouse(w, n1, n2, up, mods, 0);
      }
    } else if (scmd == "drag") {
      sscanf(rest, "%d %d %d %d", &n1, &n2, &n3, &n4);
      script_mouse(w, n1, n2, WM_MOUSEMOVE, mods, 0);
      script_mouse(w, n1, n2, WM_LBUTTONDOWN, mods, KS_LBUTTON);
      for (int i = 1; i <= 8; i++) {
        usleep(20000);
        script_mouse(w, n1 + (n3 - n1) * i / 8, n2 + (n4 - n2) * i / 8, WM_MOUSEMOVE, mods, KS_LBUTTON);
      }
      script_mouse(w, n3, n4, WM_LBUTTONUP, mods, 0);
    } else if (scmd == "type") {
      for (const char* p = rest; *p; p++) {
        char c = *p;
        if (c == '\\' && p[1] == 'n') { c = '\r'; p++; }
        int vk = c == '\r' ? VK_RETURN : toupper((unsigned char) c);
        std::u16string chars(1, (char16_t) (unsigned char) c);
        script_key(w, vk, chars, (c >= 'A' && c <= 'Z') ? KS_SHIFT : 0);
        usleep(5000);
      }
    } else if (scmd == "key") {
      int vk = (int) strtol(rest, NULL, 0);
      std::u16string chars;
      if (mods & KS_CONTROL) { if (vk >= 'A' && vk <= 'Z') chars += (char16_t) (vk - 'A' + 1); }
      else if (vk == VK_RETURN || vk == VK_BACK || vk == VK_TAB || vk == VK_ESCAPE) chars += (char16_t) vk;
      script_key(w, vk, chars, mods);
    } else if (scmd == "realclick" || scmd == "realclose") {	// through AppKit: NSEvents, performClose:
      sscanf(rest, "%d %d", &n1, &n2);
      HANDLE h = w->handle;
      bool close = scmd == "realclose";
      run_on_main_sync(^{
        NSWindow* nsw = nil;
        { GUILocker l; Window* x = window(h); if (x) nsw = ns_window(x); }
        if (nsw == nil) return;
        if (close) { [nsw performClose: nil]; return; }
        [nsw makeKeyAndOrderFront: nil];
        NSPoint p = NSMakePoint(n1, nsw.contentView.bounds.size.height - n2);	// window coordinates (y up)
        for (int i = 0; i < 2; i++) {
          NSEvent* e = [NSEvent mouseEventWithType: i == 0 ? NSEventTypeLeftMouseDown : NSEventTypeLeftMouseUp
                                          location: p modifierFlags: 0 timestamp: [NSProcessInfo processInfo].systemUptime
                                      windowNumber: nsw.windowNumber context: nil eventNumber: 0 clickCount: 1 pressure: i == 0 ? 1 : 0];
          [nsw sendEvent: e];
        }
      });
    } else if (scmd == "realhover") {		// realhover <title> <ms>: mouse moves (NSEvents) over the window
      int ms = atoi(rest);
      HANDLE h = w->handle;
      for (int t = 0; t < ms; t += 16) {
        int k = t / 16;
        run_on_main(^{
          NSWindow* nsw = nil;
          { GUILocker l; Window* x = window(h); if (x) nsw = ns_window(x); }
          if (nsw == nil) return;
          NSSize sz = nsw.contentView.bounds.size;
          NSPoint p = NSMakePoint(fmod(37.0 * k, sz.width), fmod(23.0 * k, sz.height));
          NSEvent* e = [NSEvent mouseEventWithType: NSEventTypeMouseMoved location: p modifierFlags: 0
                                          timestamp: [NSProcessInfo processInfo].systemUptime windowNumber: nsw.windowNumber
                                            context: nil eventNumber: 0 clickCount: 0 pressure: 0];
          [nsw sendEvent: e];
        });
        usleep(16000);
      }
    } else if (scmd == "resize") {		// resize <title> <w> <h>: as the user (AppKit setFrame)
      sscanf(rest, "%d %d", &n1, &n2);
      HANDLE h = w->handle;
      run_on_main_sync(^{
        NSWindow* nsw = nil;
        { GUILocker l; Window* x = window(h); if (x) nsw = ns_window(x); }
        if (nsw == nil) return;
        NSRect c = [nsw contentRectForFrameRect: nsw.frame];
        c.origin.y += c.size.height - n2;
        c.size = NSMakeSize(n1, n2);
        [nsw setFrame: [nsw frameRectForContentRect: c] display: YES];
      });
    } else if (scmd == "screen") {		// screen <title> <name>: what the view shows, after an AppKit redisplay
      HANDLE h = w->handle;
      std::string name = rest;
      __block CGImageRef shown = NULL;
      run_on_main_sync(^{
        NSWindow* nsw = nil;
        { GUILocker l; Window* x = window(h); if (x) nsw = ns_window(x); }
        if (nsw == nil) return;
        [nsw.contentView setNeedsDisplay: YES];
        [nsw displayIfNeeded];
        [CATransaction flush];
        id c = nsw.contentView.layer.contents;
        if (c != nil && CFGetTypeID((__bridge CFTypeRef) c) == CGImageGetTypeID()) shown = CGImageRetain((__bridge CGImageRef) c);
      });
      char out[PATH_MAX];
      snprintf(out, sizeof out, "%s/%s.png", dir ? dir : "/tmp", name.c_str());
      if (shown == NULL) { fprintf(stderr, "[script] the view of \"%s\" shows no image\n", title.c_str()); continue; }
      CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8*) out, strlen(out), false);
      CGImageDestinationRef dest = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, NULL);
      CGImageDestinationAddImage(dest, shown, NULL);
      CGImageDestinationFinalize(dest);
      CFRelease(dest); CFRelease(url); CGImageRelease(shown);
      fprintf(stderr, "[script] wrote %s\n", out);
    } else if (scmd == "menus") {		// menus <title>: print the menu bar
      GUILocker l;
      std::function<void(word, int)> print = [&](word hmenu, int depth) {
        Menu* m = lookup_as<Menu>(hmenu, K_MENU);
        if (m == NULL) return;
        for (MenuItem& i : m->items) {
          fprintf(stderr, "[menu] %*s%s%s\n", depth * 2, "", (i.flags & MF_SEPARATOR) ? "----" : plain_text(i.text).c_str(),
                  (i.flags & (MF_GRAYED | MF_DISABLED)) ? " (disabled)" : "");
          if (i.flags & MF_POPUP) print(i.submenu, depth + 1);
        }
      };
      print(w->menu, 0);
    } else if (scmd == "menu") {		// menu <title> <item text>: as chosen from the menu bar
      word id = 0, popup = 0;
      HANDLE h = w->handle;
      {
        GUILocker l;
        Menu* m = lookup_as<Menu>(w->menu, K_MENU);
        if (m != NULL) id = find_item_by_text(m, rest, &popup);
      }
      if (id == 0) { fprintf(stderr, "[script] no menu item \"%s\"\n", rest); continue; }
      bool ok;
      send_from_main(h, WM_INITMENUPOPUP, popup, 0, 5000, &ok);
      post_message(h, WM_COMMAND, id & 0xFFFF, 0);
    } else if (scmd == "alert") {		// alert - <button>: the next message box answers it
      GUILocker l;
      scripted_alert_choice = rest;
    } else if (scmd == "popup") {		// popup <title> <item text>: the next popup menu chooses it
      GUILocker l;
      scripted_popup_choice = rest;
    } else if (scmd == "close") {
      post_message(w->handle, WM_CLOSE, 0, 0);
    } else {
      fprintf(stderr, "[script] unknown command %s\n", cmd);
    }
    (void) a; (void) b;
  }
  fclose(f);
  return NULL;
}

static void start_script() {
  const char* path = getenv("STRONGTALK_GUI_SCRIPT");
  if (path == NULL) return;
  pthread_t t;
  pthread_create(&t, NULL, &script_thread, (void*) path);
  pthread_detach(t);
}

// ---------------------------------------------------------------------------
// Diagnostics: "Dump GUI State" (application menu) writes the state of the windows and the
// message queues to /tmp/strongtalk-gui-state.txt and the window contents to /tmp/strongtalk-gui-*.png

static void dump_gui_state() {
  FILE* f = fopen("/tmp/strongtalk-gui-state.txt", "w");
  if (f == NULL) return;
  std::vector<std::pair<HANDLE, std::string>> pngs;
  {
    GUILocker l;
    uint32_t now = tick_count();
    fprintf(f, "Strongtalk GUI state (tick %u)\n\nqueues:\n", now);
    for (Queue* q : queues) {
      fprintf(f, "  thread %u: %zu messages, %zu sent, %zu timers, focus 0x%lx, quit %d, waiting %d, last message 0x%lx (%u ms ago)\n",
              q->thread_id, q->messages.size(), q->sent.size(), q->timers.size(), (long) q->focus, q->quit, q->waiting,
              (long) q->last.message, now - q->last.time);
      for (size_t i = 0; i < q->messages.size() && i < 10; i++)
        fprintf(f, "    0x%lx 0x%04lx 0x%lx 0x%lx\n", (long) q->messages[i].hwnd, (long) q->messages[i].message,
                (long) q->messages[i].wparam, (long) q->messages[i].lparam);
    }
    fprintf(f, "\nwindows:\n");
    std::function<void(Window*, int)> print = [&](Window* w, int depth) {
      fprintf(f, "  %*s0x%x \"%s\" class %s rect %d,%d,%d,%d%s%s update %d,%d,%d,%d thread %u%s\n", depth * 2, "",
              w->handle, w->text.c_str(), w->cls ? w->cls->name.c_str() : "?", w->rect.left, w->rect.top, w->rect.right, w->rect.bottom,
              w->visible ? "" : " hidden", w->enabled ? "" : " disabled", w->update.left, w->update.top, w->update.right, w->update.bottom,
              w->queue ? w->queue->thread_id : 0, w->display_pending ? " display-pending" : "");
      for (Window* c : w->children) print(c, depth + 1);
    };
    for (Window* t : top_levels) {
      print(t, 0);
      char name[64];
      snprintf(name, sizeof name, "/tmp/strongtalk-gui-0x%x.png", t->handle);
      pngs.push_back(std::make_pair(t->handle, std::string(name)));
    }
    fprintf(f, "\ncapture 0x%lx\n", (long) capture_hwnd);
  }
  fclose(f);
  for (auto& p : pngs) { Window* w = window(p.first); if (w) write_png(w, p.second.c_str()); }
  win32_dump_processes("/tmp/strongtalk-gui-state.txt");
}

// ---------------------------------------------------------------------------
// AppKit

static void run_app() {
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy: HiddenGUI ? NSApplicationActivationPolicyAccessory : NSApplicationActivationPolicyRegular];
    STAppDelegate* delegate = [[STAppDelegate alloc] init];
    NSApp.delegate = delegate;
    menu_target = [[STMenuTarget alloc] init];
    update_main_menu();
    [NSApp run];
  }
}

} // namespace w32

@implementation STAppDelegate
- (void) applicationDidFinishLaunching: (NSNotification*) n {
  pthread_mutex_lock(&start_mutex);
  gui_ready = true;
  pthread_cond_broadcast(&start_cond);
  pthread_mutex_unlock(&start_mutex);
  start_script();
}
- (NSApplicationTerminateReply) applicationShouldTerminate: (NSApplication*) app {
  // as closing the Launcher (the first window): the image asks and quits
  word first = 0;
  {
    GUILocker l;
    for (Window* t : top_levels) if (t->visible && !t->destroyed) { first = t->handle; break; }
  }
  if (first == 0) return NSTerminateNow;
  post_message(first, WM_CLOSE, 0, 0);
  return NSTerminateCancel;
}
- (BOOL) applicationShouldTerminateAfterLastWindowClosed: (NSApplication*) app {
  return NO;
}
@end

@implementation STMenuTarget
- (void) dumpGUIState: (id) sender {
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{ dump_gui_state(); });
}
- (void) menuItemSelected: (NSMenuItem*) item {
  STMenuItemInfo* info = item.representedObject;
  if (info == nil) return;
  if (info->menu == 0) {		// TrackPopupMenu
    objc_setAssociatedObject(self, "popup_result", info, OBJC_ASSOCIATION_RETAIN);
    return;
  }
  post_message(info->menu, WM_COMMAND, info->id & 0xFFFF, 0);
}
- (void) menuNeedsUpdate: (NSMenu*) menu {
  STMenuItemInfo* info = objc_getAssociatedObject(menu, "info");
  if (info == nil || info->id == 0) return;
  // let the window update the menu (WM_INITMENUPOPUP), then show its current items
  bool ok;
  send_from_main(info->id, WM_INITMENUPOPUP, info->menu, 0, 1000, &ok);
  fill_ns_menu(menu, info->menu, info->id);
}
@end

@implementation STWindowDelegate
- (BOOL) windowShouldClose: (NSWindow*) sender {
  post_message(hwnd, WM_CLOSE, 0, 0);
  return NO;
}
- (void) windowDidResize: (NSNotification*) n { top_level_moved(hwnd, n.object); }
- (void) windowDidMove: (NSNotification*) n   { top_level_moved(hwnd, n.object); }
- (void) windowDidBecomeKey: (NSNotification*) n {
  if (TraceMessages) fprintf(stderr, "[key window 0x%x]\n", hwnd);
  top_level_activated(hwnd, true);
  menu_bar_owner = hwnd;
  update_main_menu();
}
- (void) windowDidResignKey: (NSNotification*) n { top_level_activated(hwnd, false); }
@end

@implementation STView
- (BOOL) isFlipped               { return YES; }
- (BOOL) acceptsFirstResponder   { return YES; }
- (BOOL) acceptsFirstMouse: (NSEvent*) e { return YES; }
- (void) updateTrackingAreas {
  if (tracking) [self removeTrackingArea: tracking];
  tracking = [[NSTrackingArea alloc] initWithRect: self.bounds
                options: NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect | NSTrackingCursorUpdate
                owner: self userInfo: nil];
  [self addTrackingArea: tracking];
  [super updateTrackingAreas];
}
- (NSPoint) pointOf: (NSEvent*) e { return [self convertPoint: e.locationInWindow fromView: nil]; }
- (void) mouse: (NSEvent*) e message: (word) msg button: (uint32_t) bit down: (bool) down {
  {
    GUILocker l;
    current_modifiers = modifier_state(e.modifierFlags);
    if (down) current_modifiers |= bit; else current_modifiers &= ~bit;
  }
  word m = msg;
  if (down && e.clickCount == 2) {
    Window* w = window(hwnd);
    if (w != NULL) m = msg + 2;		// WM_xBUTTONDBLCLK (the image's windows have CS_DBLCLKS)
  }
  post_mouse(hwnd, [self pointOf: e], m);
}
- (bool) isRightClick: (NSEvent*) e { return (e.modifierFlags & NSEventModifierFlagControl) != 0; }
- (void) mouseDown: (NSEvent*) e {
  if ([self isRightClick: e]) { [self rightMouseDown: e]; return; }
  [self mouse: e message: WM_LBUTTONDOWN button: KS_LBUTTON down: true];
}
- (void) mouseUp: (NSEvent*) e {
  if (current_modifiers & KS_RBUTTON) { [self rightMouseUp: e]; return; }
  [self mouse: e message: WM_LBUTTONUP button: KS_LBUTTON down: false];
}
- (void) rightMouseDown: (NSEvent*) e  { [self mouse: e message: WM_RBUTTONDOWN button: KS_RBUTTON down: true]; }
- (void) rightMouseUp: (NSEvent*) e    { [self mouse: e message: WM_RBUTTONUP button: KS_RBUTTON down: false]; }
- (void) otherMouseDown: (NSEvent*) e  { [self mouse: e message: WM_MBUTTONDOWN button: KS_MBUTTON down: true]; }
- (void) otherMouseUp: (NSEvent*) e    { [self mouse: e message: WM_MBUTTONUP button: KS_MBUTTON down: false]; }
- (void) mouseMoved: (NSEvent*) e {
  { GUILocker l; current_modifiers = modifier_state(e.modifierFlags); }
  post_mouse(hwnd, [self pointOf: e], WM_MOUSEMOVE);
}
- (void) mouseDragged: (NSEvent*) e      { [self mouseMoved: e]; }
- (void) rightMouseDragged: (NSEvent*) e { [self mouseMoved: e]; }
- (void) otherMouseDragged: (NSEvent*) e { [self mouseMoved: e]; }
- (void) cursorUpdate: (NSEvent*) e      { }	// the windows set the cursor (WM_SETCURSOR)
- (void) scrollWheel: (NSEvent*) e {
  CGFloat dy = e.hasPreciseScrollingDeltas ? e.scrollingDeltaY / 8.0 : e.scrollingDeltaY;
  static CGFloat pending = 0;
  pending += dy;
  int notches = (int) pending;
  if (notches == 0) return;
  pending -= notches;
  post_mouse(hwnd, [self pointOf: e], WM_MOUSEWHEEL, (word) (uint32_t) ((notches * 120) << 16));
}
- (void) keyDown: (NSEvent*) e      { post_key(hwnd, e, true); }
- (void) keyUp: (NSEvent*) e        { post_key(hwnd, e, false); }
- (void) flagsChanged: (NSEvent*) e { post_modifier_keys(hwnd, e.modifierFlags); }
- (BOOL) performKeyEquivalent: (NSEvent*) e {
  // Command keys go to the window (Command is Control), except those of the application menu
  NSString* c = e.charactersIgnoringModifiers;
  if ((e.modifierFlags & NSEventModifierFlagCommand) && !([c isEqualToString: @"q"] || [c isEqualToString: @"h"])) {
    if (self.window.isKeyWindow) { post_key(hwnd, e, true); return YES; }
  }
  return [super performKeyEquivalent: e];
}
@end

namespace w32 {

// for the controls (win32Controls.cpp)
word BeginPaint_user(word hwnd, word ps)  { return BeginPaint(hwnd, ps); }
word EndPaint_user(word hwnd, word ps)    { return EndPaint(hwnd, ps); }
word SetCapture_user(word hwnd)           { return SetCapture(hwnd); }
word ReleaseCapture_user()                { return ReleaseCapture(); }
word SetFocus_user(word hwnd)             { return set_focus(hwnd); }
void add_builtin_class(const char* name, uint32_t style, word wndproc) {
  add_class(name, style, wndproc, arrow_cursor(), 0);
}

// ---------------------------------------------------------------------------
// The function tables

extern word FillRect_user(word hdc, word rect, word brush);
extern word DrawFocusRect_user(word hdc, word rect);
extern word GetScrollInfo_user(word hwnd, word bar, word info);
extern word SetScrollInfo_user(word hwnd, word bar, word info, word redraw);
extern word ShowScrollBar_user(word hwnd, word bar, word show);

static word FillRect(word hdc, word rect, word brush)               { return FillRect_user(hdc, rect, brush); }
static word DrawFocusRect(word hdc, word rect)                      { return DrawFocusRect_user(hdc, rect); }
static word GetScrollInfo(word hwnd, word bar, word info)           { return GetScrollInfo_user(hwnd, bar, info); }
static word SetScrollInfo(word hwnd, word bar, word info, word r)   { return SetScrollInfo_user(hwnd, bar, info, r); }
static word ShowScrollBar(word hwnd, word bar, word show)           { return ShowScrollBar_user(hwnd, bar, show); }

} // namespace w32

#define SHIM(f) { #f, (dll_func) &w32::f }
const ShimFunction win32_user32_functions[] = {
  SHIM(AdjustWindowRect), SHIM(AppendMenuA), SHIM(BeginPaint), SHIM(CallWindowProcA), SHIM(ClientToScreen),
  SHIM(CloseClipboard), SHIM(CreateMenu), SHIM(CreatePopupMenu), SHIM(CreateWindowExA), SHIM(DefWindowProcA),
  SHIM(DestroyMenu), SHIM(DestroyWindow), SHIM(DispatchMessageA), SHIM(DrawFocusRect), SHIM(DrawMenuBar),
  SHIM(EmptyClipboard), SHIM(EnableWindow), SHIM(EndPaint), SHIM(FillRect), SHIM(GetClientRect),
  SHIM(GetClipboardData), SHIM(GetDC), SHIM(GetFocus), SHIM(GetKeyState), SHIM(GetMessageA), SHIM(GetParent),
  SHIM(GetScrollInfo), SHIM(GetSystemMetrics), SHIM(GetWindowLongA), SHIM(GetWindowRect), SHIM(InsertMenuItemA),
  SHIM(InvalidateRect), SHIM(IsClipboardFormatAvailable), SHIM(KillTimer), SHIM(LoadCursorA), SHIM(LoadIconA),
  SHIM(LoadImageA), SHIM(MessageBoxA), SHIM(ModifyMenuA), SHIM(MoveWindow), SHIM(OpenClipboard),
  SHIM(PeekMessageA), SHIM(PostMessageA), SHIM(PostQuitMessage), SHIM(PostThreadMessageA), SHIM(RegisterClassA),
  SHIM(ReleaseCapture), SHIM(ReleaseDC), SHIM(RemoveMenu), SHIM(ScreenToClient), SHIM(SendMessageA),
  SHIM(SetCapture), SHIM(SetClipboardData), SHIM(SetCursor), SHIM(SetFocus), SHIM(SetForegroundWindow),
  SHIM(SetMenu), SHIM(SetMenuItemBitmaps), SHIM(SetMenuItemInfoA), SHIM(SetScrollInfo), SHIM(SetTimer),
  SHIM(SetWindowLongA), SHIM(ShowScrollBar), SHIM(ShowWindow), SHIM(TrackPopupMenu),
  SHIM(TranslateMessage),
  { "SetWindowTextA", (dll_func) &w32::SetWindowTextA },
  { "SetWindowText",  (dll_func) &w32::SetWindowTextA },
  { NULL, NULL }
};

const ShimFunction win32_comdlg32_functions[] = {
  SHIM(GetOpenFileNameA), SHIM(GetSaveFileNameA), SHIM(ChooseFontA),
  { NULL, NULL }
};
#undef SHIM

#endif // _WIN32
