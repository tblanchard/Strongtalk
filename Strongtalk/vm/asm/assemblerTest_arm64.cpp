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

// Run-time tests of the arm64 assembler (+TestArm64Assembler): small functions are
// generated with the x86-shaped interface, executed, and their results compared with
// the x86 semantics of the operations (on 64-bit words).

#include "config.hpp"
#ifdef STRONGTALK_ARM64

#include "error.hpp"
#include "oopsHierarchy.hpp"
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
#include "relocInfo.hpp"
#include "codeBuffer.hpp"
#include "assembler.hpp"
#include "runtime.hpp"
#include "process.hpp"
#include "smiOop.hpp"
#include "rSet.hpp"
#include "longInt.hpp"
#include "os.hpp"
#include "dll.hpp"
#include "assemblerTest_arm64.hpp"
#include <sys/mman.h>
#include <pthread.h>
#include <libkern/OSCacheControl.h>
#include <math.h>
#include <string.h>

using namespace a64;

// ---------------------------------------------------------------------------
// Executable memory

static char* jit_memory = NULL;
static int   jit_used   = 0;
const  int   jit_size   = 1 << 20;
const  int   scratch_size = 64 * 1024;	// the first part of the memory is reused for each test function

static void copy_to_jit(char* p, char* code, int size) {
  { JITWriteScope write_code;
    memcpy(p, code, size);
  }
  os::flush_icache(p, size);
}

static void init_jit_memory() {
  if (jit_memory == NULL) {
    jit_memory = os::allocate_code_memory(jit_size);
    if (jit_memory == NULL) fatal("cannot allocate JIT memory");
    jit_used = scratch_size;
  }
}

// Copies code into executable memory (kept until the end of the test) and returns its address.
static char* install(char* code, int size) {
  init_jit_memory();
  jit_used = (jit_used + 15) & ~15;
  if (jit_used + size > jit_size) fatal("test JIT memory exhausted");
  char* p = jit_memory + jit_used;
  copy_to_jit(p, code, size);
  jit_used += size;
  return p;
}

// Copies code into the scratch area (valid until the next test function is installed).
static char* install_scratch(char* code, int size) {
  init_jit_memory();
  if (size > scratch_size) fatal("test function too large");
  copy_to_jit(jit_memory, code, size);
  return jit_memory;
}

// ---------------------------------------------------------------------------
// Test functions: intptr_t f(eax, ecx, edx, esp). The prologue saves the callee saved
// registers and sets esp (and ebp) to the given Delta stack; eax is the result.

typedef intptr_t (*TestFn)(intptr_t eax_, intptr_t ecx_, intptr_t edx_, intptr_t* esp_);

class TestCode {
 public:
  ResourceMark    rm;		// releases the code buffer at the end of the test
  CodeBuffer*     cb;
  MacroAssembler* m;

  TestCode() {
    cb = new CodeBuffer(16 * 1024, 4 * 1024);
    m  = new MacroAssembler(cb);
    m->emit(sub_imm(sp, sp, 64));
    m->emit(ldst_uimm(STR, 3, 19, sp, 0));
    m->emit(ldst_uimm(STR, 3, 20, sp, 8));
    m->emit(ldst_uimm(STR, 3, 21, sp, 16));
    m->emit(ldst_uimm(STR, 3, 28, sp, 24));
    m->emit(ldst_uimm(STR, 3, 29, sp, 32));
    m->emit(ldst_uimm(STR, 3, 30, sp, 40));
    m->emit(mov_reg(28, 3));
    m->emit(mov_reg(29, 3));
    m->emit(movz(19, 0, 0));
    m->emit(movz(20, 0, 0));
    m->emit(movz(21, 0, 0));
  }

  TestFn finish() {
    m->emit(ldst_uimm(LDR, 3, 19, sp, 0));
    m->emit(ldst_uimm(LDR, 3, 20, sp, 8));
    m->emit(ldst_uimm(LDR, 3, 21, sp, 16));
    m->emit(ldst_uimm(LDR, 3, 28, sp, 24));
    m->emit(ldst_uimm(LDR, 3, 29, sp, 32));
    m->emit(ldst_uimm(LDR, 3, 30, sp, 40));
    m->emit(add_imm(sp, sp, 64));
    m->emit(a64::ret());
    m->finalize();
    return (TestFn) install_scratch(cb->code_begin(), cb->code_size());
  }
};

// Delta stack for the tests (grows downwards from the middle, so stack arguments above esp exist)
static intptr_t test_stack[512];
static intptr_t* stack_top() { return &test_stack[384]; }

static intptr_t run(TestFn f, intptr_t a, intptr_t b = 0, intptr_t c = 0) {
  return f(a, b, c, stack_top());
}

static int failures = 0;
static int checks   = 0;

static void check(bool ok, const char* what, intptr_t got, intptr_t expected) {
  checks++;
  if (!ok) {
    failures++;
    if (failures <= 50) lprintf("  FAILED %s: got %#lx, expected %#lx\n", what, (long) got, (long) expected);
  }
}

static void check_eq(const char* what, intptr_t got, intptr_t expected) {
  check(got == expected, what, got, expected);
}

// ---------------------------------------------------------------------------
// x86 semantics

struct Flags { bool zf, sf, cf, of; };

static Flags flags_sub(intptr_t a, intptr_t b) {
  intptr_t r = (intptr_t) ((uint64_t) a - (uint64_t) b);
  Flags f = { r == 0, r < 0, (uint64_t) a < (uint64_t) b, ((a ^ b) & (a ^ r)) < 0 };
  return f;
}

static Flags flags_add(intptr_t a, intptr_t b) {
  intptr_t r = (intptr_t) ((uint64_t) a + (uint64_t) b);
  Flags f = { r == 0, r < 0, (uint64_t) r < (uint64_t) a, (~(a ^ b) & (a ^ r)) < 0 };
  return f;
}

static Flags flags_logical(intptr_t r) {
  Flags f = { r == 0, r < 0, false, false };
  return f;
}

