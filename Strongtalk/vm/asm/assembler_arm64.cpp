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

// The arm64 assembler with the interface of the x86 assembler (see assembler_arm64.hpp).

#include "config.hpp"
#ifdef STRONGTALK_ARM64

#include "error.hpp"
#include "oopsHierarchy.hpp"
#include "longInt.hpp"
#include "types.hpp"
#include "util.hpp"
#include "asserts.hpp"
#include "macros.hpp"
#include "bits.hpp"
#include "lprintf.hpp"
#include "allocation.hpp"
#include "ostream.hpp"
#include "tag.hpp"
#include "top.hpp"
#include "iCache.hpp"
#include "nativeInstruction.hpp"
#include "iterator.hpp"
#include "growableArray.hpp"
#include "runtime.hpp"
#include "compiledIC.hpp"
#include "relocInfo.hpp"
#include "codeBuffer.hpp"
#include "assembler.hpp"
#include "os.hpp"
#include "dll.hpp"
#include "stubRoutines.hpp"
#include "debug.hpp"
#include "process.hpp"
#include "smiOop.hpp"
#include "rSet.hpp"
#include "universe.hpp"
#include <math.h>

extern "C" bool     have_nlr_through_C;	// abort.cpp
extern "C" oop      nlr_result;
extern "C" intptr_t nlr_home;
extern "C" int      nlr_home_id;

using namespace a64;

// scratch registers of the lowering (never x86 registers)
const int S0 = 16;	// data
const int S1 = 17;	// addresses, return addresses
const int T0 = 9;	// data
const int T1 = 10;	// data
const int T2 = 11;	// address displacements
const int FS = 31;	// d31: FP scratch
const int fpu_base = 8;	// ST registers: d8 .. d15 (callee saved in C)

// x86 register number -> arm64 register
static const int arm64_register[nofRegisters] = {
  0,	// eax
  1,	// ecx
  2,	// edx
  19,	// ebx
  28,	// esp (Delta stack pointer)
  29,	// ebp
  20,	// esi
  21	// edi
};

int Assembler::arm(Register r) {
  return arm64_register[r.number()];
}

static inline bool is_esp(Register r) { return r.isValid() && r.number() == 4; }


// Implementation of Assembler

Assembler::Assembler(CodeBuffer* code) {
  _code          = code;
  _code_begin    = code->code_begin();
  _code_limit    = code->code_limit();
  _code_pos      = code->code_end();
  _binding_pos   = 0;
  _flags_kind    = flags_unknown;
  _flags_reg     = 0;
  _flags_pending = false;
  _fpu_compared  = true;
  _fpu_depth     = 0;
  assert(((uintptr_t) _code_begin & 7) == 0, "code buffer must be 8-byte aligned (literals)");
}


void Assembler::finalize() {
  // nothing delayed (no jump elimination)
}


void Assembler::emit(insn i) {
  assert(_code_pos + sizeof(insn) <= _code_limit, "code buffer overflow");
  *(insn*) _code_pos = i;
  _code_pos += sizeof(insn);
  code()->set_code_end(_code_pos);
}


void Assembler::emit_word64(intptr_t x, relocInfo::relocType rtype) {
  assert((offset() & 7) == 0, "literal must be 8-byte aligned");
  assert(_code_pos + sizeof(intptr_t) <= _code_limit, "code buffer overflow");
  if (rtype != relocInfo::none) code()->relocate(_code_pos, rtype);
  *(intptr_t*) _code_pos = x;
  _code_pos += sizeof(intptr_t);
  code()->set_code_end(_code_pos);
}


void Assembler::align_for_literal_at(int offset_in_sequence) {
  while (((offset() + offset_in_sequence) & 7) != 0) emit(a64::nop());
}


// ---------------------------------------------------------------------------
// Operands

void Assembler::mov_imm(int rd, intptr_t imm) {
  uint64_t v = (uint64_t) imm;
  int zeros = 0, ones = 0;
  for (int i = 0; i < 4; i++) {
    int hw = (int) ((v >> (16 * i)) & 0xFFFF);
    if (hw == 0)      zeros++;
    if (hw == 0xFFFF) ones++;
  }
  bool first = true;
  if (ones > zeros) {
    for (int i = 0; i < 4; i++) {
      int hw = (int) ((v >> (16 * i)) & 0xFFFF);
      if (hw == 0xFFFF) continue;
      emit(first ? movn(rd, ~hw & 0xFFFF, i) : movk(rd, hw, i));
      first = false;
    }
    if (first) emit(movn(rd, 0, 0));	// -1
  } else {
    for (int i = 0; i < 4; i++) {
      int hw = (int) ((v >> (16 * i)) & 0xFFFF);
      if (hw == 0) continue;
      emit(first ? movz(rd, hw, i) : movk(rd, hw, i));
      first = false;
    }
    if (first) emit(movz(rd, 0, 0));	// 0
  }
}


// ldr rd, literal: [ldr rd, #8] [b #12] [8-byte literal]
void Assembler::mov_literal(int rd, intptr_t value, relocInfo::relocType rtype) {
  align_for_literal_at(8);
  emit(ldr_literal(rd, 8));
  emit(b(12));
  emit_word64(value, rtype);
}


// Computes the base register and offset of a load/store of 1 << size bytes; emits
// instructions computing the address into S1 if needed (S1 and T2 are clobbered).
int Assembler::address(Address adr, int size, int* offset, bool* scaled) {
  intptr_t disp = adr._disp;
  int base;
  if (!adr._base.isValid() && !adr._index.isValid()) {
    // absolute address; VM addresses do not move, so external words need no relocation
    relocInfo::relocType rtype = adr._rtype == relocInfo::external_word_type ? relocInfo::none : adr._rtype;
    mov_imm(S1, disp, rtype);
    base = S1;
    disp = 0;
  } else if (adr._index.isValid()) {
    int scale = adr._scale;
    if (adr._base.isValid()) {
      emit(add_reg(S1, arm(adr._base), arm(adr._index), LSL, scale));
    } else {
      emit(lsl_imm(S1, arm(adr._index), scale));
    }
    base = S1;
  } else {
    base = arm(adr._base);
  }
  int unit = 1 << size;
  if ((disp & (unit - 1)) == 0 && 0 <= disp && (disp >> size) < 4096) {
    *offset = (int) disp; *scaled = true;
  } else if (is_simm9(disp)) {
    *offset = (int) disp; *scaled = false;
  } else {
    mov_imm(T2, disp);
    emit(add_reg(S1, base, T2));
    base = S1;
    *offset = 0; *scaled = true;
  }
  return base;
}


