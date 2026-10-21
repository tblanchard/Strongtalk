/* Copyright 1994, 1995 LongView Technologies L.L.C. $Revision: 1.19 $ */
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
#include "os.hpp"
#include "dll.hpp"
#include "stubRoutines.hpp"
#include "proxyOop.hpp"
#include "vmOperations.hpp"
#include "codeIterator.hpp"
#include "oop.inline.hpp"
// InterpretedDLL_Cache implementation

void InterpretedDLL_Cache::verify() {
  // check oops
  if (!dll_name()->is_symbol()) fatal("dll name is not a symbolOop");
  if (!funct_name()->is_symbol()) fatal("function name is not a symbolOop");
  if (number_of_arguments() < 0) fatal("illegal number of arguments");
}


void InterpretedDLL_Cache::print() {
  stdOut->print("DLL call ");
  dll_name()->print_value();
  stdOut->print("::");
  funct_name()->print_value();
  stdOut->print(" (0x%x, %s, interpreted)\n", entry_point(), async() ? "asynchronous" : "synchronous");
}


// CompiledDLL_Cache implementation

bool CompiledDLL_Cache::async() const {
  char* d = destination();
  return
    d == StubRoutines::lookup_DLL_entry(true) ||
    d == StubRoutines::call_DLL_entry(true);
}


void CompiledDLL_Cache::verify() {
  // check layout
#ifdef STRONGTALK_ARM64
  if (*(a64::insn*) addr_at(ldr_edx_offset) != a64::ldr_literal(2, 8) ||
      *(a64::insn*) addr_at(ldr_edx_offset + 4) != a64::b(28)) fatal("not a compiled DLL cache");
#else
  mov_at(mov_edx_instruction_offset)->verify();
  test_at(test_1_instruction_offset)->verify();
  test_at(test_2_instruction_offset)->verify();
#endif
  NativeCall::verify();
  // check oops
  if (!dll_name()->is_symbol()) fatal("dll name is not a symbolOop");
  if (!function_name()->is_symbol()) fatal("function name is not a symbolOop");
  // check destination
  char* d = destination();
  if (d != StubRoutines::lookup_DLL_entry(true)  &&
      d != StubRoutines::lookup_DLL_entry(false) &&
      d != StubRoutines::call_DLL_entry(true)    &&
      d != StubRoutines::call_DLL_entry(false))  {
    fatal1("CompiledDLL_Cache destination 0x%x incorrect", d);
  }
}


void CompiledDLL_Cache::print() {
  stdOut->print("DLL call ");
  dll_name()->print_value();
  stdOut->print("::");
  function_name()->print_value();
  stdOut->print(" (0x%x, %s, compiled)\n", entry_point(), async() ? "asynchronous" : "synchronous");
}


// DLLs implementation

dll_func DLLs::lookup(symbolOop name, DLL* library) {
  char buffer[200];
  name->copy_null_terminated(buffer, 200);
  return os::dll_lookup(buffer, library);
}


DLL* DLLs::load(symbolOop name) {
  char buffer[200];
  name->copy_null_terminated(buffer, 200);
  return os::dll_load(buffer);
}


bool DLLs::unload(DLL* library) {
  return os::dll_unload(library);
}


extern Compiler* theCompiler;

dll_func DLLs::lookup_fail(symbolOop dll_name, symbolOop function_name) {
  // Call backs to Delta
  if (Universe::dll_lookup_receiver()->is_nil()) {
    warning("dll lookup receiver is not set");
  }
  assert(theCompiler == NULL, "Cannot handle call back during compilation");

  oop res = Delta::call(Universe::dll_lookup_receiver(), Universe::dll_lookup_selector(), function_name, dll_name);

  return res->is_proxy() 
       ? (dll_func) proxyOop(res)->get_pointer()
       : NULL;
}


dll_func DLLs::lookup(symbolOop dll_name, symbolOop function_name) {
  dll_func result = lookup_fail(dll_name, function_name);
  if (result) {
    if (TraceDLLLookup) {
      stdOut->print("DLL looking for ");
      dll_name->print_symbol_on(stdOut);
      stdOut->print("::");
      function_name->print_symbol_on(stdOut);
      stdOut->print_cr(" -> 0x%lx", result);
    }
    return result;
  }

  stdOut->print("DLL lookup ");
  function_name->print_symbol_on(stdOut);
  stdOut->print(" in ");
  dll_name->print_symbol_on(stdOut);
  stdOut->print(" failed.");
  stdOut->cr();

  DeltaProcess::active()->suspend(DLL_lookup_error);
  return NULL;
}


dll_func DLLs::lookup_and_patch_InterpretedDLL_Cache() {
  // get DLL call info
  frame f = DeltaProcess::active()->last_frame();
  methodOop m = f.method();
  CodeIterator it(m, m->bci_from(f.hp()));
  InterpretedDLL_Cache* cache = it.dll_cache();
  assert(cache->entry_point() == NULL, "should not be set yet");
  // do lookup, patch & return entry point
  dll_func function = lookup(cache->dll_name(), cache->funct_name());
  cache->set_entry_point(function);
  return function;
}


dll_func DLLs::lookup_and_patch_CompiledDLL_Cache() {
  // get DLL call info
  frame f = DeltaProcess::active()->last_frame();
  CompiledDLL_Cache* cache = compiledDLL_Cache_from_return_address(f.pc());
  assert(cache->entry_point() == NULL, "should not be set yet");
  // do lookup, patch & return entry point
  dll_func function = lookup(cache->dll_name(), cache->function_name());
  cache->set_entry_point(function);
  cache->set_destination(StubRoutines::call_DLL_entry(cache->async()));
  return function;
}


void DLLs::enter_async_call(DeltaProcess** addr) {
  DeltaProcess* proc = DeltaProcess::active();
  *addr = proc; // proc will be retrieved in dll_enter_async_call
  proc->transfer_and_continue();
}


void DLLs::exit_async_call(DeltaProcess** addr) {
  DeltaProcess* proc = *addr;
  proc->wait_for_control();
}


void DLLs::exit_sync_call(DeltaProcess** addr) {
  // nothing to do here for now
}

