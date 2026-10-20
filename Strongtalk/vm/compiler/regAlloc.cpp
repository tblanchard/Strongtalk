/* Copyright 1994, LongView Technologies L.L.C. $Revision: 1.20 $ */
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
#include "klassOop.hpp"
#include "memOopKlass.hpp"
#include "iCache.hpp"
#include "location.hpp"
#include "nativeInstruction.hpp"
#include "runtime.hpp"
#include "compiledIC.hpp"
#include "relocInfo.hpp"
#include "codeBuffer.hpp"
#include "assembler.hpp"
#include "x86_mapping.hpp"
#include "nameDesc.hpp"
#include "byteArrayOop.hpp"
#include "symbolOop.hpp"
#include "key.hpp"
#include "bytecodes.hpp"
#include "methodOop.hpp"
#include "scopeDesc.hpp"
#include "floats.hpp"
#include "os.hpp"
#include "dll.hpp"
#include "codeIterator.hpp"
#include "ic_iterator.hpp"
#include "ncode.hpp"
#include "jumpTable.hpp"
#include "codeTable.hpp"
#include "zoneHeap.hpp"
#include "zone.hpp"
#include "nmethodScopes.hpp"
#include "nmethod.hpp"
#include "lookupCache.hpp"
#include "interpretedIC.hpp"
#include "methodIterator.hpp"
#include "opcode.hpp"
#include "prim.hpp"
#include "nodeBuilder.hpp"
#include "compileTimeClosure.hpp"
#include "slist.hpp"
#include "defUse.hpp"
#include "frame.hpp"
#include "blockOop.hpp"
#include "blockKlass.hpp"
#include "blockOop.inline.hpp"
#include "scopeDescRecorder.hpp"
#include "preg.hpp"
#include "basicBlock.hpp"
#include "expr.hpp"
#include "bitVector.hpp"
#include "registerMask.hpp"
#include "compUtils.hpp"
#include "inliner.hpp"
#include "compiler.hpp"
#include "vframe.hpp"
#include "scope.hpp"
#include "node.hpp"
#include "regAlloc.hpp"
# ifdef DELTA_COMPILER


RegisterAllocator* theAllocator;


static int compare_pregBegs(PReg** a, PReg** b) {
  return (*a)->begBCI() - (*b)->begBCI();
}


static int compare_pregEnds(PReg** a, PReg** b) {
  return (*a)->endBCI() - (*b)->endBCI();
}


RegisterAllocator::RegisterAllocator() {
  theAllocator = this; 
  _stackLocs = new IntFreeList(2);
}


void RegisterAllocator::preAllocate(PReg* r) {
  r->allocateTo(Mapping::localTemporary(_stackLocs->allocate()));
}


void RegisterAllocator::allocate(GrowableArray<PReg*>* globals) {
  GrowableArray<PReg*>* regs = new GrowableArray<PReg*>(globals->length());
  int i = globals->length();
  while (i-- > 0) {
    PReg* r = globals->at(i);
    assert(r->ndefs() + r->nuses() > 0 || r->incorrectDU(), "PReg is unused");
    if (r->loc != unAllocated) {
      // already allocated
    } else if (r->isConstPReg()) {
      // don't allocate constants for now
    } else {
      // collect for later allocation
      regs->append(r);
    }
  }

  int len = regs->length();
  if (len > 0) {
    // sort begBCIs & distribute to scopes
    regs->sort(&compare_pregBegs);
    assert(regs->isEmpty() || regs->first()->begBCI() <= regs->last()->begBCI(), "wrong sort order");
    for (i = 0; i < len; i++) {
      PReg* r = regs->at(i);
      assert(r->begBCI() != IllegalBCI, "illegal begBCI");
      assert(r->endBCI() != IllegalBCI, "illegal endBCI");
      r->scope()->addToPRegsBegSorted(r);
    }

    // sort endBCIs & distribute to scopes
    regs->sort(&compare_pregEnds);
    assert(regs->isEmpty() || regs->first()->endBCI() <= regs->last()->endBCI(), "wrong sort order");
    for (i = 0; i < len; i++) {
      PReg* r = regs->at(i);
      r->scope()->addToPRegsEndSorted(r);
    }
    // do register allocation
    theCompiler->topScope->allocatePRegs(_stackLocs);

    // result
    if (CompilerDebug) cout(PrintRegAlloc)->print("%d (-2) stack locations allocated for %d PRegs\n", _stackLocs->length(), regs->length());
  }
}


# endif // DELTA_COMPILER
