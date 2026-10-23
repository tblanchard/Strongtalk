/* Copyright 1994 - 1996 LongView Technologies L.L.C. $Revision: 1.2 $ */
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
#ifndef VM_RUNTIME_SAVEDREGISTERS_HPP_HPP
#define VM_RUNTIME_SAVEDREGISTERS_HPP_HPP


// SavedRegisters is used for storing the values of data registers
// when entering the vm at special entry points like uncommon_trap
// where the current scope has values in the registers.
//
// Note: stack pointer and frame pointer are not saved - all other
//       (user) register are saved and their value is not destroyed.

class SavedRegisters : AllStatic {
 public:
  static oop  fetch(int register_number, intptr_t* frame_pointer);
  static void clear();
  static void save_registers();
  // for generated code that saves the registers itself (arm64)
  static oop*       register_address(int register_number);
  static intptr_t** stored_frame_pointer_address();
  // GC roots: the saved registers hold the registers of the last Delta frame while it is
  // examined (uncommon traps, deoptimization), which may allocate
  static void oops_do(void f(oop*));
};

// The registers saved by a stub are valid (and GC roots) only during the stub's C call:
// the C entry points (uncommon_trap, nmethod_invocation_counter_overflow) clear them on exit.
// Stale saved values are not oops any more (or never were: the registers may hold interior
// pointers that look like memOops) and must not be visited by the GC.
class SavedRegistersScope : StackObj {
 public:
  ~SavedRegistersScope()	{ SavedRegisters::clear(); }
};


#endif // VM_RUNTIME_SAVEDREGISTERS_HPP_HPP