void Assembler::load(int size, bool sign, int rt, Address adr) {
  int off; bool scaled;
  int base = address(adr, size, &off, &scaled);
  MemOp op = (sign && size < 3) ? LDRS : LDR;
  emit(scaled ? ldst_uimm(op, size, rt, base, off) : ldst_simm9(op, size, rt, base, off));
}


void Assembler::store(int size, int rt, Address adr) {
  int off; bool scaled;
  int base = address(adr, size, &off, &scaled);
  emit(scaled ? ldst_uimm(STR, size, rt, base, off) : ldst_simm9(STR, size, rt, base, off));
}


void Assembler::fload(int size, int dt, Address adr) {
  int off; bool scaled;
  int base = address(adr, size, &off, &scaled);
  emit(scaled ? fldst_uimm(LDR, size, dt, base, off) : fldst_simm9(LDR, size, dt, base, off));
}


void Assembler::fstore(int size, int dt, Address adr) {
  int off; bool scaled;
  int base = address(adr, size, &off, &scaled);
  emit(scaled ? fldst_uimm(STR, size, dt, base, off) : fldst_simm9(STR, size, dt, base, off));
}


// ---------------------------------------------------------------------------
// Flags

void Assembler::materialize_flags() {
  if (_flags_pending) {
    emit(tst_reg(_flags_reg, _flags_reg));
    _flags_pending = false;
  }
}


Cond Assembler::condition(Condition cc) {
  materialize_flags();
  switch (cc) {
    case equal:		return EQ;
    case notEqual:	return NE;
    case less:		return LT;
    case lessEqual:	return LE;
    case greater:	return GT;
    case greaterEqual:	return GE;
    case negative:	return MI;
    case positive:	return PL;
    case overflow:	return _flags_kind == flags_mul ? NE : VS;
    case noOverflow:	return _flags_kind == flags_mul ? EQ : VC;
    default: break;
  }
  // carry based conditions
  switch (_flags_kind) {
    case flags_sub:
    case flags_unknown:
      switch (cc) {
        case below:      return LO;
        case aboveEqual: return HS;
        case belowEqual: return LS;
        case above:      return HI;
        default: break;
      }
      break;
    case flags_add:
    case flags_logical:
      // C is the x86 carry
      switch (cc) {
        case carrySet:   return HS;
        case carryClear: return LO;
        case belowEqual: if (_flags_kind == flags_logical) return EQ; break;	// CF = 0
        case above:      if (_flags_kind == flags_logical) return NE; break;
        default: break;
      }
      break;
    default:
      break;
  }
  fatal1("arm64 assembler: x86 condition 0x%x is not available after this operation", cc);
  return AL;
}


void Assembler::x86_carry(int rd) {
  materialize_flags();
  switch (_flags_kind) {
    case flags_add:
    case flags_logical: emit(cset(rd, HS)); break;
    case flags_sub:     emit(cset(rd, LO)); break;
    default: fatal("arm64 assembler: x86 carry flag is not available after this operation");
  }
}


void Assembler::arith_imm(bool sub, int rd, int rn, intptr_t imm, bool flags) {
  if (flags) {
    // exact x86 flags: the immediate is the operand (no negation)
    if (is_uimm12(imm)) {
      emit(sub ? subs_imm(rd, rn, (int) imm) : adds_imm(rd, rn, (int) imm));
    } else {
      mov_imm(S0, imm);
      emit(sub ? subs_reg(rd, rn, S0) : adds_reg(rd, rn, S0));
    }
    set_flags(sub ? flags_sub : flags_add);
  } else {
    intptr_t v = sub ? -imm : imm;
    if (is_uimm12(v))       emit(add_imm(rd, rn, (int) v));
    else if (is_uimm12(-v)) emit(sub_imm(rd, rn, (int) -v));
    else { mov_imm(S0, v); emit(add_reg(rd, rn, S0)); }
  }
}


void Assembler::logical(int op, int rd, int rn, int rm) {
  switch (op) {
    case 0: emit(ands_reg(rd, rn, rm)); set_flags(flags_logical); return;
    case 1: emit(orr_reg(rd, rn, rm)); break;
    case 2: emit(eor_reg(rd, rn, rm)); break;
  }
  emit(tst_reg(rd, rd));
  set_flags(flags_logical);
}


// ---------------------------------------------------------------------------
// Stack

void Assembler::pushad() {
  // as x86: eax, ecx, edx, ebx, esp (before pushad), ebp, esi, edi
  emit(mov_reg(S0, arm(esp)));
  pushl(eax); pushl(ecx); pushl(edx); pushl(ebx);
  emit(ldst_pre(STR, 3, S0, arm(esp), -oopSize));
  pushl(ebp); pushl(esi); pushl(edi);
}


void Assembler::popad() {
  popl(edi); popl(esi); popl(ebp);
  emit(add_imm(arm(esp), arm(esp), oopSize));	// skip esp
  popl(ebx); popl(edx); popl(ecx); popl(eax);
}


void Assembler::pushl(int imm32) {
  pushl((intptr_t) imm32);
}


void Assembler::pushl(intptr_t imm) {
  mov_imm(S0, imm);
  emit(ldst_pre(STR, 3, S0, arm(esp), -oopSize));
}


void Assembler::pushl(unsigned int imm32) {
  pushl((intptr_t) imm32);
}


void Assembler::pushl(oop obj) {
  if (obj->is_mem()) mov_literal(S0, (intptr_t) obj, relocInfo::oop_type);
  else               mov_imm(S0, (intptr_t) obj);
  emit(ldst_pre(STR, 3, S0, arm(esp), -oopSize));
}


void Assembler::pushl(Register src) {
  int r = arm(src);
  if (is_esp(src)) {	// pushes the value before the push (writeback to the stored register is undefined)
    emit(mov_reg(S0, r));
    r = S0;
  }
  emit(ldst_pre(STR, 3, r, arm(esp), -oopSize));
}


void Assembler::pushl(Address src) {
  load(3, false, S0, src);
  emit(ldst_pre(STR, 3, S0, arm(esp), -oopSize));
}


void Assembler::popl(Register dst) {
  if (is_esp(dst)) {
    emit(ldst_uimm(LDR, 3, arm(esp), arm(esp), 0));
  } else {
    emit(ldst_post(LDR, 3, arm(dst), arm(esp), oopSize));
  }
}


void Assembler::popl(Address dst) {
  // as x86: the address is computed after esp has been incremented
  emit(ldst_post(LDR, 3, S0, arm(esp), oopSize));
  store(3, S0, dst);
}


// ---------------------------------------------------------------------------
// Moves

