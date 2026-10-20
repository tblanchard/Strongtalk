#ifndef VM_TOPINCLUDES_PLATFORM_HPP
#define VM_TOPINCLUDES_PLATFORM_HPP

// Host platform compatibility.
//
// The VM was originally built with MSVC for 32-bit Windows, where a
// precompiled header supplied the C runtime declarations and a number of
// Microsoft specific names. This header provides the standard equivalents
// so the sources build with clang on 64-bit macOS (arm64).

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <strings.h>
#ifndef _WIN32
#include <unistd.h>
#endif

// Debug builds (Xcode defines DEBUG in the Debug configuration) enable VM assertions.
#if defined(DEBUG) && !defined(ASSERT)
  #define ASSERT 1
#endif

#if defined(__aarch64__) || defined(__arm64__)
  #define STRONGTALK_ARM64 1
#endif

#if defined(__i386__) || defined(_M_IX86)
  // The x86-32 interpreter, assembler and code generator can only execute on x86-32.
  #define STRONGTALK_X86_BACKEND 1
#endif

#ifndef _WIN32
  // Calling convention keywords have no meaning outside 32-bit Windows.
  #define __stdcall
  #define __cdecl

  // MSVC C runtime names
  #define _vsnprintf vsnprintf
  #define _unlink    unlink
  #define _fileno    fileno
  #define _snprintf  snprintf
  #define _stricmp   strcasecmp
  #define _strnicmp  strncasecmp
  inline int _finite(double x) { return isfinite(x); }
  inline int _isnan(double x)  { return isnan(x); }
#endif

// Convert a word (address, oop, ...) to an x86 32-bit immediate/displacement.
inline int32_t x86_imm32(intptr_t value) {
  if (value != (intptr_t) (int32_t) value) abort();   // does not fit: x86-32 code cannot reference it
  return (int32_t) value;
}
inline int32_t x86_imm32(const volatile void* p) { return x86_imm32((intptr_t) p); }
template<class R, class... A> inline int32_t x86_imm32(R (*f)(A...))      { return x86_imm32((intptr_t) f); }
template<class R, class... A> inline int32_t x86_imm32(R (*f)(A..., ...)) { return x86_imm32((intptr_t) f); }

#endif // VM_TOPINCLUDES_PLATFORM_HPP
