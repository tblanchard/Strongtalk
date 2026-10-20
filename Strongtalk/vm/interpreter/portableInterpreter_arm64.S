// Copyright (c) 2006, Sun Microsystems, Inc. All rights reserved.
// (see the license in portableInterpreter.cpp)
//
// Transitions between the portable (C++) interpreter and compiled code on arm64
// (see portableInterpreter.cpp, "Compiled code").
//
// Registers of Delta code (see assembler_arm64.hpp): eax = x0, ecx = x1, edx = x2,
// ebx = x19, esi = x20, edi = x21, esp = x28 (Delta stack pointer), ebp = x29.
// NLRs: eax = result, edi = home frame pointer, esi = home scope id.
//
// x27 points to the save area of the innermost delta_call_compiled while compiled
// code runs (compiled code does not use x22-x27, C preserves them).

#if defined(__aarch64__) || defined(__arm64__)

        .text

// ---------------------------------------------------------------------------
// Interpreter return points: the return pcs of interpreted frames. Each point is
// 16 bytes: IC info (an NLR target 12 bytes ahead), then the continuations for a
// normal return and an NLR from compiled code called by the interpreter.
// return_pc(p) = portable_interpreter_return_points + 16 * p.

        .p2align 4
        .globl _portable_interpreter_return_points
_portable_interpreter_return_points:
        .rept 8
        movz    xzr, #0xc00             // IC info: NLR target = this + 12, no flags
        movk    xzr, #0, lsl #16
        b       _compiled_returned_normal
        b       _compiled_returned_nlr
        .endr
        .globl _portable_interpreter_return_points_end
_portable_interpreter_return_points_end:

// The return pc of entry frames (Delta frames called from C, see frame::is_entry_frame);
// compiled code called from C returns here.
        .p2align 4
        .globl _return_from_Delta
_return_from_Delta:
        movz    xzr, #0xc00
        movk    xzr, #0, lsl #16
        b       _compiled_returned_normal
        b       _compiled_returned_nlr


// ---------------------------------------------------------------------------
// void delta_call_compiled(char* entry, oop receiver, oop* sp, intptr_t* fp, char* return_pc, CompiledReturn* out)
//
// Calls compiled code at entry with esp = sp (arguments on top), ebp = fp and eax = receiver,
// pushing return_pc as its return address. Returns when the compiled code returns to
// return_pc, normally or with an NLR; out (see CompiledReturn) describes how.

        .equ    SAVE_SIZE, 112          // x19-x30 (96), out (8), padding (8)
        .equ    SAVE_OUT, 96

        .p2align 2
        .globl _delta_call_compiled
_delta_call_compiled:
        sub     sp, sp, #SAVE_SIZE
        stp     x19, x20, [sp, #0]
        stp     x21, x22, [sp, #16]
        stp     x23, x24, [sp, #32]
        stp     x25, x26, [sp, #48]
        stp     x27, x28, [sp, #64]
        stp     x29, x30, [sp, #80]
        str     x5, [sp, #SAVE_OUT]
        mov     x27, sp                 // the innermost save area
        sub     x28, x2, #8             // push the return pc
        str     x4, [x28]
        mov     x29, x3
        mov     x16, x0
        mov     x0, x1                  // eax := receiver
        br      x16

// compiled code returned normally: eax = result, esp = after popping the return pc and the arguments
_compiled_returned_normal:
        ldr     x9, [x27, #SAVE_OUT]
        str     x0, [x9, #0]            // out->result
        str     x28, [x9, #8]           // out->sp
        str     x29, [x9, #16]          // out->fp
        str     xzr, [x9, #24]          // out->kind = normal
        b       restore_C

// compiled code returned with an NLR (continue_NLR jumped to the NLR target of the return point)
_compiled_returned_nlr:
        ldr     x9, [x27, #SAVE_OUT]
        str     x0, [x9, #0]            // out->result
        str     x28, [x9, #8]           // out->sp
        str     x29, [x9, #16]          // out->fp
        mov     x10, #1
        str     x10, [x9, #24]          // out->kind = NLR
        str     x21, [x9, #32]          // out->home    (edi)
        str     x20, [x9, #40]          // out->home_id (esi)

restore_C:
        mov     sp, x27
        ldp     x19, x20, [sp, #0]
        ldp     x21, x22, [sp, #16]
        ldp     x23, x24, [sp, #32]
        ldp     x25, x26, [sp, #48]
        ldp     x27, x28, [sp, #64]
        ldp     x29, x30, [sp, #80]
        add     sp, sp, #SAVE_SIZE
        ret


// ---------------------------------------------------------------------------
// The method entry for calls of interpreted methods from compiled code (method_entry_point,
// jumped to by the PIC and megamorphic IC stubs):
//
// eax = receiver, ecx = methodOop, esp: return address into compiled code, arguments.
//
// interpret_from_compiled(method, receiver, sp, fp, out) runs the method in the interpreter;
// out (see CompiledReturn) gives the Delta registers and the continuation in compiled code
// (the return address, or the NLR target of the IC info there).

        .p2align 2
        .globl _compiled_to_interpreted
_compiled_to_interpreted:
        sub     sp, sp, #64             // CompiledReturn (7 words) + padding
        mov     x9, x0
        mov     x0, x1                  // method
        mov     x1, x9                  // receiver
        mov     x2, x28                 // sp (the return address is at sp[0])
        mov     x3, x29                 // fp of the compiled caller
        mov     x4, sp                  // out
        bl      _interpret_from_compiled
continue_compiled:                      // sp: the CompiledReturn filled in by the interpreter
        ldr     x0, [sp, #0]            // eax := result
        ldr     x28, [sp, #8]           // esp
        ldr     x29, [sp, #16]          // ebp
        ldr     x21, [sp, #32]          // edi := NLR home    (NLR only)
        ldr     x20, [sp, #40]          // esi := NLR home id (NLR only)
        ldr     x16, [sp, #48]          // continuation
        add     sp, sp, #64
        br      x16



// ---------------------------------------------------------------------------
// restart_primitiveValue: redoes the evaluation of a block (jumped to by the zombie block and
// deoptimize block stubs after patching the closure):
//
// eax = block closure, esp: return address of the primitiveValue call, the block's arguments,
// the closure.

        .equ    METHOD_OR_ENTRY, 15     // blockClosureOopDesc::method_or_entry_byte_offset() (tagged)

        .p2align 2
        .globl _restart_primitiveValue
_restart_primitiveValue:
        ldur    x9, [x0, #METHOD_OR_ENTRY]
        tst     x9, #1                  // Mem_Tag: methodOop
        b.ne    1f
        br      x9                      // compiled block: its jump table entry
1:      sub     sp, sp, #64             // interpreted block: as compiled_to_interpreted
        mov     x1, x28                 // sp (the return address is at sp[0])
        mov     x2, x29                 // fp
        mov     x3, sp                  // out
        bl      _interpret_block_from_compiled
        b       continue_compiled

#endif