void Assembler::movb(Register dst, Address src) {
  load(0, false, S0, src);
  emit(bfxil(arm(dst), S0, 0, 8));	// x86 only replaces the low byte
}


void Assembler::movb(Address dst, int imm8) {
  mov_imm(S0, imm8 & 0xFF);
  store(0, S0, dst);
}


void Assembler::movb(Address dst, Register src) {
  store(0, arm(src), dst);
}


void Assembler::movw(Register dst, Address src) {
  load(1, false, S0, src);
  emit(bfxil(arm(dst), S0, 0, 16));
}


void Assembler::movw(Address dst, Register src) {
  store(1, arm(src), dst);
}


void Assembler::movl(Register dst, int imm32) {
  mov_imm(arm(dst), imm32);
}


void Assembler::movl(Register dst, intptr_t imm) {
  mov_imm(arm(dst), imm);
}


void Assembler::movl(Register dst, unsigned int imm32) {
  mov_imm(arm(dst), (intptr_t) imm32);
}


void Assembler::movl(Register dst, oop obj) {
  if (obj->is_mem()) mov_literal(arm(dst), (intptr_t) obj, relocInfo::oop_type);
  else               mov_imm(arm(dst), (intptr_t) obj);
}


void Assembler::movl(Register dst, Register src) {
  if (dst != src) emit(mov_reg(arm(dst), arm(src)));
}


void Assembler::movl(Register dst, Address src) {
  load(3, false, arm(dst), src);
}


void Assembler::movl(Address dst, int imm32) {
  movl(dst, (intptr_t) imm32);
}


void Assembler::movl(Address dst, intptr_t imm) {
  int r = zr;
  if (imm != 0) { mov_imm(S0, imm); r = S0; }
  store(3, r, dst);
}


void Assembler::movl(Address dst, unsigned int imm32) {
  movl(dst, (intptr_t) imm32);
}


void Assembler::movl(Address dst, oop obj) {
  if (obj->is_mem()) mov_literal(S0, (intptr_t) obj, relocInfo::oop_type);
  else               mov_imm(S0, (intptr_t) obj);
  store(3, S0, dst);
}


void Assembler::movl(Address dst, Register src) {
  store(3, arm(src), dst);
}


void Assembler::movsxb(Register dst, Address src) { load(0, true, arm(dst), src); }
void Assembler::movsxb(Register dst, Register src) { emit(sxtb(arm(dst), arm(src))); }
void Assembler::movsxw(Register dst, Address src) { load(1, true, arm(dst), src); }
void Assembler::movsxw(Register dst, Register src) { emit(sxth(arm(dst), arm(src))); }


// ---------------------------------------------------------------------------
// Conditional moves

void Assembler::cmovccl(Condition cc, Register dst, int imm32) {
  Cond c = condition(cc);
  mov_imm(S0, imm32);
  emit(csel(arm(dst), S0, arm(dst), c));
}


void Assembler::cmovccl(Condition cc, Register dst, oop obj) {
  Cond c = condition(cc);
  if (obj->is_mem()) mov_literal(S0, (intptr_t) obj, relocInfo::oop_type);
  else               mov_imm(S0, (intptr_t) obj);
  emit(csel(arm(dst), S0, arm(dst), c));
}


void Assembler::cmovccl(Condition cc, Register dst, Register src) {
  emit(csel(arm(dst), arm(src), arm(dst), condition(cc)));
}


void Assembler::cmovccl(Condition cc, Register dst, Address src) {
  Cond c = condition(cc);
  load(3, false, S0, src);
  emit(csel(arm(dst), S0, arm(dst), c));
}


// ---------------------------------------------------------------------------
// Arithmetics

void Assembler::adcl(Register dst, Register src) {
  // dst := dst + src + CF; arm64 adcs adds C, which must be the x86 carry
  if (_flags_kind != flags_add) {
    x86_carry(S0);
    emit(subs_imm(zr, S0, 1));		// C := (S0 >= 1) = CF
  }
  emit(adcs(arm(dst), arm(dst), arm(src)));
  set_flags(flags_add);
}


void Assembler::adcl(Register dst, int imm32) {
  if (_flags_kind != flags_add) {
    x86_carry(S0);
    emit(subs_imm(zr, S0, 1));
  }
  mov_imm(T0, imm32);			// does not change the flags
  emit(adcs(arm(dst), arm(dst), T0));
  set_flags(flags_add);
}


void Assembler::addl(Address dst, int imm32) {
  load(3, false, T0, dst);
  arith_imm(false, T0, T0, imm32, true);
  store(3, T0, dst);
}


void Assembler::addl(Register dst, int imm32) {
  arith_imm(false, arm(dst), arm(dst), imm32, true);
}


void Assembler::addl(Register dst, Register src) {
  emit(adds_reg(arm(dst), arm(dst), arm(src)));
  set_flags(flags_add);
}


void Assembler::andl(Register dst, int imm32) {
  mov_imm(S0, imm32);
  logical(0, arm(dst), arm(dst), S0);
}


void Assembler::andl(Register dst, Register src) {
  logical(0, arm(dst), arm(dst), arm(src));
}


void Assembler::cmpl(Address dst, int imm32) {
  load(3, false, T0, dst);
  arith_imm(true, zr, T0, imm32, true);
}


void Assembler::cmpl(Address dst, oop obj) {
  load(3, false, T0, dst);
  if (obj->is_mem()) mov_literal(S0, (intptr_t) obj, relocInfo::oop_type);
  else               mov_imm(S0, (intptr_t) obj);
  emit(cmp_reg(T0, S0));
  set_flags(flags_sub);
}


void Assembler::cmpl(Register dst, int imm32) {
  arith_imm(true, zr, arm(dst), imm32, true);
}


void Assembler::cmpl(Register dst, intptr_t imm) {
  arith_imm(true, zr, arm(dst), imm, true);
}


void Assembler::cmpl(Register dst, oop obj) {
  if (obj->is_mem()) mov_literal(S0, (intptr_t) obj, relocInfo::oop_type);
  else               mov_imm(S0, (intptr_t) obj);
  emit(cmp_reg(arm(dst), S0));
  set_flags(flags_sub);
}


void Assembler::cmpl(Register dst, Register src) {
  emit(cmp_reg(arm(dst), arm(src)));
  set_flags(flags_sub);
}


void Assembler::cmpl(Register dst, Address src) {
  load(3, false, S0, src);
  emit(cmp_reg(arm(dst), S0));
  set_flags(flags_sub);
}


void Assembler::decb(Register dst) {
  // byte arithmetic in the top byte gives the x86 byte flags
  emit(lsl_imm(S0, arm(dst), 56));
  emit(movz(T0, 0x0100, 3));		// 1 << 56
  emit(subs_reg(S0, S0, T0));
  emit(bfxil(arm(dst), S0, 56, 8));
  set_flags(flags_sub);
}


