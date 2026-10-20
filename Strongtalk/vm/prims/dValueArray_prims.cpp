/* Copyright 1996 LongView Technologies L.L.C. $Revision: 1.2 $ */
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
#include "dValueArray_prims.hpp"
#include "symbolOop.hpp"
#include "klassOop.hpp"
#include "dValueArrayOop.hpp"
#include "oopFactory.hpp"
#include "universe.inline.hpp"
#include "doubleOop.hpp"
#include "vmSymbols.hpp"
#include "oop.inline.hpp"
#include "prim_impl.hpp"
TRACE_FUNC(TraceDoubleValueArrayPrims, "doubleValueArray")

int doubleValueArrayPrimitives::number_of_calls;

#define ASSERT_RECEIVER assert(receiver->is_doubleValueArray(), "receiver must be double value array")

PRIM_DECL_2(doubleValueArrayPrimitives::allocateSize, oop receiver, oop argument) {
  PROLOGUE_2("allocateSize", receiver, argument)
  assert(receiver->is_klass() && klassOop(receiver)->klass_part()->oop_is_doubleValueArray(),
         "receiver must double byte array class");
  if (!argument->is_smi())
    markSymbol(vmSymbols::first_argument_has_wrong_type());

  if (smiOop(argument)->value() < 0)
    return markSymbol(vmSymbols::negative_size());
  if (smiOop(argument)->value() > max_array_bytes / sizeof(double))
    return markSymbol(vmSymbols::argument_is_invalid());

  smi length = smiOop(argument)->value();

  klassOop k        = klassOop(receiver);
  int      ni_size  = k->klass_part()->non_indexable_size();
  int      obj_size = ni_size + 1 + roundTo(length * sizeof(double), oopSize) / oopSize;
  // allocate
  doubleValueArrayOop obj = as_doubleValueArrayOop(Universe::allocate(obj_size, (memOop*)&k));
  // header
  memOop(obj)->initialize_header(true, k);
  // instance variables
  memOop(obj)->initialize_body(memOopDesc::header_size(), ni_size);
  obj->set_length(length);
  for (int index = 1; index <= length; index++) {
    obj->double_at_put(index, 0.0);
  }
  return obj;
}

PRIM_DECL_1(doubleValueArrayPrimitives::size, oop receiver) {
  PROLOGUE_1("size", receiver);
  ASSERT_RECEIVER;

  // do the operation
  return as_smiOop(doubleValueArrayOop(receiver)->length());
}

PRIM_DECL_2(doubleValueArrayPrimitives::at, oop receiver, oop index) {
  PROLOGUE_2("at", receiver, index);
  ASSERT_RECEIVER;

  // check index type
  if (!index->is_smi())
    return markSymbol(vmSymbols::first_argument_has_wrong_type());

  // check index value
  if (!doubleValueArrayOop(receiver)->is_within_bounds(smiOop(index)->value()))
     return markSymbol(vmSymbols::out_of_bounds());

  return oopFactory::new_double(doubleValueArrayOop(receiver)->double_at(smiOop(index)->value()));
}

PRIM_DECL_3(doubleValueArrayPrimitives::atPut, oop receiver, oop index, oop value) {
  PROLOGUE_3("atPut", receiver, index, value);
  ASSERT_RECEIVER;

  // check index type
  if (!index->is_smi())
    return markSymbol(vmSymbols::first_argument_has_wrong_type());

  // check value type
  if (!value->is_double())
    return markSymbol(vmSymbols::second_argument_has_wrong_type());

  // check index value
  if (!doubleValueArrayOop(receiver)->is_within_bounds(smiOop(index)->value()))
     return markSymbol(vmSymbols::out_of_bounds());

  // do the operation
  doubleValueArrayOop(receiver)->double_at_put(smiOop(index)->value(), doubleOop(value)->value());
  return receiver;
}

