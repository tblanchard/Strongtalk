/* Copyright 1994 - 1996, LongView Technologies L.L.C. $Revision: 1.3 $ */
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
#include "nativeInstruction.hpp"
#include "ostream.hpp"
#ifdef STRONGTALK_ARM64

#include "os.hpp"

void native_code_write_word(char* addr, intptr_t value) {
  assert(((intptr_t) addr & 7) == 0, "unaligned code word");
  JITWriteScope write_code;
  *(intptr_t*) addr = value;		// atomic for threads executing the code
}

void native_code_write_insn(char* addr, a64::insn i) {
  { JITWriteScope write_code;
    *(a64::insn*) addr = i;
  }
  os::flush_icache(addr, sizeof(a64::insn));
}

void NativeCall::verify() {
  // a Delta call: adr x17, ret; str x17, [x28, #-8]!; ldr x16, #8; br x16; literal
  a64::insn* p = (a64::insn*) instruction_address();
  if (p[0] == a64::adr(17, instruction_size) && p[1] == a64::ldst_pre(a64::STR, 3, 17, 28, -8) &&
      p[2] == a64::ldr_literal(16, 8) && p[3] == a64::br(16)) return;
  // a C call (MacroAssembler::call_C_sequence, e.g. primitives): ldr x16, #12; blr x16; b #12; literal
  a64::insn* c = (a64::insn*) addr_at(-20);
  if (c[0] == a64::ldr_literal(16, 12) && c[1] == a64::blr(16) && c[2] == a64::b(12)) return;
  fatal1("not a call at %p", return_address());
}

void NativeCall::print() {
  stdOut->print_cr("%p: call %p", instruction_address(), destination());
}

void NativeTest::verify() {
  a64::insn lo = *(a64::insn*) addr_at(0), hi = *(a64::insn*) addr_at(4);
  if ((lo & 0xFFE0001F) != a64::movz(a64::zr, 0, 0) || (hi & 0xFFE0001F) != a64::movk(a64::zr, 0, 1)) {
    fatal1("not an IC info at %p", instruction_address());
  }
}

void NativeTest::print() {
  stdOut->print_cr("%p: info 0x%x", instruction_address(), data());
}

#else // x86

// Implementation of NativeCall

void NativeCall::verify() {
  // make sure code pattern is actually a call imm32 instruction
  if (*(u_char*)instruction_address() != instruction_code) fatal("not a call imm32");
}


void NativeCall::print() {
  stdOut->print_cr("0x%x: call 0x%x", instruction_address(), destination());
}

// Implementation of NativeMov
void NativeMov::verify() {
  // make sure code pattern is actually a mov reg, imm32 instruction
  if ((*(u_char*)instruction_address() & !register_mask) != instruction_code) fatal("not a mov reg, imm32");
}


void NativeMov::print() {
  stdOut->print_cr("0x%x: mov reg, 0x%x", instruction_address(), data());
}

// Implementation of NativeTest
void NativeTest::verify() {
  // make sure code pattern is actually a test eax, imm32 instruction
  if (*(u_char*)instruction_address() != instruction_code) fatal("not a test eax, imm32");
}


void NativeTest::print() {
  stdOut->print_cr("0x%x: test eax, 0x%x", instruction_address(), data());
}

#endif // STRONGTALK_ARM64