void Assembler::decl(Register dst) {
  emit(subs_imm(arm(dst), arm(dst), 1));
  set_flags(flags_sub);
}


void Assembler::idivl(Register src) {
  // eax := edx:eax / src, edx := remainder (signed); edx must be the sign extension of eax
  int s = arm(src);
  if (s == arm(eax) || s == arm(edx)) { emit(mov_reg(T0, s)); s = T0; }
  emit(sdiv(S0, arm(eax), s));
  emit(msub(arm(edx), S0, s, arm(eax)));	// edx := eax - quotient * src
  emit(mov_reg(arm(eax), S0));
  set_flags(flags_unknown);
}


void Assembler::imull(Register dst, Register src) {
  // the product overflows if the high word is not the sign extension of the low word
  int d = arm(dst), s = arm(src);
  emit(smulh(S0, d, s));
  emit(a64::mul(d, d, s));
  emit(subs_reg(zr, S0, d, ASR, 63));
  set_flags(flags_mul);
}


void Assembler::imull(Register dst, Register src, int value) {
  int d = arm(dst), s = arm(src);
  mov_imm(T0, value);
  emit(smulh(S0, s, T0));
  emit(a64::mul(d, s, T0));
  emit(subs_reg(zr, S0, d, ASR, 63));
  set_flags(flags_mul);
}


void Assembler::incl(Register dst) {
  emit(adds_imm(arm(dst), arm(dst), 1));
  set_flags(flags_add);
}


void Assembler::incl(Address dst) {
  load(3, false, T0, dst);
  emit(adds_imm(T0, T0, 1));
  store(3, T0, dst);
  set_flags(flags_add);
}


void Assembler::leal(Register dst, Address src) {
  int off; bool scaled;
  int base = address(src, 0, &off, &scaled);
  if (off >= 0) emit(add_imm(arm(dst), base, off));
  else          emit(sub_imm(arm(dst), base, -off));
}


void Assembler::mull(Register src) {
  // edx:eax := eax * src (unsigned); CF = OF = (edx != 0)
  int s = arm(src);
  emit(umulh(S0, arm(eax), s));
  emit(a64::mul(arm(eax), arm(eax), s));
  emit(mov_reg(arm(edx), S0));
  emit(subs_imm(zr, arm(edx), 0));
  set_flags(flags_mul);
}


void Assembler::negl(Register dst) {
  emit(negs_reg(arm(dst), arm(dst)));
  set_flags(flags_sub);
}


void Assembler::notl(Register dst) {
  emit(mvn_reg(arm(dst), arm(dst)));
}


void Assembler::orl(Register dst, int imm32) {
  mov_imm(S0, imm32);
  logical(1, arm(dst), arm(dst), S0);
}


void Assembler::orl(Register dst, Register src) {
  logical(1, arm(dst), arm(dst), arm(src));
}


void Assembler::orl(Register dst, Address src) {
  load(3, false, S0, src);
  logical(1, arm(dst), arm(dst), S0);
}


void Assembler::rcll(Register dst, int imm8) {
  Unimplemented();	// not used by the arm64 code generators
}


void Assembler::sarl(Register dst, int imm8) {
  assert(isShiftCount(imm8), "illegal shift count");
  emit(asr_imm(arm(dst), arm(dst), imm8));
  emit(tst_reg(arm(dst), arm(dst)));
  set_flags(flags_shift);
}


void Assembler::sarl(Register dst) {
  emit(asrv(arm(dst), arm(dst), arm(ecx)));
  emit(tst_reg(arm(dst), arm(dst)));
  set_flags(flags_shift);
}


void Assembler::sbbl(Register dst, Register src) {
  // dst := dst - (src + CF); arm64 sbcs subtracts !C, so C must be the inverted x86 carry
  if (_flags_kind != flags_sub) {
    x86_carry(S0);
    emit(subs_reg(zr, zr, S0));		// C := (0 >= S0) = !CF
  }
  emit(sbcs(arm(dst), arm(dst), arm(src)));
  set_flags(flags_sub);
}


void Assembler::sbbl(Register dst, int imm32) {
  if (_flags_kind != flags_sub) {
    x86_carry(S0);
    emit(subs_reg(zr, zr, S0));
  }
  mov_imm(T0, imm32);
  emit(sbcs(arm(dst), arm(dst), T0));
  set_flags(flags_sub);
}


void Assembler::shldl(Register dst, Register src) {
  Unimplemented();	// not used by the arm64 code generators
}


void Assembler::shll(Register dst, int imm8) {
  assert(isShiftCount(imm8), "illegal shift count");
  emit(lsl_imm(arm(dst), arm(dst), imm8));
  emit(tst_reg(arm(dst), arm(dst)));
  set_flags(flags_shift);
}


void Assembler::shll(Register dst) {
  emit(lslv(arm(dst), arm(dst), arm(ecx)));
  emit(tst_reg(arm(dst), arm(dst)));
  set_flags(flags_shift);
}


void Assembler::shrdl(Register dst, Register src) {
  Unimplemented();	// not used by the arm64 code generators
}


void Assembler::shrl(Register dst, int imm8) {
  assert(isShiftCount(imm8), "illegal shift count");
  emit(lsr_imm(arm(dst), arm(dst), imm8));
  emit(tst_reg(arm(dst), arm(dst)));
  set_flags(flags_shift);
}


void Assembler::shrl(Register dst) {
  emit(lsrv(arm(dst), arm(dst), arm(ecx)));
  emit(tst_reg(arm(dst), arm(dst)));
  set_flags(flags_shift);
}


void Assembler::subl(Register dst, int imm32) {
  arith_imm(true, arm(dst), arm(dst), imm32, true);
}


void Assembler::subl(Register dst, Register src) {
  emit(subs_reg(arm(dst), arm(dst), arm(src)));
  set_flags(flags_sub);
}


void Assembler::testb(Register dst, int imm8) {
  // byte test in the top byte gives the x86 byte flags (sign = bit 7)
  emit(lsl_imm(S0, arm(dst), 56));
  emit(movz(T0, (imm8 & 0xFF) << 8, 3));	// imm8 << 56
  emit(tst_reg(S0, T0));
  set_flags(flags_logical);
}


void Assembler::testl(Register dst, int imm32) {
  mov_imm(S0, imm32);
  emit(tst_reg(arm(dst), S0));
  set_flags(flags_logical);
}


void Assembler::testl(Register dst, Register src) {
  emit(tst_reg(arm(dst), arm(src)));
  set_flags(flags_logical);
}


