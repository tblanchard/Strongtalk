#ifndef VM_RUNTIME_WIN32SHIM_HPP
#define VM_RUNTIME_WIN32SHIM_HPP

// Win32 compatibility for the image on non-Windows hosts (see win32Shim.cpp).

#include "platform.hpp"

class DLL;
typedef void (*dll_func)(...);	// as in os.hpp

void     win32_shim_set_arguments(int argc, char** argv);	// for GetCommandLineA
DLL*     win32_shim_load(const char* name);			// NULL if not a Win32 library
bool     win32_shim_is_library(DLL* library);
dll_func win32_shim_lookup(const char* name, DLL* library);	// NULL if not available

struct ShimFunction { const char* name; dll_func fn; };		// tables end with { NULL, NULL }

#if defined(__APPLE__)
// The GUI libraries (win32User.mm, win32Gdi.cpp)
extern const ShimFunction win32_user32_functions[];
extern const ShimFunction win32_gdi32_functions[];
extern const ShimFunction win32_comdlg32_functions[];

void win32_posix_path(const char* win, char* buffer, size_t size);	// Win32 path -> POSIX
void win32_windows_path(const char* posix, char* buffer, size_t size);	// POSIX path -> Win32

// main() runs vm_main on a VM thread; the main thread runs AppKit once the GUI is used
int  win32_gui_main(int argc, char** argv, int (*vm_main)(int argc, char** argv));
void win32_gui_exit(int code);		// exit (safe while AppKit runs)
#endif

#endif // VM_RUNTIME_WIN32SHIM_HPP
