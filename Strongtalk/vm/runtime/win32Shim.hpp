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

#endif // VM_RUNTIME_WIN32SHIM_HPP
