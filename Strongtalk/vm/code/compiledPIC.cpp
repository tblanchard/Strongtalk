/* Copyright 1994 - 1996 LongView Technologies L.L.C. $Revision: 1.37 $ */
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
#include "os.hpp"
#include "codeBuffer.hpp"
#include "assembler.hpp"
#include "dll.hpp"
#include "stubRoutines.hpp"
#include "klassOop.hpp"
#include "ic_iterator.hpp"
#include "compiledPIC.hpp"
#include "disassembler.hpp"
#include "oop.inline.hpp"
#ifdef DELTA_COMPILER



// A PIC implements a Polymorphic Inline Cache for compiled code.
// The layout comes in 3 variants: a) PICs that contain only m methodOop
// entries, b) PICs that contain both, n nmethod and m methodOop entries
// and c) MICs (Megamorphic Inline Caches) which keep only the selector
// so lookup is fast.The 3 formats can be distinguished by looking at
// the first instruction.
//
// Layout a)		(methodOop entries only)
//
//  0			: call PIC stub routine
//  5			: m methodOop entries a 8 bytes each
//  5 + m*8		: <end of PIC>
//
// i.th methodOop entry (0 <= i < m; m > 0)
//
//  5 + i*8		: klass(i)
//  9 + i*8		: methodOop(i)
//
//
// Layout b)		(nmethod & methodOop entries)
//
//  0			: test al, Mem_Tag
//  2			: jz smi_nmethod/jz methodOop call stub/jz cache_miss
//  8			: mov edx, [eax.klass] (always here to simplify iteration, even if n = 0)
// 11			: n nmethod entries a 12 bytes each
// 11 + n*12		: call PIC stub routine (m > 0)/jmp cache_miss (m = 0)
// 16 + n*12		: m methodOop entries a 8 bytes each
// 16 + n*12 + m*8	: <end of PIC>
//
// i.th nmethod entry (0 <= i < n; n >= 0):
//
// 11 + i*12		: cmp edx, klass(i)
// 11 + i*12 + 6	: je nmethod(i)
//
// i.th methodOop (0 <= i < m; m >= 0):
//
// 16 + n*12 + i*8	: klass
// 16 + n*12 + i*8 + 4	: methodOop
//
//
// Layout c)		(megamorphic inline cache, selector only)
//
//  0			: call MIC stub routine
//  5			: selector
//  9			: <end of MIC>
//
// The PIC stub routine interprets the remaining entries of the PIC; there
// are different stub routines for different m (starting point for interpretation
// is the return address). Entries for smis are treated especially in the sense
// that an initial check for them is always there.
//
// NB: The reason for interpreting the interpreter entries is space, a smallest
// native code implementation for these cases requires 18 bytes per entry (cmp,
// je and load of methodOop). Furthermore, in each case it would be necessary
// to call a stub routine to setup an interpreter frame anyway.
//
// The MIC stub routine interprets the one entry in the MIC. In case of a miss,
// no new PIC/MIC is generated but the current entries are updated. The reason
// for having a specialized PIC allocated in the MIC case is only so the selector
// can be stored. It is used for fast lookup (usually the selector is recomputed
// via debug-info from the corresponding interpreted method).


// Opcodes for code pattern generation/parsing
static const char   test_opcode = '\xa8';
static const char   call_opcode = '\xe8';
static const char   jmp_opcode  = '\xe9';
static const uint16 jz_opcode   = 0x840f;
static const uint16 mov_opcode  = 0x508b;
static const uint16 cmp_opcode  = 0xfa81;	static const int cmp_opcode_size = sizeof(uint16);


// Helper routines for code pattern generation/parsing
static inline void  put_byte(char*& p, u_char b){ *p++ = b; }
static inline void  put_shrt(char*& p, uint16 s){ *(uint16*)p = s; p += sizeof(uint16); }
static inline void  put_word(char*& p, int w)	{ *(int*)p = w; p += sizeof(int); }
static inline void  put_disp(char*& p, char* d)	{ put_word(p, x86_imm32(d - p - sizeof(int))); }

static inline int   get_shrt(char* p)		{ return *(uint16*)p; }
static inline char* get_disp(char* p)		{ return *(int*)p + p + sizeof(int); }


// Structure for storing the entries of a PIC
class PIC_contents {
 public:
  // smi case
  char*		smi_nmethod;
  methodOop	smi_methodOop;

  // nmethod entries
  klassOop	nmethod_klasses[PIC::max_nof_entries];
  char*		nmethods[PIC::max_nof_entries];
  int n;	// nmethods index

  // methodOop entries
  klassOop	methodOop_klasses[PIC::max_nof_entries];
  methodOop	methodOops[PIC::max_nof_entries];
  int m;	// methodOops index

  void append_nmethod_entry(klassOop klass, char* entry);
  void append_method(klassOop klass, methodOop method);

  int number_of_compiled_targets() const	{ return (smi_nmethod   ? 1 : 0) + n; }
  int number_of_interpreted_targets() const	{ return (smi_methodOop ? 1 : 0) + m; }
  int number_of_targets() const			{ return number_of_compiled_targets() + number_of_interpreted_targets(); }

  bool has_smi_case() const			{ return (smi_methodOop != NULL) || (smi_nmethod != NULL); }
  bool has_nmethods() const			{ return (n > 0) || (smi_nmethod != NULL); }


#ifdef STRONGTALK_ARM64
  int code_size() const {
    return PIC::PIC_entries_offset + number_of_targets() * PIC::PIC_entry_size;
  }
#else
  int code_size() const {
    int methodOop_size = number_of_interpreted_targets() * PIC::PIC_methodOop_entry_size;
    if (has_nmethods()) {
      return PIC::PIC_methodOop_entry_offset + n * PIC::PIC_nmethod_entry_size + methodOop_size;
    } else {
      return PIC::PIC_methodOop_only_offset + methodOop_size;
    }
  }
#endif


