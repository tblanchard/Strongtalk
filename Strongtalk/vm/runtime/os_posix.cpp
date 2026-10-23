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

// POSIX / Mach implementation of the os interface (macOS, arm64 and x86_64).
// The Win32 implementation lives in os.cpp.

#ifndef _WIN32

#include "config.hpp"
#include "error.hpp"
#include "lprintf.hpp"
#include "oopsHierarchy.hpp"
#include "longInt.hpp"
#include "types.hpp"
#include "macros.hpp"
#include "os.hpp"
#include "win32Shim.hpp"

#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <libkern/OSCacheControl.h>

// ---------------------------------------------------------------------------
// Threads and events

// The VM refers to threads and events through the opaque Thread*/Event* types.
class Thread {
 public:
  pthread_t   pthread;
  mach_port_t mach_thread;
  int         id;
  int       (*main)(void* parameter);
  void*       parameter;
};

// A manual-reset event (the semantics of a Win32 CreateEvent(NULL, TRUE, ...) event).
class Event {
 public:
  pthread_mutex_t mutex;
  pthread_cond_t  cond;
  bool            signaled;
};

static Thread           main_thread;
static int              next_thread_id = 1;
static pthread_mutex_t  thread_id_lock = PTHREAD_MUTEX_INITIALIZER;
static thread_local int current_id     = 0;

static int new_thread_id() {
  pthread_mutex_lock(&thread_id_lock);
  int id = next_thread_id++;
  pthread_mutex_unlock(&thread_id_lock);
  return id;
}

static void* thread_start(void* arg) {
  Thread* thread = (Thread*) arg;
  current_id = thread->id;
  return (void*) (intptr_t) thread->main(thread->parameter);
}

Thread* os::starting_thread(int* id_addr) {
  *id_addr = main_thread.id;
  return &main_thread;
}

Thread* os::create_thread(int main(void* parameter), void* parameter, int* id_addr) {
  Thread* thread    = (Thread*) calloc(1, sizeof(Thread));	// (the VM forbids the global operator new)
  thread->main      = main;
  thread->parameter = parameter;
  thread->id        = new_thread_id();
  // the portable interpreter nests C frames (call_delta, unwind protects) and the GUI calls
  // system frameworks: more than the default 512 KB
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 16 * 1024 * 1024);
  int failed = pthread_create(&thread->pthread, &attr, &thread_start, thread);
  pthread_attr_destroy(&attr);
  if (failed != 0) {
    free(thread);
    return NULL;
  }
  thread->mach_thread = pthread_mach_thread_np(thread->pthread);
  *id_addr = thread->id;
  return thread;
}

void os::terminate_thread(Thread* thread) {
  if (thread == &main_thread) return;
  pthread_cancel(thread->pthread);
  pthread_detach(thread->pthread);
  free(thread);
}

int os::current_thread_id() {
  return current_id;
}

Event* os::create_event(bool initial_state) {
  Event* event = (Event*) calloc(1, sizeof(Event));
  pthread_mutex_init(&event->mutex, NULL);
  pthread_cond_init(&event->cond, NULL);
  event->signaled = initial_state ? true : false;
  return event;
}

void os::delete_event(Event* event) {
  pthread_cond_destroy(&event->cond);
  pthread_mutex_destroy(&event->mutex);
  free(event);
}

void os::reset_event(Event* event) {
  pthread_mutex_lock(&event->mutex);
  event->signaled = false;
  pthread_mutex_unlock(&event->mutex);
}

void os::signal_event(Event* event) {
  pthread_mutex_lock(&event->mutex);
  event->signaled = true;
  pthread_cond_broadcast(&event->cond);
  pthread_mutex_unlock(&event->mutex);
}

void os::wait_for_event(Event* event) {
  pthread_mutex_lock(&event->mutex);
  while (!event->signaled) pthread_cond_wait(&event->cond, &event->mutex);
  pthread_mutex_unlock(&event->mutex);
}