static bool holds(Assembler::Condition cc, Flags f) {
  switch (cc) {
    case Assembler::overflow:     return f.of;
    case Assembler::noOverflow:   return !f.of;
    case Assembler::below:        return f.cf;
    case Assembler::aboveEqual:   return !f.cf;
    case Assembler::equal:        return f.zf;
    case Assembler::notEqual:     return !f.zf;
    case Assembler::belowEqual:   return f.cf || f.zf;
    case Assembler::above:        return !f.cf && !f.zf;
    case Assembler::negative:     return f.sf;
    case Assembler::positive:     return !f.sf;
    case Assembler::less:         return f.sf != f.of;
    case Assembler::greaterEqual: return f.sf == f.of;
    case Assembler::lessEqual:    return f.zf || f.sf != f.of;
    case Assembler::greater:      return !f.zf && f.sf == f.of;
    default: ShouldNotReachHere(); return false;
  }
}

static const Assembler::Condition all_conditions[] = {
  Assembler::overflow, Assembler::noOverflow, Assembler::below, Assembler::aboveEqual,
  Assembler::equal, Assembler::notEqual, Assembler::belowEqual, Assembler::above,
  Assembler::negative, Assembler::positive, Assembler::less, Assembler::greaterEqual,
  Assembler::lessEqual, Assembler::greater
};
const int nof_conditions = sizeof(all_conditions) / sizeof(all_conditions[0]);

static const intptr_t values[] = {
  0, 1, 2, 3, -1, -2, 4, 7, 0x7F, 0x80, 0xFF, 0x100, 0xFFF, 0x1000, 0x7FFFFFFF, (intptr_t) 0x80000000,
  (intptr_t) 0xFFFFFFFF, (intptr_t) 0x100000000, INTPTR_MAX, INTPTR_MIN, INTPTR_MAX - 1, INTPTR_MIN + 1,
  (intptr_t) 0x123456789ABCDEF0, -(intptr_t) 0x123456789ABCDEF, 0x5555, -0x5555, 1000003, -77777
};
const int nof_values = sizeof(values) / sizeof(values[0]);

// eax := cc ? 1 : 0 (after the flag setting code already emitted)
static void emit_condition_result(MacroAssembler* m, Assembler::Condition cc) {
  Label yes, done;
  m->jcc(cc, yes);
  m->movl(eax, 0);
  m->jmp(done);
  m->bind(yes);
  m->movl(eax, 1);
  m->bind(done);
}

// ---------------------------------------------------------------------------
// Tests

static void test_immediates() {
  for (int i = 0; i < nof_values; i++) {
    TestCode t;
    t.m->movl(eax, values[i]);
    TestFn f = t.finish();
    check_eq("movl(reg, imm)", run(f, 0), values[i]);
  }
  intptr_t special[] = { (intptr_t) 0xFFFF0000FFFF0000, (intptr_t) 0x0000FFFF0000FFFF, (intptr_t) 0xFFFFFFFF0000FFFF, 0x10001 };
  for (int i = 0; i < 4; i++) {
    TestCode t;
    t.m->movl(ecx, special[i]);
    t.m->movl(eax, ecx);
    check_eq("movl(reg, imm) halfword patterns", run(t.finish(), 0), special[i]);
  }
  { TestCode t; t.m->movl(eax, (unsigned int) 0xbadbabe0); check_eq("movl(reg, unsigned) zero extends", run(t.finish(), 0), (intptr_t) 0xbadbabe0); }
  { TestCode t; t.m->movl(eax, (int) -5); check_eq("movl(reg, int) sign extends", run(t.finish(), 0), -5); }
  { TestCode t; t.m->movl(eax, oop(0x1001)); check_eq("movl(reg, mem oop literal)", run(t.finish(), 0), 0x1001); }
}


static void test_arithmetic() {
  for (int i = 0; i < nof_values; i++) {
    for (int j = 0; j < nof_values; j++) {
      intptr_t a = values[i], b = values[j];
      struct { const char* name; int op; intptr_t expected; } ops[] = {
        { "addl", 0, (intptr_t) ((uint64_t) a + (uint64_t) b) },
        { "subl", 1, (intptr_t) ((uint64_t) a - (uint64_t) b) },
        { "andl", 2, a & b },
        { "orl",  3, a | b },
        { "xorl", 4, a ^ b },
      };
      // register and immediate (32-bit immediates only, sign extended as on x86-64)
      for (int k = 0; k < 5; k++) {
        TestCode t;
        switch (ops[k].op) {
          case 0: t.m->addl(eax, ecx); break;
          case 1: t.m->subl(eax, ecx); break;
          case 2: t.m->andl(eax, ecx); break;
          case 3: t.m->orl(eax, ecx);  break;
          case 4: t.m->xorl(eax, ecx); break;
        }
        check_eq(ops[k].name, run(t.finish(), a, b), ops[k].expected);
      }
      if (b == (intptr_t) (int) b) {
        for (int k = 0; k < 5; k++) {
          TestCode t;
          switch (ops[k].op) {
            case 0: t.m->addl(eax, (int) b); break;
            case 1: t.m->subl(eax, (int) b); break;
            case 2: t.m->andl(eax, (int) b); break;
            case 3: t.m->orl(eax, (int) b);  break;
            case 4: t.m->xorl(eax, (int) b); break;
          }
          check_eq(ops[k].name, run(t.finish(), a), ops[k].expected);
        }
      }
    }
  }
  // unary
  for (int i = 0; i < nof_values; i++) {
    intptr_t a = values[i];
    { TestCode t; t.m->negl(eax); check_eq("negl", run(t.finish(), a), (intptr_t) (0 - (uint64_t) a)); }
    { TestCode t; t.m->notl(eax); check_eq("notl", run(t.finish(), a), ~a); }
    { TestCode t; t.m->incl(eax); check_eq("incl", run(t.finish(), a), (intptr_t) ((uint64_t) a + 1)); }
    { TestCode t; t.m->decl(eax); check_eq("decl", run(t.finish(), a), (intptr_t) ((uint64_t) a - 1)); }
    { TestCode t; t.m->decb(eax); check_eq("decb", run(t.finish(), a), (a & ~(intptr_t) 0xFF) | ((a - 1) & 0xFF)); }
  }
}