void Assembler::xorl(Register dst, int imm32) {
  mov_imm(S0, imm32);
  logical(2, arm(dst), arm(dst), S0);
}


void Assembler::xorl(Register dst, Register src) {
  logical(2, arm(dst), arm(dst), arm(src));
}


// ---------------------------------------------------------------------------
// Miscellaneous

void Assembler::hlt()  { emit(brk(0xF000)); }
void Assembler::int3() { emit(brk(3)); }
void Assembler::nop()  { emit(a64::nop()); }


void Assembler::ret(int imm16) {
  emit(ldst_post(LDR, 3, S1, arm(esp), oopSize));	// pop the return address
  if (imm16 != 0) arith_imm(false, arm(esp), arm(esp), imm16, false);
  emit(br(S1));
}


// ---------------------------------------------------------------------------
// Labels
//
// The uses of an unbound label form a chain through the instructions: the offset
// field of each use holds (position of the previous use / 4) + 1, 0 ending the chain.
// Uses are b (imm26), b.cond (imm19), adr (immhi, 19 bits) and ic_info (32 bits in
// the immediates of its two instructions, together with the IC flags).

static inline bool is_adr_insn(insn i)     { return (i & 0x9F000000) == 0x10000000; }
static inline bool is_ic_info_insn(insn i) { return (i & 0xFF80001F) == 0xD280001F; }	// movz xzr, #imm16

void Assembler::link(Label& L, FixupKind kind, insn i) {
  assert(!L.is_bound(), "label is bound");
  int v = L.is_unbound() ? L.pos() / 4 + 1 : 0;
  L.link_to(offset());
  switch (kind) {
    case fixup_b:      emit(i | (v & 0x3FFFFFF));          break;
    case fixup_b_cond: emit(i | ((v & 0x7FFFF) << 5));     break;
    case fixup_adr:    emit(i | ((v & 0x7FFFF) << 5));     break;
    default:           ShouldNotReachHere();
  }
}


void Assembler::print(Label& L) {
  if (L.is_unused()) {
    stdOut->print_cr("undefined label");
  } else if (L.is_bound()) {
    stdOut->print_cr("bound label to %d", L.pos());
  } else {
    stdOut->print_cr("unbound label at %d", L.pos());
  }
}


void Assembler::bind_to(Label& L, int pos) {
  assert(0 <= pos && pos <= offset(), "must have a valid binding position");
  while (L.is_unbound()) {
    int p = L.pos();
    insn i = insn_at(p);
    int v;
    int offs = pos - p;
    if (is_b(i)) {
      v = i & 0x3FFFFFF;
      assert(is_simm26_words(offs), "branch out of range");
      insn_at_put(p, set_b_offset(i, offs));
    } else if (is_b_cond(i)) {
      v = (i >> 5) & 0x7FFFF;
      assert(is_simm19_words(offs), "conditional branch out of range");
      insn_at_put(p, set_b_cond_offset(i, offs));
    } else if (is_adr_insn(i)) {
      v = (i >> 5) & 0x7FFFF;
      assert(is_simm21(offs), "adr out of range");
      insn_at_put(p, adr(i & 31, offs));
    } else if (is_ic_info_insn(i)) {
      insn hi = insn_at(p + 4);
      int lo16 = (i >> 5) & 0xFFFF, hi16 = (hi >> 5) & 0xFFFF;
      v = lo16 | ((hi16 & 0xFF) << 16);
      int flags = hi16 >> 8;
      assert(((offs << IC_Info::number_of_flags) >> IC_Info::number_of_flags) == offs, "NLR offset out of bounds");
      int data = (offs << IC_Info::number_of_flags) | flags;
      insn_at_put(p,     movz(zr, data & 0xFFFF, 0));
      insn_at_put(p + 4, movk(zr, (data >> 16) & 0xFFFF, 1));
    } else {
      fatal("arm64 assembler: unknown label use");
      return;
    }
    if (v == 0) L.unuse(); else L.link_to((v - 1) * 4);
  }
  L.bind_to(pos);
}


void Assembler::link_to(Label& L, Label& appendix) {
  Unimplemented();	// jump elimination is not done on arm64
}


void Assembler::bind(Label& L) {
  bind_to(L, offset());
}


void Assembler::merge(Label& L, Label& with) {
  Unimplemented();
}


// ---------------------------------------------------------------------------
// Calls (Delta convention) and jumps

void Assembler::push_return_address(Label& ret) {
  if (ret.is_bound()) emit(adr(S1, ret.pos() - offset()));
  else                link(ret, fixup_adr, adr(S1, 0));
  emit(ldst_pre(STR, 3, S1, arm(esp), -oopSize));
}


void Assembler::call(Label& L) {
  Label ret;
  push_return_address(ret);
  jmp(L);
  bind(ret);
}


void Assembler::call(char* entry, relocInfo::relocType rtype) {
  // [adr x17, ret] [str x17, [esp, #-8]!] [ldr x16, literal] [br x16] [literal] ret:
  align_for_literal_at(16);
  int start = offset();
  emit(adr(S1, sizeOfCall));
  emit(ldst_pre(STR, 3, S1, arm(esp), -oopSize));
  emit(ldr_literal(S0, 8));
  emit(br(S0));
  emit_word64((intptr_t) entry, rtype);
  assert(offset() - start == sizeOfCall, "call has wrong size");
  set_flags(flags_unknown);
}


void Assembler::call(Register reg) {
  Label ret;
  push_return_address(ret);
  emit(br(arm(reg)));
  bind(ret);
  set_flags(flags_unknown);
}


void Assembler::call(Address adr) {
  load(3, false, S0, adr);		// before esp changes
  Label ret;
  push_return_address(ret);
  emit(br(S0));
  bind(ret);
  set_flags(flags_unknown);
}


void Assembler::jmp(char* entry, relocInfo::relocType rtype) {
  // [ldr x16, literal] [br x16] [literal]
  align_for_literal_at(8);
  emit(ldr_literal(S0, 8));
  emit(br(S0));
  emit_word64((intptr_t) entry, rtype);
}


void Assembler::jmp(Register reg) {
  emit(br(arm(reg)));
}


void Assembler::jmp(Address adr) {
  load(3, false, S0, adr);
  emit(br(S0));
}


void Assembler::jmp(Label& L) {
  if (L.is_bound()) {
    int offs = L.pos() - offset();
    assert(offs <= 0, "assembler error");
    emit(b(offs));
  } else {
    link(L, fixup_b, b(0));
  }
}