// Returns true if the wait timed out.
bool os::wait_for_event_or_timer(Event* event, int timeout_in_ms) {
  struct timeval now;
  gettimeofday(&now, NULL);
  struct timespec deadline;
  long long nsec = (long long) now.tv_usec * 1000 + (long long) timeout_in_ms * 1000000;
  deadline.tv_sec  = now.tv_sec + (time_t) (nsec / 1000000000);
  deadline.tv_nsec = (long) (nsec % 1000000000);

  bool timed_out = false;
  pthread_mutex_lock(&event->mutex);
  while (!event->signaled) {
    if (pthread_cond_timedwait(&event->cond, &event->mutex, &deadline) == ETIMEDOUT) {
      timed_out = !event->signaled;
      break;
    }
  }
  pthread_mutex_unlock(&event->mutex);
  return timed_out;
}

void os::transfer(Thread* from_thread, Event* from_event, Thread* to_thread, Event* to_event) {
  reset_event(from_event);
  signal_event(to_event);
  wait_for_event(from_event);
}

void os::transfer_and_continue(Thread* from_thread, Event* from_event, Thread* to_thread, Event* to_event) {
  reset_event(from_event);
  signal_event(to_event);
}

void os::sleep(int ms) {
  usleep((useconds_t) ms * 1000);
}

// Thread support for profiling

void os::suspend_thread(Thread* thread) {
  if (thread == NULL) return;		// no process running yet
  thread_suspend(thread->mach_thread);
}

void os::resume_thread(Thread* thread) {
  if (thread == NULL) return;
  thread_resume(thread->mach_thread);
}

void os::fetch_top_frame(Thread* thread, intptr_t** sp, intptr_t** fp, char** pc) {
  *sp = NULL;
  *fp = NULL;
  *pc = NULL;
#if defined(__arm64__) || defined(__aarch64__)
  arm_thread_state64_t state;
  mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
  if (thread_get_state(thread->mach_thread, ARM_THREAD_STATE64, (thread_state_t) &state, &count) == KERN_SUCCESS) {
    *sp = (intptr_t*) arm_thread_state64_get_sp(state);
    *fp = (intptr_t*) arm_thread_state64_get_fp(state);
    *pc = (char*)     arm_thread_state64_get_pc(state);
  }
#elif defined(__x86_64__)
  x86_thread_state64_t state;
  mach_msg_type_number_t count = x86_THREAD_STATE64_COUNT;
  if (thread_get_state(thread->mach_thread, x86_THREAD_STATE64, (thread_state_t) &state, &count) == KERN_SUCCESS) {
    *sp = (intptr_t*) state.__rsp;
    *fp = (intptr_t*) state.__rbp;
    *pc = (char*)     state.__rip;
  }
#endif
}

// ---------------------------------------------------------------------------
// Timing

static struct rusage process_usage;

// Debugging aid: with $STRONGTALK_FAKE_TIME set, all clocks return a counter that advances by
// 1 ms per query, so that runs are repeatable (with -t, which turns off the timer tasks).
bool st_fake_time_enabled() {
  static int enabled = -1;
  if (enabled < 0) enabled = getenv("STRONGTALK_FAKE_TIME") != NULL;
  return enabled != 0;
}
uint64_t st_fake_micros() {
  static uint64_t now = 1000000000ull * 1000;	// microseconds
  return now += 1000;
}

int os::updateTimes() {
  return getrusage(RUSAGE_SELF, &process_usage) == 0;
}

static inline double timeval_as_double(const struct timeval& t) {
  return (double) t.tv_sec + (double) t.tv_usec / 1000000.0;
}

double os::userTime() {
  if (st_fake_time_enabled()) return (double) st_fake_micros() / 1.0e6;
  return timeval_as_double(process_usage.ru_utime);
}

double os::systemTime() {
  if (st_fake_time_enabled()) return 0.0;
  return timeval_as_double(process_usage.ru_stime);
}

static bool thread_times(Thread* thread, double* user, double* system) {
  mach_port_t port = thread == NULL ? mach_thread_self() : thread->mach_thread;
  thread_basic_info_data_t info;
  mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
  bool ok = thread_info(port, THREAD_BASIC_INFO, (thread_info_t) &info, &count) == KERN_SUCCESS;
  if (thread == NULL) mach_port_deallocate(mach_task_self(), port);
  if (!ok) return false;
  *user   = info.user_time.seconds   + info.user_time.microseconds   / 1000000.0;
  *system = info.system_time.seconds + info.system_time.microseconds / 1000000.0;
  return true;
}

double os::user_time_for(Thread* thread) {
  if (st_fake_time_enabled()) return (double) st_fake_micros() / 1.0e6;
  double user, system;
  return thread_times(thread, &user, &system) ? user : 0.0;
}

