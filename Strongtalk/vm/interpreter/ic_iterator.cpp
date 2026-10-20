/* Copyright 1994, 1995, LongView Technologies, L.L.C. $Revision: 1.8 $ */
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
#include "bytecodes.hpp"
#include "iCache.hpp"
#include "klassOop.hpp"
#include "ncode.hpp"
#include "symbolOop.hpp"
#include "key.hpp"
#include "location.hpp"
#include "nativeInstruction.hpp"
#include "runtime.hpp"
#include "compiledIC.hpp"
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
#include "os.hpp"
#include "dll.hpp"
#include "codeIterator.hpp"
#include "ic_iterator.hpp"
#include "interpretedIC.hpp"
#include "compiledPIC.hpp"
// Implementation of IC

IC::IC(CompiledIC* ic)    : _iter(new CompiledIC_Iterator(ic)) {}
IC::IC(InterpretedIC* ic) : _iter(new InterpretedIC_Iterator(ic)) {}

GrowableArray<klassOop>* IC::receiver_klasses() const {
  GrowableArray<klassOop>* result = new GrowableArray<klassOop>();
  IC_Iterator* it = iterator();
  it->init_iteration();
  while (!it->at_end()) {
    result->append(it->klass());
    it->advance();
  }
  return result;
}


void IC::replace(nmethod* nm) {
  Unimplemented();
  IC_Iterator* it = iterator();
  it->init_iteration();
  while (!it->at_end()) {
    // replace if found
    it->advance();
  }
}


void IC::print() {
  char* s;
  switch (shape()) {
    case anamorphic : s = "Anamorphic";  break;
    case monomorphic: s = "Monomorphic"; break;
    case polymorphic: s = "Polymorphic"; break;
    case megamorphic: s = "Megamorphic"; break;
    default         : ShouldNotReachHere();
  }
  stdOut->print("%s IC: %d entries\n", s, number_of_targets());

  IC_Iterator* it = iterator();
  it->init_iteration();
  while (!it->at_end()) {
    lprintf("\t- klass: ");
    it->klass()->print_value();
    if (it->is_interpreted()) {
      lprintf(";\tmethod  %#x\n", it->interpreted_method());
    } else {
      lprintf(";\tnmethod %#x\n", it->compiled_method());
    }
    it->advance();
  }
}

void IC_Iterator::goto_elem(int n) {
  init_iteration();
  for (int i = 0; i < n; i++) advance();
}

methodOop IC_Iterator::interpreted_method(int i) {
  goto_elem(i);
  return interpreted_method();
}

nmethod*  IC_Iterator::compiled_method(int i) {
  goto_elem(i);
  return compiled_method();
}

klassOop  IC_Iterator::klass(int i) {
  goto_elem(i);
  return klass();
}

