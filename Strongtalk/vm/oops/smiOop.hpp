/* Copyright 1994, 1995 LongView Technologies L.L.C. $Revision: 1.11 $ */
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
#ifndef VM_OOPS_SMIOOP_HPP
#define VM_OOPS_SMIOOP_HPP


// 0, 1 in smiOop format

# define smiOop_zero  smiOop((smi(0) << Tag_Size) + Int_Tag)
# define smiOop_one   smiOop((smi(1) << Tag_Size) + Int_Tag)

// minimum and maximum smiOops
// (BitsPerWord - Tag_Size - 1 magnitude bits: +/-2^29 on 32-bit, +/-2^61 on 64-bit)
# define smi_min  (-(smi(1) << (BitsPerWord - 3)))
# define smi_max  ( (smi(1) << (BitsPerWord - 3)) - 1)

# define smiOop_min   smiOop((smi_min << Tag_Size) + Int_Tag)
# define smiOop_max   smiOop((smi_max << Tag_Size) + Int_Tag)

smiOop as_smiOop(smi value);
class smiOopDesc: public oopDesc {
 public:
  // constructors
  friend smiOop as_smiOop(smi value);

  // accessors
  // %note: value() depends on an arithmetic shift to preserve sign.
  smi value()      const { return smi((intptr_t)this) >> Tag_Size; }
  
  smi identity_hash() const { return value(); }
  
  // arithmetic
  smiOop increment() { return smiOop(smi((intptr_t)this) + smi((intptr_t)smiOop_one)); }
  smiOop decrement() { return smiOop(smi((intptr_t)this) - smi((intptr_t)smiOop_one)); }
  
  // printing
  void print_on(outputStream* st);

  // operations provided for the optimizing compiler.
  friend smiOop as_byte_count_smiOop(smi value) {
    assert(lowerBits(value, Tag_Size) == 0, "not a legal byte count");
    return smiOop(value + Int_Tag); }

  smi byte_count() const { return smi((intptr_t)this) - Int_Tag;   }
};

inline smiOop as_smiOop(smi value)
{
    // shift as unsigned: left-shifting a negative value is undefined in C++
    return smiOop(smi(uintptr_t(value) << Tag_Size) + Int_Tag);
}
#endif // VM_OOPS_SMIOOP_HPP