double os::system_time_for(Thread* thread) {
  if (st_fake_time_enabled()) return 0.0;
  double user, system;
  return thread_times(thread, &user, &system) ? system : 0.0;
}

static uint64_t                  initial_counter;
static mach_timebase_info_data_t timebase;

// Counter ticks are mach absolute time units (nanoseconds * numer/denom).
long_int os::elapsed_counter() {
  uint64_t count = st_fake_time_enabled() ? st_fake_micros() * 24 : mach_absolute_time();
  return long_int((unsigned int) count, (unsigned int) (count >> 32));
}

long_int os::elapsed_frequency() {
  double ticks_per_second = 1.0e9 * (double) timebase.denom / (double) timebase.numer;
  return long_int(ticks_per_second);
}

double os::elapsedTime() {
  if (st_fake_time_enabled()) return (double) st_fake_micros() / 1.0e6;
  uint64_t ticks = mach_absolute_time() - initial_counter;
  return (double) ticks * timebase.numer / timebase.denom / 1.0e9;
}

double os::currentTime() {
  if (st_fake_time_enabled()) return (double) st_fake_micros() / 1.0e6;
  struct timeval now;
  gettimeofday(&now, NULL);
  return timeval_as_double(now);
}

void os::timerStart()       {}
void os::timerStop()        {}
void os::timerPrintBuffer() {}

// ---------------------------------------------------------------------------
// Virtual memory
//
// reserve: address space only (no access); commit: make readable/writable;
// uncommit: release the pages but keep the reservation.

char* os::reserve_memory(int size) {
  void* result = mmap(NULL, (size_t) size, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
  return result == MAP_FAILED ? NULL : (char*) result;
}

bool os::commit_memory(char* addr, int size) {
  bool result = mprotect(addr, (size_t) size, PROT_READ | PROT_WRITE) == 0;
  if (!result) lprintf("commit_memory error %d\n", errno);
  return result;
}

bool os::uncommit_memory(char* addr, int size) {
  madvise(addr, (size_t) size, MADV_FREE);
  return mprotect(addr, (size_t) size, PROT_NONE) == 0;
}

bool os::release_memory(char* addr, int size) {
  return munmap(addr, (size_t) size) == 0;
}

// Win32 guard pages trap once and then become accessible; here the pages simply trap.
bool os::guard_memory(char* addr, int size) {
  return mprotect(addr, (size_t) size, PROT_NONE) == 0;
}

int os::_vm_page_size = 0;

void os::initialize_system_info() {
  _vm_page_size = (int) getpagesize();
  mach_timebase_info(&timebase);
  initial_counter = mach_absolute_time();
}

// ---------------------------------------------------------------------------
// Miscellaneous

int os::getenv(const char* name, char* buffer, int len) {
  const char* value = ::getenv(name);
  if (value == NULL || len <= 0) return 0;
  strncpy(buffer, value, (size_t) len - 1);
  buffer[len - 1] = '\0';
  return 1;
}

bool os::move_file(char* from, char* to) {
  return rename(from, to) == 0;
}

bool os::check_directory(char* dir_name) {
  if (mkdir(dir_name, 0755) == 0) return true;
  return errno == EEXIST;
}

void os::breakpoint() {
  __builtin_debugtrap();
}

// Without a GUI the question goes to the terminal (answered "no" when not interactive).
int os::message_box(const char* title, const char* message) {
  fprintf(stderr, "\n%s\n%s [y/n] ", title, message);
  fflush(stderr);
  if (::getenv("STRONGTALK_STACK_TRACE") != NULL) {	// non-interactive runs: answer yes
    fprintf(stderr, "y\n");
    return true;
  }
  if (!isatty(fileno(stdin))) {
    fprintf(stderr, "n\n");
    return false;
  }
  char answer[16];
  if (fgets(answer, sizeof answer, stdin) == NULL) return false;
  return answer[0] == 'y' || answer[0] == 'Y';
}

void os::fatalExit(int num) {
  exit(num);
}

void* os::get_hInstance()    { return NULL; }
void* os::get_prevInstance() { return NULL; }
int   os::get_nCmdShow()     { return 1; }	// SW_SHOWNORMAL (the image shows its windows with it)

// DLL support

// The Win32 libraries the image uses are provided by win32Shim.cpp.
dll_func os::dll_lookup(const char* name, DLL* library) {
  if (win32_shim_is_library(library)) return win32_shim_lookup(name, library);
  return (dll_func) dlsym((void*) library, name);
}

DLL* os::dll_load(const char* name) {
  DLL* shim = win32_shim_load(name);
  if (shim != NULL) return shim;
  return (DLL*) dlopen(name, RTLD_LAZY | RTLD_LOCAL);
}

bool os::dll_unload(DLL* library) {
  if (win32_shim_is_library(library)) return true;
  return dlclose((void*) library) == 0;
}

// ---------------------------------------------------------------------------
// ThreadCritical (recursive, like a Win32 CRITICAL_SECTION)

static pthread_mutex_t thread_section;

void ThreadCritical::intialize() {
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&thread_section, &attr);
  pthread_mutexattr_destroy(&attr);
}