static void test_shifts() {
  for (int i = 0; i < nof_values; i++) {
    intptr_t a = values[i];
    int counts[] = { 0, 1, 2, 5, 31, 32, 62, 63 };
    for (int k = 0; k < 8; k++) {
      int n = counts[k];
      { TestCode t; t.m->shll(eax, n); check_eq("shll imm", run(t.finish(), a), (intptr_t) ((uint64_t) a << n)); }
      { TestCode t; t.m->shrl(eax, n); check_eq("shrl imm", run(t.finish(), a), (intptr_t) ((uint64_t) a >> n)); }
      { TestCode t; t.m->sarl(eax, n); check_eq("sarl imm", run(t.finish(), a), a >> n); }
      { TestCode t; t.m->sarl(eax); check_eq("sarl ecx", run(t.finish(), a, n), a >> n); }
      { TestCode t; t.m->shll(eax); check_eq("shll ecx", run(t.finish(), a, n), (intptr_t) ((uint64_t) a << n)); }
      { TestCode t; t.m->shrl(eax); check_eq("shrl ecx", run(t.finish(), a, n), (intptr_t) ((uint64_t) a >> n)); }
      // zero flag after a shift
      { TestCode t; t.m->sarl(eax, n); emit_condition_result(t.m, Assembler::zero); check_eq("sarl zero flag", run(t.finish(), a), (a >> n) == 0); }
    }
  }
}


static void test_conditions() {
  for (int i = 0; i < nof_values; i++) {
    for (int j = 0; j < nof_values; j++) {
      intptr_t a = values[i], b = values[j];
      for (int k = 0; k < nof_conditions; k++) {
        Assembler::Condition cc = all_conditions[k];
        // after cmp (register, and immediate when it fits a 32-bit immediate)
        { TestCode t; t.m->cmpl(eax, ecx); emit_condition_result(t.m, cc); check_eq("cmpl reg + jcc", run(t.finish(), a, b), holds(cc, flags_sub(a, b))); }
        if (b == (intptr_t) (int) b) {
          TestCode t; t.m->cmpl(eax, (int) b); emit_condition_result(t.m, cc); check_eq("cmpl imm + jcc", run(t.finish(), a), holds(cc, flags_sub(a, b)));
        }
        // after sub
        { TestCode t; t.m->subl(eax, ecx); emit_condition_result(t.m, cc); check_eq("subl + jcc", run(t.finish(), a, b), holds(cc, flags_sub(a, b))); }
        // after test / and / or / xor
        { TestCode t; t.m->testl(eax, ecx); emit_condition_result(t.m, cc); check_eq("testl + jcc", run(t.finish(), a, b), holds(cc, flags_logical(a & b))); }
        { TestCode t; t.m->orl(eax, ecx); emit_condition_result(t.m, cc); check_eq("orl + jcc", run(t.finish(), a, b), holds(cc, flags_logical(a | b))); }
        { TestCode t; t.m->xorl(eax, ecx); emit_condition_result(t.m, cc); check_eq("xorl + jcc", run(t.finish(), a, b), holds(cc, flags_logical(a ^ b))); }
        // after add (above / belowEqual cannot be expressed after an addition on arm64)
        if (cc != Assembler::above && cc != Assembler::belowEqual) {
          TestCode t; t.m->addl(eax, ecx); emit_condition_result(t.m, cc); check_eq("addl + jcc", run(t.finish(), a, b), holds(cc, flags_add(a, b)));
        }
      }
      // conditional move and sbb idiom (eax := -CF after cmp)
      { TestCode t; t.m->movl(eax, 0); t.m->movl(edx, 7); t.m->cmpl(ecx, edx); t.m->cmovccl(Assembler::less, eax, ecx);
        check_eq("cmovccl", run(t.finish(), 0, b), b < 7 ? b : 0); }
      { TestCode t; t.m->cmpl(eax, ecx); t.m->sbbl(eax, eax); check_eq("cmpl + sbbl", run(t.finish(), a, b), flags_sub(a, b).cf ? -1 : 0); }
      { TestCode t; t.m->addl(eax, ecx); t.m->movl(eax, 10); t.m->adcl(eax, 0); check_eq("addl + adcl", run(t.finish(), a, b), 10 + (flags_add(a, b).cf ? 1 : 0)); }
    }
  }
  // testb: x86 byte flags (sign is bit 7 of the byte)
  intptr_t bytes[] = { 0x00, 0x01, 0x80, 0xFF, 0x17F, 0x280 };
  for (int i = 0; i < 6; i++) {
    { TestCode t; t.m->testb(eax, 0xFF); emit_condition_result(t.m, Assembler::negative); check_eq("testb sign", run(t.finish(), bytes[i]), (bytes[i] & 0x80) != 0); }
    { TestCode t; t.m->testb(eax, 0xFF); emit_condition_result(t.m, Assembler::zero); check_eq("testb zero", run(t.finish(), bytes[i]), (bytes[i] & 0xFF) == 0); }
    { TestCode t; t.m->decb(eax); emit_condition_result(t.m, Assembler::zero); check_eq("decb zero", run(t.finish(), bytes[i]), (bytes[i] & 0xFF) == 1); }
  }
  // flags survive moves, pushes and loads
  { TestCode t; t.m->cmpl(eax, ecx); t.m->movl(eax, 12345); t.m->pushl(eax); t.m->popl(edx); t.m->movl(edx, Address(esp, -8));
    emit_condition_result(t.m, Assembler::less); check_eq("flags preserved by moves", run(t.finish(), 1, 2), 1); }
}