  PIC_contents() {
    smi_nmethod   = NULL;
    smi_methodOop = NULL;
    n = 0;
    m = 0;
  }
};


void PIC_contents::append_nmethod_entry(klassOop klass, char* entry) {
  // add new entry
  if (klass == smiKlassObj) {
    assert(!has_smi_case(), "cannot overwrite smi case");
    smi_nmethod = entry;
  } else {
    nmethod_klasses[n] = klass;
    nmethods[n] = entry;
    n++;
  }
}


void PIC_contents::append_method(klassOop klass, methodOop method) {
  // add new entry
  assert(method->is_method(), "must be methodOop");
  if (klass == smiKlassObj) {
    assert(!has_smi_case(), "cannot overwrite smi case");
    smi_methodOop = method;
  } else {
    methodOop_klasses[m] = klass;
    methodOops[m] = method;
    m++;
  }
}


// Implementation of PIC_Iterators

#ifdef STRONGTALK_ARM64

// arm64 PIC layout (all PICs, and MICs):
//
//  0	: ldr x16, #16		stub routine
//  4	: adr x17, #24		data (for the stub routine)
//  8	: br x16
// 12	: nop
// 16	: stub routine address	(PIC stub / megamorphic IC stub)
// 24	: PIC: number of entries n; MIC: selector
// 32	: PIC: n entries of 16 bytes: klass, target
//
// The target of an entry is the verified entry point of an nmethod or a methodOop (an
// interpreted method): they are distinguished by the tag (code addresses are word aligned).
// Smis have an entry with klass smiKlassObj. The PIC stub routine (StubRoutines::PIC_stub_entry)
// compares the receiver's klass with the entries and jumps to the target, calls the interpreter
// for methodOops, or calls the lookup routine.

static inline bool is_methodOop_target(char* t) { return ((intptr_t) t & Tag_Mask) == Mem_Tag; }

PIC_Iterator::PIC_Iterator(PIC* pic) {
  _pic = pic;
  _pos = pic->entry() + PIC::PIC_entries_offset;
  if (pic->is_megamorphic()) {
    _state = at_the_end;
  } else {
    _methodOop_counter = (int) *(intptr_t*) (pic->entry() + PIC::PIC_count_offset);	// remaining entries
    computeNextState();
  }
}

void PIC_Iterator::computeNextState() {
  if (_methodOop_counter <= 0) {
    _state = at_the_end;
  } else {
    _state = is_methodOop_target(*target_addr()) ? at_methodOop : at_nmethod;
  }
}

void PIC_Iterator::advance() {
  assert(_state != at_the_end, "iterator at end");
  _pos += PIC::PIC_entry_size;
  _methodOop_counter--;
  computeNextState();
}

klassOop* PIC_Iterator::klass_addr() const {
  assert(_state == at_nmethod || _state == at_methodOop, "no entry");
  return (klassOop*) (_pos + PIC::PIC_klass_offset);
}

char** PIC_Iterator::target_addr() const {
  return (char**) (_pos + PIC::PIC_target_offset);
}

methodOop* PIC_Iterator::methodOop_addr() const {
  assert(_state == at_methodOop, "no methodOop entry");
  return (methodOop*) target_addr();
}

#else // x86

PIC_Iterator::PIC_Iterator(PIC* pic) {
  _pic   = pic;
  _pos   = pic->entry();
  // determine initial state
  if (pic->is_megamorphic()) {
    // MIC -> do not use cached information
    assert(get_disp(_pos + 1) == StubRoutines::megamorphic_ic_entry(), "MIC stub expected");
    _state = at_the_end;
  } else if (*_pos == call_opcode) {
    // PIC without nmethods
    _state = at_methodOop;
    _methodOop_counter = PIC::nof_entries(get_disp(_pos + 1));
    _pos += PIC::PIC_methodOop_only_offset;
  } else {
    // nmethods -> handle smis first
    char* dest = get_disp(_pos + PIC::PIC_smi_nmethod_offset);
    if (dest == CompiledIC::normalLookupRoutine() || _pic->contains(dest)) {
      // no smis or smi case is treated in methodOop section
      _state = at_nmethod;
      _pos += PIC::PIC_nmethod_entry_offset;
    } else {
      // smi entry is treated here
      _state = at_smi_nmethod;
    }
  }
}


void PIC_Iterator::computeNextState() {
  if (get_shrt(_pos) == cmp_opcode) {
     // same state
  } else if (*_pos == call_opcode) {
     _state = at_methodOop;
     _methodOop_counter = PIC::nof_entries(get_disp(_pos + 1));
     _pos += PIC::PIC_methodOop_entry_offset - PIC::PIC_nmethod_entry_offset;
  } else {
    assert(*_pos == jmp_opcode, "jump to lookup routine expected");
    _state = at_the_end;
  }
}


void PIC_Iterator::advance() {
  switch (_state) {
    case at_smi_nmethod:
      assert(_pos == _pic->entry(), "must be at beginning");
      _pos += PIC::PIC_nmethod_entry_offset;
      _state = at_nmethod;
      computeNextState();
      break;
    case at_nmethod:
      _pos += PIC::PIC_nmethod_entry_size;
      computeNextState();
      break;
    case at_methodOop:
      if (--_methodOop_counter > 0) {
        _pos += PIC::PIC_methodOop_entry_size;
      } else {
        _state = at_the_end;
      }
      break;
    case at_the_end:
      ShouldNotCallThis();
    default:
      ShouldNotReachHere();
  }
}


