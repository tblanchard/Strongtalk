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

// The assembler for arm64 (included by assembler.hpp; Register, Label and Address are shared).
//
// The interface is the one of the x86 assembler, so the code generators can be ported
// mostly unchanged: each x86 operation is lowered to a short arm64 sequence.
//
//  - The eight x86 registers map to arm64 registers (see arm64_register): eax = x0,
//    ecx = x1, edx = x2, ebx = x19, esp = x28, ebp = x29, esi = x20, edi = x21.
//    x16, x17 and x9..x11 are scratch registers of the lowering; x18 is never used.
//  - All "l" operations are 64-bit (word) operations.
//  - esp (x28) is the Delta stack pointer: Delta code keeps the x86 frame layout on the
//    Delta stack. call pushes the return address on the Delta stack and ret pops it;
//    the arm64 link register is only used for calls to C (call_C, call_native).
//  - x86 condition codes: arithmetic and compares set the arm64 flags directly; the
//    lowering remembers the kind of the last flag setting operation because arm64
//    sets the carry flag inversely to x86 for subtractions (see condition()).
//    Logical operations set the flags lazily (a tst is emitted before a use).
//  - The x87 floating point stack is mapped at assembly time onto d8..d15 (ST(0) is the
//    register of the current stack depth). The stack depth must be the same on all
//    paths reaching a label, as it is for x87 code.
//  - Calls and jumps to absolute addresses that may be patched load the target from an
//    8-byte aligned literal in the instruction stream; relocation entries point at the
//    literal. Oops in code are literals too, so the GC can update them.
//  - A call sequence has a fixed size (sizeOfCall) and ends with its literal, so the
//    return address is the end of the sequence (as on x86).

#ifndef VM_ASM_ASSEMBLER_ARM64_HPP
#define VM_ASM_ASSEMBLER_ARM64_HPP

#include "a64.hpp"

class Assembler: public ResourceObj {
 protected:
  CodeBuffer* _code;

  char* _code_begin;			// first byte of code buffer
  char* _code_limit;			// first byte after code buffer
  char* _code_pos;			// current code generation position

  Label _unbound_label;			// for compatibility with the x86 assembler (jump elimination is not done)
  int	_binding_pos;

  // The kind of the operation that set the flags last (see condition())
  enum FlagKind {
    flags_add,				// adds/adcs: C is the x86 carry
    flags_sub,				// subs/cmp/neg: C is the inverted x86 carry (borrow)
    flags_logical,			// x86 CF = OF = 0; N, Z from _flags_reg (tst emitted on use)
    flags_shift,			// as logical, but the x86 carry (last bit shifted out) is not available
    flags_mul,				// imul: Z is set if the product did not overflow
    flags_fpu_status,			// fcompp/ftst: arm64 flags of the fcmp (for fnstsw_ax)
    flags_unknown
  };
  FlagKind _flags_kind;
  int      _flags_reg;			// flags_logical/flags_shift: the register holding the result
  bool     _flags_pending;		// flags_logical/flags_shift: the tst has not been emitted yet
  bool     _fpu_compared;		// the last FPU status setting operation was a compare (vs. fprem)

  int      _fpu_depth;			// emulated x87 stack depth

  char* addr_at(int pos) 		{ return _code_begin + pos; }

  int  byte_at(int pos)			{ return *(unsigned char*)addr_at(pos); }
  void byte_at_put(int pos, int x)	{ *(unsigned char*)addr_at(pos) = (unsigned char)x; }

  int  long_at(int pos)			{ return *(int*)addr_at(pos); }
  void long_at_put(int pos, int x)	{ *(int*)addr_at(pos) = x; }

  a64::insn insn_at(int pos)			{ return *(a64::insn*) addr_at(pos); }
  void      insn_at_put(int pos, a64::insn i)	{ *(a64::insn*) addr_at(pos) = i; }

  bool is8bit(int x)			{ return -0x80 <= x && x < 0x80; }
  bool isByte(int x)			{ return 0 <= x && x < 0x100; }
  bool isShiftCount(int x)		{ return 0 <= x && x < 64; }