static void test_multiply_divide() {
  for (int i = 0; i < nof_values; i++) {
    for (int j = 0; j < nof_values; j++) {
      intptr_t a = values[i], b = values[j];
      __int128 p = (__int128) a * (__int128) b;
      bool ovf = p != (__int128) (intptr_t) p;
      { TestCode t; t.m->imull(eax, ecx); check_eq("imull", run(t.finish(), a, b), (intptr_t) ((uint64_t) a * (uint64_t) b)); }
      { TestCode t; t.m->imull(eax, ecx); emit_condition_result(t.m, Assembler::overflow); check_eq("imull overflow", run(t.finish(), a, b), ovf); }
      { TestCode t; t.m->imull(eax, ecx); emit_condition_result(t.m, Assembler::noOverflow); check_eq("imull noOverflow", run(t.finish(), a, b), !ovf); }
      if (b == (intptr_t) (int) b) {
        TestCode t; t.m->imull(eax, eax, (int) b); check_eq("imull imm", run(t.finish(), a), (intptr_t) ((uint64_t) a * (uint64_t) b));
      }
      if (b != 0 && !(a == INTPTR_MIN && b == -1)) {
        // edx := sign extension of eax, then idiv ecx; returns quotient, and remainder in a second run
        { TestCode t; t.m->movl(edx, eax); t.m->sarl(edx, 63); t.m->idivl(ecx); check_eq("idivl quotient", run(t.finish(), a, b), a / b); }
        { TestCode t; t.m->movl(edx, eax); t.m->sarl(edx, 63); t.m->idivl(ecx); t.m->movl(eax, edx); check_eq("idivl remainder", run(t.finish(), a, b), a % b); }
      }
      unsigned __int128 u = (unsigned __int128) (uint64_t) a * (uint64_t) b;
      { TestCode t; t.m->mull(ecx); t.m->movl(eax, edx); check_eq("mull high", run(t.finish(), a, b), (intptr_t) (uint64_t) (u >> 64)); }
    }
  }
}


static intptr_t memory_cell[8];

static void test_memory() {
  intptr_t* s = stack_top();
  // stack slots via esp: positive, negative, large, indexed
  for (int i = 0; i < nof_values; i++) {
    intptr_t v = values[i];
    { TestCode t; t.m->movl(Address(esp, -16), eax); t.m->movl(eax, 0); t.m->movl(eax, Address(esp, -16)); check_eq("movl esp-16", run(t.finish(), v), v); }
    { TestCode t; t.m->movl(Address(esp, 8), eax); t.m->movl(eax, 0); t.m->movl(eax, Address(esp, 8)); check_eq("movl esp+8", run(t.finish(), v), v); }
    { TestCode t; t.m->movl(Address(esp, -1000), eax); t.m->movl(eax, Address(esp, -1000)); check_eq("movl esp-1000", run(t.finish(), v), v); }
    { TestCode t; t.m->movl(ecx, 3); t.m->movl(Address(esp, ecx, Address::times_8, -64), eax); t.m->movl(eax, Address(esp, -40)); check_eq("movl indexed", run(t.finish(), v), v); }
    { TestCode t; t.m->movl(Address(esp, -24), (int) v); t.m->movl(eax, Address(esp, -24)); if (v == (intptr_t) (int) v) check_eq("movl(adr, imm)", run(t.finish(), 0), v); }
    { TestCode t; t.m->movl(Address(esp, -8), 0x1122); t.m->movb(Address(esp, -8), eax); t.m->movl(eax, Address(esp, -8)); check_eq("movb(adr, reg)", run(t.finish(), v), 0x1100 | (v & 0xFF)); }
    { TestCode t; t.m->movl(Address(esp, -8), ecx); t.m->movb(eax, Address(esp, -8)); check_eq("movb(reg, adr)", run(t.finish(), v, 0x3456), (v & ~(intptr_t) 0xFF) | 0x56); }
    { TestCode t; t.m->movl(Address(esp, -8), eax); t.m->movsxb(eax, Address(esp, -8)); check_eq("movsxb", run(t.finish(), v), (intptr_t) (int8_t) v); }
    { TestCode t; t.m->movl(Address(esp, -8), eax); t.m->movsxw(eax, Address(esp, -8)); check_eq("movsxw", run(t.finish(), v), (intptr_t) (int16_t) v); }
    { TestCode t; t.m->movl(Address(esp, -8), ecx); t.m->movw(eax, Address(esp, -8)); check_eq("movw(reg, adr)", run(t.finish(), v, 0x123456), (v & ~(intptr_t) 0xFFFF) | 0x3456); }
    { TestCode t; t.m->movsxb(eax, eax); check_eq("movsxb reg", run(t.finish(), v), (intptr_t) (int8_t) v); }
    { TestCode t; t.m->movl(Address(esp, -8), eax); t.m->addl(Address(esp, -8), 5); t.m->incl(Address(esp, -8)); t.m->movl(eax, Address(esp, -8)); check_eq("addl/incl adr", run(t.finish(), v), (intptr_t) ((uint64_t) v + 6)); }
    { TestCode t; t.m->movl(Address(esp, -8), ecx); t.m->cmpl(eax, Address(esp, -8)); emit_condition_result(t.m, Assembler::less); check_eq("cmpl(reg, adr)", run(t.finish(), v, 5), v < 5); }
    { TestCode t; t.m->movl(Address(esp, -8), eax); t.m->cmpl(Address(esp, -8), 5); emit_condition_result(t.m, Assembler::greater); check_eq("cmpl(adr, imm)", run(t.finish(), v), v > 5); }
    { TestCode t; t.m->movl(Address(esp, -8), ecx); t.m->orl(eax, Address(esp, -8)); check_eq("orl(reg, adr)", run(t.finish(), v, 0x30), v | 0x30); }
  }
  // lea
  { TestCode t; t.m->leal(eax, Address(eax, ecx, Address::times_4, 12)); check_eq("leal index", run(t.finish(), 100, 3), 124); }
  { TestCode t; t.m->leal(eax, Address(eax, -20)); check_eq("leal negative", run(t.finish(), 100), 80); }
  { TestCode t; t.m->leal(eax, Address(eax, 100000)); check_eq("leal large", run(t.finish(), 100), 100100); }
  // absolute addresses
  memory_cell[2] = 0;
  { TestCode t; t.m->movl(Address((intptr_t) &memory_cell[2], relocInfo::external_word_type), eax); t.m->movl(eax, 0);
    t.m->movl(eax, Address((intptr_t) &memory_cell[2], relocInfo::external_word_type));
    check_eq("absolute address", run(t.finish(), 4242), 4242); check_eq("absolute address store", memory_cell[2], 4242); }
  // push / pop / pushad / popad keep esp balanced; esp is returned as the result
  { TestCode t; t.m->pushl(eax); t.m->pushl(ecx); t.m->popl(eax); t.m->popl(ecx); t.m->subl(eax, ecx); check_eq("push/pop order", run(t.finish(), 10, 3), 3 - 10); }
  { TestCode t; t.m->pushl(5); t.m->pushl(oop(0x2001)); t.m->popl(eax); t.m->popl(ecx); t.m->addl(eax, ecx); check_eq("push imm/oop", run(t.finish(), 0), 0x2006); }
  { TestCode t; t.m->pushad(); t.m->movl(eax, 0); t.m->movl(ecx, 0); t.m->popad(); t.m->addl(eax, ecx); check_eq("pushad/popad", run(t.finish(), 40, 2), 42); }
  { TestCode t; t.m->pushad(); t.m->popad(); t.m->movl(eax, esp); check_eq("pushad/popad esp", run(t.finish(), 0), (intptr_t) s); }
  { TestCode t; t.m->pushl(esp); t.m->popl(eax); check_eq("push esp", run(t.finish(), 0), (intptr_t) s); }
  { TestCode t; t.m->movl(Address(esp, -8), eax); t.m->subl(esp, 8); t.m->pushl(Address(esp, 0)); t.m->popl(Address(esp, 8)); t.m->popl(ecx); t.m->movl(eax, ecx); check_eq("push/pop adr", run(t.finish(), 77), 77); }
  { TestCode t; t.m->enter(); t.m->movl(eax, ebp); t.m->leave(); t.m->subl(eax, esp); check_eq("enter/leave", run(t.finish(), 0), -8); }
}


