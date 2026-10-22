/* Copyright 1994 - 1996 LongView Technologies L.L.C. $Revision: 1.5 $ */
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




#include "config.hpp"
#include "error.hpp"
#include "oopsHierarchy.hpp"
#include "longInt.hpp"
#include "types.hpp"
#include "asserts.hpp"
#include "macros.hpp"
#include "bits.hpp"
#include "lprintf.hpp"
#include "util.hpp"
#include "allocation.hpp"
#include "ostream.hpp"
#include "tag.hpp"
#include "top.hpp"
#include "eventLog.hpp"
#include "iterator.hpp"
#include "debug.hpp"
#include "oop.hpp"
#include "savedRegisters.hpp"
#include "growableArray.hpp"
#include "process.hpp"
#include "x86_mapping.hpp"
#include "assembler.hpp"
#include "universe.hpp"
// Need to be static so they can be accessed in assembly code
// of SavedRegisters::save_registers() (compiler doesn't accept
// static class variables).
static intptr_t* stored_frame_pointer = NULL;
static oop  saved_eax;
static oop  saved_ecx;
static oop  saved_edx;
static oop  saved_ebx;
static oop  saved_esi;
static oop  saved_edi;


oop SavedRegisters::fetch(int register_number, intptr_t* frame_pointer) {
  if (frame_pointer != stored_frame_pointer) {
    stdOut->print_cr("Cannot fetch register from non-bottom frame:");
    stdOut->print_cr(" register number = %d, fp = 0x%lx", register_number, frame_pointer);
    fatal("vm aborted");
  }
  if (register_number == eax.number()) return saved_eax;
  if (register_number == ecx.number()) return saved_ecx;
  if (register_number == edx.number()) return saved_edx;
  if (register_number == ebx.number()) return saved_ebx;
  if (register_number == esi.number()) return saved_esi;
  if (register_number == edi.number()) return saved_edi;
  fatal("cannot fetch esp or ebp from saved registers");
  return NULL;
}


oop* SavedRegisters::register_address(int register_number) {
  if (register_number == eax.number()) return &saved_eax;
  if (register_number == ecx.number()) return &saved_ecx;
  if (register_number == edx.number()) return &saved_edx;
  if (register_number == ebx.number()) return &saved_ebx;
  if (register_number == esi.number()) return &saved_esi;
  if (register_number == edi.number()) return &saved_edi;
  fatal("esp and ebp are not saved");
  return NULL;
}

void SavedRegisters::oops_do(void f(oop*)) {
  if (stored_frame_pointer == NULL) return;		// no registers saved
  oop* regs[] = { &saved_eax, &saved_ecx, &saved_edx, &saved_ebx, &saved_esi, &saved_edi };
  for (int i = 0; i < 6; i++) {
    // registers may hold non-oops (e.g. untagged loop values): only heap objects are roots
    oop o = *regs[i];
    if (o->is_mem() && Universe::is_heap((oop*) o)) f(regs[i]);
  }
}

intptr_t** SavedRegisters::stored_frame_pointer_address() {
  return &stored_frame_pointer;
}

void SavedRegisters::clear() {
  stored_frame_pointer = NULL;
}


#if defined(_MSC_VER) && defined(_M_IX86)
#define Naked __declspec( naked )
Naked void SavedRegisters::save_registers() {
  __asm {
    // save the registers
    mov	saved_eax, eax
    mov	saved_ecx, ecx
    mov	saved_edx, edx
    mov	saved_ebx, ebx
    mov	saved_esi, esi
    mov	saved_edi, edi
    // save frame pointer w/o destroying any register contents
    mov eax, last_Delta_fp
    mov stored_frame_pointer, eax
    mov eax, saved_eax
    // return
    ret
  }
}
#undef Naked
#else
void SavedRegisters::save_registers() {
  // Called only from generated x86 code (see StubRoutines); the x86 backend
  // does not run on this architecture.
  fatal("SavedRegisters::save_registers is only available with the x86-32 backend");
}
#endif