  // emission
  void emit_word64(intptr_t x, relocInfo::relocType rtype);	// 8-byte aligned data word
  void align_for_literal_at(int offset_in_sequence);		// pads so that a literal at the given offset is 8-byte aligned

  // registers
  static int arm(Register r);		// arm64 register number

  // operand helpers
  void mov_imm(int rd, intptr_t imm);				// materializes imm (no relocation)
  void mov_literal(int rd, intptr_t value, relocInfo::relocType rtype);	// ldr rd, literal
  void mov_imm(int rd, intptr_t imm, relocInfo::relocType rtype) {
    if (rtype == relocInfo::none) mov_imm(rd, imm); else mov_literal(rd, imm, rtype);
  }
  int  address(Address adr, int size, int* offset, bool* scaled);	// base register for a load/store of size (log2 bytes)
  void load (int size, bool sign, int rt, Address adr);		// rt := [adr] (zero or sign extended)
  void store(int size, int rt, Address adr);			// [adr] := rt
  void fload (int size, int dt, Address adr);
  void fstore(int size, int dt, Address adr);

  // flags
  void set_flags(FlagKind kind)				{ _flags_kind = kind; _flags_pending = false; }
  void set_logical_flags(int reg, FlagKind kind = flags_logical) { _flags_kind = kind; _flags_reg = reg; _flags_pending = true; }
  void materialize_flags();					// emits a pending tst
  void x86_carry(int rd);					// rd := x86 carry flag (0 or 1)

  // arithmetic with an immediate, flag setting
  void arith_imm(bool sub, int rd, int rn, intptr_t imm, bool set_flags);
  void logical(int op, int rd, int rn, int rm);		// op: 0 = and, 1 = orr, 2 = eor

  // x87 stack
  int  st(int i) const;					// d register of ST(i)
  void fpu_push()					{ _fpu_depth++; assert(_fpu_depth <= 8, "x87 stack overflow"); }
  void fpu_pop()					{ assert(_fpu_depth > 0, "x87 stack underflow"); _fpu_depth--; }

  // labels; unbound uses are chained through the instructions (see .cpp)
  enum FixupKind { fixup_b, fixup_b_cond, fixup_adr, fixup_ic_info };
  void link(Label& L, FixupKind kind, a64::insn i);	// emits i as a use of the unbound label L
  void print  (Label& L);
  void bind_to(Label& L, int pos);
  void link_to(Label& L, Label& appendix);

  void push_return_address(Label& ret);			// Delta convention: [--esp] := address of ret

 public:
  enum Condition {
    zero	= 0x4,
    notZero	= 0x5,
    equal	= 0x4,
    notEqual	= 0x5,
    less	= 0xc,
    lessEqual	= 0xe,
    greater	= 0xf,
    greaterEqual= 0xd,
    below	= 0x2,
    belowEqual	= 0x6,
    above	= 0x7,
    aboveEqual	= 0x3,
    overflow	= 0x0,
    noOverflow	= 0x1,
    carrySet	= 0x2,
    carryClear	= 0x3,
    negative	= 0x8,
    positive	= 0x9,
  };

  enum Constants {
    sizeOfCall		= 24,		// a patchable Delta call (call(entry, rtype)): adr, str, ldr, br, literal
    sizeOfJump		= 16,		// a patchable jump (jmp(entry, rtype)): ldr, br, literal
    sizeOfCallC		= 20		// a C call (call_native): movz/movk x16 (4), blr
  };

  Assembler(CodeBuffer* code);

  void		finalize();		// call this before using/copying the code
  CodeBuffer*	code() const		{ return _code; }
  char*		pc() const		{ return _code_pos; }
  int		offset() const		{ return _code_pos - _code_begin; }