klassOop* PIC_Iterator::klass_addr() const {
  int offs;
  switch (state()) {
    case at_smi_nmethod: ShouldNotCallThis();			// no klass stored -> no klass address available
    case at_nmethod    : offs = PIC::PIC_nmethod_klass_offset;	break;
    case at_methodOop  : offs = PIC::PIC_methodOop_klass_offset;break;
    case at_the_end    : ShouldNotCallThis();			// no klass stored -> no klass address available
    default            : ShouldNotReachHere();
  }
  return (klassOop*)(_pos + offs);
}


int* PIC_Iterator::nmethod_disp_addr() const {
  int offs;
  switch (state()) {
    case at_smi_nmethod: offs = PIC::PIC_smi_nmethod_offset;	break;
    case at_nmethod    : offs = PIC::PIC_nmethod_offset;	break;
    case at_methodOop  : ShouldNotCallThis();			// no nmethod stored -> no nmethod address available
    case at_the_end    : ShouldNotCallThis();			// no nmethod stored -> no nmethod address available
    default            : ShouldNotReachHere();
  }
  return (int*)(_pos + offs);
}


methodOop* PIC_Iterator::methodOop_addr() const {
  int offs;
  switch (state()) {
    case at_smi_nmethod: ShouldNotCallThis();			// no methodOop stored -> no methodOop address available
    case at_nmethod    : ShouldNotCallThis();			// no methodOop stored -> no methodOop address available
    case at_methodOop  : offs = PIC::PIC_methodOop_offset;	break;
    case at_the_end    : ShouldNotCallThis();
    default            : ShouldNotReachHere();
  }
  return (methodOop*)(_pos + offs);
}


#endif // STRONGTALK_ARM64

void PIC_Iterator::print() {
  lprintf("a PIC_Iterator\n");
}


// Implementation of PICs

bool PIC::in_heap(char* addr) {
  return Universe::code->picHeap->contains(addr);
}


PIC* PIC::find(char* addr) {
  if (Universe::code->picHeap->contains(addr)) {
    PIC* result = (PIC*)Universe::code->picHeap->findStartOfBlock(addr);
    return result;
  }
  return NULL;
}


#ifdef STRONGTALK_ARM64

// Accessing PIC entries

klassOop PIC_Iterator::get_klass() const { 
  return *klass_addr(); 
}

char* PIC_Iterator::get_call_addr() const { 
  assert(is_compiled(), "not an nmethod entry");
  return *target_addr();
}

bool PIC_Iterator::is_compiled() const {
  switch (state()) {
    case at_nmethod    : return true;
    case at_methodOop  : return false;
    default            : ShouldNotCallThis();
  }
  return false;
}

bool PIC_Iterator::is_interpreted() const { 
  return !is_compiled();
}

nmethod* PIC_Iterator::compiled_method() const { 
  if (!is_compiled()) return NULL;  
  return findNMethod(get_call_addr() - sizeof(nmethod)); 
}

methodOop PIC_Iterator::interpreted_method() const { 
  if (is_interpreted()) {
    return *methodOop_addr(); 
  } else {
    return compiled_method()->method();
  }
}

// Modifying PIC entries (aligned words: atomic for threads executing the PIC)

void PIC_Iterator::set_klass(klassOop klass) { 
  native_code_write_word((char*) klass_addr(), (intptr_t) klass);
}

void PIC_Iterator::set_nmethod(nmethod* nm) { 
  assert(get_klass() == nm->key.klass(), "mismatched receiver klass");
  assert(is_compiled(), "not an nmethod entry");
  native_code_write_word((char*) target_addr(), (intptr_t) nm->verifiedEntryPoint());
}

void PIC_Iterator::set_methodOop(methodOop method) { 
  native_code_write_word((char*) methodOop_addr(), (intptr_t) method);
}

symbolOop* PIC::MIC_selector_address() const {
  assert(is_megamorphic(), "not a MIC");
  return (symbolOop*)(entry() + MIC_selector_offset);
}

#else // x86

// Accessing PIC entries
klassOop PIC_Iterator::get_klass() const { 
  return state() == at_smi_nmethod ? smiKlassObj : *klass_addr(); 
}


char* PIC_Iterator::get_call_addr() const { 
  int* a = nmethod_disp_addr(); 
  return (char*)a + sizeof(int) + *a; 
}


bool PIC_Iterator::is_compiled() const {
  switch (state()) {
    case at_smi_nmethod: return true;
    case at_nmethod    : return true;
    case at_methodOop  : return false;
    case at_the_end    : ShouldNotCallThis();
    default            : ShouldNotReachHere();
  }
  return false;
}


bool PIC_Iterator::is_interpreted() const { 
  return !is_compiled();
}


nmethod* PIC_Iterator::compiled_method() const { 
  if (!is_compiled()) return NULL;  
  return findNMethod(get_call_addr() - sizeof(nmethod)); 
}


methodOop PIC_Iterator::interpreted_method() const { 
  if (is_interpreted()) {
    return *methodOop_addr(); 
  } else {
    return compiled_method()->method();
  }
}


// Modifying PIC entries
void PIC_Iterator::set_klass(klassOop klass) { 
  assert(state() != at_smi_nmethod, "cannot be set"); 
  *klass_addr() = klass; 
}


void PIC_Iterator::set_nmethod(nmethod* nm) { 
  assert(get_klass() == nm->key.klass(), "mismatched receiver klass");
  int* a = nmethod_disp_addr(); 
  *a = nm->verifiedEntryPoint() - (char*)a - sizeof(int); 
}