void Assembler::jcc(Condition cc, Label& L) {
  Cond c = condition(cc);
  if (L.is_bound()) {
    int offs = L.pos() - offset();
    assert(offs <= 0, "assembler error");
    assert(is_simm19_words(offs), "conditional branch out of range");
    emit(b_cond(c, offs));
  } else {
    link(L, fixup_b_cond, b_cond(c, 0));
  }
}


void Assembler::jcc(Condition cc, char* dst, relocInfo::relocType rtype) {
  Cond c = condition(cc);
  Label skip;
  link(skip, fixup_b_cond, b_cond(invert(c), 0));
  jmp(dst, rtype);
  bind(skip);
}


void Assembler::ic_info(Label& L, int flags) {
  assert((unsigned int)flags >> IC_Info::number_of_flags == 0, "too many flags set");
  if (L.is_bound()) {
    int offs = L.pos() - offset();
    assert(offs <= 0, "assembler error");
    assert(((offs << IC_Info::number_of_flags) >> IC_Info::number_of_flags) == offs, "NLR offset out of bounds");
    int data = (offs << IC_Info::number_of_flags) | flags;
    emit(movz(zr, data & 0xFFFF, 0));
    emit(movk(zr, (data >> 16) & 0xFFFF, 1));
  } else {
    // chain link (24 bits) and flags in the two immediates
    int v = L.is_unbound() ? L.pos() / 4 + 1 : 0;
    assert(v < (1 << 24), "code too large for ic_info chain");
    L.link_to(offset());
    emit(movz(zr, v & 0xFFFF, 0));
    emit(movk(zr, ((v >> 16) & 0xFF) | (flags << 8), 1));
  }
}


// ---------------------------------------------------------------------------
// FPU (x87 stack emulation on d8..d15)

int Assembler::st(int i) const {
  int index = _fpu_depth - 1 - i;
  assert(0 <= index && index < 8, "x87 stack index out of range");
  return fpu_base + index;
}


void Assembler::fld1() { fpu_push(); emit(fmov_one(st(0))); }
void Assembler::fldz() { fpu_push(); emit(fmov_from_x(st(0), zr)); }

void Assembler::fld_s(Address adr) { fpu_push(); fload(2, st(0), adr); emit(fcvt_d_s(st(0), st(0))); }
void Assembler::fld_d(Address adr) { fpu_push(); fload(3, st(0), adr); }

void Assembler::fstp_s(Address adr) { emit(fcvt_s_d(FS, st(0))); fstore(2, FS, adr); fpu_pop(); }
void Assembler::fstp_d(Address adr) { fstore(3, st(0), adr); fpu_pop(); }

void Assembler::fild_s(Address adr) { load(2, true, S0, adr); fpu_push(); emit(scvtf_d_x(st(0), S0)); }	// 32-bit integer
void Assembler::fild_d(Address adr) { load(3, false, S0, adr); fpu_push(); emit(scvtf_d_x(st(0), S0)); }	// 64-bit integer

// x87 stores round to nearest (the default rounding mode)
void Assembler::fistp_s(Address adr) { emit(fcvtns_w_d(S0, st(0))); store(2, S0, adr); fpu_pop(); }
void Assembler::fistp_d(Address adr) { emit(fcvtns_x_d(S0, st(0))); store(3, S0, adr); fpu_pop(); }

void Assembler::fabs() { emit(fabs_(st(0), st(0))); }
void Assembler::fchs() { emit(fneg(st(0), st(0))); }

// ST(i) := ST(i) op ST(0)
void Assembler::fadd(int i) { emit(a64::fadd(st(i), st(i), st(0))); }
void Assembler::fsub(int i) { emit(a64::fsub(st(i), st(i), st(0))); }
void Assembler::fmul(int i) { emit(a64::fmul(st(i), st(i), st(0))); }
void Assembler::fdiv(int i) { emit(a64::fdiv(st(i), st(i), st(0))); }

// ST(i) := ST(i) op ST(0), pop
void Assembler::faddp(int i)  { emit(a64::fadd(st(i), st(i), st(0))); fpu_pop(); }
void Assembler::fsubp(int i)  { emit(a64::fsub(st(i), st(i), st(0))); fpu_pop(); }
void Assembler::fsubrp(int i) { emit(a64::fsub(st(i), st(0), st(i))); fpu_pop(); }	// ST(i) := ST(0) - ST(i)
void Assembler::fmulp(int i)  { emit(a64::fmul(st(i), st(i), st(0))); fpu_pop(); }
void Assembler::fdivp(int i)  { emit(a64::fdiv(st(i), st(i), st(0))); fpu_pop(); }


// ST(0) := f(ST(0), ST(1)) by a C call; eax, ecx and edx are preserved (the other
// x86 registers and the ST registers are callee saved in C).
static void emit_fpu_libcall(Assembler* a, int st0, int st1, double (*f)(double, double)) {
  a->emit(sub_imm(sp, sp, 32));
  a->emit(ldst_uimm(STR, 3, 0, sp, 0));
  a->emit(ldst_uimm(STR, 3, 1, sp, 8));
  a->emit(ldst_uimm(STR, 3, 2, sp, 16));
  a->emit(fmov(0, st0));
  a->emit(fmov(1, st1));
  uint64_t v = (uint64_t) (intptr_t) f;
  a->emit(movz(S0, (int) (v & 0xFFFF), 0));
  a->emit(movk(S0, (int) ((v >> 16) & 0xFFFF), 1));
  a->emit(movk(S0, (int) ((v >> 32) & 0xFFFF), 2));
  a->emit(movk(S0, (int) ((v >> 48) & 0xFFFF), 3));
  a->emit(blr(S0));
  a->emit(fmov(st0, 0));
  a->emit(ldst_uimm(LDR, 3, 0, sp, 0));
  a->emit(ldst_uimm(LDR, 3, 1, sp, 8));
  a->emit(ldst_uimm(LDR, 3, 2, sp, 16));
  a->emit(add_imm(sp, sp, 32));
}

static double fpu_fmod(double x, double y)      { return fmod(x, y); }
static double fpu_remainder(double x, double y) { return remainder(x, y); }

// the remainder is always complete (the x87 C2 status bit is 0)
void Assembler::fprem()  { emit_fpu_libcall(this, st(0), st(1), &fpu_fmod);      _fpu_compared = false; set_flags(flags_unknown); }
void Assembler::fprem1() { emit_fpu_libcall(this, st(0), st(1), &fpu_remainder); _fpu_compared = false; set_flags(flags_unknown); }

void Assembler::fxch(int i) {
  if (i == 0) return;
  emit(fmov(FS, st(0)));
  emit(fmov(st(0), st(i)));
  emit(fmov(st(i), FS));
}