static void test_labels_and_calls() {
  // several forward uses of one label, and backward jumps (a counting loop)
  { TestCode t; Label L;
    t.m->cmpl(eax, 1); t.m->jcc(Assembler::equal, L);
    t.m->cmpl(eax, 2); t.m->jcc(Assembler::equal, L);
    t.m->cmpl(eax, 3); t.m->jcc(Assembler::equal, L);
    t.m->movl(eax, 0); Label done; t.m->jmp(done);
    t.m->bind(L); t.m->movl(eax, 1); t.m->bind(done);
    TestFn f = t.finish();
    for (int i = 0; i < 5; i++) check_eq("label chain", run(f, i), i >= 1 && i <= 3);
  }
  { TestCode t; Label loop; t.m->movl(ecx, 0);
    t.m->bind(loop); t.m->addl(ecx, eax); t.m->decl(eax); t.m->jcc(Assembler::notZero, loop);
    t.m->movl(eax, ecx);
    check_eq("backward loop", run(t.finish(), 10), 55); }
  // local subroutine (Delta convention), ret with argument popping
  { TestCode t; Label sub, over;
    t.m->jmp(over);
    t.m->bind(sub); t.m->movl(edx, Address(esp, 8)); t.m->addl(eax, edx); t.m->ret(8);
    t.m->bind(over); t.m->pushl(ecx); t.m->call(sub); t.m->movl(ecx, esp);
    TestFn f = t.finish();
    check_eq("call(Label)/ret(n)", run(f, 30, 12), 42); }
  { TestCode t; Label sub, over;
    t.m->jmp(over); t.m->bind(sub); t.m->incl(eax); t.m->ret(0);
    t.m->bind(over); t.m->call(sub); t.m->call(sub);
    check_eq("call twice", run(t.finish(), 1), 3); }
  { TestCode t; Label sub, over;
    t.m->jmp(over); t.m->bind(sub); t.m->ret(0);
    t.m->bind(over); t.m->call(sub); t.m->movl(eax, esp);
    check_eq("call/ret balance esp", run(t.finish(), 0), (intptr_t) stack_top()); }
  // calls and jumps to absolute addresses (another code blob)
  { // the stub is Delta code (no prologue/epilogue)
    CodeBuffer* cb = new CodeBuffer(1024, 256);
    MacroAssembler* sm = new MacroAssembler(cb);
    sm->addl(eax, ecx); sm->ret(0);
    char* stub = install(cb->code_begin(), cb->code_size());
    { TestCode t; t.m->call(stub, relocInfo::runtime_call_type); t.m->call(stub, relocInfo::runtime_call_type); check_eq("call(entry)", run(t.finish(), 1, 20), 41); }
    { TestCode t; t.m->movl(edx, (intptr_t) stub); t.m->call(edx); check_eq("call(reg)", run(t.finish(), 1, 20), 21); }
    { TestCode t; t.m->movl(Address(esp, -32), (intptr_t) stub); t.m->call(Address(esp, -32)); check_eq("call(adr)", run(t.finish(), 1, 20), 21); }
    // jmp(entry): the stub returns to the caller of the jumping code
    CodeBuffer* cb2 = new CodeBuffer(1024, 256);
    MacroAssembler* jm = new MacroAssembler(cb2);
    jm->incl(eax); jm->jmp(stub, relocInfo::runtime_call_type);
    char* jumper = install(cb2->code_begin(), cb2->code_size());
    { TestCode t; t.m->call(jumper, relocInfo::runtime_call_type); check_eq("jmp(entry)", run(t.finish(), 1, 20), 22); }
    // jcc(cc, entry)
    CodeBuffer* cb3 = new CodeBuffer(1024, 256);
    MacroAssembler* cm = new MacroAssembler(cb3);
    cm->cmpl(eax, 0); cm->jcc(Assembler::less, stub, relocInfo::runtime_call_type); cm->movl(eax, 99); cm->ret(0);
    char* cjumper = install(cb3->code_begin(), cb3->code_size());
    { TestCode t; t.m->call(cjumper, relocInfo::runtime_call_type); TestFn f = t.finish();
      check_eq("jcc(entry) taken", run(f, -5, 20), 15); check_eq("jcc(entry) not taken", run(f, 5, 20), 99); }
  }
  // ic_info: bound and unbound labels encode the offset (relative to the info) and the flags
  { CodeBuffer* cb = new CodeBuffer(1024, 256);
    MacroAssembler* m = new MacroAssembler(cb);
    Label back, fwd;
    m->bind(back); m->nop(); m->nop();
    int p1 = m->offset(); m->ic_info(back, 5);
    int p2 = m->offset(); m->ic_info(fwd, 3);
    int p3 = m->offset(); m->ic_info(fwd, 7);
    m->nop();
    int target = m->offset(); m->bind(fwd);
    insn* code = (insn*) cb->code_begin();
    for (int k = 0; k < 3; k++) {
      int p = k == 0 ? p1 : (k == 1 ? p2 : p3);
      int data = ((code[p / 4] >> 5) & 0xFFFF) | (((code[p / 4 + 1] >> 5) & 0xFFFF) << 16);
      int expected_offs  = k == 0 ? 0 - p1 : target - p;
      int expected_flags = k == 0 ? 5 : (k == 1 ? 3 : 7);
      check_eq("ic_info offset", data >> IC_Info::number_of_flags, expected_offs);
      check_eq("ic_info flags", data & IC_Info::flags_mask, expected_flags);
    }
  }
  check_eq("sizeOfCall", Assembler::sizeOfCall, 24);
}


