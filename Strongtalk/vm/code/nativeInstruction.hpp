/* Copyright 1994 - 1996, LongView Technologies, L.L.C. $Revision: 1.4 $ */
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
#ifndef VM_CODE_NATIVEINSTRUCTION_HPP_HPP
#define VM_CODE_NATIVEINSTRUCTION_HPP_HPP


// The base class for differnt kinds of native instruction abstractions.
// Provides the primitive operations to manipulate code relative to this.

class NativeInstruction: ValueObj {
 protected:
  char*	addr_at(int offset) const		{ return (char*)this + offset; }

  char  char_at(int offset) const		{ return *addr_at(offset); }
  int   long_at(int offset) const		{ return *(int*)addr_at(offset); }
  oop   oop_at (int offset) const		{ return *(oop*)addr_at(offset); }

  void  set_char_at(int offset, char c)		{ *addr_at(offset) = c; }
  void  set_long_at(int offset, int  i)		{ *(int*)addr_at(offset) = i; }
  void  set_oop_at (int offset, oop  o)		{ *(oop*)addr_at(offset) = o; }
};


#ifdef STRONGTALK_ARM64

// arm64 code patterns (see assembler_arm64.hpp). The patchable parts are aligned 8-byte
// literals (calls) or the immediates of two instructions without effect (IC info).
// Writes are done within JITWriteScopes.

#include "a64.hpp"

void native_code_write_word(char* addr, intptr_t value);	// in a JITWriteScope
void native_code_write_insn(char* addr, a64::insn i);		// in a JITWriteScope, flushes the instruction

// A Delta call (Assembler::call(entry, rtype)), addressed by its return address:
//   -24: adr x17, ret   -20: str x17, [esp, #-8]!   -16: ldr x16, literal   -12: br x16   -8: literal   0: ret
class NativeCall;
NativeCall* nativeCall_from_relocInfo(char* displacement_address);
NativeCall* nativeCall_from_return_address(char* return_address);

class NativeCall: public NativeInstruction {
 public:
  enum Arm64_specific_constants {
    instruction_size		=   24,
    instruction_offset		=  -24,
    displacement_offset		=   -8,	// the literal holding the destination (relocation address)
    return_address_offset	=    0,
  };

  char* instruction_address() const		{ return addr_at(instruction_offset); }
  char* next_instruction_address() const	{ return addr_at(return_address_offset); }
  char* return_address() const			{ return addr_at(return_address_offset); }
  char* destination() const			{ return *(char**) addr_at(displacement_offset); }
  void  set_destination(char* dest)		{ native_code_write_word(addr_at(displacement_offset), (intptr_t) dest); }

  void  verify();
  void  print();

  // Creation
  friend NativeCall* nativeCall_at(char* address);
  friend NativeCall* nativeCall_from_return_address(char* return_address);
  friend NativeCall* nativeCall_from_relocInfo(char* displacement_address);
};

// address: the start of the call sequence (an alignment nop before it is skipped)
inline NativeCall* nativeCall_at(char* address) {
  if (*(a64::insn*) address == a64::nop()) address += sizeof(a64::insn);
  NativeCall* call = (NativeCall*) (address - NativeCall::instruction_offset);
#ifdef ASSERT
  call->verify();
#endif
  return call;
}

inline NativeCall* nativeCall_from_return_address(char* return_address) {
  NativeCall* call = (NativeCall*) (return_address - NativeCall::return_address_offset);
#ifdef ASSERT
  call->verify();
#endif
  return call;
}

inline NativeCall* nativeCall_from_relocInfo(char* displacement_address) {
  NativeCall* call = (NativeCall*) (displacement_address - NativeCall::displacement_offset);
#ifdef ASSERT
  call->verify();
#endif
  return call;
}


// 32 bits of data in the immediates of two instructions without effect (Assembler::ic_info):
//   0: movz xzr, #lo16   4: movk xzr, #hi16, lsl #16
class NativeTest: public NativeInstruction {
 public:
  enum Arm64_specific_constants {
    instruction_size		=    8,
    instruction_offset		=    0,
    next_instruction_offset	=    8,
  };

  char* instruction_address() const		{ return addr_at(instruction_offset); }
  char* next_instruction_address() const	{ return addr_at(next_instruction_offset); }
  int   data() const {
    a64::insn lo = *(a64::insn*) addr_at(0), hi = *(a64::insn*) addr_at(4);
    return (int) (((lo >> 5) & 0xFFFF) | (((hi >> 5) & 0xFFFF) << 16));
  }
  void  set_data(int x) {
    native_code_write_insn(addr_at(0), a64::movz(a64::zr, x & 0xFFFF, 0));
    native_code_write_insn(addr_at(4), a64::movk(a64::zr, (x >> 16) & 0xFFFF, 1));
  }

  void  verify();
  void  print();

  // Creation
  friend NativeTest* nativeTest_at(char* address);
};

inline NativeTest* nativeTest_at(char* address) {
  NativeTest* test = (NativeTest*) address;
#ifdef ASSERT
  test->verify();
#endif
  return test;
}


class IC_Info: public NativeTest {
 public:
  enum IC_Info_specific_constants {
    number_of_flags	= 8,
    flags_mask		= (1 << number_of_flags) - 1,
  };

