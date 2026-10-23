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
#include "codeIterator.hpp"
#include "proxyOop.hpp"
#include "prim_def.hpp"
#include "proxy_prims.hpp"
#include "oopFactory.hpp"
#include "universe.inline.hpp"
#include "vmSymbols.hpp"
#include "oop.inline.hpp"
#include "ic_iterator.hpp"
#include "callBack.hpp"
#include "interpretedIC.hpp"
#include "abort.hpp"
void callBack::initialize(oop receiver, symbolOop selector) {
  assert(selector->is_symbol(), "must be symbol");
  Universe::set_callBack(receiver, selector);
}

static char* store_byte(char* chunk, char b) {
  *chunk = b;
  return chunk + sizeof(char);
}

static char* store_long(char* chunk, long l) {
  *((long*) chunk) = l;
  return chunk + sizeof(long);
}

#ifdef STRONGTALK_ARM64
// arm64: callbacks are the precompiled thunks in portableInterpreter_arm64.S (both calling
// conventions are the same; arguments in x0..x7). Thunk i is callback index i.
extern "C" char callback_thunks[];
static const int callback_thunk_count = 256;
static const int callback_thunk_size  = 8;

static void* callback_thunk(int index) {
  if (index < 0 || index >= callback_thunk_count) return NULL;
  return callback_thunks + index * callback_thunk_size;
}

void* callBack::registerPascalCall(int index, int nofArgs) {
  return nofArgs <= 8 ? callback_thunk(index) : NULL;
}

void* callBack::registerCCall(int index) {
  return callback_thunk(index);
}

void callBack::unregister(void* block) {
  assert(block, "block is not valid");	// thunks are static
}

#else
extern "C" int handlePascalCallBackStub();
extern "C" int handleCCallBackStub();

void* callBack::registerPascalCall(int index, int nofArgs) {
  void* result = malloc(15);
  char* chunk = (char*) result;

  // MOV ECX, index
  chunk = store_byte(chunk, '\xB9');
  chunk = store_long(chunk, index);

  // MOV EDX, number of bytes for parameters
  chunk = store_byte(chunk, '\xBA');
  chunk = store_long(chunk, nofArgs * sizeof(int));

  // JMP _handleCCallStub
  chunk = store_byte(chunk, '\xE9');
  chunk = store_long(chunk, ((long) &handlePascalCallBackStub) - (4 + (long) chunk));

  return result;
}

void* callBack::registerCCall(int index) {
  void* result = malloc(10);
  char* chunk = (char*) result;

  // MOV ECX, index
  chunk = store_byte(chunk, '\xB9');
  chunk = store_long(chunk, index);

  // JMP _handleCCallStub
  chunk = store_byte(chunk, '\xE9');
  chunk = store_long(chunk, ((long) &handleCCallBackStub) - (4 + (long) chunk));

  return result;
}

void callBack::unregister(void* block) {
  assert(block, "block is not valid");
  free((char*) block);
}

#endif // STRONGTALK_ARM64

// handleCallBack is called from the two assembly stubs:
// - handlePascalCallBackStub
// - handleCCallBackStub

typedef void* (*call_out_func_4)(intptr_t a, intptr_t b, intptr_t c, intptr_t d);

extern "C" bool have_nlr_through_C;

// params: the address of the arguments (32-bit words), passed to the image in two parts
// (CallBack>>vmCallBackIndex:stackHigh:stackLow:, ExternalProxy>>setHigh:low:)
static void* call_back_into_delta(int index, uintptr_t params) {
  DeltaProcess* proc = NULL;

  if (Universe::callBack_receiver()->is_nil()) {
    warning("callBack receiver is not set");
  }

  smi low  = (smi) (params & 0xFFFF);
  smi high = (smi) (params >> 16);

  if (DeltaProcess::active()->thread_id() != os::current_thread_id()) {
    // We'are now back in a asynchronous DLL call so give up the control
    // Fix this:
    //   remove warning when it has been tested
    proc = Processes::find_from_thread_id(os::current_thread_id());
    assert(proc, "process must be present");
    DLLs::exit_async_call(&proc);
  }
 

  oop res = Delta::call(Universe::callBack_receiver(),
                        Universe::callBack_selector(),
			as_smiOop(index),
                        as_smiOop(high),
                        as_smiOop(low));

  assert(DeltaProcess::active()->thread_id() == os::current_thread_id(), "check for process torch");

  void* result;

  // convert return result

  if (have_nlr_through_C) {
    // Continues the NLR after at the next Delta frame
    ErrorHandler::continue_nlr_in_delta();
  }

  if (res->is_smi()) {
    result = (void*) smiOop(res)->value();
  } else if (res->is_proxy()) {
    result = (void*) proxyOop(res)->get_pointer();
  } else {
    warning("Wrong return type for call back, returning NULL");
    result = NULL;
  }

  // Return value has to be converted before we transfer control to another 
  // thread.

  if (proc) {
    // We'are now back in a asynchronous DLL call so give up the control
    proc->transfer_and_continue();
  }

  // Call Delta level error routine
  return result;
}

extern "C" volatile void* handleCallBack(int index, int params) {
  return call_back_into_delta(index, (uint32_t) params);
}

#ifdef STRONGTALK_ARM64
// Called by the callback thunks with the register arguments x0..x7. The image reads callback
// arguments as 32-bit words (CallBack>>fetchArgument:stack:class:): values that fit in 32 bits
// are passed as is, pointers as external words (see ExternalWords).
extern "C" intptr_t arm64_handle_callback(int index, intptr_t* args) {
  uint32_t words[8];
  for (int i = 0; i < 8; i++) {
    intptr_t v = args[i];
    words[i] = v == (intptr_t) (int32_t) v || v == (intptr_t) (uint32_t) v
             ? (uint32_t) v
             : ExternalWords::encode((void*) v);
  }
  return (intptr_t) call_back_into_delta(index, (uintptr_t) words);
}
#endif
