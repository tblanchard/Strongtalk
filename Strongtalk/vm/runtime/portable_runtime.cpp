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

// Portable C++ replacements for the x86-32 assembly runtime routines
// (memory/util_asm.asm, prims/smi_prims_asm.asm, prims/double_prims_asm.asm,
//  prims/behavior_prims_asm.asm, prims/block_prims_asm.asm).
//
// Note on argument order: primitives declared with PRIM_DECL_n receive their
// arguments in reverse order (see prim_def.hpp), i.e. a binary primitive is
// called as f(argument, receiver). The definitions below follow that order.

#include "platform.hpp"
#ifndef STRONGTALK_X86_BACKEND

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
#include "debug.hpp"
#include "oop.hpp"
#include "iterator.hpp"
#include "growableArray.hpp"
#include "process.hpp"
#include "rSet.hpp"
#include "space.hpp"
#include "virtualspace.hpp"
#include "generation.hpp"
#include "generation.inline.hpp"
#include "spaceSize.hpp"
#include "universe.hpp"
#include "smiOop.hpp"
#include "markOop.hpp"
#include "memOop.hpp"
#include "universe.store.hpp"
#include "klass.hpp"
#include "byteArrayOop.hpp"
#include "prim_def.hpp"
#include "symbolOop.hpp"
#include "klassOop.hpp"
#include "oopFactory.hpp"
#include "smi_prims.hpp"
#include "universe.inline.hpp"
#include "doubleOop.hpp"
#include "objArrayOop.hpp"
#include "vmSymbols.hpp"
#include "oop.inline.hpp"
#include "objectIDTable.hpp"
#include "prim_impl.hpp"
#include "blockKlass.hpp"
#include "blockOop.hpp"
#include "block_prims.hpp"
#include "behavior_prims.hpp"
#include "double_prims.hpp"

// ---------------------------------------------------------------------------
// memory/util_asm.asm

// Copies count words from 'from' to 'to' in ascending address order
// (safe for overlapping ranges with to < from).
extern "C" void copy_oops_up(oop* from, oop* to, int count) {
  for (int i = 0; i < count; i++) to[i] = from[i];
}

extern "C" void set_oops(oop* to, int count, oop value) {
  for (int i = 0; i < count; i++) to[i] = value;
}

// ---------------------------------------------------------------------------
// prims/smi_prims_asm.asm (receiver is known to be a smi)

static inline bool fits_smi(smi v) { return smi_min <= v && v <= smi_max; }

extern "C" oop PRIM_API smiOopPrimitives_add(oop argument, oop receiver) {
  if (!argument->is_smi()) return markSymbol(vmSymbols::first_argument_has_wrong_type());
  intptr_t result;	// tagged smis (tag 0) can be added directly
  if (__builtin_add_overflow((intptr_t) receiver, (intptr_t) argument, &result))
    return markSymbol(vmSymbols::smi_overflow());
  return oop(result);
}

extern "C" oop PRIM_API smiOopPrimitives_subtract(oop argument, oop receiver) {
  if (!argument->is_smi()) return markSymbol(vmSymbols::first_argument_has_wrong_type());
  intptr_t result;
  if (__builtin_sub_overflow((intptr_t) receiver, (intptr_t) argument, &result))
    return markSymbol(vmSymbols::smi_overflow());
  return oop(result);
}

extern "C" oop PRIM_API smiOopPrimitives_multiply(oop argument, oop receiver) {
  if (!argument->is_smi()) return markSymbol(vmSymbols::first_argument_has_wrong_type());
  intptr_t result;	// tagged receiver * untagged argument = tagged product
  if (__builtin_mul_overflow((intptr_t) receiver, smiOop(argument)->value(), &result))
    return markSymbol(vmSymbols::smi_overflow());
  return oop(result);
}

// Common checks for the division primitives; returns NULL if x and y are valid.
static inline oop division_check(oop argument) {
  if (argument == smiOop_zero) return markSymbol(vmSymbols::division_by_zero());
  if (!argument->is_smi())     return markSymbol(vmSymbols::first_argument_has_wrong_type());
  return NULL;
}

// x // y, rounded towards negative infinity
extern "C" oop PRIM_API smiOopPrimitives_div(oop argument, oop receiver) {
  if (oop error = division_check(argument)) return error;
  smi x = smiOop(receiver)->value(), y = smiOop(argument)->value();
  smi q = x / y, r = x % y;
  if (r != 0 && ((r ^ y) < 0)) q--;
  if (!fits_smi(q)) return markSymbol(vmSymbols::smi_overflow());
  return as_smiOop(q);
}