void PIC_Iterator::set_methodOop(methodOop method) { 
  *methodOop_addr() = method; 
}


symbolOop* PIC::MIC_selector_address() const {
  assert(is_megamorphic(), "not a MIC");
  return (symbolOop*)(entry() + MIC_selector_offset);
}

#endif // STRONGTALK_ARM64

PIC* PIC::replace(nmethod* nm) {
  // nothing to do in megamorphic case
  if (is_megamorphic()) return this;

  LOG_EVENT3("compiled PIC at 0x%x: new nmethod 0x%x for klass 0x%x replaces old entry", this, nm, nm->key.klass());

  { // do the replace without creating a new PIC if possible
    PIC_Iterator it(this);
    while (it.get_klass() != nm->key.klass()) it.advance();
    assert(!it.at_end(), "unexpected end during replace");
    if (it.is_compiled()) {
      it.set_nmethod(nm);
      return this;
    }
  }
   
  { // Create a new PIC 
    PIC_contents contents;
    PIC_Iterator it(this);
    while (!it.at_end()) {
      klassOop receiver_klass = it.get_klass();
      if (receiver_klass == nm->key.klass()) {
        contents.append_nmethod_entry(nm->key.klass(), nm->verifiedEntryPoint());
      } else {
        if (it.is_interpreted()) {
          contents.append_method(it.get_klass(), it.interpreted_method());
        } else {
          contents.append_nmethod_entry(it.get_klass(), it.get_call_addr());
        }
      }
      it.advance();
    }
    int allocated_code_size = contents.code_size();
    return new (allocated_code_size) PIC(_ic, &contents, allocated_code_size);
  }
}


PIC* PIC::cleanup(nmethod** nm) {
  // nothing to do in megamorphic case
  if (is_megamorphic()) return this;

  bool pic_layout_has_changed = false;

  // Iterate over the PIC and
  //  - patch the PIC if possible
  //  - check if the layout has changed
  //  - collect the PIC information.

  PIC_contents contents;
  PIC_Iterator it(this);
  while (!it.at_end()) {
    klassOop receiver_klass = it.get_klass();
    if (it.is_interpreted()) {
      // Interpreted methodOop
      if(compiled_ic()->isSuperSend()) {
        contents.append_method(it.get_klass(), it.interpreted_method());
      } else {
        LookupKey key(it.get_klass(), it.interpreted_method()->selector());
        LookupResult result = lookupCache::lookup(&key);
        if (result.matches(it.interpreted_method())) {
	  contents.append_method(it.get_klass(), it.interpreted_method());
	} else {
          if (result.is_method()) {
            contents.append_method(it.get_klass(), result.method());
            it.set_methodOop(result.method());
	  } else if (result.is_entry()) {
            contents.append_nmethod_entry(it.get_klass(), result.get_nmethod()->verifiedEntryPoint());
            pic_layout_has_changed = true;
	  } else {
            pic_layout_has_changed = true;
	  }
	}
      }
    } else {
      // Compiled nmethod
      nmethod* nm = it.compiled_method();
      LookupResult result = lookupCache::lookup(&nm->key);
      if (result.matches(nm)) {
        contents.append_nmethod_entry(it.get_klass(), it.get_call_addr());
      } else {
        if (result.is_method()) {
          contents.append_method(it.get_klass(), result.method());
          pic_layout_has_changed = true;
	} else if (result.is_entry()) {
          contents.append_nmethod_entry(it.get_klass(), result.get_nmethod()->verifiedEntryPoint());
          it.set_nmethod(result.get_nmethod());
	} else {
          pic_layout_has_changed = true;
	}
      }
    }
    it.advance();
  }

  *nm = NULL;
  if (pic_layout_has_changed) {
    
    if (contents.number_of_targets() == 0) {
      // no targets left
      return NULL;
    }

    if (contents.number_of_targets() == 1 && contents.has_nmethods()) {
      // 1 nmethod target
      *nm = findNMethod(contents.has_smi_case() ? contents.smi_nmethod : contents.nmethods[0]);
      assert(*nm, "nmethod must be present");
      return NULL;
    }

    int allocated_code_size = contents.code_size();
    return new (allocated_code_size) PIC(_ic, &contents, allocated_code_size);
  }

  // The layout has not changed so return the patched self
  return this;
}


int PIC::nof_entries(char* pic_stub) {
  int i = 1;
  while (true) {
    if (pic_stub == StubRoutines::PIC_stub_entry(i)) return i;
    i++;
  }
  ShouldNotReachHere();
  return 0;
}


#ifdef STRONGTALK_ARM64

// The common code of PICs and MICs: jumps to the stub routine with x17 = data (see layout above)
static void put_PIC_header(char* entry, char* stub) {
  assert(((intptr_t) entry & 7) == 0, "PIC entry must be 8-byte aligned");
  a64::insn* code = (a64::insn*) entry;
  code[0] = a64::ldr_literal(16, PIC::PIC_stub_offset);
  code[1] = a64::adr(17, PIC::PIC_count_offset - 4);
  code[2] = a64::br(16);
  code[3] = a64::nop();
  *(char**) (entry + PIC::PIC_stub_offset) = stub;
}

int PIC::code_for_methodOops_only(char* entry, PIC_contents* c) {
  return code_for_polymorphic_case(entry, c);
}