void Assembler::fincstp()   { fpu_pop(); }
void Assembler::ffree(int i) { /* the register is popped by fincstp */ }

void Assembler::ftst()   { emit(fcmp_zero(st(0))); set_flags(flags_fpu_status); _fpu_compared = true; }
void Assembler::fcompp() { emit(fcmp(st(0), st(1))); fpu_pop(); fpu_pop(); set_flags(flags_fpu_status); _fpu_compared = true; }


void Assembler::fnstsw_ax() {
  // ax := x87 status word: C0 (bit 8) = less or unordered, C2 (bit 10) = unordered,
  // C3 (bit 14) = equal or unordered (from the arm64 flags of the last compare)
  if (_fpu_compared) {
    assert(_flags_kind == flags_fpu_status, "fnstsw_ax: no FPU compare");
    emit(cset(S0, LT));
    emit(lsl_imm(S0, S0, 8));
    emit(cset(T0, VS));
    emit(orr_reg(S0, S0, T0, LSL, 10));
    emit(orr_reg(S0, S0, T0, LSL, 14));
    emit(cset(T0, EQ));
    emit(orr_reg(S0, S0, T0, LSL, 14));
  } else {
    emit(movz(S0, 0, 0));
  }
  emit(bfxil(arm(eax), S0, 0, 16));	// only ax is written
}


void Assembler::fwait() {
  // nothing to wait for
}


// ---------------------------------------------------------------------------
// Implementation of MacroAssembler

void MacroAssembler::align(int modulus) {
  assert(modulus % 4 == 0, "arm64 code is aligned to 4 bytes");
  while (offset() % modulus != 0) nop();
}


void MacroAssembler::test(Register dst, int imm8) {
  if (imm8 & 0x80) testb(dst, imm8);	// the x86 sign flag is bit 7 of the result
  else             testl(dst, imm8);
}


void MacroAssembler::enter() {
  pushl(ebp);
  movl(ebp, esp);
}


void MacroAssembler::leave() {
  movl(esp, ebp);
  popl(ebp);
}


void MacroAssembler::inline_oop(oop o) {
  // [b over] [8-byte literal] over:
  align_for_literal_at(4);
  emit(b(12));
  emit_word64((intptr_t) o, relocInfo::oop_type);
}


// Calls to C land
//
// When entering C land, the ebp & esp of the last Delta frame have to be recorded.
// When leaving C land, last_Delta_fp has to be reset to 0. This is required to
// allow proper stack traversal.

void MacroAssembler::set_last_Delta_frame_before_call() {
  mov_imm(S1, (intptr_t) &last_Delta_fp);
  emit(ldst_uimm(STR, 3, arm(ebp), S1, 0));
  mov_imm(S1, (intptr_t) &last_Delta_sp);
  emit(ldst_uimm(STR, 3, arm(esp), S1, 0));
}


void MacroAssembler::set_last_Delta_frame_after_call() {
  // esp before the return address was pushed
  mov_imm(S1, (intptr_t) &last_Delta_fp);
  emit(ldst_uimm(STR, 3, arm(ebp), S1, 0));
  emit(add_imm(S0, arm(esp), oopSize));
  mov_imm(S1, (intptr_t) &last_Delta_sp);
  emit(ldst_uimm(STR, 3, S0, S1, 0));
}


void MacroAssembler::reset_last_Delta_frame() {
  mov_imm(S1, (intptr_t) &last_Delta_fp);
  emit(ldst_uimm(STR, 3, zr, S1, 0));
}


void MacroAssembler::call_C(Label& L) {
  // L is Delta code (it returns with ret)
  set_last_Delta_frame_before_call();
  call(L);
  reset_last_Delta_frame();
}


void MacroAssembler::call_C(Label& L, Label& nlrTestPoint) {
  set_last_Delta_frame_before_call();
  call(L);
  Assembler::ic_info(nlrTestPoint, 0);
  reset_last_Delta_frame();
}


// A call to a C function (entry, or the function in entry_reg). The first nof_stack_args
// words on the Delta stack (esp[0] is the first) are passed as the arguments, as an x86
// C function would see them. As for an x86 call, the return address is stored below esp,
// so the last Delta frame ends with a return address. C preserves ebx, esi, edi, esp, ebp
// (x19, x20, x21, x28, x29) and clobbers eax, ecx, edx; the result is in eax (x0).
void MacroAssembler::call_C_sequence(char* entry, int entry_reg, int nof_stack_args, Label* nlrTestPoint) {
  assert(0 <= nof_stack_args && nof_stack_args <= 8, "too many C arguments");
  set_last_Delta_frame_before_call();
  if (entry_reg >= 0 && entry_reg < nof_stack_args) {	// would be overwritten by an argument
    emit(mov_reg(S0, entry_reg));
    entry_reg = S0;
  }
  for (int i = 0; i < nof_stack_args; i++) {
    emit(ldst_uimm(LDR, 3, i, arm(esp), i * oopSize));
  }
  Label ret;
  link(ret, fixup_adr, adr(S1, 0));
  emit(ldst_simm9(STR, 3, S1, arm(esp), -oopSize));	// return address (esp is not changed)
  if (entry_reg < 0) {
    mov_imm(S0, (intptr_t) entry);
    entry_reg = S0;
  }
  emit(blr(entry_reg));
  bind(ret);
  if (nlrTestPoint != NULL) Assembler::ic_info(*nlrTestPoint, 0);
  reset_last_Delta_frame();
  if (nlrTestPoint != NULL) {
    // NLRs through C: the portable interpreter returns from C with have_nlr_through_C set
    // (e.g. a block evaluated by primitiveValue doing an NLR); continue the NLR here with
    // the NLR registers (eax = result, edi = home, esi = home id)
    Label no_nlr;
    load(0, false, S0, Address((intptr_t) &have_nlr_through_C, relocInfo::external_word_type));
    emit(subs_imm(zr, S0, 0));
    set_flags(flags_sub);
    jcc(equal, no_nlr);
    store(0, zr, Address((intptr_t) &have_nlr_through_C, relocInfo::external_word_type));
    load(3, false, arm(eax), Address((intptr_t) &nlr_result,  relocInfo::external_word_type));
    load(3, false, arm(edi), Address((intptr_t) &nlr_home,    relocInfo::external_word_type));
    load(2, true,  arm(esi), Address((intptr_t) &nlr_home_id, relocInfo::external_word_type));
    jmp(*nlrTestPoint);
    bind(no_nlr);
  }
  set_flags(flags_unknown);
}