static intptr_t c_sum(intptr_t a, intptr_t b, intptr_t c, intptr_t d) { return a + 2 * b + 3 * c + 4 * d; }
static intptr_t c_args8(intptr_t a, intptr_t b, intptr_t c, intptr_t d, intptr_t e, intptr_t f, intptr_t g, intptr_t h) {
  return a + b * 10 + c * 100 + d * 1000 + e * 10000 + f * 100000 + g * 1000000 + h * 10000000;
}
static intptr_t* seen_last_Delta_sp;
static intptr_t  seen_return_address;
static intptr_t c_frame_probe(intptr_t a) {
  seen_last_Delta_sp  = (intptr_t*) last_Delta_sp;
  seen_return_address = seen_last_Delta_sp[-1];
  return a + 1;
}

static void test_c_calls() {
  intptr_t* saved_fp = last_Delta_fp;
  oop*      saved_sp = last_Delta_sp;
  // register arguments, including permutations of eax/ecx/edx
  { TestCode t; t.m->movl(ebx, 4); t.m->call_C((char*) &c_sum, eax, ecx, edx, ebx); check_eq("call_C 4 register args", run(t.finish(), 1, 2, 3), 1 + 4 + 9 + 16); }
  { TestCode t; t.m->movl(ebx, 4); t.m->call_C((char*) &c_sum, edx, eax, ecx, ebx); check_eq("call_C permuted args", run(t.finish(), 1, 2, 3), 3 + 2 + 6 + 16); }
  // callee saved registers survive, esp and ebp too
  // (eax, ecx and edx are clobbered by C, as on x86)
  { TestCode t; t.m->movl(ebx, 100); t.m->movl(esi, 20); t.m->movl(edi, 3);
    t.m->call_C((char*) &c_sum, eax, eax, eax, eax);
    t.m->addl(ebx, esi); t.m->addl(ebx, edi); t.m->movl(eax, ebx);
    check_eq("call_C preserves ebx/esi/edi", run(t.finish(), 1), 123); }
  { TestCode t; t.m->movl(ebp, 7); t.m->call_C((char*) &c_sum, eax, eax, eax, eax); t.m->movl(eax, esp); t.m->addl(eax, ebp);
    check_eq("call_C preserves esp/ebp", run(t.finish(), 1), (intptr_t) stack_top() + 7); }
  // stack arguments (x86 convention: pushed in reverse order)
  { TestCode t; for (int i = 8; i >= 1; i--) t.m->pushl(i);
    t.m->call_C((char*) &c_args8, relocInfo::runtime_call_type); t.m->addl(esp, 8 * oopSize);
    check_eq("call_C 8 stack args", run(t.finish(), 0), 87654321); }
  // the last Delta frame ends with a return address in the calling code
  { TestCode t; t.m->call_C((char*) &c_frame_probe, eax);
    TestFn f = t.finish();
    check_eq("call_C result", run(f, 41), 42);
    check_eq("call_C last_Delta_sp", (intptr_t) seen_last_Delta_sp, (intptr_t) stack_top());
    check(seen_return_address > (intptr_t) f && seen_return_address < (intptr_t) f + 4096, "call_C return address in code", seen_return_address, (intptr_t) f); }
  check_eq("call_C resets last_Delta_fp", (intptr_t) last_Delta_fp, 0);
  last_Delta_fp = saved_fp;
  last_Delta_sp = saved_sp;
  // store check: marks the card of the object
  { static char cards[64];
    char* saved_base = byte_map_base;
    byte_map_base = cards;
    memset(cards, 1, sizeof cards);
    TestCode t; t.m->store_check(eax, ecx); run(t.finish(), 5 << card_shift);
    check_eq("store_check card", cards[5], 0); check_eq("store_check neighbour", cards[4], 1);
    byte_map_base = saved_base; }
}


static double dcell[8];