void ThreadCritical::release()   { pthread_mutex_destroy(&thread_section); }
ThreadCritical::ThreadCritical()  { pthread_mutex_lock(&thread_section); }
ThreadCritical::~ThreadCritical() { pthread_mutex_unlock(&thread_section); }

// ---------------------------------------------------------------------------
// Signals (replace the Win32 console control handler and exception filter)

extern void intercept_for_single_step();
void process_settings_file(const char* file_name, bool quiet);
void trace_stack_at_exception(intptr_t* sp, intptr_t* fp, char* pc);
void real_time_tick(int delay_time);

static int number_of_ctrl_c = 0;

// Ctrl-\ (SIGQUIT) plays the role of Ctrl-Break: single step into the debugger.
// Ctrl-C (SIGINT) first reads .breakrc, the second time aborts.
static void interrupt_handler(int sig) {
  if (sig == SIGQUIT) {
    lprintf("\n{receiving break}\n");
    intercept_for_single_step();
    return;
  }
  if (number_of_ctrl_c == 0) {
    lprintf("\n{reading .breakrc}");
    process_settings_file(".breakrc", false);
  } else {
    lprintf("\n{aborting}\n");
    __builtin_debugtrap();
  }
  number_of_ctrl_c++;
}

static const char* signal_name(int sig) {
  switch (sig) {
    case SIGSEGV: return "Access violation (SIGSEGV)";
    case SIGBUS:  return "Bus error (SIGBUS)";
    case SIGILL:  return "Illegal instruction (SIGILL)";
    case SIGFPE:  return "Arithmetic exception (SIGFPE)";
    default:      return "Unknown signal";
  }
}

// ---------------------------------------------------------------------------
// Code memory (MAP_JIT): per thread, code memory is either writable or executable.

static const int max_code_areas = 32;
static char* code_area_start[max_code_areas];
static char* code_area_end[max_code_areas];
static int   nof_code_areas = 0;
static volatile int jit_faults = 0;
static thread_local int jit_write_depth = 0;