// x \\ y, result has the sign of y
extern "C" oop PRIM_API smiOopPrimitives_mod(oop argument, oop receiver) {
  if (oop error = division_check(argument)) return error;
  smi x = smiOop(receiver)->value(), y = smiOop(argument)->value();
  smi r = x % y;
  if (r != 0 && ((r ^ y) < 0)) r += y;
  return as_smiOop(r);
}

// x quo: y, truncated towards zero
extern "C" oop PRIM_API smiOopPrimitives_quo(oop argument, oop receiver) {
  if (oop error = division_check(argument)) return error;
  smi q = smiOop(receiver)->value() / smiOop(argument)->value();
  if (!fits_smi(q)) return markSymbol(vmSymbols::smi_overflow());
  return as_smiOop(q);
}

// x rem: y, result has the sign of x
extern "C" oop PRIM_API smiOopPrimitives_remainder(oop argument, oop receiver) {
  if (oop error = division_check(argument)) return error;
  return as_smiOop(smiOop(receiver)->value() % smiOop(argument)->value());
}

// ---------------------------------------------------------------------------
// prims/double_prims_asm.asm (receiver is known to be a double)

#define DOUBLE_OPERATION(name, op)                                                    \
  extern "C" oop PRIM_API name(oop argument, oop receiver) {                         \
    if (!argument->is_double()) return markSymbol(vmSymbols::first_argument_has_wrong_type()); \
    return oopFactory::new_double(doubleOop(receiver)->value() op doubleOop(argument)->value()); \
  }

DOUBLE_OPERATION(double_add,      +)
DOUBLE_OPERATION(double_subtract, -)
DOUBLE_OPERATION(double_multiply, *)
DOUBLE_OPERATION(double_divide,   /)

extern "C" oop PRIM_API double_from_smi(oop receiver) {
  return oopFactory::new_double((double) smiOop(receiver)->value());
}

// ---------------------------------------------------------------------------
// prims/behavior_prims_asm.asm: instantiate a class with n instance variables (nil filled)

#define PRIMITIVE_NEW(n)                                                              \
  extern "C" oop primitiveNew##n(oop receiver) {                                      \
    return klassOop(receiver)->klass_part()->allocateObject();                        \
  }
PRIMITIVE_NEW(0) PRIMITIVE_NEW(1) PRIMITIVE_NEW(2) PRIMITIVE_NEW(3) PRIMITIVE_NEW(4)
PRIMITIVE_NEW(5) PRIMITIVE_NEW(6) PRIMITIVE_NEW(7) PRIMITIVE_NEW(8) PRIMITIVE_NEW(9)

// ---------------------------------------------------------------------------
// prims/block_prims_asm.asm: block closures and contexts

#define ALLOCATE_BLOCK(n, klass)                                                      \
  extern "C" blockClosureOop allocateBlock##n() {                                     \
    return blockClosureOop(klass->klass_part()->allocateObject());                    \
  }
ALLOCATE_BLOCK(0, zeroArgumentBlockKlassObj)  ALLOCATE_BLOCK(1, oneArgumentBlockKlassObj)
ALLOCATE_BLOCK(2, twoArgumentBlockKlassObj)   ALLOCATE_BLOCK(3, threeArgumentBlockKlassObj)
ALLOCATE_BLOCK(4, fourArgumentBlockKlassObj)  ALLOCATE_BLOCK(5, fiveArgumentBlockKlassObj)
ALLOCATE_BLOCK(6, sixArgumentBlockKlassObj)   ALLOCATE_BLOCK(7, sevenArgumentBlockKlassObj)
ALLOCATE_BLOCK(8, eightArgumentBlockKlassObj) ALLOCATE_BLOCK(9, nineArgumentBlockKlassObj)

// Allocates a context with nofVars temporaries (nil filled) and no home (parent = NULL).
static contextOop new_context(int nofVars) {
  contextOop con = contextKlass::allocate_context(nofVars);
  con->set_parent(oop(NULL));
  return con;
}

extern "C" contextOop allocateContext(smiOop nofVars) { return new_context((int) nofVars->value()); }
extern "C" contextOop allocateContext0()               { return new_context(0); }
extern "C" contextOop allocateContext1()               { return new_context(1); }
extern "C" contextOop allocateContext2()               { return new_context(2); }

#endif // STRONGTALK_X86_BACKEND
