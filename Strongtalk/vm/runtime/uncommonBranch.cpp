/* Copyright 1994, 1995 LongView Technologies L.L.C. $Revision: 1.24 $ */
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
#include "recompiler.hpp"
#include "vmOperations.hpp"
#include "recompile.hpp"
#include "abort.hpp"
#include "frame.hpp"
#include "uncommonBranch.hpp"
#include "slist.hpp"
#include "compUtils.hpp"
#include "blockOop.hpp"
#include "blockKlass.hpp"
#include "blockOop.inline.hpp"
#include "vframe.hpp"
#include "inliner.hpp"
#include "compiler.hpp"
#include "stackChunk.hpp"
#include "stubRoutines.hpp"
#include "savedRegisters.hpp"
#ifdef DELTA_COMPILER

// Tells whether the call has been executed before
// %note:
//    This function is highly INTEL specific.

bool patch_uncommon_call(frame* f) {
  // patch the call site:
  //  from: call _unused_uncommon_trap
  //  to:   call _used_uncommon_trap
#ifdef STRONGTALK_ARM64
  NativeCall* call = nativeCall_from_return_address((char*) f->pc());
  if (call->destination() == (char*)StubRoutines::used_uncommon_trap_entry()) return true;	// executed before
  assert(call->destination() == (char*)StubRoutines::unused_uncommon_trap_entry(), "Make sure we are patching the right call");
  call->set_destination((char*)StubRoutines::used_uncommon_trap_entry());
  return false;
#endif

  // INTEL-SPECIFIC: the call is a rel32 call; its displacement precedes the return address
  char*    next_inst = (char*) f->pc();
  int32_t* dest_addr = ((int32_t*) next_inst) - 1;
  char*    dest      = next_inst + *dest_addr;

  // return true if the call has been executed before
  if (dest == (char*)StubRoutines::used_uncommon_trap_entry())
    return true;

  assert(dest == (char*)StubRoutines::unused_uncommon_trap_entry(), "Make sure we are patching the right call");

  // patch with used_uncommon_trap
  *dest_addr = (int32_t) ((char*)StubRoutines::used_uncommon_trap_entry() - next_inst);

  assert(next_inst + *dest_addr == (char*)StubRoutines::used_uncommon_trap_entry(), "Check the patch");

  // return false since the call is patched
  return false;
}


// Tells whether the frame is a candidate for deoptimization by
// checking if the frame uses contextOops with forward pointers.
static bool has_invalid_context(frame* f) {
  // Return false if we're not in compiled code
  if (!f->is_compiled_frame()) return false;

  // Iterate over the vframes and check the compiled_context
  compiledVFrame* vf = (compiledVFrame*) vframe::new_vframe(f);
  assert(vf->is_compiled_frame(), "should be compiled vframe");
  while (true) {
    contextOop con = vf->compiled_context();

    // stdOut->print_cr("checking context fp = 0x%lx, pc = 0x%lx", f->fp(), f->pc());
    if (con) con->print();

    if (con && con->unoptimized_context()) return true;
    if (vf->is_top()) break;
    vf = (compiledVFrame*) vf->sender();
    assert(vf->is_compiled_frame(), "should be compiled vframe");
  }
  return false;
}


class FrameAndContextElement : public ResourceObj {
 public:
  frame      fr;
  contextOop con;

  FrameAndContextElement(frame* f, contextOop c) {
    fr  = *f;
    con = c;
  }
};


void collect_compiled_contexts_for(frame* f, GrowableArray<FrameAndContextElement*>* elements) {
  // Return false if we're not in compiled code
  if (!f->is_compiled_frame()) return;

  // Iterate over the vframes and check the compiled_context
  compiledVFrame* vf = (compiledVFrame*) vframe::new_vframe(f);
  assert(vf->is_compiled_frame(), "should be compiled vframe");
  while (true) {
    contextOop con = vf->compiled_context();
    if (con) {
      elements->append(new FrameAndContextElement(f, con));
    }
    if (vf->is_top()) break;
    vf = (compiledVFrame*) vf->sender();
    assert(vf->is_compiled_frame(), "should be compiled vframe");
  }
}


