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

#ifndef VM_ASM_A64_HPP
#define VM_ASM_A64_HPP

// arm64 (A64) instruction encodings.
//
// Each function returns the 32-bit encoding of one instruction; registers are numbers
// 0..31 where 31 is xzr or sp depending on the instruction (as in the architecture).
// There are no dependencies on the rest of the VM, so the encodings can be checked
// against an assembler (see porting/a64test.sh).

#include <stdint.h>

namespace a64 {

typedef uint32_t insn;

const int zr = 31;	// xzr (or sp, for instructions that take sp)
const int sp = 31;

enum Cond {
  EQ = 0, NE = 1, HS = 2, LO = 3, MI = 4, PL = 5, VS = 6, VC = 7,
  HI = 8, LS = 9, GE = 10, LT = 11, GT = 12, LE = 13, AL = 14
};

inline Cond invert(Cond c) { return Cond(c ^ 1); }

enum Shift { LSL = 0, LSR = 1, ASR = 2 };

// ranges
inline bool is_uimm12(int64_t v)        { return 0 <= v && v < 4096; }
inline bool is_simm9(int64_t v)         { return -256 <= v && v < 256; }
inline bool is_simm19_words(int64_t b)  { return (b & 3) == 0 && -(1 << 20) <= b && b < (1 << 20); }
inline bool is_simm26_words(int64_t b)  { return (b & 3) == 0 && -(1 << 27) <= b && b < (1 << 27); }
inline bool is_simm21(int64_t b)        { return -(1 << 20) <= b && b < (1 << 20); }

inline insn R(int r) { return (insn) (r & 31); }

// ---------------------------------------------------------------------------
// Integer arithmetic (64-bit)

inline insn add_reg (int d, int n, int m, Shift s = LSL, int amount = 0) { return 0x8B000000 | (s << 22) | (R(m) << 16) | ((amount & 63) << 10) | (R(n) << 5) | R(d); }
inline insn adds_reg(int d, int n, int m, Shift s = LSL, int amount = 0) { return 0xAB000000 | (s << 22) | (R(m) << 16) | ((amount & 63) << 10) | (R(n) << 5) | R(d); }
inline insn sub_reg (int d, int n, int m, Shift s = LSL, int amount = 0) { return 0xCB000000 | (s << 22) | (R(m) << 16) | ((amount & 63) << 10) | (R(n) << 5) | R(d); }
inline insn subs_reg(int d, int n, int m, Shift s = LSL, int amount = 0) { return 0xEB000000 | (s << 22) | (R(m) << 16) | ((amount & 63) << 10) | (R(n) << 5) | R(d); }

// imm12 (optionally shifted left by 12); n and d may be sp (31)
inline insn add_imm (int d, int n, int imm12, bool lsl12 = false) { return 0x91000000 | (lsl12 << 22) | ((imm12 & 0xFFF) << 10) | (R(n) << 5) | R(d); }
inline insn adds_imm(int d, int n, int imm12, bool lsl12 = false) { return 0xB1000000 | (lsl12 << 22) | ((imm12 & 0xFFF) << 10) | (R(n) << 5) | R(d); }
inline insn sub_imm (int d, int n, int imm12, bool lsl12 = false) { return 0xD1000000 | (lsl12 << 22) | ((imm12 & 0xFFF) << 10) | (R(n) << 5) | R(d); }
inline insn subs_imm(int d, int n, int imm12, bool lsl12 = false) { return 0xF1000000 | (lsl12 << 22) | ((imm12 & 0xFFF) << 10) | (R(n) << 5) | R(d); }

// add with carry
inline insn adc (int d, int n, int m) { return 0x9A000000 | (R(m) << 16) | (R(n) << 5) | R(d); }
inline insn adcs(int d, int n, int m) { return 0xBA000000 | (R(m) << 16) | (R(n) << 5) | R(d); }
inline insn sbcs(int d, int n, int m) { return 0xFA000000 | (R(m) << 16) | (R(n) << 5) | R(d); }

inline insn mov_sp(int d, int n)     { return add_imm(d, n, 0); }	// mov to/from sp
inline insn mov_reg(int d, int m)    { return 0xAA000000 | (R(m) << 16) | (R(zr) << 5) | R(d); }	// orr d, xzr, m
inline insn cmp_reg(int n, int m)    { return subs_reg(zr, n, m); }
inline insn cmn_reg(int n, int m)    { return adds_reg(zr, n, m); }
inline insn neg_reg(int d, int m)    { return sub_reg(d, zr, m); }
inline insn negs_reg(int d, int m)   { return subs_reg(d, zr, m); }

// logical (shifted register)
inline insn and_reg (int d, int n, int m, Shift s = LSL, int amount = 0) { return 0x8A000000 | (s << 22) | (R(m) << 16) | ((amount & 63) << 10) | (R(n) << 5) | R(d); }
inline insn ands_reg(int d, int n, int m, Shift s = LSL, int amount = 0) { return 0xEA000000 | (s << 22) | (R(m) << 16) | ((amount & 63) << 10) | (R(n) << 5) | R(d); }
inline insn orr_reg (int d, int n, int m, Shift s = LSL, int amount = 0) { return 0xAA000000 | (s << 22) | (R(m) << 16) | ((amount & 63) << 10) | (R(n) << 5) | R(d); }
inline insn eor_reg (int d, int n, int m, Shift s = LSL, int amount = 0) { return 0xCA000000 | (s << 22) | (R(m) << 16) | ((amount & 63) << 10) | (R(n) << 5) | R(d); }
inline insn orn_reg (int d, int n, int m)                                { return 0xAA200000 | (R(m) << 16) | (R(n) << 5) | R(d); }
inline insn mvn_reg (int d, int m)                                       { return orn_reg(d, zr, m); }
inline insn tst_reg (int n, int m)                                       { return ands_reg(zr, n, m); }

// move wide
inline insn movz(int d, int imm16, int hw) { return 0xD2800000 | ((hw & 3) << 21) | ((imm16 & 0xFFFF) << 5) | R(d); }
inline insn movn(int d, int imm16, int hw) { return 0x92800000 | ((hw & 3) << 21) | ((imm16 & 0xFFFF) << 5) | R(d); }
inline insn movk(int d, int imm16, int hw) { return 0xF2800000 | ((hw & 3) << 21) | ((imm16 & 0xFFFF) << 5) | R(d); }

// shifts
inline insn lslv(int d, int n, int m) { return 0x9AC02000 | (R(m) << 16) | (R(n) << 5) | R(d); }
inline insn lsrv(int d, int n, int m) { return 0x9AC02400 | (R(m) << 16) | (R(n) << 5) | R(d); }
inline insn asrv(int d, int n, int m) { return 0x9AC02800 | (R(m) << 16) | (R(n) << 5) | R(d); }
inline insn ubfm(int d, int n, int immr, int imms) { return 0xD3400000 | ((immr & 63) << 16) | ((imms & 63) << 10) | (R(n) << 5) | R(d); }
inline insn sbfm(int d, int n, int immr, int imms) { return 0x93400000 | ((immr & 63) << 16) | ((imms & 63) << 10) | (R(n) << 5) | R(d); }
inline insn lsl_imm(int d, int n, int s) { return ubfm(d, n, (64 - s) & 63, 63 - s); }
inline insn lsr_imm(int d, int n, int s) { return ubfm(d, n, s, 63); }
inline insn asr_imm(int d, int n, int s) { return sbfm(d, n, s, 63); }
inline insn sxtb(int d, int n)           { return sbfm(d, n, 0, 7); }
inline insn sxth(int d, int n)           { return sbfm(d, n, 0, 15); }
inline insn sxtw(int d, int n)           { return sbfm(d, n, 0, 31); }
inline insn uxtb(int d, int n)           { return ubfm(d, n, 0, 7); }

// multiply / divide
inline insn madd (int d, int n, int m, int a) { return 0x9B000000 | (R(m) << 16) | (R(a) << 10) | (R(n) << 5) | R(d); }
inline insn mul  (int d, int n, int m)        { return madd(d, n, m, zr); }
inline insn smulh(int d, int n, int m)        { return 0x9B407C00 | (R(m) << 16) | (R(n) << 5) | R(d); }
inline insn umulh(int d, int n, int m)        { return 0x9BC07C00 | (R(m) << 16) | (R(n) << 5) | R(d); }
inline insn sdiv (int d, int n, int m)        { return 0x9AC00C00 | (R(m) << 16) | (R(n) << 5) | R(d); }
inline insn msub (int d, int n, int m, int a) { return 0x9B008000 | (R(m) << 16) | (R(a) << 10) | (R(n) << 5) | R(d); }

// conditional select
inline insn csel (int d, int n, int m, Cond c) { return 0x9A800000 | (R(m) << 16) | (c << 12) | (R(n) << 5) | R(d); }
inline insn csinc(int d, int n, int m, Cond c) { return 0x9A800400 | (R(m) << 16) | (c << 12) | (R(n) << 5) | R(d); }
inline insn cset (int d, Cond c)               { return csinc(d, zr, zr, invert(c)); }

// flags
inline insn mrs_nzcv(int t) { return 0xD53B4200 | R(t); }
inline insn msr_nzcv(int t) { return 0xD51B4200 | R(t); }

// ---------------------------------------------------------------------------
// Loads and stores. size: 0 = byte, 1 = halfword, 2 = word, 3 = doubleword.

enum MemOp { STR = 0, LDR = 1, LDRS = 2 };	// LDRS: sign extending load to 64 bits

inline insn mem_opc(MemOp op) { return op == STR ? 0 : (op == LDR ? 1 : 2); }

// unsigned scaled 12-bit offset: offset must be a multiple of (1 << size), 0 <= offset/(1<<size) < 4096
inline insn ldst_uimm(MemOp op, int size, int t, int n, int offset) {
  return 0x39000000 | (size << 30) | (mem_opc(op) << 22) | (((offset >> size) & 0xFFF) << 10) | (R(n) << 5) | R(t);
}
// unscaled signed 9-bit offset
inline insn ldst_simm9(MemOp op, int size, int t, int n, int offset) {
  return 0x38000000 | (size << 30) | (mem_opc(op) << 22) | ((offset & 0x1FF) << 12) | (R(n) << 5) | R(t);
}
// register offset: [n, m, lsl #(scaled ? size : 0)]
inline insn ldst_reg(MemOp op, int size, int t, int n, int m, bool scaled) {
  return 0x38206800 | (size << 30) | (mem_opc(op) << 22) | (R(m) << 16) | (scaled << 12) | (R(n) << 5) | R(t);
}
// pre-index (writeback before): [n, #offset]!  and post-index: [n], #offset
inline insn ldst_pre (MemOp op, int size, int t, int n, int offset) { return 0x38000C00 | (size << 30) | (mem_opc(op) << 22) | ((offset & 0x1FF) << 12) | (R(n) << 5) | R(t); }
inline insn ldst_post(MemOp op, int size, int t, int n, int offset) { return 0x38000400 | (size << 30) | (mem_opc(op) << 22) | ((offset & 0x1FF) << 12) | (R(n) << 5) | R(t); }

// pc relative literal load of a 64-bit value; byte_offset from this instruction
inline insn ldr_literal(int t, int byte_offset) { return 0x58000000 | (((byte_offset >> 2) & 0x7FFFF) << 5) | R(t); }
// pc relative address
inline insn adr(int d, int byte_offset) {
  return 0x10000000 | ((byte_offset & 3) << 29) | (((byte_offset >> 2) & 0x7FFFF) << 5) | R(d);
}

// ---------------------------------------------------------------------------
// Branches; offsets in bytes from this instruction

inline insn b     (int byte_offset)         { return 0x14000000 | ((byte_offset >> 2) & 0x3FFFFFF); }
inline insn bl    (int byte_offset)         { return 0x94000000 | ((byte_offset >> 2) & 0x3FFFFFF); }
inline insn b_cond(Cond c, int byte_offset) { return 0x54000000 | (((byte_offset >> 2) & 0x7FFFF) << 5) | c; }
inline insn br    (int n)                   { return 0xD61F0000 | (R(n) << 5); }
inline insn blr   (int n)                   { return 0xD63F0000 | (R(n) << 5); }
inline insn ret   (int n = 30)              { return 0xD65F0000 | (R(n) << 5); }

// field accessors for patching
inline bool is_b     (insn i) { return (i & 0xFC000000) == 0x14000000; }
inline bool is_b_cond(insn i) { return (i & 0xFF000010) == 0x54000000; }
inline bool is_ldr_literal(insn i) { return (i & 0xFF000000) == 0x58000000; }
inline insn set_b_offset     (insn i, int byte_offset) { return (i & 0xFC000000) | ((byte_offset >> 2) & 0x3FFFFFF); }
inline insn set_b_cond_offset(insn i, int byte_offset) { return (i & 0xFF00001F) | (((byte_offset >> 2) & 0x7FFFF) << 5); }
inline int  b_offset     (insn i) { return ((int32_t) (i << 6)) >> 4; }
inline int  b_cond_offset(insn i) { return ((int32_t) ((i >> 5) << 13)) >> 11; }
inline int  ldr_literal_offset(insn i) { return b_cond_offset(i); }	// same field position

// ---------------------------------------------------------------------------
// Miscellaneous

inline insn nop()             { return 0xD503201F; }
inline insn brk(int imm16)    { return 0xD4200000 | ((imm16 & 0xFFFF) << 5); }
inline insn udf(int imm16)    { return imm16 & 0xFFFF; }

// ---------------------------------------------------------------------------
// Floating point (double precision unless noted); register numbers are d/s registers

inline insn fadd (int d, int n, int m) { return 0x1E602800 | (R(m) << 16) | (R(n) << 5) | R(d); }
inline insn fsub (int d, int n, int m) { return 0x1E603800 | (R(m) << 16) | (R(n) << 5) | R(d); }
inline insn fmul (int d, int n, int m) { return 0x1E600800 | (R(m) << 16) | (R(n) << 5) | R(d); }
inline insn fdiv (int d, int n, int m) { return 0x1E601800 | (R(m) << 16) | (R(n) << 5) | R(d); }
inline insn fabs_(int d, int n)        { return 0x1E60C000 | (R(n) << 5) | R(d); }
inline insn fneg (int d, int n)        { return 0x1E614000 | (R(n) << 5) | R(d); }
inline insn fmov (int d, int n)        { return 0x1E604000 | (R(n) << 5) | R(d); }
inline insn fcmp (int n, int m)        { return 0x1E602000 | (R(m) << 16) | (R(n) << 5); }
inline insn fcmp_zero(int n)           { return 0x1E602008 | (R(n) << 5); }
inline insn fmov_one(int d)            { return 0x1E6E1000 | R(d); }			// fmov d, #1.0
inline insn fmov_from_x(int d, int n)  { return 0x9E670000 | (R(n) << 5) | R(d); }	// fmov d, x (xzr: 0.0)
inline insn fmov_to_x(int d, int n)    { return 0x9E660000 | (R(n) << 5) | R(d); }	// fmov x, d
inline insn scvtf_d_x(int d, int n)    { return 0x9E620000 | (R(n) << 5) | R(d); }	// x -> d
inline insn scvtf_d_w(int d, int n)    { return 0x1E620000 | (R(n) << 5) | R(d); }	// w -> d
inline insn fcvtzs_x_d(int d, int n)   { return 0x9E780000 | (R(n) << 5) | R(d); }	// d -> x (truncate)
inline insn fcvtzs_w_d(int d, int n)   { return 0x1E780000 | (R(n) << 5) | R(d); }	// d -> w (truncate)
inline insn fcvt_s_d(int d, int n)     { return 0x1E624000 | (R(n) << 5) | R(d); }	// d -> s
inline insn fcvt_d_s(int d, int n)     { return 0x1E22C000 | (R(n) << 5) | R(d); }	// s -> d

// FP loads/stores: size 2 = s, 3 = d
inline insn fldst_uimm (MemOp op, int size, int t, int n, int offset) { return 0x3D000000 | (size << 30) | ((op == STR ? 0 : 1) << 22) | (((offset >> size) & 0xFFF) << 10) | (R(n) << 5) | R(t); }
inline insn fldst_simm9(MemOp op, int size, int t, int n, int offset) { return 0x3C000000 | (size << 30) | ((op == STR ? 0 : 1) << 22) | ((offset & 0x1FF) << 12) | (R(n) << 5) | R(t); }

} // namespace a64

#endif // VM_ASM_A64_HPP
