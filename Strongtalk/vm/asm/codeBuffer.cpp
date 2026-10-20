/* Copyright 1994 - 1996, LongView Technologies L.L.C. $Revision: 1.3 $ */
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
#include "location.hpp"
#include "nativeInstruction.hpp"
#include "runtime.hpp"
#include "compiledIC.hpp"
#include "relocInfo.hpp"
#include "codeBuffer.hpp"
#include "assembler.hpp"
#include "x86_mapping.hpp"
#include "nameDesc.hpp"
#include "ncode.hpp"
#include "symbolOop.hpp"
#include "key.hpp"
#include "methodOop.hpp"
#include "scopeDesc.hpp"
#include "jumpTable.hpp"
#include "codeTable.hpp"
#include "zoneHeap.hpp"
#include "zone.hpp"
#include "nmethodScopes.hpp"
#include "nmethod.hpp"
#include "doubleOop.hpp"
#include "prim.hpp"
#include "disassembler.hpp"
# ifdef DELTA_COMPILER


MacroAssembler* theMacroAssm = NULL;


CodeBuffer::CodeBuffer(int instsSize, int locsSize) {
  instsStart    = instsEnd =  NEW_RESOURCE_ARRAY(char, instsSize);
  instsOverflow = instsStart + instsSize;
  locsStart = locsEnd = (relocInfo*) NEW_RESOURCE_ARRAY(char, locsSize);
  locsOverflow = (relocInfo*) ((char*) locsStart + locsSize);
  last_reloc_offset = code_size();
  _decode_begin = NULL;
}


CodeBuffer::CodeBuffer(char* code_start, int code_size) {
  instsStart    = instsEnd =  (char*)code_start;
  instsOverflow = instsStart + code_size;
  locsStart     = locsEnd = NULL;
  locsOverflow  = NULL;
  last_reloc_offset = CodeBuffer::code_size();
  _decode_begin = NULL;
}


char* CodeBuffer::decode_begin() {
  return _decode_begin == NULL ? (char*)instsStart : _decode_begin;
}


void CodeBuffer::set_code_end(char* end) {
  if ((char*)end < instsStart || instsOverflow < (char*)end) fatal("code end out of bounds");
  instsEnd = (char*)end;
}


void CodeBuffer::relocate(char* at, relocInfo::relocType rtype) {
  assert(code_begin() <= at && at <= code_end(), "cannot relocate data outside code boundaries");
  if (locsEnd == NULL) {
    // no space for relocation information provided => code cannot be relocated
    // make sure that relocate is only called with rtypes that can be ignored for
    // this kind of code.
    assert(rtype == relocInfo::none              ||
           rtype == relocInfo::runtime_call_type ||
	   rtype == relocInfo::external_word_type, "code needs relocation information");
  } else {
    assert(sizeof(relocInfo) == sizeof(short), "change this code");
    int len = at - instsStart;
    int offset = len - last_reloc_offset;
    last_reloc_offset = len;
    *locsEnd++ = relocInfo(rtype, offset);
    if (locsEnd >= locsOverflow) fatal("routine is too long to compile");
  }
}


void CodeBuffer::decode() {
  Disassembler::decode((char*)decode_begin(), (char*)code_end());
  _decode_begin = code_end();
}


void CodeBuffer::decode_all() {
  Disassembler::decode((char*)code_begin(), (char*)code_end());
}


void CodeBuffer::copyTo(nmethod* nm) {
#ifdef STRONGTALK_ARM64
  while (code_size()  % oopSize != 0) { *(a64::insn*) instsEnd = a64::nop(); instsEnd += sizeof(a64::insn); }	// align code
#else
  const char hlt = '\xF4';
  while (code_size()  % oopSize != 0) *instsEnd++ = hlt;			// align code
#endif
  while (reloc_size() % oopSize != 0) *locsEnd++  = relocInfo(false, 0);	// align relocation info

  copy_oops((oop*)instsStart, (oop*)nm->insts(), code_size() /oopSize);
  copy_oops((oop*)locsStart,  (oop*)nm->locs(),  reloc_size()/oopSize);

  // Fix the pc relative information after the move
  int delta = (char*)instsStart - (char*)nm->insts();
  nm->fix_relocation_at_move(delta);
}


void CodeBuffer::print() {
  stdOut->print("CodeBuffer:\n");
  stdOut->print("Code  [0x%x <- used -> 0x%x[ <- free -> 0x%x[\n", instsStart, instsEnd, instsOverflow);
  stdOut->print("Reloc [0x%x <- used -> 0x%x[ <- free -> 0x%x[\n", locsStart,  locsEnd,  locsOverflow );
}


# endif