class EnableDeoptimization: StackObj {
 public:
  EnableDeoptimization()  { StackChunkBuilder::begin_deoptimization(); }
  ~EnableDeoptimization() { StackChunkBuilder::end_deoptimization();   }
};


void uncommon_trap() {
  /*
  if (UseNewBackend) {
    warning("uncommon traps not supported yet for new backend");
    Unimplemented();
  }
  */

  SavedRegistersScope srs;
  ResourceMark rm;
  // Find the frame that caused the uncommon trap
  DeltaProcess* process = DeltaProcess::active();

#ifdef ASSERT
  process->verify();
#endif

  FlagSetting fl(processSemaphore, true);

  process->enter_uncommon();
  frame f = process->last_frame();

  // Patch the call destination if necessary
  bool used = patch_uncommon_call(&f);

  // Find the nmethod containing the uncommon trap
  compiledVFrame* vf = (compiledVFrame*) vframe::new_vframe(&f);
  assert(vf->is_compiled_frame(), "must be compiled frame");
  nmethod* nm = vf->code();

  nm->inc_uncommon_trap_counter();

  LOG_EVENT3("Uncommon trap in 0x%lx@%d #%d", nm, vf->scope()->offset(), nm->uncommon_trap_counter());

  /* For Debugging inserted by Lars Bak 5-13-96
  if (nm->is_block()) { 
    PrintUncommonBranches = true;
    TraceDeoptimization = true;
  } else {
    PrintUncommonBranches = false;
    TraceDeoptimization = false;
  }
  */

  if (PrintUncommonBranches)  {
    stdOut->print("%s trap in ", used ? "Uncommon" : "New uncommon");
    nm->print_value_on(stdOut);
    stdOut->print(" #%d", nm->uncommon_trap_counter());
    if (WizardMode) stdOut->print(" @%d called from %#x", vf->scope()->offset(), f.pc() - Assembler::sizeOfCall);
    stdOut->cr();
    if (TraceDeoptimization) vf->print_activation(0);
    process->trace_top(0, 3);
  }

  // if counter is high enough, recompile the nmethod
  if (RecompilationPolicy::shouldRecompileAfterUncommonTrap(nm)) {
    if (!nm->isZombie()) nm->makeZombie(false);
    Recompilation recomp(vf->receiver(), nm, true);
    VMProcess::execute(&recomp);
  }


  {
  FinalResourceMark rm;
  EnableDeoptimization ed; // Wrapper that enables canonicalization when deoptimization.

  DeltaProcess::deoptimize_redo_last_send();
  process->deoptimize_stretch(&f, &f);
  if (!nm->is_method()) {
    // This is a top level block nmethod so we have to make sure all frames on the stack
    // referring the context chain are deoptimized.
    // Frames on other stacks might be candidates for deoptimization but are ignored for now.  fix this later
    //
    // Recipe:
    //   walk the stack and collect a work list of {frame, compiled_context} pairs.
    //   iterate over the work list until no deoptimized contextOops are present.
    GrowableArray<FrameAndContextElement*>* elements  = new GrowableArray<FrameAndContextElement*>(10);
    for (frame current_frame = f.sender(); !current_frame.is_first_frame(); current_frame = current_frame.sender()) {
      collect_compiled_contexts_for(&current_frame, elements);
    }
    
    bool done = false;

    while (!done) {
      done = true;      
      for (int index = 0; index < elements->length() && done; index++) {
        FrameAndContextElement* e = elements->at(index);
	if (e && e->con->unoptimized_context()) {
	  process->deoptimize_stretch(&e->fr, &e->fr);

          for (int j = 0; j < elements->length(); j++) {
            if (elements->at(j) && elements->at(j)->fr.fp() == e->fr.fp())
	      elements->at_put(j, NULL);
	  }
          done = false;
	}
      } 
    }
  }
  }
  process->exit_uncommon();
}

#endif
