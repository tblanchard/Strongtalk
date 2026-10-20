/* Copyright 1994 - 1996 LongView Technologies L.L.C. $Revision: 1.24 $ */
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
#include "bytecodes.hpp"
#include "klassOop.hpp"
#include "ncode.hpp"
#include "symbolOop.hpp"
#include "key.hpp"
#include "location.hpp"
#include "nativeInstruction.hpp"
#include "runtime.hpp"
#include "compiledIC.hpp"
#include "iCache.hpp"
#include "relocInfo.hpp"
#include "codeBuffer.hpp"
#include "assembler.hpp"
#include "x86_mapping.hpp"
#include "nameDesc.hpp"
#include "methodOop.hpp"
#include "scopeDesc.hpp"
#include "jumpTable.hpp"
#include "codeTable.hpp"
#include "zoneHeap.hpp"
#include "zone.hpp"
#include "nmethodScopes.hpp"
#include "nmethod.hpp"
#include "lookupCache.hpp"
#include "objArrayOop.hpp"
#include "delta.hpp"
#include "oopFactory.hpp"
#include "ageTable.hpp"
#include "universe.inline.hpp"
#include "doubleOop.hpp"
#include "vmSymbols.hpp"
#include "oop.inline.hpp"
#include "objectIDTable.hpp"
#include "oop_prims.hpp"
#include "markSweep.hpp"
#include "prim_impl.hpp"
#include "memOop.inline.hpp"
TRACE_FUNC(TraceOopPrims, "oop")

int oopPrimitives::number_of_calls;

PRIM_DECL_2(oopPrimitives::become, oop receiver, oop argument){
  PROLOGUE_2("become", receiver, argument)
  PRIM_NOT_IMPLEMENTED
}

PRIM_DECL_2(oopPrimitives::instVarAt, oop receiver, oop index) {
  PROLOGUE_2("instVarAt", receiver, index)
  if (!receiver->is_mem())
    return markSymbol(vmSymbols::receiver_has_wrong_type());
  if (!index->is_smi())
    return markSymbol(vmSymbols::first_argument_has_wrong_type());

  smi raw_index = smiOop(index)->value() - 1;

  if (!memOop(receiver)->is_within_instVar_bounds(raw_index))
    markSymbol(vmSymbols::out_of_bounds());

  return memOop(receiver)->instVarAt(raw_index);
}

PRIM_DECL_2(oopPrimitives::instance_variable_name_at, oop obj, oop index) {
  PROLOGUE_2("instance_variable_name_at", obj, index)
  if (!obj->is_mem())
    return markSymbol(vmSymbols::receiver_has_wrong_type());
  if (!index->is_smi())
    return markSymbol(vmSymbols::first_argument_has_wrong_type());

  smi raw_index = smiOop(index)->value() - 1;

  if (!memOop(obj)->is_within_instVar_bounds(raw_index))
    markSymbol(vmSymbols::out_of_bounds());

  return obj->blueprint()->inst_var_name_at(raw_index);
}

PRIM_DECL_3(oopPrimitives::instVarAtPut, oop receiver, oop index, oop value) {
  PROLOGUE_3("instVarAtPut", receiver, index, value)
  if (!receiver->is_mem())
    return markSymbol(vmSymbols::receiver_has_wrong_type());
  if (!index->is_smi())
    return markSymbol(vmSymbols::first_argument_has_wrong_type());

  smi raw_index = smiOop(index)->value() - 1;

  if (!memOop(receiver)->is_within_instVar_bounds(raw_index))
    markSymbol(vmSymbols::out_of_bounds());

  return memOop(receiver)->instVarAtPut(raw_index, value);
}

PRIM_DECL_1(oopPrimitives::hash, oop receiver) {
  PROLOGUE_1("hash", receiver)
  return as_smiOop(receiver->identity_hash());
}

PRIM_DECL_1(oopPrimitives::hash_of, oop obj) {
  PROLOGUE_1("hash_of", obj)
  return as_smiOop(obj->identity_hash());
}

PRIM_DECL_1(oopPrimitives::shallowCopy, oop receiver) {
  PROLOGUE_1("shallowCopy", receiver)
  return receiver->shallow_copy(false);
}

PRIM_DECL_1(oopPrimitives::copy_tenured, oop receiver) {
  PROLOGUE_1("copy_tenured", receiver)
  return receiver->shallow_copy(true);
}

PRIM_DECL_2(oopPrimitives::equal, oop receiver, oop argument){ 
  PROLOGUE_2("equal", receiver, argument)
  return receiver == argument ? trueObj : falseObj;
}

PRIM_DECL_2(oopPrimitives::not_equal, oop receiver, oop argument){ 
  PROLOGUE_2("not_equal", receiver, argument)
  return receiver != argument ? trueObj : falseObj;
}

PRIM_DECL_1(oopPrimitives::oop_size, oop receiver) {
  PROLOGUE_1("oop_size", receiver)
  // used for file out (Object>>fileOutSize): methods are filed out in the external (32-bit) code layout
  if (receiver->is_method()) return as_smiOop(methodOop(receiver)->fileout_object_size());
  return as_smiOop(receiver->is_mem() ? memOop(receiver)->size() : 0);
}

PRIM_DECL_1(oopPrimitives::klass, oop receiver) {
  PROLOGUE_1("klass", receiver)
  return receiver->klass();
}

PRIM_DECL_1(oopPrimitives::klass_of, oop obj) {
  PROLOGUE_1("klass_of", obj)
  return obj->klass();
}

PRIM_DECL_1(oopPrimitives::print, oop receiver) {
  PROLOGUE_1("print", receiver)
  ResourceMark rm;
  receiver->print();
  return receiver;
}

PRIM_DECL_1(oopPrimitives::printValue, oop receiver) {
  PROLOGUE_1("printString", receiver)
  ResourceMark rm;
  receiver->print_value();
  return receiver;
}

PRIM_DECL_1(oopPrimitives::asObjectID, oop receiver) {
  PROLOGUE_1("asObjectID", receiver)
  return smiOop(objectIDTable::insert(receiver)); 
}

PRIM_DECL_2(oopPrimitives::perform, oop receiver, oop selector) {
  PROLOGUE_2("perform", receiver, selector);
  return Delta::call(receiver, selector);
}

PRIM_DECL_3(oopPrimitives::performWith, oop receiver, oop selector, oop arg1) {
  PROLOGUE_3("performWith", receiver, selector, arg1);
  return Delta::call(receiver, selector, arg1);
}

PRIM_DECL_4(oopPrimitives::performWithWith, oop receiver, oop selector, oop arg1, oop arg2) {
  PROLOGUE_4("performWithWith", receiver, selector, arg1, arg2);
  return Delta::call(receiver, selector, arg1, arg2);
}

PRIM_DECL_5(oopPrimitives::performWithWithWith, oop receiver, oop selector, oop arg1, oop arg2, oop arg3) {
  PROLOGUE_5("performWithWithWith", receiver, selector, arg1, arg2, arg3);
  return Delta::call(receiver, selector, arg1, arg2, arg3);
}

PRIM_DECL_3(oopPrimitives::performArguments, oop receiver, oop selector, oop args) {
  PROLOGUE_3("performArguments", receiver, selector, args);
  if (!args->is_objArray())
    return markSymbol(vmSymbols::third_argument_has_wrong_type());
  return Delta::call(receiver, selector, objArrayOop(args));
}