int PIC::code_for_polymorphic_case(char* entry, PIC_contents* c) {
  // (PICs are created after initialization; only +TestArm64Runtime creates them without stubs)
  put_PIC_header(entry, StubRoutines::is_initialized() ? StubRoutines::PIC_stub_entry(c->number_of_targets()) : NULL);
  *(intptr_t*) (entry + PIC_count_offset) = c->number_of_targets();
  char* p = entry + PIC_entries_offset;
  // smis first (as on x86), then nmethods, then methodOops
  if (c->smi_nmethod != NULL) {
    *(klassOop*) p = smiKlassObj; *(char**) (p + PIC_target_offset) = c->smi_nmethod;            p += PIC_entry_size;
  }
  if (c->smi_methodOop != NULL) {
    *(klassOop*) p = smiKlassObj; *(char**) (p + PIC_target_offset) = (char*) c->smi_methodOop;  p += PIC_entry_size;
  }
  for (int i = 0; i < c->n; i++) {
    assert(((intptr_t) c->nmethods[i] & Tag_Mask) == 0, "nmethod entry points are aligned");
    *(klassOop*) p = c->nmethod_klasses[i]; *(char**) (p + PIC_target_offset) = c->nmethods[i];  p += PIC_entry_size;
  }
  for (int i = 0; i < c->m; i++) {
    *(klassOop*) p = c->methodOop_klasses[i]; *(char**) (p + PIC_target_offset) = (char*) c->methodOops[i]; p += PIC_entry_size;
  }
  assert(p - entry == c->code_size(), "PIC size inconsistent");
  return p - entry;
}

int PIC::code_for_megamorphic_case(char* entry) {
  put_PIC_header(entry, StubRoutines::megamorphic_ic_entry());
  *(symbolOop*) (entry + MIC_selector_offset) = selector();	// used for fast lookup
  return MIC_code_size;
}

#else // x86

int PIC::code_for_methodOops_only(char* entry, PIC_contents* c) {
  char* p = entry;
  put_byte(p, call_opcode);
  if (c->smi_methodOop == NULL) {
    // no smi methodOop
    put_disp(p, StubRoutines::PIC_stub_entry(c->m));
    assert(entry + PIC_methodOop_only_offset == p, "constant inconsistent");
  } else {
    // handle smi methodOop first
    put_disp(p, StubRoutines::PIC_stub_entry(1 + c->m));
    put_word(p, x86_imm32(smiKlassObj));
    put_word(p, x86_imm32(c->smi_methodOop));
    assert(entry + PIC_methodOop_only_offset + PIC_methodOop_entry_size == p, "constant value inconsistent with code pattern");
  }
  char* p1 = p;
  for (int i = 0; i < c->m; i++) {
    assert(c->methodOop_klasses[i] != smiKlassObj, "should not be smiKlassObj");
    put_word(p, x86_imm32(c->methodOop_klasses[i]));
    put_word(p, x86_imm32(c->methodOops[i]));
  }
  assert(p1 + c->m * PIC_methodOop_entry_size == p, "constant value inconsistent with code pattern");
  return p - entry;
}


int PIC::code_for_polymorphic_case(char* entry, PIC_contents* c) {
  if (c->has_nmethods()) {
    // nmethods & methodOops
    // test al, Mem_Tag
    char* p     = entry;
    char* fixup = NULL;
    put_byte(p, test_opcode);
    put_byte(p, Mem_Tag);
    // jz ...
    put_shrt(p, jz_opcode);
    if (c->smi_nmethod != NULL) {
      assert(c->smi_methodOop == NULL, "can only have one method for smis");
      put_disp(p, c->smi_nmethod);
    } else if (c->smi_methodOop != NULL) {
      // smi method is methodOop -> handle it in methodOop section
      fixup = p;
      put_disp(p, 0);
    } else {
      // no smi entries
      put_disp(p, CompiledIC::normalLookupRoutine()); // Fix this for super sends!
    }
    // always load klass to simplify decoding/iteration of PIC
    // mov edx, [eax.klass]
    put_shrt(p, mov_opcode);
    put_byte(p, memOopDesc::klass_byte_offset());
    assert(entry + PIC_nmethod_entry_offset == p, "constant value inconsistent with code pattern");
    // handle nmethods
    for (int i = 0; i < c->n; i++) {
      // cmp edx, klass(i)
      assert(c->nmethod_klasses[i] != smiKlassObj, "should not be smiKlassObj");
      put_shrt(p, cmp_opcode);
      assert(entry + PIC_nmethod_entry_offset + i*PIC_nmethod_entry_size + PIC_nmethod_klass_offset == p, "constant value inconsistent with code pattern");
      put_word(p, x86_imm32(c->nmethod_klasses[i]));
      // je nmethod(j)
      put_shrt(p, jz_opcode);
      assert(entry + PIC_nmethod_entry_offset + i*PIC_nmethod_entry_size + PIC_nmethod_offset == p, "constant value inconsistent with code pattern");
      put_disp(p, c->nmethods[i]);
    }
    assert(entry + PIC_nmethod_entry_offset + c->n*PIC_nmethod_entry_size == p, "constant value inconsistent with code pattern");
    if (c->smi_methodOop != NULL || c->m > 0) {
      // handle methodOops
      if (fixup != NULL) put_disp(fixup, p);
      p += code_for_methodOops_only(p, c);
    } else {
      // jmp cache_miss
      put_byte(p, jmp_opcode);
      put_disp(p, CompiledIC::normalLookupRoutine());
    }
    return p - entry;
  } else {
    // no nmethods -> call PIC stub routine directly
    return code_for_methodOops_only(entry, c);
  }
}


int PIC::code_for_megamorphic_case(char* entry) {
  char* p = entry;
  put_byte(p, call_opcode);
  put_disp(p, StubRoutines::megamorphic_ic_entry());
  assert(entry + MIC_selector_offset == p, "layout constant inconsistent with code pattern");
  put_word(p, x86_imm32(selector()));	// used for fast lookup
  assert(entry + MIC_code_size == p, "layout constant inconsistent with code pattern");
  return p - entry;
}