void Assembler::call_native(char* entry, int nof_args) {
  // a C call without last Delta frame (for C code that does not walk the stack)
  assert(0 <= nof_args && nof_args <= 8, "too many C arguments");
  for (int i = 0; i < nof_args; i++) {
    emit(ldst_uimm(LDR, 3, i, arm(esp), i * oopSize));
  }
  mov_imm(S0, (intptr_t) entry);
  emit(blr(S0));
  set_flags(flags_unknown);
}


void MacroAssembler::call_C(char* entry, relocInfo::relocType rtype) {
  call_C_sequence(entry, -1, 8, NULL);
}


void MacroAssembler::call_C(char* entry, relocInfo::relocType rtype, Label& nlrTestPoint) {
  call_C_sequence(entry, -1, 8, &nlrTestPoint);
}


void MacroAssembler::call_C(Register entry) {
  call_C_sequence(NULL, arm(entry), 8, NULL);
}


void MacroAssembler::call_C(Register entry, Label& nlrTestPoint) {
  call_C_sequence(NULL, arm(entry), 8, &nlrTestPoint);
}


// C calls with the arguments in registers: moved to x0.. (through scratch registers,
// since the argument registers may themselves be x0..x3)
void MacroAssembler::move_C_arguments(Register* args, int n) {
  for (int i = 0; i < n; i++) emit(mov_reg(T0 + i, arm(args[i])));	// x9 .. x12
  for (int i = 0; i < n; i++) emit(mov_reg(i, T0 + i));
}


void MacroAssembler::call_C(char* entry, Register arg1) {
  Register args[] = { arg1 };
  move_C_arguments(args, 1);
  call_C_sequence(entry, -1, 0, NULL);
}


void MacroAssembler::call_C(char* entry, Register arg1, Register arg2) {
  Register args[] = { arg1, arg2 };
  move_C_arguments(args, 2);
  call_C_sequence(entry, -1, 0, NULL);
}


void MacroAssembler::call_C(char* entry, Register arg1, Register arg2, Register arg3) {
  Register args[] = { arg1, arg2, arg3 };
  move_C_arguments(args, 3);
  call_C_sequence(entry, -1, 0, NULL);
}


void MacroAssembler::call_C(char* entry, Register arg1, Register arg2, Register arg3, Register arg4) {
  Register args[] = { arg1, arg2, arg3, arg4 };
  move_C_arguments(args, 4);
  call_C_sequence(entry, -1, 0, NULL);
}


void MacroAssembler::load_ic_info_nlr_offset(Register dst, Register info) {
  // info: movz xzr, #lo16; movk xzr, #hi16, lsl #16 -> data = hi16:lo16 = offset << number_of_flags | flags
  emit(ldst_uimm(LDR, 2, S0, arm(info), 0));
  emit(ldst_uimm(LDR, 2, S1, arm(info), 4));
  emit(ubfm(S0, S0, 5, 20));			// lo16
  emit(ubfm(S1, S1, 5, 20));			// hi16
  emit(orr_reg(S0, S0, S1, LSL, 16));
  emit(sxtw(S0, S0));				// the data is a signed 32-bit value
  emit(asr_imm(arm(dst), S0, IC_Info::number_of_flags));
}


void MacroAssembler::call_DLL_cache(char* entry, char* function, oop dll_name, oop function_name) {
  // [ldr x2, #8] [b call] [entry point] [dll_name] [function_name] call:  (call 8-byte aligned)
  while ((offset() & 7) != 0) nop();
  int start = offset();
  emit(ldr_literal(arm(edx), 8));
  emit(b(28));
  emit_word64((intptr_t) function, relocInfo::none);
  emit_word64((intptr_t) dll_name, relocInfo::oop_type);
  emit_word64((intptr_t) function_name, relocInfo::oop_type);
  assert(offset() - start == 32 && (offset() & 7) == 0, "DLL cache layout");
  call(entry, relocInfo::runtime_call_type);	// no alignment padding: the call is 8-byte aligned
  assert(offset() - start == 32 + sizeOfCall, "DLL cache layout");
}


void MacroAssembler::store_check(Register obj, Register tmp) {
  // Does a store check for the oop in register obj. The content of
  // register obj is destroyed afterwards.
  assert(obj != tmp, "registers must be different");
  mov_imm(S1, (intptr_t) &byte_map_base);
  emit(ldst_uimm(LDR, 3, arm(tmp), S1, 0));
  emit(lsr_imm(arm(obj), arm(obj), card_shift));
  emit(ldst_reg(STR, 0, zr, arm(tmp), arm(obj), false));	// byte_map_base[obj >> card_shift] := 0
}


void MacroAssembler::fpu_mask_and_cond_for(Condition cc, int& mask, Condition& cond) {
  switch (cc) {
    case equal		: mask = 0x4000; cond = notZero;	break;
    case notEqual	: mask = 0x4000; cond = zero;		break;
    case less		: mask = 0x0100; cond = notZero;	break;
    case lessEqual	: mask = 0x4500; cond = notZero;	break;
    case greater	: mask = 0x4500; cond = zero;		break;
    case greaterEqual	: mask = 0x0100; cond = zero;		break;
    default		: Unimplemented();
  };
}


void MacroAssembler::fpop() {
  ffree();
  fincstp();
}


// debugging

void MacroAssembler::print_reg(const char* name, oop obj) {
  stdOut->print("%s = ", name);
  if (obj == NULL) {
    stdOut->print_cr("NULL");
  } else if (obj->is_smi()) {
    stdOut->print_cr("smi (%ld)", (long) smiOop(obj)->value());
  } else if (obj->is_mem() && Universe::is_heap((oop*)obj)) {
    if (obj == Universe::nilObj()) {
      stdOut->print_cr("nil (%p)", obj);
    } else if (obj == Universe::trueObj()) {
      stdOut->print_cr("true (%p)", obj);
    } else if (obj == Universe::falseObj()) {
      stdOut->print_cr("false (%p)", obj);
    } else {
      stdOut->print_cr("memOop (%p)", obj);
    }
  } else {
    stdOut->print_cr("%p", obj);
  }
}


void MacroAssembler::inspector(oop edi, oop esi, oop ebp, oop esp, oop ebx, oop edx, oop ecx, oop eax, char* eip) {
  print_reg("eax", eax);
  print_reg("ebx", ebx);
  print_reg("ecx", ecx);
  print_reg("edx", edx);
  print_reg("edi", edi);
  print_reg("esi", esi);
  stdOut->print_cr("ebp = %p", ebp);
  stdOut->print_cr("esp = %p", esp);
  stdOut->cr();
}


void MacroAssembler::inspect(char* title) {
  const char* s = (title == NULL) ? "" : title;
  stdOut->print_cr("cannot call inspector for \"%s\" on arm64", s);
}

#endif // STRONGTALK_ARM64
