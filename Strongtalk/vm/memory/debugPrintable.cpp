/* Copyright 1994, 1995 LongView Technologies L.L.C. $Revision: 1.42 $ */
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
#include "objArrayOop.hpp"
#include "universe.inline.hpp"
#include "vmSymbols.hpp"
#include "oop.inline.hpp"
#include "objectIDTable.hpp"
#include "compUtils.hpp"
#include "vmOperations.hpp"
#include "debugPrintable.hpp"
#include "vframe.hpp"
#include "inliner.hpp"
#include "compiler.hpp"
#include "prettyPrinter.hpp"
#include "expr.hpp"
#include "bitVector.hpp"
#include "registerMask.hpp"
#include "scope.hpp"
#include "oopFactory.hpp"
// ------ helper functions for debugging go here ------------

#ifndef DEBUG
#  ifdef ASSERT
   // NOTE: don't turn the lines below into a comment -- if you're getting
   // a compile error here, change the settings to define DEBUG
   DEBUG should be defined when ASSERT is defined.  It's intended to be used for debugging
   functions that don't slow down the system too much and thus can be left in optimized code.
   On the other hand, the code shouldn't be included in a production version.
#  endif
#endif

// All debug entries should be wrapped with a stack allocated
// Command object. It makes sure a resource mark is set and
// flushes the logfile to prevent file sharing problems.

class Command {
 private:
  ResourceMark rm;
 public:
  Command(char* str) {
    stdOut->cr();
	stdOut->print_cr("\"Executing %s\"", str);
  }

  ~Command() {
    flush_logFile();
  }
};


void pp(void* p) {
  Command c("pp");
  FlagSetting fl(PrintVMMessages, true);
  if (p == NULL) {
    lprintf("0x0");
    return;
  }

  if (Universe::is_heap((oop*) p)) {
    memOop obj = as_memOop(Universe::object_start((oop*) p));
    obj->print();
    if (obj->is_method()) {
      int bci = methodOop(obj)->bci_from((u_char*) p);
      prettyPrinter::print(methodOop(obj), NULL, bci);
    }
    return;
  }

  if (oop(p)->is_smi() || oop(p)->is_mark()) {
    oop(p)->print();
    return;
  }
}

// pv: print vm-printable object
void pv(intptr_t p)   { ((PrintableResourceObj*) p)->print(); }

void pp_short(void* p) {
  Command c("pp_short");
  FlagSetting fl(PrintVMMessages, true);
  if (p == NULL) {
    lprintf("0x0");
  } else if (oop(p)->is_mem()) {
    // guess that it's a memOop
    oop(p)->print();
  } else {
    // guess that it's a VMObj*
    // FIX LATER ((VMObj*) p)->print_short_zero();
  }
}


void pk(Klass* p) {
  Command c("pk");
  FlagSetting fl(PrintVMMessages, true);
  p->print_klass();
}

void pr(void* m) {
  Command c("pr");
  Universe::remembered_set->print_set_for_object(memOop(m));
}

void ps() { // print stack
  // Retrieve the frame pointer of the current frame 
  intptr_t* fp;
  fp = (intptr_t*) __builtin_frame_address(0);
  {
    Command c("ps");
    // Prints the stack of the current Delta process
    DeltaProcess* p = DeltaProcess::active();
    stdOut->print(" for process: ");
    p->print();
    stdOut->cr();

    if (p->last_Delta_fp() != NULL) {
      // If the last_Delta_fp is set we are in C land and
      // can call the standard stack_trace function.
      p->trace_stack();
    } else {
      // fp point to the frame of the ps stub routine
      frame f(NULL, fp, NULL);
      f = f.sender();
      p->trace_stack_from(vframe::new_vframe(&f));
    }
  }
}

void pss() { // print all stack
  Command c("pss");
  Processes::print();
}


void pd() { // print stack
  // Retrieve the frame pointer of the current frame 
  intptr_t* fp;
  fp = (intptr_t*) __builtin_frame_address(0);
  {
    Command c("pd");
    // Prints the stack of the current Delta process
    DeltaProcess* p = DeltaProcess::active();
    stdOut->print(" for process: ");
    p->print();
    stdOut->cr();

    if (p->last_Delta_fp() != NULL) {
      // If the last_Delta_fp is set we are in C land and
      // can call the standard stack_trace function.
      p->trace_stack_for_deoptimization();
    } else {
      // fp point to the frame of the ps stub routine
      frame f(NULL, fp, NULL);
      f = f.sender();
      p->trace_stack_for_deoptimization(&f);
    }
  }
}