#endif // STRONGTALK_ARM64

void PIC::shrink_and_generate(PIC* pic, klassOop klass, void* method) {
  Unimplemented();
}


void* PIC::operator new(size_t size, int code_size){
  return Universe::code->picHeap->allocate(size + code_size);
}


void PIC::operator delete(void* p) {
  Universe::code->picHeap->deallocate(p, 0);
}


PIC* PIC::allocate(CompiledIC* ic, klassOop klass, LookupResult result) {
  assert(!result.is_empty(), "lookup result cannot be empty");

  PIC_contents contents;

  // Always add the new entry first
  if (result.is_entry()) {
    contents.append_nmethod_entry(klass, result.get_nmethod()->verifiedEntryPoint());
  } else {
    contents.append_method(klass, result.method());
  }

  PIC*     old_pic       = ic->pic();
  nmethod* old_nmethod   = ic->target();
  bool     switch_to_MIC = false;
  
  // 3 possible cases:
  //
  // 1. IC is empty but lookup result returns methodOop -> needs 1-element PIC to call methodOop
  // 2. IC contains 1 nmethod -> generate 2-element PIC
  // 3. IC contains pic -> grow PIC or switch to MIC

  if (old_pic != NULL) {
    // ic contains pic
    assert(old_nmethod == NULL, "just checking");
    assert(!old_pic->is_megamorphic(), "MICs should not change anymore");
    if (old_pic->number_of_targets() >= max_nof_entries) {
      if (UseMICs) {
        // switch to MIC, keep only no lookup result
        switch_to_MIC = true;
      } else {
        ic->resetOptimized();	// make sure it doesn't force the creation of nmethods
        return NULL;
      }
    } else {
      // append old PIC entries
      PIC_Iterator it(old_pic);
      while (!it.at_end()) {
        if (it.is_interpreted()) {
          contents.append_method(it.get_klass(), it.interpreted_method());
        } else {
          contents.append_nmethod_entry(it.get_klass(), it.get_call_addr());
        }
        it.advance();
      }
    }
  } else if (old_nmethod != NULL) {
    // ic contains 1 nmethod
    contents.append_nmethod_entry(ic->get_klass(0), old_nmethod->verifiedEntryPoint());
  } else {
    // empty ic -> nothing to do
    assert(ic->is_empty(), "just checking");
  }

  assert(switch_to_MIC ||
         contents.number_of_interpreted_targets() > 0 ||
         contents.number_of_compiled_targets() > 1,
	 "no PIC required for only 1 compiled target");

  PIC* new_pic = NULL;
  if (switch_to_MIC) {
    new_pic = new (MIC_code_size) PIC(ic);
  } else {
    int allocated_code_size = contents.code_size();
    new_pic = new (allocated_code_size) PIC(ic, &contents, allocated_code_size);
  }

  #ifdef ASSERT
    new_pic->verify();
  #endif

  return new_pic;
}



PIC::PIC(CompiledIC* ic, PIC_contents* contents, int allocated_code_size) {
  assert(contents->number_of_targets() >= 1, "at least one entry needed for non-megamorphic case");
  JITWriteScope write_code;	// PICs are built in the (code memory) PIC heap
  _ic			= ic;
  _number_of_targets	= contents->number_of_targets();
  _code_size		= code_for_polymorphic_case(entry(), contents);
  assert(code_size() == allocated_code_size, "Please adjust PIC_contents::code_size()");
  flushICacheRange(entry(), entry() + code_size());
}


PIC::PIC(CompiledIC* ic) {
  JITWriteScope write_code;
  _ic			= ic;
  _number_of_targets	= 0; // indicates megamorphic case
  _code_size		= code_for_megamorphic_case(entry());
  flushICacheRange(entry(), entry() + code_size());
  assert(code_size() == MIC_code_size, "Please adjust PIC_contents::code_size()");
}


GrowableArray<klassOop>* PIC::klasses() const {
  GrowableArray<klassOop>* k = new GrowableArray<klassOop>(2);
  PIC_Iterator it((PIC*)this);
  while(!it.at_end()) {
    k->append(it.get_klass());
    it.advance();
  }
  return k;
}


void PIC::oops_do(void f(oop*)) {
  JITWriteScope write_code;	// f may update the oops
#ifdef STRONGTALK_ARM64
  // all entries have a klass (also smis)
  if (!is_megamorphic()) {
    PIC_Iterator it(this);
    while (!it.at_end()) {
      f((oop*) it.klass_addr());
      if (it.state() == PIC_Iterator::at_methodOop) f((oop*) it.methodOop_addr());
      it.advance();
    }
    return;
  }
#endif
  if (is_megamorphic()) {
    // cannot use PIC_Iterator (0 entries) -> deal with MIC directly
    f((oop*)MIC_selector_address());
  } else {
    PIC_Iterator it(this);
    while(!it.at_end()) {
      switch(it.state()) {
        case PIC_Iterator::at_methodOop: f((oop*) it.methodOop_addr());  // fall through
        case PIC_Iterator::at_nmethod  : f((oop*) it.klass_addr());
      }
      it.advance();
    }
  }
}


