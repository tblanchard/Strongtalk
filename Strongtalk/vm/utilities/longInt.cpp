/* Copyright 1994, 1995 LongView Technologies L.L.C. $Revision: 1.1 $ */
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
#include "oopsHierarchy.hpp"
#include "longInt.hpp"
#include "types.hpp"
#include "error.hpp"
#include "asserts.hpp"
#include "macros.hpp"
#include "bits.hpp"
#include "lprintf.hpp"
#include "util.hpp"
#include "allocation.hpp"
#include "ostream.hpp"
inline long_int::long_int() {}

long_int::long_int(unsigned int low, unsigned int high) {
  this->low  = low;
  this->high = high;
}

long_int::long_int(double value) {
  long_int result = double_conversion(value);
  low  = result.low;
  high = result.high;
}

long_int long_int::operator +(long_int arg) {
  // Portable replacement for the MSVC inline assembly that added the two
  // 64-bit integers (split into low/high 32-bit halves).
  unsigned long long receiver = ((unsigned long long)high << 32) | low;
  unsigned long long a        = ((unsigned long long)arg.high << 32) | arg.low;
  unsigned long long result   = receiver + a;
  long_int r;
  r.low  = (unsigned int)result;
  r.high = (unsigned int)(result >> 32);
  return r;
}

long_int long_int::operator -(long_int arg) {
  // Portable replacement for the MSVC inline assembly that subtracted the
  // two 64-bit integers (split into low/high 32-bit halves).
  unsigned long long receiver = ((unsigned long long)high << 32) | low;
  unsigned long long a        = ((unsigned long long)arg.high << 32) | arg.low;
  unsigned long long result   = receiver - a;
  long_int r;
  r.low  = (unsigned int)result;
  r.high = (unsigned int)(result >> 32);
  return r;
}

double long_int::as_double() {
  // Portable replacement for the MSVC inline assembly (fild/fstp) that
  // converted the 64-bit integer to a double.
  long long value = (long long)(((unsigned long long)high << 32) | low);
  return (double)value;
}

long_int long_int::double_conversion(double value) {
  // Portable replacement for the MSVC inline assembly (fld/fistp) that
  // converted a double to a 64-bit integer.
  long long v = (long long)value;
  long_int result;
  result.low  = (unsigned int)v;
  result.high = (unsigned int)(v >> 32);
  return result;
}