void oat(int index) {
  Command c("oat");
  if (objectIDTable::is_index_ok(index)) {
    oop obj = objectIDTable::at(index);
    obj->print();
  } else {
    stdOut->print_cr("index %d out of bounds", index);
  }
}

// please use this to print stacks when reporting compiler bugs
void urs_ps() {
  FlagSetting f1(WizardMode, true);
  FlagSetting f2(PrintOopAddress, true);
  FlagSetting f3(ActivationShowCode, true);
  FlagSetting f4(MaterializeEliminatedBlocks, false);
  FlagSetting f5(BreakAtWarning, false);
  ps();
}

void pc() 	{
  Command c("pc");
  theCompiler->print_code(false);
}

void pscopes()	{
  Command c("pscopes");
  theCompiler->topScope->printTree();
}

void debug() {		// to set things up for compiler debugging
  Command c("debug");
  WizardMode = true;
  PrintVMMessages = true;
  PrintCompilation = PrintInlining = PrintSplitting = PrintCode = PrintAssemblyCode = PrintEliminateUnnededNodes = true;
  PrintEliminateContexts = PrintCopyPropagation = PrintRScopes = PrintExposed = PrintLoopOpts = true;
  AlwaysFlushVMMessages = true;
  flush_logFile();
}

void ndebug() {		// undo debug()
  Command c("ndebug");
  PrintCompilation = PrintInlining = PrintSplitting = PrintCode = PrintAssemblyCode = PrintEliminateUnnededNodes = false;
  PrintEliminateContexts = PrintCopyPropagation = PrintRScopes = PrintExposed = PrintLoopOpts = false;
  AlwaysFlushVMMessages = false;
  flush_logFile();
}

void flush()  {
  Command c("flush");
  flush_logFile();
}

void events() {
  Command c("events");
  eventLog->printPartial(50);
}

nmethod* find(intptr_t addr) {
  Command c("find");
  return findNMethod((void*)addr);
}

methodOop findm(intptr_t hp) { 
  Command c("findm");
  return methodOopDesc::methodOop_from_hcode((u_char*)hp); 
}

// int versions of all methods to avoid having to type casts in the debugger

void pp(intptr_t p) {
  pp((void*)p);
}

void pp_short(intptr_t p) {
  pp_short((void*)p);
}

void pk(intptr_t p) {
  pk((Klass*)p);
}

void ph(intptr_t hp)	{ 
  Command c("ph");
  findm(hp)->pretty_print(); 
}

void pm(intptr_t m) { 
  Command c("pm");
  methodOop(m)->pretty_print(); 
}

void print_codes(char* class_name, char* selector) { 
  Command c("print_codes");
  stdOut->print_cr("Finding %s in %s.", selector, class_name);
  oop result = Universe::find_global(class_name);
  if (!result) {
    stdOut->print_cr("Could not find global %s.", class_name);
  } else if (!result->is_klass()) {
    stdOut->print_cr("Global %s is not a class.", class_name);
  } else {
    symbolOop sel = oopFactory::new_symbol(selector);
    methodOop method = klassOop(result)->klass_part()->lookup(sel);
    if (!method) method = result->blueprint()->lookup(sel);
    if (!method) {
      stdOut->print_cr("Method %s is not in %s.", selector, class_name);
    } else {
      method->pretty_print();
      method->print_codes();
    }
  }
}

void help() { 
  Command c("help");


  stdOut->print_cr("basic");
  stdOut->print_cr("  pp(void* p)   - try to make sense of p");
  stdOut->print_cr("  pv(int p)     - ((PrintableResourceObj*) p)->print()");
  stdOut->print_cr("  ps()          - print current process stack");
  stdOut->print_cr("  pss()         - print all process stacks");
  stdOut->print_cr("  oat(int i)    - print object with id = i");
 
  stdOut->print_cr("methodOop");
  stdOut->print_cr("  pm(int m)     - pretty print methodOop(m)");
  stdOut->print_cr("  ph(int hp)    - pretty print method containing hp");
  stdOut->print_cr("  findm(int hp) - returns methodOop containing hp");

  stdOut->print_cr("misc.");
  stdOut->print_cr("  flush()       - flushes the log file");
  stdOut->print_cr("  events()      - dump last 50 event");


  stdOut->print_cr("compiler debugging");
  stdOut->print_cr("  debug()       - to set things up for compiler debugging");
  stdOut->print_cr("  ndebug()      - undo debug");
  stdOut->print_cr("  pc()          - theCompiler->print_code(false)");
  stdOut->print_cr("  pscopes()     - theCompiler->topScope->printTree()");
  stdOut->print_cr("  urs_ps()      - print current process stack with many flags turned on");
}