static void test_fpu() {
  double xs[] = { 0.0, 1.0, -1.0, 2.5, -3.75, 1e10, 1e-10, 7.0, 0.5 };
  int nx = sizeof(xs) / sizeof(xs[0]);
  for (int i = 0; i < nx; i++) {
    for (int j = 0; j < nx; j++) {
      double x = xs[i], y = xs[j];
      dcell[0] = x; dcell[1] = y;
      Address a0((intptr_t) &dcell[0], relocInfo::external_word_type);
      Address a1((intptr_t) &dcell[1], relocInfo::external_word_type);
      Address a2((intptr_t) &dcell[2], relocInfo::external_word_type);
      // ST(1) = x, ST(0) = y: binary operations ST(1) op ST(0)
      struct { const char* name; int op; double expected; } ops[] = {
        { "faddp", 0, x + y }, { "fsubp", 1, x - y }, { "fmulp", 2, x * y }, { "fdivp", 3, x / y }, { "fsubrp", 4, y - x },
      };
      for (int k = 0; k < 5; k++) {
        if (ops[k].op == 3 && y == 0.0) continue;
        TestCode t; t.m->fld_d(a0); t.m->fld_d(a1);
        switch (ops[k].op) {
          case 0: t.m->faddp(); break; case 1: t.m->fsubp(); break; case 2: t.m->fmulp(); break;
          case 3: t.m->fdivp(); break; case 4: t.m->fsubrp(); break;
        }
        t.m->fstp_d(a2);
        dcell[2] = 12345;
        run(t.finish(), 0);
        check(dcell[2] == ops[k].expected, ops[k].name, (intptr_t) (dcell[2] * 1000), (intptr_t) (ops[k].expected * 1000));
      }
      // compares via fnstsw_ax + mask (as the code generators do): ST(0) = x compared with ST(1) = y
      Assembler::Condition ccs[] = { Assembler::equal, Assembler::notEqual, Assembler::less, Assembler::lessEqual, Assembler::greater, Assembler::greaterEqual };
      bool expected[] = { x == y, x != y, x < y, x <= y, x > y, x >= y };
      for (int k = 0; k < 6; k++) {
        int mask; Assembler::Condition cond;
        MacroAssembler::fpu_mask_and_cond_for(ccs[k], mask, cond);
        TestCode t; t.m->fld_d(a1); t.m->fld_d(a0); t.m->fcompp(); t.m->fnstsw_ax(); t.m->testl(eax, mask);
        emit_condition_result(t.m, cond);
        check_eq("fcompp + fnstsw_ax", run(t.finish(), 0), expected[k]);
      }
      if (y != 0.0) {
        TestCode t; t.m->fld_d(a1); t.m->fld_d(a0); t.m->fprem(); t.m->fstp_d(a2); t.m->fpop();
        dcell[2] = 12345; run(t.finish(), 0);
        check(dcell[2] == fmod(x, y), "fprem", (intptr_t) (dcell[2] * 1000), (intptr_t) (fmod(x, y) * 1000));
      }
    }
    double x = xs[i];
    dcell[0] = x;
    Address a0((intptr_t) &dcell[0], relocInfo::external_word_type);
    Address a2((intptr_t) &dcell[2], relocInfo::external_word_type);
    { TestCode t; t.m->fld_d(a0); t.m->fchs(); t.m->fstp_d(a2); run(t.finish(), 0); check(dcell[2] == -x, "fchs", 0, 0); }
    { TestCode t; t.m->fld_d(a0); t.m->fabs(); t.m->fstp_d(a2); run(t.finish(), 0); check(dcell[2] == fabs(x), "fabs", 0, 0); }
    { TestCode t; t.m->fld_d(a0); t.m->ftst(); t.m->fnstsw_ax(); t.m->fpop(); t.m->testl(eax, 0x4000); emit_condition_result(t.m, Assembler::notZero);
      check_eq("ftst equal zero", run(t.finish(), 0), x == 0.0); }
    { TestCode t; t.m->fld1(); t.m->fld_d(a0); t.m->faddp(); t.m->fldz(); t.m->faddp(); t.m->fstp_d(a2); run(t.finish(), 0); check(dcell[2] == x + 1.0, "fld1/fldz", 0, 0); }
    { TestCode t; t.m->fld_d(a0); t.m->fld1(); t.m->fxch(); t.m->fsubp(); t.m->fstp_d(a2); run(t.finish(), 0); check(dcell[2] == 1.0 - x, "fxch", 0, 0); }
    // float (single) load/store
    { float fs = (float) x; Address af((intptr_t) &fs, relocInfo::external_word_type);
      TestCode t; t.m->fld_s(af); t.m->fstp_d(a2); run(t.finish(), 0); check(dcell[2] == (double) (float) x, "fld_s", 0, 0); }
  }
  // integer conversions (x87 rounds to nearest even)
  intptr_t ints[] = { 0, 1, -1, 123456, -98765, (intptr_t) 1 << 40 };
  for (int i = 0; i < 6; i++) {
    intptr_t cell = ints[i];
    Address ac((intptr_t) &cell, relocInfo::external_word_type);
    Address a2((intptr_t) &dcell[2], relocInfo::external_word_type);
    { TestCode t; t.m->fild_d(ac); t.m->fstp_d(a2); run(t.finish(), 0); check(dcell[2] == (double) ints[i], "fild_d", 0, 0); }
    if (ints[i] == (int32_t) ints[i]) {
      int32_t c32 = (int32_t) ints[i];
      Address a32((intptr_t) &c32, relocInfo::external_word_type);
      TestCode t; t.m->fild_s(a32); t.m->fstp_d(a2); run(t.finish(), 0); check(dcell[2] == (double) ints[i], "fild_s", 0, 0);
    }
  }
  double halves[] = { 2.5, 3.5, -2.5, 2.4, -2.6 };
  intptr_t rounded[] = { 2, 4, -2, 2, -3 };
  for (int i = 0; i < 5; i++) {
    dcell[0] = halves[i];
    intptr_t cell = 0;
    Address a0((intptr_t) &dcell[0], relocInfo::external_word_type);
    Address ac((intptr_t) &cell, relocInfo::external_word_type);
    TestCode t; t.m->fld_d(a0); t.m->fistp_d(ac); run(t.finish(), 0);
    check_eq("fistp_d rounds to nearest even", cell, rounded[i]);
  }
}


// Code memory: writes in a JITWriteScope, and writes/executions outside of one that are
// handled by the fault handler switching the thread's mode.
typedef intptr_t (*Fn0)();

static void test_code_memory() {
  init_jit_memory();
  char* p = install(NULL, 0);	// a fresh slot
  p = (char*) (((intptr_t) p + 15) & ~15);
  insn ret7[] = { movz(0, 7, 0), a64::ret() };
  insn ret9[] = { movz(0, 9, 0), a64::ret() };
  { JITWriteScope w; memcpy(p, ret7, sizeof ret7); }
  os::flush_icache(p, sizeof ret7);
  check_eq("code memory: written in scope, executed", ((Fn0) p)(), 7);
  int faults = os::jit_mode_faults();
  memcpy(p, ret9, sizeof ret9);		// outside a scope: write fault switches to writing
  os::flush_icache(p, sizeof ret9);
  check_eq("code memory: written without scope, executed", ((Fn0) p)(), 9);	// fetch fault switches back
  check_eq("code memory: mode faults", os::jit_mode_faults() - faults, 2);
  // a nested scope keeps the thread writing until the outermost scope ends
  { JITWriteScope a; { JITWriteScope b; memcpy(p, ret7, sizeof ret7); } p[64] = 1; }
  os::flush_icache(p, sizeof ret7);
  check_eq("code memory: nested scopes", ((Fn0) p)(), 7);
  check_eq("code memory: no faults with scopes", os::jit_mode_faults() - faults, 2);
  check_eq("code memory: is_code_memory", os::is_code_memory(p), 1);
  check_eq("code memory: heap is not code memory", os::is_code_memory(&failures), 0);
}


static intptr_t c_sum(intptr_t a, intptr_t b, intptr_t c, intptr_t d);