void PIC::print() {
  lprintf("\tPIC with %d entr%s\n", number_of_targets(), number_of_targets() == 1 ? "y" : "ies");
  lprintf("\t- selector    : ");
  selector()->print_symbol_on();
  lprintf("\n");

  // Disassembler::decode(entry(), entry() + code_size());

  int i = 1;
  PIC_Iterator it(this);
  while (!it.at_end()) {
    lprintf("\t- %d. klass    : ", i);
    it.get_klass()->print_value();
    lprintf("\n");
    switch (it.state()) {
      case PIC_Iterator::at_smi_nmethod: // fall through
      case PIC_Iterator::at_nmethod    : printf("\t-    nmethod  : %#x (entry %#x)\n", 
      						    x86_imm32(it.compiled_method()), x86_imm32(it.get_call_addr())); break;
      case PIC_Iterator::at_methodOop  : printf("\t-    methodOop: %s\n", it.interpreted_method()->print_value_string()); break;
      default: ShouldNotReachHere();
    }
    i++;
    it.advance();
  }
}


void PIC::verify() {
  // check for multiple entries for same class
  ResourceMark rm;
  GrowableArray<klassOop>* k = klasses();
  for (int i = 0; i < k->length() - 1; i++) {
    for (int j = i + 1; j < k->length(); j++) {
      if (k->at(i) == k->at(j)) {
	stdOut->print("The class ");
        k->at(i)->klass_part()->print_name_on(stdOut);
	stdOut->print_cr("is twice in PIC 0x%lx", this);
        warning("PIC verify error");
      }
    }
  }
}


// Implementation of CompiledIC_Iterator

CompiledIC_Iterator::CompiledIC_Iterator(CompiledIC* ic) {
  _ic = ic;
  init_iteration();
}


void CompiledIC_Iterator::init_iteration() {
  _picit = NULL;
  _index = 0;
  if (_ic->is_empty()) {
    _number_of_targets = 0;
    _shape = anamorphic;
  } else if (_ic->is_monomorphic()) {
    _number_of_targets = 1;
    _shape = monomorphic;
    PIC* pic = _ic->pic();
    if (pic) _picit = new PIC_Iterator(pic);  // calls an interpreted method
  } else if (_ic->is_polymorphic()) {
    PIC* pic = _ic->pic();
    _number_of_targets = pic->number_of_targets();
    _shape = polymorphic;
    _picit = new PIC_Iterator(pic);
  } else if (_ic->is_megamorphic()) {
    _number_of_targets = 0;
    _shape = megamorphic;
  } else {
    ShouldNotReachHere();
  }
}


void CompiledIC_Iterator::advance() {
  assert(!at_end(), "iterated over the end");
  if (_picit != NULL) {
    _picit->advance();
  }
  _index++;
}


klassOop CompiledIC_Iterator::klass() const {
  assert(!at_end(), "iterated over the end");
  if (_picit != NULL) {
    return _picit->get_klass();
  } else {
    return _ic->get_klass(0);
  }
}


bool CompiledIC_Iterator::is_interpreted() const {
  assert(!at_end(), "iterated over the end");
  if (_picit != NULL) {
    return _picit->is_interpreted();
  } else {
    return false;
  }
}


bool CompiledIC_Iterator::is_compiled() const {
  assert(!at_end(), "iterated over the end");
  if (_picit != NULL) {
    return _picit->is_compiled();
  } else {
    return true;
  }
}


bool CompiledIC_Iterator::is_super_send() const {
  extern bool SuperSendsAreAlwaysInlined;
  assert(SuperSendsAreAlwaysInlined, "fix this");
  return false;		// for now, super sends are always inlined
}


methodOop CompiledIC_Iterator::interpreted_method() const {
  assert(!at_end(), "iterated over the end");
  if (_picit != NULL) {
    return _picit->interpreted_method();
  } else {
    return compiled_method()->method();
  }
}


nmethod* CompiledIC_Iterator::compiled_method() const {
  assert(!at_end(), "iterated over the end");
  if (_picit != NULL) {
    return _picit->compiled_method();
  } else {
    assert(number_of_targets() == 1, "must be monomorphic");
    return _ic->target();
  }
}


void CompiledIC_Iterator::print() {
  lprintf("CompiledIC_Iterator for ((CompiledIC*)%#x) (%s)\n", _ic, selector()->as_string());
}

#ifdef STRONGTALK_ARM64

class FirstMethods : public ObjectClosure {
 public:
  methodOop methods[3];
  int n;
  FirstMethods() : n(0) {}
  void do_object(memOop obj) { if (n < 3 && obj->is_method()) methods[n++] = methodOop(obj); }
};

static int pic_test_oops;
static void count_pic_oop(oop* p) { pic_test_oops++; }