char* os::allocate_code_memory(int size) {
  ThreadCritical tc;
  if (nof_code_areas == max_code_areas) fatal("too many code areas");
  void* p = mmap(NULL, (size_t) size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
  if (p == MAP_FAILED) return NULL;
  code_area_start[nof_code_areas] = (char*) p;
  code_area_end[nof_code_areas]   = (char*) p + size;
  nof_code_areas++;
  return (char*) p;
}

bool os::is_code_memory(const void* addr) {
  for (int i = 0; i < nof_code_areas; i++) {
    if (code_area_start[i] <= (char*) addr && (char*) addr < code_area_end[i]) return true;
  }
  return false;
}

void os::jit_write_begin() {
  if (jit_write_depth++ == 0) pthread_jit_write_protect_np(0);
}

void os::jit_write_end() {
  if (--jit_write_depth == 0) pthread_jit_write_protect_np(1);
}

int os::jit_mode_faults() {
  return jit_faults;
}

void os::flush_icache(void* start, int size) {
  sys_icache_invalidate(start, (size_t) size);
}

// Switches the thread between writing and executing code memory if the fault is
// caused by the current mode; returns false for other faults.
static bool handle_jit_mode_fault(siginfo_t* info, void* context) {
#if defined(__arm64__) || defined(__aarch64__)
  static thread_local char* last_addr = NULL;
  static thread_local char* last_pc   = NULL;
  char* addr = (char*) info->si_addr;
  if (!os::is_code_memory(addr)) return false;
  ucontext_t* uc = (ucontext_t*) context;
  char* pc = (char*) arm_thread_state64_get_pc(uc->uc_mcontext->__ss);
  if (addr == last_addr && pc == last_pc) {	// switching did not help: a real fault
    last_addr = last_pc = NULL;
    return false;
  }
  last_addr = addr;
  last_pc   = pc;
  jit_faults++;
  pthread_jit_write_protect_np(pc == addr ? 1 : 0);	// fetch: execute; data access: write
  return true;
#else
  return false;
#endif
}

static void fault_handler(int sig, siginfo_t* info, void* context) {
  if ((sig == SIGBUS || sig == SIGSEGV) && handle_jit_mode_fault(info, context)) return;
  lprintf("Exception caught \"%s\" at address %p.\n", signal_name(sig), info->si_addr);
#if defined(__arm64__) || defined(__aarch64__)
  { void* pc = (void*) arm_thread_state64_get_pc(((ucontext_t*) context)->uc_mcontext->__ss);
    Dl_info di;
    if (dladdr(pc, &di) && di.dli_sname != NULL) {
      lprintf("  pc = %p (%s + %ld)%s\n", pc, di.dli_sname, (long) ((char*) pc - (char*) di.dli_saddr),
              os::is_code_memory(info->si_addr) ? ", address is in code memory" : "");
    } else {
      lprintf("  pc = %p%s\n", pc, os::is_code_memory(info->si_addr) ? ", address is in code memory" : "");
    }
  }
#endif
  report_vm_state();

  if (os::message_box("Exception caught", "Do you want a stack trace?")) {
    ucontext_t* uc = (ucontext_t*) context;
#if defined(__arm64__) || defined(__aarch64__)
    trace_stack_at_exception((intptr_t*) arm_thread_state64_get_sp(uc->uc_mcontext->__ss),
                             (intptr_t*) arm_thread_state64_get_fp(uc->uc_mcontext->__ss),
                             (char*)     arm_thread_state64_get_pc(uc->uc_mcontext->__ss));
#elif defined(__x86_64__)
    trace_stack_at_exception((intptr_t*) uc->uc_mcontext->__ss.__rsp,
                             (intptr_t*) uc->uc_mcontext->__ss.__rbp,
                             (char*)     uc->uc_mcontext->__ss.__rip);
#endif
  }
  // Re-raise with the default action so the debugger / crash reporter sees it.
  signal(sig, SIG_DFL);
}

static void install_signal_handlers() {
  // Run fault handlers on an alternate stack so stack overflows can be reported.
  static char alt_stack[64 * 1024];
  stack_t ss;
  ss.ss_sp    = alt_stack;
  ss.ss_size  = sizeof alt_stack;
  ss.ss_flags = 0;
  sigaltstack(&ss, NULL);

  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_sigaction = fault_handler;
  sa.sa_flags     = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGSEGV, &sa, NULL);
  sigaction(SIGBUS,  &sa, NULL);
  sigaction(SIGILL,  &sa, NULL);
  sigaction(SIGFPE,  &sa, NULL);

  struct sigaction ia;
  memset(&ia, 0, sizeof ia);
  ia.sa_handler = interrupt_handler;
  sigemptyset(&ia.sa_mask);
  sigaction(SIGINT,  &ia, NULL);
  sigaction(SIGQUIT, &ia, NULL);
}

// The sole purpose of the watcher thread is simulating timer interrupts.
static void* watcher_main(void*) {
  const int delay_interval = 10; // Delay 10 ms
  while (true) {
    os::sleep(delay_interval);
    real_time_tick(delay_interval);
  }
  return NULL;
}

extern "C" bool EnableTasks;

void os_init() {
  ThreadCritical::intialize();
  os::initialize_system_info();

  main_thread.pthread     = pthread_self();
  main_thread.mach_thread = pthread_mach_thread_np(main_thread.pthread);
  main_thread.id          = new_thread_id();
  main_thread.main        = NULL;
  main_thread.parameter   = NULL;
  current_id              = main_thread.id;

  install_signal_handlers();

  if (EnableTasks) {
    pthread_t watcher;
    pthread_create(&watcher, NULL, &watcher_main, NULL);
    pthread_detach(watcher);
  }
}

void os_exit() {
  ThreadCritical::release();
}

#endif // _WIN32