  // arm64 specific
  void emit(a64::insn i);					// emits one instruction
  a64::Cond condition(Condition cc);				// arm64 condition for an x86 condition after the last flag setting operation
  int  fpu_depth() const		{ return _fpu_depth; }
  void set_fpu_depth(int depth)		{ _fpu_depth = depth; }	// for code reached with a known x87 stack depth
  void call_native(char* entry, int nof_args);			// calls a C function; arguments are taken from the Delta stack (esp[0] = first)

  // Stack
  void pushad();
  void popad();

  void pushl(int imm32);
  void pushl(intptr_t imm);
  void pushl(unsigned int imm32);			// zero extended (as C converts it to a word)
  void pushl(oop obj);
  void pushl(Register src);
  void pushl(Address src);

  void popl(Register dst);
  void popl(Address dst);

  // Moves
  void movb(Register dst, Address src);
  void movb(Address dst, int imm8);
  void movb(Address dst, Register src);

  void movw(Register dst, Address src);
  void movw(Address dst, Register src);

  void movl(Register dst, int imm32);
  void movl(Register dst, intptr_t imm);
  void movl(Register dst, unsigned int imm32);
  void movl(Register dst, oop obj);
  void movl(Register dst, Register src);
  void movl(Register dst, Address src);

  void movl(Address dst, int imm32);
  void movl(Address dst, intptr_t imm);
  void movl(Address dst, unsigned int imm32);
  void movl(Address dst, oop obj);
  void movl(Address dst, Register src);

  void movsxb(Register dst, Address src);
  void movsxb(Register dst, Register src);

  void movsxw(Register dst, Address src);
  void movsxw(Register dst, Register src);

  // Conditional moves
  void cmovccl(Condition cc, Register dst, int imm32);
  void cmovccl(Condition cc, Register dst, oop obj);
  void cmovccl(Condition cc, Register dst, Register src);
  void cmovccl(Condition cc, Register dst, Address src);

  // Arithmetics
  void adcl(Register dst, int imm32);
  void adcl(Register dst, Register src);

  void addl(Address dst, int imm32);
  void addl(Register dst, int imm32);
  void addl(Register dst, Register src);

  void andl(Register dst, int imm32);
  void andl(Register dst, Register src);

  void cmpl(Address dst, int imm32);
  void cmpl(Address dst, oop obj);
  void cmpl(Register dst, int imm32);
  void cmpl(Register dst, intptr_t imm);
  void cmpl(Register dst, oop obj);
  void cmpl(Register dst, Register src);
  void cmpl(Register dst, Address src);

  void decb(Register dst);
  void decl(Register dst);

  void idivl(Register src);

  void imull(Register dst, Register src);
  void imull(Register dst, Register src, int value);

  void incl(Register dst);
  void incl(Address dst);

  void leal(Register dst, Address src);

  void mull(Register src);

  void negl(Register dst);

  void notl(Register dst);

  void orl(Register dst, int imm32);
  void orl(Register dst, Register src);
  void orl(Register dst, Address src);

  void rcll(Register dst, int imm8);

  void sarl(Register dst, int imm8);
  void sarl(Register dst);

  void sbbl(Register dst, int imm32);
  void sbbl(Register dst, Register src);

  void shldl(Register dst, Register src);

  void shll(Register dst, int imm8);
  void shll(Register dst);

  void shrdl(Register dst, Register src);

  void shrl(Register dst, int imm8);
  void shrl(Register dst);

  void subl(Register dst, int imm32);
  void subl(Register dst, Register src);

  void testb(Register dst, int imm8);
  void testl(Register dst, int imm32);
  void testl(Register dst, Register src);

  void xorl(Register dst, int imm32);
  void xorl(Register dst, Register src);

  // Miscellaneous
  void hlt();
  void int3();
  void nop();
  void ret(int imm16);

  // Labels

  void bind(Label& L);			// binds an unbound label L to the current code position
  void merge(Label& L, Label& with);	// merges L and with, L is the merged label

  // Calls (Delta convention: the return address is pushed on the Delta stack)
  void call(Label& L);
  void call(char* entry, relocInfo::relocType rtype);
  void call(Register reg);
  void call(Address adr);