bool PIC_arm64_test() {
  ResourceMark rm;
  int failures = 0;
#define PIC_CHECK(cond, what) if (!(cond)) { failures++; lprintf("  FAILED %s\n", what); }
  klassOop k1 = Universe::nilObj()->klass(), k2 = Universe::trueObj()->klass(), k3 = Universe::falseObj()->klass(), k4 = doubleKlassObj;
  FirstMethods fm;
  Universe::object_iterate(&fm);
  if (fm.n < 3) { lprintf("  FAILED: no methods in the heap\n"); return false; }
  methodOop m1 = fm.methods[0], m3 = fm.methods[2];
  char* n0 = (char*) 0x10000, *n1 = (char*) 0x20000, *n2 = (char*) 0x30000;
  PIC_contents c;
  c.append_nmethod_entry(smiKlassObj, n0);
  c.append_nmethod_entry(k1, n1);
  c.append_nmethod_entry(k2, n2);
  c.append_method(k3, m1);		// 4 entries (PIC::max_nof_entries)
  int size = c.code_size();
  PIC_CHECK(size == PIC::PIC_entries_offset + 4 * PIC::PIC_entry_size, "PIC code size");
  PIC* pic = new (size) PIC((CompiledIC*) NULL, &c, size);
  PIC_CHECK(((intptr_t) pic->entry() & 7) == 0, "PIC entry alignment");
  PIC_CHECK(pic->number_of_targets() == 4 && pic->is_polymorphic(), "PIC targets");
  a64::insn* code = (a64::insn*) pic->entry();
  PIC_CHECK(code[0] == a64::ldr_literal(16, 16) && code[1] == a64::adr(17, 20) && code[2] == a64::br(16), "PIC header");
  if (StubRoutines::is_initialized()) PIC_CHECK(*(char**) (pic->entry() + PIC::PIC_stub_offset) == StubRoutines::PIC_stub_entry(4), "PIC stub");
  // iteration: smis, nmethods, methodOops
  klassOop  klasses[] = { smiKlassObj, k1, k2, k3 };
  bool      compiled[] = { true, true, true, false };
  char*     targets[] = { n0, n1, n2, (char*) m1 };
  PIC_Iterator it(pic);
  for (int i = 0; i < 4; i++) {
    PIC_CHECK(!it.at_end(), "PIC iterator ends early");
    if (it.at_end()) break;
    PIC_CHECK(it.get_klass() == klasses[i], "PIC entry klass");
    PIC_CHECK(it.is_compiled() == compiled[i], "PIC entry kind");
    if (compiled[i]) { PIC_CHECK(it.get_call_addr() == targets[i], "PIC nmethod target"); }
    else             { PIC_CHECK((char*) it.interpreted_method() == targets[i], "PIC methodOop target"); }
    it.advance();
  }
  PIC_CHECK(it.at_end(), "PIC iterator end");
  // modifying entries
  { PIC_Iterator m(pic);
    m.advance(); m.advance(); m.advance();	// the first methodOop entry
    m.set_methodOop(m3);
    m.set_klass(k1);
    PIC_Iterator r(pic);
    r.advance(); r.advance(); r.advance();
    PIC_CHECK(r.interpreted_method() == m3 && r.get_klass() == k1, "PIC entry update");
  }
  pic_test_oops = 0;
  pic->oops_do(&count_pic_oop);
  PIC_CHECK(pic_test_oops == 5, "PIC oops_do visits 4 klasses and 1 methodOop");
  PIC_CHECK(PIC::find(pic->entry() + 40) == pic, "PIC::find");
  delete pic;

  // dispatch through the PIC stub routine: the targets are code returning 100 + their index
  if (StubRoutines::is_initialized()) {
    char* code = os::allocate_code_memory(16 * 1024);
    char* targets[4];
    PIC* dpic;
    { JITWriteScope write_code;
    for (int i = 0; i < 4; i++) {
      CodeBuffer cb(code + i * 64, 64);
      MacroAssembler m(&cb);
      m.movl(eax, 100 + i);
      m.ret(0);
      targets[i] = code + i * 64;
    }
    PIC_contents d;
    d.append_nmethod_entry(smiKlassObj, targets[0]);
    d.append_nmethod_entry(k1, targets[1]);
    d.append_nmethod_entry(k2, targets[2]);
    d.append_nmethod_entry(k3, targets[3]);
    int dsize = d.code_size();
    dpic = new (dsize) PIC((CompiledIC*) NULL, &d, dsize);
    // a C callable function: f(receiver, delta_stack) calls the PIC as a compiled send would
    CodeBuffer cb(code + 1024, 1024);
    MacroAssembler m(&cb);
    m.emit(a64::sub_imm(a64::sp, a64::sp, 64));
    int saved[] = { 19, 20, 21, 28, 29, 30 };
    for (int i = 0; i < 6; i++) m.emit(a64::ldst_uimm(a64::STR, 3, saved[i], a64::sp, 8 * i));
    m.emit(a64::mov_reg(28, 1));			// esp := the Delta stack
    m.call(dpic->entry(), relocInfo::runtime_call_type);	// (this code buffer has no relocation information)
    for (int i = 0; i < 6; i++) m.emit(a64::ldst_uimm(a64::LDR, 3, saved[i], a64::sp, 8 * i));
    m.emit(a64::add_imm(a64::sp, a64::sp, 64));
    m.emit(a64::ret());
    }
    os::flush_icache(code, 2048);
    typedef intptr_t (*Caller)(oop receiver, oop* stack);
    Caller f = (Caller) (code + 1024);
    static oop stack[64];
    PIC_CHECK(f(as_smiOop(17), &stack[48]) == 100, "PIC dispatch: smi");
    PIC_CHECK(f(Universe::nilObj(), &stack[48]) == 101, "PIC dispatch: nil");
    PIC_CHECK(f(Universe::trueObj(), &stack[48]) == 102, "PIC dispatch: true");
    PIC_CHECK(f(Universe::falseObj(), &stack[48]) == 103, "PIC dispatch: false");
    PIC_CHECK(*(char**) (dpic->entry() + PIC::PIC_stub_offset) == StubRoutines::PIC_stub_entry(4), "PIC stub address");
    // retargeting an entry in place
    { PIC_Iterator it(dpic);
      it.advance();				// nil's entry
      native_code_write_word((char*) (dpic->entry() + PIC::PIC_entries_offset + PIC::PIC_entry_size + PIC::PIC_target_offset), (intptr_t) targets[3]);
    }
    PIC_CHECK(f(Universe::nilObj(), &stack[48]) == 103, "PIC dispatch after retargeting");
    delete dpic;
    lprintf("PIC arm64 dispatch: executed\n");
  }
  lprintf("PIC arm64 test: %d failures\n", failures);
  return failures == 0;
#undef PIC_CHECK
}

#endif // STRONGTALK_ARM64

#endif
