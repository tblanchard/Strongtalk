/* Copyright 1994, 1995 LongView Technologies L.L.C. $Revision: 1.35 */
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
#include "iCache.hpp"
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
#include "klassOop.hpp"
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
#include "slist.hpp"
#include "frame.hpp"
#include "compUtils.hpp"
#include "vmOperations.hpp"
#include "abort.hpp"
#include "inliner.hpp"
#include "compiler.hpp"
#include "evaluator.hpp"
#include "markSweep.hpp"
#include "oop.inline.hpp"
#include "memOop.inline.hpp"
void VM_Operation::evaluate() {
  EventMarker  em("VM operation %s", name());
  ResourceMark rm;
  doit();
}

VM_Genesis::VM_Genesis() {}

void VM_Genesis::doit() {
  ErrorHandler::genesis();
}

void VM_Scavenge::doit() {
  // For debugging gc-problems
  if (false) { 
    ResourceMark rm;
    FlagSetting fs(PrintLongFrames, true);
    frame f = calling_process()->last_frame();
    f.print();
  }

  if (PrintStackAtScavenge) {
    stdOut->print_cr("*** BEFORE ***");
    Processes::print();
  }
  Universe::scavenge(addr);
  if (PrintStackAtScavenge) {
    stdOut->print_cr("*** AFTER ***");
    Processes::print();
    stdOut->print_cr("******");
  }
} 			
 			
void VM_GarbageCollect::doit() {
  *addr = MarkSweep::collect(*addr); 
}

void VM_DeoptimizeStacks::doit() {
  Processes::deoptimize_all();
  CompiledCodeOnly = false;
}

void VM_TerminateProcess::doit() {
  if (calling_process() != target) {	// (only a process terminates itself)
    lprintf("[VM_TerminateProcess: calling process %p is not the target %p]\n", calling_process(), target);
  }
  VMProcess::terminate(target);
  if (calling_process() == target) delete calling_process();
  set_calling_process(NULL);
}

# ifdef DELTA_COMPILER

void VM_OptimizeMethod::doit() {
  if (_method->is_blockMethod()) {
    //Compiler c(closure, scope);
    //return c.compile();  
    compiler_warning("can't recompile block yet");
    _nm = NULL;
    return;
  }
  Compiler c(&_key, _method);
  _nm = c.compile();
}

void VM_OptimizeRScope::doit() {
  Compiler c(_scope);
  _nm = c.compile();
}

void VM_OptimizeBlockMethod::doit() {
  Compiler c(closure, scope);
  nm = c.compile();
}

# endif