  // Jumps
  void jmp(char* entry, relocInfo::relocType rtype);
  void jmp(Register reg);
  void jmp(Address adr);

  void jmp(Label& L);		// unconditional jump to L

  void jcc(Condition cc, char* dst, relocInfo::relocType rtype = relocInfo::runtime_call_type);
  void jcc(Condition cc, Label& L);

  // Support for inline cache information (see also IC_Info): two instructions without
  // effect (movz/movk xzr) holding the 32-bit info word.
  void ic_info(Label& L, int flags);

  // Floating-point operations (x87 stack emulation)
  void fld1();
  void fldz();

  void fld_s(Address adr);
  void fld_d(Address adr);

  void fstp_s(Address adr);
  void fstp_d(Address adr);

  void fild_s(Address adr);
  void fild_d(Address adr);

  void fistp_s(Address adr);
  void fistp_d(Address adr);

  void fabs();
  void fchs();

  void fadd(int i);
  void fsub(int i);
  void fmul(int i);
  void fdiv(int i);

  void faddp(int i = 1);
  void fsubp(int i = 1);
  void fsubrp(int i = 1);
  void fmulp(int i = 1);
  void fdivp(int i = 1);
  void fprem();
  void fprem1();

  void fxch(int i = 1);
  void fincstp();
  void ffree(int i = 0);

  void ftst();
  void fcompp();
  void fnstsw_ax();
  void fwait();

  // For compatibility with old assembler only - should be removed at some point
  void Load (Register base, int disp, Register dst) { movl(dst, Address(base, disp)); }
  void Store(Register src, Register base, int disp) { movl(Address(base, disp), src); }
};


// MacroAssembler extends Assembler by a few macros used for
// generating the interpreter and for compiled code.

class MacroAssembler: public Assembler {
 public:
  MacroAssembler(CodeBuffer* code) : Assembler(code) {}

  // Alignment
  void align(int modulus);

  // Test-Instructions
  void test (Register dst, int imm8);

  // Stack frame operations
  void enter();
  void leave();

  // Support for inlined data
  void inline_oop(oop o);

  // C calls
  void set_last_Delta_frame_before_call();	// assumes that the return address has not been pushed yet
  void set_last_Delta_frame_after_call();	// assumes that the return address has been pushed already
  void reset_last_Delta_frame();

  void call_C(Label& L);
  void call_C(Label& L, Label& nlrTestPoint);

  void call_C(char* entry, relocInfo::relocType rtype);
  void call_C(char* entry, relocInfo::relocType rtype, Label& nlrTestPoint);

  void call_C(Register entry);
  void call_C(Register entry, Label& nlrTestPoint);

  // C calls to run-time routines with arguments (args are not preserved)
  void call_C(char* entry, Register arg1);
  void call_C(char* entry, Register arg1, Register arg2);
  void call_C(char* entry, Register arg1, Register arg2, Register arg3);
  void call_C(char* entry, Register arg1, Register arg2, Register arg3, Register arg4);

  // DLL calls: emits a CompiledDLL_Cache (see dll.hpp) and the call of the DLL stub (entry)
  void call_DLL_cache(char* entry, char* function, oop dll_name, oop function_name);

  // Stores
  void store_check(Register obj, Register tmp);

  // Floating-point comparisons
  static void fpu_mask_and_cond_for(Condition cc, int& mask, Condition& cond);

  // Pop ST (ffree & fincstp combined)
  void fpop();

  // debugging
  static void print_reg(const char* name, oop obj);
  static void inspector(oop edi, oop esi, oop ebp, oop esp, oop ebx, oop edx, oop ecx, oop eax, char* eip);
  void inspect(char* title = NULL);

 private:
  void call_C_sequence(char* entry, int entry_reg, int nof_stack_args, Label* nlrTestPoint);
  void move_C_arguments(Register* args, int n);
};

#endif // VM_ASM_ASSEMBLER_ARM64_HPP