// Code patterns read and patched by the runtime (nativeInstruction.hpp, dll.hpp, relocInfo.hpp)
static void test_native_instructions() {
  // Delta call stubs (return eax + 1 and eax + 2)
  CodeBuffer* sb = new CodeBuffer(1024, 256);
  MacroAssembler* sm = new MacroAssembler(sb);
  sm->incl(eax); sm->ret(0);
  int second = sm->offset(); sm->addl(eax, 2); sm->ret(0);
  char* stubs = install(sb->code_begin(), sb->code_size());
  char* stub1 = stubs, *stub2 = stubs + second;

  // a call with IC info (as a compiled send), copied to code memory; the offsets are those of the copy
  for (int pad = 0; pad < 2; pad++) {
    TestCode t;
    if (pad) t.m->nop();				// changes the alignment: the call may get a leading nop
    int call_start = t.m->offset();
    Label nlr;
    t.m->call(stub1, relocInfo::ic_type);
    int ret = t.m->offset();
    t.m->ic_info(nlr, 0x5A);
    t.m->bind(nlr);
    TestFn f = t.finish();
    char* code = (char*) f;
    NativeCall* c = nativeCall_from_return_address(code + ret);
    check_eq("NativeCall destination", (intptr_t) c->destination(), (intptr_t) stub1);
    check_eq("nativeCall_at", (intptr_t) nativeCall_at(code + call_start), (intptr_t) c);
    check_eq("nativeCall_from_relocInfo", (intptr_t) nativeCall_from_relocInfo(code + ret - 8), (intptr_t) c);
    IC_Info* info = ic_info_at(c->next_instruction_address());
    check_eq("IC_Info flags", info->flags(), 0x5A);
    check_eq("IC_Info NLR target", (intptr_t) info->NLR_target(), (intptr_t) code + ret + 8);
    info->set_flags(0x13);
    check_eq("IC_Info set_flags", info->flags(), 0x13);
    check_eq("IC_Info set_flags keeps NLR target", (intptr_t) info->NLR_target(), (intptr_t) code + ret + 8);
    check_eq("patched code: call before", run(f, 10), 11);
    c->set_destination(stub2);
    check_eq("NativeCall set_destination", (intptr_t) c->destination(), (intptr_t) stub2);
    check_eq("patched code: call after", run(f, 10), 12);
  }

  // compiled DLL cache: the DLL stub sees the entry point in edx
  { CodeBuffer* db = new CodeBuffer(1024, 256);
    MacroAssembler* dm = new MacroAssembler(db);
    dm->movl(eax, edx); dm->ret(0);
    char* dll_stub = install(db->code_begin(), db->code_size());
    TestCode t;
    t.m->call_DLL_cache(dll_stub, (char*) 0x1234560, oop(0x2001), oop(0x3001));
    int ret = t.m->offset();
    TestFn f = t.finish();
    CompiledDLL_Cache* d = (CompiledDLL_Cache*) nativeCall_from_return_address((char*) f + ret);
    check_eq("DLL cache entry point", (intptr_t) d->entry_point(), 0x1234560);
    check_eq("DLL cache dll name", (intptr_t) d->dll_name(), 0x2001);
    check_eq("DLL cache function name", (intptr_t) d->function_name(), 0x3001);
    check_eq("DLL cache destination", (intptr_t) d->destination(), (intptr_t) dll_stub);
    check_eq("DLL cache: edx = entry point", run(f, 0), 0x1234560);
    d->set_entry_point((dll_func) 0x7654320);
    check_eq("DLL cache set_entry_point", run(f, 0), 0x7654320);
  }

  // a C call (as for primitives) is a NativeCall too: destination literal before the return address
  for (int pad = 0; pad < 2; pad++) {
    TestCode t;
    if (pad) t.m->nop();
    t.m->movl(ebx, 4);
    t.m->pushl(ebx); t.m->pushl(ebx); t.m->pushl(ebx); t.m->pushl(ebx);
    int before = t.m->offset();
    t.m->call_C((char*) &c_sum, relocInfo::prim_type);
    // the return address is the end of the destination literal (code after it checks for deoptimization)
    int ret = -1;
    for (int p = before; p + 8 <= t.m->offset(); p += 4) {
      if (*(intptr_t*) (t.cb->code_begin() + p) == (intptr_t) &c_sum) { ret = p + 8; break; }
    }
    check(ret > 0 && (ret & 7) == 0, "C call: aligned destination literal", ret, 0);
    t.m->addl(esp, 4 * oopSize);
    TestFn f = t.finish();
    NativeCall* c = nativeCall_from_return_address((char*) f + ret);
    check_eq("C call: NativeCall destination", (intptr_t) c->destination(), (intptr_t) &c_sum);
    check_eq("C call: result", run(f, 0), 4 + 8 + 12 + 16);
  }

  // relocation offsets (4-byte units on arm64)
  int offsets[] = { 4, 8, 400, 8188, 32764 };
  for (int i = 0; i < 5; i++) {
    relocInfo r(relocInfo::ic_type, offsets[i]);
    check_eq("relocInfo offset", r.offset(), offsets[i]);
    check_eq("relocInfo type", r.type(), relocInfo::ic_type);
  }
}


bool arm64_assembler_test() {
  ResourceMark rm;
  lprintf("arm64 assembler test\n");
  test_code_memory();     lprintf("  code memory: %d checks\n", checks);
  test_immediates();      lprintf("  immediates: %d checks\n", checks);
  test_arithmetic();      lprintf("  arithmetic: %d checks\n", checks);
  test_shifts();          lprintf("  shifts: %d checks\n", checks);
  test_conditions();      lprintf("  conditions: %d checks\n", checks);
  test_multiply_divide(); lprintf("  multiply/divide: %d checks\n", checks);
  test_memory();          lprintf("  memory: %d checks\n", checks);
  test_labels_and_calls();lprintf("  labels/calls: %d checks\n", checks);
  test_c_calls();         lprintf("  C calls: %d checks\n", checks);
  test_fpu();             lprintf("  fpu: %d checks\n", checks);
  test_native_instructions(); lprintf("  native instructions: %d checks\n", checks);
  lprintf("arm64 assembler test: %d checks, %d failures\n", checks, failures);
  return failures == 0;
}

#endif // STRONGTALK_ARM64