  char* NLR_target() const			{ return instruction_address() + (data() >> number_of_flags); }
  int	flags() const				{ return data() & flags_mask; }
  void  set_flags(int flags)			{ set_data((data() & ~flags_mask) | (flags & flags_mask)); }

  // Creation
  friend IC_Info* ic_info_at(char* address);
};

inline IC_Info* ic_info_at(char* address) {
  return (IC_Info*) nativeTest_at(address);
}

#else // x86

// An abstraction for accessing/manipulating native call imm32 instructions.
// (used to manipulate inline caches, primitive & dll calls, etc.)

class NativeCall;
NativeCall* nativeCall_from_relocInfo(char* displacement_address);
NativeCall* nativeCall_from_return_address(char* return_address);
class NativeCall: public NativeInstruction {
 public:
  enum Intel_specific_constants {
    instruction_code		= 0xE8,
    instruction_size		=    5,
    instruction_offset		=   -5,
    displacement_offset		=   -4,
    return_address_offset	=    0,
  };

  char* instruction_address() const		{ return addr_at(instruction_offset); }
  char* next_instruction_address() const	{ return addr_at(return_address_offset); }
  int   displacement() const			{ return long_at(displacement_offset); }
  char* return_address() const			{ return addr_at(return_address_offset); }
  char* destination() const			{ return return_address() + displacement(); }
  void  set_destination(char* dest)		{ set_long_at(displacement_offset, dest - return_address()); }

  void  verify();
  void  print();
  
  // Creation
  friend NativeCall* nativeCall_at(char* address);

  friend NativeCall* nativeCall_from_return_address(char* return_address);

  friend NativeCall* nativeCall_from_relocInfo(char* displacement_address);

};

inline NativeCall* nativeCall_at(char* address)
{
	NativeCall* call = (NativeCall*)(address - NativeCall::instruction_offset);
    #ifdef ASSERT
      call->verify();
    #endif
    return call;
}

inline NativeCall* nativeCall_from_return_address(char* return_address)
{
	NativeCall* call = (NativeCall*)(return_address - NativeCall::return_address_offset);
    #ifdef ASSERT
      call->verify();
    #endif
    return call;
}

inline NativeCall* nativeCall_from_relocInfo(char* displacement_address)
{
	NativeCall* call = (NativeCall*)(displacement_address - NativeCall::displacement_offset);
    #ifdef ASSERT
      call->verify();
    #endif
    return call;
}

// An abstraction for accessing/manipulating native mov reg, imm32 instructions.
// (used to manipulate inlined 32bit data dll calls, etc.)

class NativeMov: public NativeInstruction {
 public:
  enum Intel_specific_constants {
    instruction_code		= 0xB8,
    instruction_size		=    5,
    instruction_offset		=    0,
    data_offset			=    1,
    next_instruction_offset	=    5,
    register_mask		= 0x07,
  };

  char* instruction_address() const		{ return addr_at(instruction_offset); }
  char* next_instruction_address() const	{ return addr_at(next_instruction_offset); }
  int   data() const				{ return long_at(data_offset); }
  void  set_data(int x)				{ set_long_at(data_offset, x); }

  void  verify();
  void  print();
  
  // Creation
  friend NativeMov* nativeMov_at(char* address);

};

inline NativeMov* nativeMov_at(char* address)
{
	NativeMov* test = (NativeMov*)(address - NativeMov::instruction_offset);
    #ifdef ASSERT
      test->verify();
    #endif
    return test;
}


// An abstraction for accessing/manipulating native test eax, imm32 instructions.
// (used to manipulate inlined 32bit data for NLRs, dll calls, etc.)

class NativeTest: public NativeInstruction {
 public:
  enum Intel_specific_constants {
    instruction_code		= 0xA9,
    instruction_size		=    5,
    instruction_offset		=    0,
    data_offset			=    1,
    next_instruction_offset	=    5,
  };

  char* instruction_address() const		{ return addr_at(instruction_offset); }
  char* next_instruction_address() const	{ return addr_at(next_instruction_offset); }
  int   data() const				{ return long_at(data_offset); }
  void  set_data(int x)				{ set_long_at(data_offset, x); }

  void  verify();
  void  print();
  
  // Creation
  friend NativeTest* nativeTest_at(char* address);

};

inline NativeTest* nativeTest_at(char* address)
{
	NativeTest* test = (NativeTest*)(address - NativeTest::instruction_offset);
    #ifdef ASSERT
      test->verify();
    #endif
    return test;
 }


// An abstraction for accessing/manipulating native test eax, imm32 instructions that serve as IC info.

class IC_Info: public NativeTest {
 public:
  enum IC_Info_specific_constants {
    info_offset		= data_offset,
    number_of_flags	= 8,
    flags_mask		= (1 << number_of_flags) - 1,
  };

  char* NLR_target() const			{ return instruction_address() + (data() >> number_of_flags); }
  int	flags() const				{ return data() & flags_mask; }
  void  set_flags(int flags)			{ set_data((data() & ~flags_mask) | (flags & flags_mask)); }

  // Creation
  friend IC_Info* ic_info_at(char* address);

};

inline IC_Info* ic_info_at(char* address)
{
    return (IC_Info*)nativeTest_at(address);
}

#endif // STRONGTALK_ARM64

#endif // VM_CODE_NATIVEINSTRUCTION_HPP_HPP