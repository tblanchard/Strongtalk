# Strongtalk → 64-bit Apple Silicon port: progress log

Scope chosen: **Option 1** — make the C++ runtime 64-bit clean and buildable on
macOS/arm64. The x86-32 interpreter/JIT backend is kept compiling but fenced off
(it cannot execute on arm64). A runnable VM needs a portable interpreter (Option 2).

## How to resume

- Syntax-check all VM sources (command line clang, does not touch Xcode):
  `porting/stcheck.sh` → per-file logs in `/tmp/stlogs/`, summary in `/tmp/stlogs/summary.txt`.
  Then: `cd /tmp/stlogs; cat *.log | grep 'error:' | sort -u > uniq_errors.txt`
- `porting/fixforscope.py` hoists MSVC-style `for (int i…)` variables used after the loop
  (reads `/tmp/stlogs/uniq_errors.txt`; run until it reports `hoisted 0`).
- `porting/batch3.py` (re-runnable) rewrites `(int)pointer` casts reported in
  `uniq_errors.txt`: `x86_imm32(...)` in x86 backend files, `(intptr_t)` elsewhere.
- `porting/batchN.py` are the one-off edit batches already applied (kept for reference;
  do not blindly re-run batch1/2/4–8).
- All changes are uncommitted in the git working tree (`git diff --stat`).

## Status

### Done
- [x] Core word size: `config.hpp` (`BytesPerWord = sizeof(void*)`, scalar typedefs incl.
      `typedef intptr_t smi`), `tag.hpp`, `bits.hpp`, `oop.hpp`, `smiOop.hpp`
      (SmallInteger range now ±2^61), `markOop.hpp` (64-bit layout, sentinel = MSB, 52-bit hash).
- [x] Removed `#define bool` and the empty `assert` override from `types.hpp`; `error.hpp`
      provides assert (enabled with `-DASSERT`).
- [x] New `topIncludes/platform.hpp`: C headers, POSIX replacements for MSVC names,
      `__stdcall` neutralized, `x86_imm32()` checked conversion for x86 code emitters.
- [x] MSVC-only C++ fixed: token pasting (`vmSymbols.hpp`, `dispatchTable.cpp`), operator-keyword
      names (`and/or/not` → `and_op/or_op/not_op`, `IntegerOps::and_/or_/xor_`, `negated`),
      for-scope hoisting (~100 sites), friend-only functions given namespace prototypes,
      `= NULL` pure specifiers, const string literals, missing includes / header cycles.
- [x] Pointer-in-int fixes (runtime): smi prims, `GrowableArray`, scopeDesc recorder
      (oops now word slots, sections word aligned), lookup cache, memory system,
      proxies/FFI call-outs (word-sized args), `nlr_home`, frame model (`intptr_t*` slots),
      deoptimization/stack chunks/evaluator/dispatchTable frame pointers.
- [x] Bytecode operand alignment uses `oopSize` (methodOop, codeIterator).
- [x] Inline asm replaced: `IntegerOps` digit ops (portable uint64_t), `ps()/pd()`
      (`__builtin_frame_address`), `halt` (`__builtin_debugtrap`), `SavedRegisters::save_registers`
      and `oopify_float` (x86/MSVC only; fatal elsewhere).
- [x] `process_asm.cpp` disabled (`#if 0`; unused per its own comment).

- [x] **All 179 `vm/*.cpp` pass `clang++ -arch arm64 -fsyntax-only`** (0 errors, 2026-10-18).
- [x] OS layer: new `runtime/os_posix.cpp` (pthreads, manual-reset events via mutex/cond,
      Mach thread suspend/get_state for the profiler, mmap/mprotect VM, mach_absolute_time,
      getrusage/thread_info, dlopen, recursive-mutex ThreadCritical, SIGINT/SIGQUIT/fault
      signal handlers, watcher thread). Win32 `runtime/os.cpp` untouched (only compiled
      under MSVC: `#ifndef __GNUC__`).

- [x] Assembly runtime ported to C++ in `runtime/portable_runtime.cpp` (copy_oops_up, set_oops,
      smi/double arithmetic primitives, primitiveNew0-9, allocateBlock0-9, allocateContext*).
      NOTE primitive args arrive reversed: f(argument, receiver) — see prim_def.hpp.
- [x] `runtime/x86_backend_stubs.cpp`: 96 fatal stubs + data for interpreter_asm.asm /
      callBackStubs.asm symbols (interpreter cases, call_delta, NLR, deopt returns, callbacks).
- [x] Debug build (`-DASSERT -DDEBUG`) also compiles (asserts were previously compiled out).
- [x] `porting/stlink.sh`: command-line compile (to /tmp/stobj) + link; undefined symbols in
      /tmp/stobj/undefined.txt. NOTE: only recompiles when the .cpp changes — `rm -rf /tmp/stobj`
      after header edits.

- [x] **Command-line build links** (`porting/stlink.sh` → /tmp/stobj/strongtalk, Mach-O arm64) and
      **VM initializes** up to loading the image ("Could not open file (strongtalk.bst)").
      `runtime/init.cpp`: stubRoutines_init / interpreter_init (x86 code generation) only run with
      STRONGTALK_X86_BACKEND.
- [x] `runtime/repro_frame.cpp` (7-line scratch repro from an earlier session) — its `main` is now
      behind `#ifdef STRONGTALK_REPRO_FRAME`; candidate for deletion.

### Remaining
- [x] Xcode target settings (via Xcode build-setting tool, not by editing pbxproj):
      HEADER_SEARCH_PATHS=`$(SRCROOT)/Strongtalk/vm/**`; GCC_PREPROCESSOR_DEFINITIONS=`$(inherited) DELTA_COMPILER`;
      EXCLUDED_SOURCE_FILE_NAMES=`main.cpp makeDeps.cpp *.asm *.bat *.sh *.tmp *.db *.bmp *.sln *.vcproj`
      (template main.cpp is excluded, not deleted; the VM's main() is in runtime/shell.cpp).
      `platform.hpp` defines ASSERT when DEBUG is defined (Xcode Debug config).
- [x] **Xcode build succeeds** (182 files, 0 errors). EXCLUDED_SOURCE_FILE_NAMES also has `Makefile *.inc`.
      `types.hpp` only defines DELTA_COMPILER if not already defined.
- [x] 64-bit audit from Xcode warnings (batch9/10):
      - set_words/copy_words wrote oop-sized words into int arrays (overrun) → int loops.
      - byteArray/doubleByteArray compare read the length oop as 32 bits → fixed.
      - allocateSize prims truncated 62-bit sizes to int (heap corruption) → reject > max_array_bytes.
      - is_within_bounds(smi); smi-width locals for primitive index arguments (17 sites).
      - IntegerOps digit→smi, compiler constant folding (intptr_t), debugger helpers take intptr_t,
        evaluator address parsing, LOG_EVENT casts.
      - Remaining 432 -Wshorten-64-to-32 reviewed: object lengths, intra-object/code offsets, strlen,
        frame/space sizes — bounded while heap spaces < 2GB. x86 backend ones are x86-only.
      - 2188 -Wwritable-strings (string literal → char*) left as is (harmless).

## Notes / decisions
- The bytecode format now uses 8-byte aligned operand slots. The image-side (Smalltalk)
  bytecode compiler must emit oopSize-slot operands; existing 32-bit `.bst` images will
  not load without conversion.
- x86 code emitters (`asm/`, code generators, PICs, stubs, interpreter generator) still emit
  x86-32 code; embedded addresses go through `x86_imm32()` which aborts if the value does
  not fit, instead of silently truncating.
- `markOop` sentinel must stay the MSB (GC pointer reversal distinguishes heap addresses from marks).

## History
- 2026-10-18: errors 7172 (153/179 files) → 12 (6 files) before batch8. Xcode hung several
  times on permission prompts; work resumed from the working tree each time.

## Option 1 complete (2026-10-18)
Xcode Debug build (assertions on) and command-line build both run on arm64 and initialize the VM
up to reading the image: `Could not open file (strongtalk.bst) for reading!`

## Next steps (beyond option 1)
1. Image: a 64-bit `.bst` (or a 32→64-bit image converter in `runtime/bootstrap.cpp`), and the
   Smalltalk-side bytecode compiler emitting oopSize (8-byte) operand slots.
2. Portable interpreter (option 2) replacing `interpreter_asm.asm` / the stubs in
   `runtime/x86_backend_stubs.cpp`; then turn off the JIT (compiler) paths that emit x86.
3. Native AArch64 backend (option 3): assembler, code generator, stubs, ICs/PICs, frames.
4. Cleanup candidates (not part of the VM): `Strongtalk/vm/runtime/repro_frame.cpp`,
   `Strongtalk/*.o.tmp`, `t1-*.o.tmp`, `Strongtalk/of`, `vm/topIncludes/Copy`, `vm/topIncludes/Copy of types.hpp`,
   template `Strongtalk/main.cpp` (excluded from the build).

# Option 2, second half: run the image (started 2026-10-18)

## Findings
- `strongtalk.bst` (1.9 MB, 2006) is a *serialized object stream* (type char + base-128 varints),
  not a memory dump. Objects are read field by field, so most of it is word-size independent.
- Word-size dependencies in the stream:
  1. Each object's `size` is in 32-bit words (read before the body; used to allocate).
     64-bit size is never larger (bytes pack 8/word, doubles 1 word) except methods after relaxation.
  2. Method bytecodes: oop/long operands in 4-byte aligned 4-byte slots ("external layout").
     The image's Smalltalk compiler (DeltaHCode.dlt `align` = 4 bytes) also produces this layout
     for `primitiveConstructMethod` at runtime.
  3. The Double class stores non_indexable_size 4 (32-bit: mark, klass, 2-word double) → 3 on 64-bit.
  4. method->tempInfo() holds bci ranges (begin end) of inlined-scope temps → must be remapped.
- Jump semantics (interpreter_asm.asm): forward offsets relative to the next instruction
  (if*, and/or, jump_else, jump_loop cond, prim-call failure offset); whileTrue/False go back
  from the current instruction. jump_loop also has an "end" distance (cond → while instr).
  ifTrue/ifFalse "structure" byte high nibble = size of the following jump_else instr
  (can be 16 on 64-bit → encode as 0, decoder maps 0 → 16).
- Runtime-only long operands (reset/unresolved when transcoding): compiled_send nmethod entry,
  prim_call fn pointers, dll_call entry point.

## Plan
- A1 `oops/bytecodeTranscoder.*`: decode instruction stream of either layout, re-layout for the
  target layout (relaxing byte jumps to word forms when offsets exceed 255), fix jump offsets,
  structure nibbles, produce old→new bci map.
- A2 bootstrap: allocate with an upper bound, then shrink (retract old-space top) or leave a
  filler objArray; transcode methods (+ remap tempInfo); fix Double class size.
- A3 `methodKlass::constructMethod`: transcode external → native.
- A4 `methodOopDesc::fileout_body`: native → external (so the image sees its own layout).
- B  Portable interpreter (C++), keeping the original frame layout on a Delta stack.

## Progress (2026-10-18)
- [x] A1 `oops/bytecodeTranscoder.{hpp,cpp}` (format-table decoder, relayout + relaxation,
      jump/structure fixup, bci map; ExternalCode / NativeCode sources and sinks; containers use a
      C-heap allocator because the VM forbids the global operator new).
- [x] A2 **The 32-bit strongtalk.bst loads on arm64**: `[Reading in strongtalk.bst, n (12994 filler objects), 0.26 secs]`.
      - bootstrap.cpp: upper-bound allocation (+3 for byte/doubleByte/symbol/double, 2x+3 for methods),
        shrink via `oldGeneration::shrink_top` (keeps the oldSpace offset array consistent) or filler objArray.
      - Double class non_indexable_size fixed after load; method code transcoded; tempInfo bcis remapped.
      - Fixed on the way: Klass vtbl copied through `int` (truncated vtbl pointers!), symbol hash now
        explicitly 32-bit (hashpjw), IfNode decodes else-jump size 0 as 16.
      - `+TraceBootstrap` prints every object (stream position, type, size, index).
- Next stop: the first Smalltalk send (`Delta::call` → `call_delta` stub) → phase B (interpreter).
- `porting/stlink.sh` rewritten: rebuilds an object if its .cpp or any header is newer.
- Run: `cd /Users/todd/Projects/strongtalk-source-r36 && /tmp/stobj/strongtalk` (image in cwd).

## B: portable interpreter design (interpreter/portableInterpreter.cpp)
- Delta frames keep the original layout, on a per-thread "Delta stack" (separate from the C stack):
  fp[1] return pc, fp[0] link, fp[-1] receiver, fp[-2] saved hp, fp[-3] temp0/context, then temps,
  expression stack; args: arg(i) = fp[1+i] (arg_1 = last pushed). Whole expression stack in memory
  (the x86 version cached tos in eax; layouts are identical at every call-out).
- Frame classification stays pc based: return pcs are addresses inside a small static array
  (interpreter_begin_addr/end_addr); the offset encodes the caller's post-call action
  (send with/without receiver, pop or not, primitive, primitive-with-failure). `return_from_Delta`
  marks entry frames; entry frame fp[-1]/fp[-2] = saved last_Delta_fp/sp (as in call_delta asm).
- Call-outs to C: save hp in fp[-2], write a return pc at sp[-1], set last_Delta_fp/sp; afterwards
  clear last_Delta_fp, reload hp (C may set it, GC may move methods), check have_nlr_through_C.
- NLR: follow the context chain from temp0 to the home fp (0 = dead frame error), unwind frames
  (zap method contexts whose parent == fp); crossing an entry frame sets the global NLR vars and
  returns from call_delta. provoke_nlr_at / continue_nlr_in_delta longjmp into the innermost
  interpreter activation.
- Primitives: arity = primitive_desc::number_of_parameters (incl. receiver); C params = stack from
  the top (args reversed, as PRIM_DECL). primitiveValueN activates the block frame directly on the
  prim args (return point "primitive").
- Floats: 2-word float slots on all hosts (floatSlotWords) so the float numbering of the image's
  compiler and the frame word layout match the 32-bit VM.
- push/store_instVar operand = smi word index (x86 used its raw bits as a byte offset).
- No compiled code: compiled_send / nmethod entries never occur; invocation counter overflow
  does not trigger recompilation.

## B status (2026-10-19)
- [x] `interpreter/portableInterpreter.cpp` written (all byte codes, sends/ICs/PICs/megamorphic,
      primitives incl. primitiveValueN, blocks, contexts, NLR incl. through C, floats, call_delta,
      provoke_nlr_at / continue_nlr_in_delta via setjmp/longjmp). Stubs trimmed.
- [x] Fixed: InterpretedIC layout constants (size 8 / second word at 4 bytes → 2*oopSize / oopSize).
- [x] **Runs**: image boots, the interpreter executes Smalltalk startup code (creates 2 processes)
      until the first DLL call: `kernel32 GetCommandLineA`.
- [ ] DLL calls: the image's platform layer calls the Win32 API directly (kernel/user/gdi DLLs).
      Needs a Win32 shim (name → macOS implementation) + generic FFI, or a macOS platform layer in
      the image sources (StrongtalkSource/*.dlt) and a rebuilt image.
- `porting/strun.sh` builds and runs the VM under lldb (output in /tmp/strun.txt).

## Command-line mode work (2026-10-19)
- [x] DLL calls in the portable interpreter (smi -> int, proxy -> pointer, result boxed in proxy;
      lookup via DLLs::lookup_and_patch_InterpretedDLL_Cache; async calls transfer control).
- [x] `runtime/win32Shim.{hpp,cpp}`: KERNEL32 functions used by the image implemented on POSIX;
      USER32/GDI32/COMDLG32 load but report missing functions. Wired into os_posix dll_load/lookup;
      main() passes argv for GetCommandLineA.
- [x] Fixed Delta::call(recv, sel, a1, a2[, a3]) passing &arg1 as an array (x86 stack layout assumption).
- [ ] Current: doesNotUnderstand loop after startup (inline_cache_miss recursion) — investigating with
      `+TracePrims +TraceLookup`.
- [x] Fixed push_arg_n addressing (0-based index: fp[2+b], not fp[1+b]).
- [x] Fixed bootstrap: doubles read while the Double class is still being read got the 32-bit size (4)
      → 1-word holes; now sized with doubleOopDesc::object_size(). `+VerifyBeforeGC` verifies the heap
      after bootstrap (all 60046 objects parse). Bootstrap now fatals on any other size mismatch.
- [x] Fixed ~ResourceMark: `for (ResourceAreaChunk* c ...)` shadowed outer `c` (MSVC for-scope) → used freed chunk.
- [ ] 39 more shadowing sites found with `-Wshadow` (list: /tmp/shadow.txt); review those where the
      name is used after the loop in the same block.
- [ ] Add KERNEL32 GetCurrentThreadId to the shim.
- [x] **First Smalltalk program runs on arm64**: `strongtalk sieve` (command line mode =
      SystemInitializer>>run with arguments → processArguments: benchmark/test names) prints results, exits 0.
- [x] GUI DLL functions resolve to a stub returning 1 (fake handle/TRUE) for command line mode;
      `STRONGTALK_TRACE_GUI=1` lists them. Added GetCurrentThreadId; os::suspend/resume_thread accept NULL.
- Shadowing review: only 2 of 39 -Wshadow sites are for-init shadows; neither is used after its loop.
- [x] Fixed byte-operand temps: push_temp_n / store_temp_n[_pop] address temp(255 - b) (cf. temp_addr(Register)).
- [x] **Benchmarks pass on arm64**: sieve, deltablue, richards, slopstones (7 tests), smopstones (7 tests);
      stanford runs through Livermore, then "identifier expected" (Smalltalk compiler parse error) — next.
- [x] stanford failure ("identifier expected" after Livermore = ParserBenchmark): the image reads class source
      info from source\_class etc. with Windows separators. win32Shim now converts '\' to '/' in every path
      argument (PosixPath). Set STRONGTALK_TRACE_FILES=1 to trace CreateFileA/GetFileAttributesA.
- [x] InterpretedIC::inline_cache_miss: the DNU retry is now a bounded loop (100) instead of recursion; on overflow
      it prints the receiver class, the selector and the Delta stack, then fatal (no more native stack overflow).
- [x] **All command-line benchmarks pass (exit 0)**: benchmarks, stanford, sieve, deltablue, richards, slopstones, smopstones.
- [x] Drive letters (win32Shim "Paths" section): image paths "X:\a\b" map to $STRONGTALK_DRIVE_X/a/b (default: root, so
      C:\Users\me = /Users/me); GetCurrentDirectoryA returns "C:\..." (longest matching drive root) so the image
      sees an absolute path (Win32FilePattern>>isAbsolute tests for ":"). Verified with a harness in /tmp/shimtest.
- [x] FindFirstFileA / FindNextFileA / FindClose (opendir + fnmatch; trailing ".*" also matches names without a dot,
      WIN32_FIND_DATA offsets 0/28/32/44). All KERNEL32 functions the image source uses are now in the shim.
- [x] **A3/A4 file out in the 32-bit layout** (methodOop.cpp "File out" section):
      - fileout_body: canonical (unlinked) copy of the native code -> BytecodeTranscoder::layout_as_compiled(4)
        -> ExternalCodeSink -> (byte | oop) pairs. layout_as_compiled reproduces DeltaHCodeGenerator's jump
        choice: while jumps short if they fit; all other jumps of a method short, or all long if one does not fit.
      - primitiveMethodSizeAndFlags / primitiveOopSize (methods) / primitiveMethodDebugInfo report the external
        code size, object size and tempInfo bcis (used only by Method/Object file out in the image).
      - +VerifyFileOut: after bootstrap compares every method's file out with its code in strongtalk.bst, and at
        quit checks that every method reads back (as bootstrap does) into its own unlinked code.
        Result: 16643/16643 methods byte-identical to the image; 16703/16703 round-trip after `benchmarks`.
- [ ] Next: native code generation for arm64 (x86 compiler backend + assembly interpreter); GUI (Cocoa).

## Phase C: arm64 native code generation (started 2026-10-19)

Scope (x86-dependent code, ~15k lines): asm/assembler + x86_mapping, compiler/oldCodeGenerator.cpp (the product
backend; UseNewBackend=false), code/stubRoutines, compiledPIC, nativeInstruction, nmethod relocations, compiledIC,
pregMapping/locations, interpreter<->compiled transitions, floats (x87), lookup.asm, double_prims_asm.asm.

Design decisions:
- Compiled frames live on the per-thread Delta stack in the same (x86) layout as interpreted frames, so frame
  walking, GC root scanning, NLR and deoptimization code is shared. arm64 registers stand in for esp/ebp; C calls
  from compiled code switch to the native sp (as the portable interpreter's enter_C/leave_C).
- arm64 Assembler keeps the x86-shaped API used by the code generator (movl/pushl/cmpl/jcc/..., Address operands,
  x87 ops); each op lowers to an arm64 sequence with scratch registers x16/x17. "l" ops are 64-bit (word) ops.
  Smi overflow checks map to the V flag; carry/borrow conditions are inverted on arm64 (sub sets C = !borrow).
- x87 float stack emulated at assembly time on d-registers (stack depth is static in generated code).
- Patchable sites (calls, IC/PIC data, oops in code) use fixed sequences with literal pools (ldr x16, =target;
  blr x16), so nativeInstruction can patch 8-byte aligned literals atomically.
- JIT memory: mmap MAP_JIT, pthread_jit_write_protect_np around writes, sys_icache_invalidate after.

Phases:
- [x] C1 arm64 assembler (x86-shaped API) + code buffer + JIT memory; encoding tests vs clang/llvm-objdump.
      - [x] asm/a64.hpp: arm64 encoder (integer, load/store all modes, branches, literals, FP); verified by
            porting/a64test.sh (116 forms, 0 mismatches vs `clang -arch arm64` + otool).
      - [x] asm/assembler_arm64.{hpp,cpp}: x86-shaped Assembler/MacroAssembler on a64.hpp, selected by
            STRONGTALK_ARM64 in assembler.hpp (x86 implementation unchanged, under #else). Address::_disp is
            word sized. Register map eax=x0 ecx=x1 edx=x2 ebx=x19 esp=x28 ebp=x29 esi=x20 edi=x21; scratch
            x16/x17 (+x9-x11). All "l" ops 64-bit; unsigned immediates zero-extend (Floats::magic).
            - Delta calls push the return address on the Delta stack (adr/str); ret pops it. Patchable
              call(entry) = adr, str, ldr literal, br, 8-byte literal (sizeOfCall = 24, return address = end,
              reloc -> literal); jmp(entry) = ldr literal, br, literal; oops (non-smi) are literals too.
            - C calls (call_C*): last Delta frame set, return address stored at esp[-1], args loaded from the
              Delta stack (esp[0..7] -> x0..x7) or moved from registers, blr; C preserves ebx/esi/edi/esp/ebp.
            - Flags: logical ops set flags eagerly (ands / tst); carry conditions follow the kind of the last
              flag op (sub: x86 below = LO; add: carrySet = HS); imul overflow via smulh; adc/sbb convert C.
              above/belowEqual after add, rcl/shld/shrd and carry after shifts are not supported (fatal).
            - ic_info = movz xzr / movk xzr (32-bit info: offset << 8 | flags), executable no-ops.
            - x87 emulated on d8..d15 (static stack depth); fprem via fmod; fnstsw_ax builds C0/C2/C3 from the
              arm64 fcmp flags; fistp rounds to nearest even.
            - Labels: unbound uses chained through the instruction offset fields (b, b.cond, adr, ic_info).
      - [x] JIT memory (MAP_JIT, pthread_jit_write_protect_np, sys_icache_invalidate) in the test harness.
      - [x] asm/assemblerTest_arm64.cpp (+TestArm64Assembler): executes generated code and compares with x86
            semantics on 64-bit words: 89088 checks, 0 failures (immediates, arithmetic, shifts, all conditions
            after cmp/sub/add/test/or/xor, cmov, adc/sbb, imul overflow, idiv, mul, memory operands of all
            forms, push/pop/pushad, enter/leave, labels, call/ret, call/jmp/jcc to entries, ic_info encoding,
            C calls with register/stack args, last Delta frame, store check, x87 ops/compares/conversions).
      - Notes for C2/C3: movl to 32-bit int fields must use 32-bit stores (all movl are 64-bit); call(entry, rtype)
        is the Delta convention - calls of C functions (primitives, DLLs) must use call_C / call_native; idivl
        expects edx = sign extension of eax (64-bit cdq); x86_imm32() callers must pass full words.
- [ ] C2 runtime glue (sub-steps, one commit each):
  - [x] C2a code memory. macOS arm64 has no RWX memory: MAP_JIT memory is, per thread, either writable or
        executable (pthread_jit_write_protect_np), probed in /tmp/jitt. os::allocate_code_memory (MAP_JIT),
        JITWriteScope (nestable per thread), os::flush_icache (sys_icache_invalidate; flushICacheWord/Range).
        Safety net: the fault handler switches the thread's mode on a write fault / instruction fetch fault in
        code memory (state survives sigreturn; ~4us per round trip vs 38ns explicit); a fault repeating at the
        same pc/address is reported as a real fault. +PrintJITModeFaults reports the count at exit (0 in the
        benchmarks). Crash reports now show the pc as symbol + offset.
        Code memory: zone heap (nmethods, PICs), jump table, stub routines (64KB on arm64). Write scopes: nmethod
        creation, zone heap operations (free lists live in the heap), jump table entries, stub generation.
        Jump table entries on arm64: [ldr x16,#8][br x16][destination 8][state] = 24 bytes (unused/link: brk);
        this also fixes a 64-bit bug: the x86 layout wrote an 8-byte char* into a 4-byte field (overwrote the
        state and the next entry; the last entry overran its allocation).
        Note: under lldb, writes outside scopes stop the debugger (EXC_BAD_ACCESS) before the VM handler runs.
  - [x] C2b code patterns read/patched by the runtime (arm64 versions under STRONGTALK_ARM64):
        - NativeCall (addressed by return address): literal at -8 holds the destination; set_destination is an
          aligned 8-byte store; nativeCall_at skips an alignment nop; verify checks adr/str/ldr/br.
        - NativeTest/IC_Info: 32-bit data in movz/movk xzr immediates (NLR offset << 8 | flags).
        - native_code_write_word/insn: patching within JITWriteScopes (+ icache flush for instructions).
        - CompiledDLL_Cache: fixed layout before an 8-aligned call (ldr x2 entry; b; entry; dll_name; function_name);
          emitted by MacroAssembler::call_DLL_cache (DLLNode::gen must use it in C3).
        - Relocations: only internal_word is position dependent (64-bit words); offsets stored in 4-byte units
          (32KB between relocations; overflow is fatal); CodeBuffer::copyTo pads with nop; relocInfo::print.
        - PICs/MICs: header ldr x16, stub / adr x17, data / br x16 / nop / stub address; PIC data: count, then
          (klass, target) pairs (target = nmethod verified entry point or methodOop, by tag; smis have klass
          smiKlassObj); MIC data: selector. PIC_Iterator rewritten (no at_smi_nmethod state on arm64); entry updates
          are atomic word stores; PIC construction and oops_do in write scopes. The PIC stub routine (C2c) scans
          the pairs. Write scopes also for zone::oops_do/switch_pointers and nmethod::oops_do (GC updates oops).
        - [x] generate_continue_NLR (arm64): leave, pop the return address, NLR offset from its IC info
              (MacroAssembler::load_ic_info_nlr_offset), jump. generate_PIC_stub (arm64, one stub for all sizes):
              x17 = PIC data; receiver klass (smiKlassObj for smis); scan (klass, target); nmethod target: jump;
              methodOop target: jump via method_entry_point with ecx = methodOop, ebx = 0 (x86 protocol; the
              portable interpreter's entry is C2d); miss: ic_normal_lookup_entry.
        - [x] stubRoutines_init runs on arm64 (all stubs generate: 3120 bytes); asm_word() replaces x86_imm32() in
              stubRoutines.cpp (full words on arm64). Only the two stubs above are verified so far; the others are
              the x86 generators run through the arm64 assembler and are reviewed in C2c.
        Tests: +TestArm64Assembler (89131 checks; patched calls executed, IC info, DLL cache incl. edx, reloc
        offsets); +TestArm64Runtime (after the image is loaded): PIC layout, iteration, updates, oops_do, find,
        PIC dispatch executed through the PIC stub (smi/nil/true/false, retargeting), continue_NLR executed
        (reaches the NLR target with the Delta stack balanced).
  - [x] C2c stub routines reviewed and ported (code/stubRoutines.cpp):
        - C functions were called with a plain x86 call after pushing their arguments: now call_C_function()
          (arm64: call_native = arguments from esp[0..n-1] into x0.., blr; x86 unchanged). Used by: IC lookup,
          zombie, zombie block, megamorphic IC lookup, compile block, DLL lookup, recompile, uncommon trap, context
          chain check, block deopt, inspector.
        - SavedRegisters::save_registers read the caller's registers (x86 asm): arm64 stores them inline
          (save_registers(); SavedRegisters::register_address / stored_frame_pointer_address).
        - Megamorphic IC: x17 = MIC data (not popped); hash mask (size - 1) * lookupCache::cache_element_size()
          (32 bytes on 64-bit, was << 4); Address(Register, disp) now takes word displacements (the lookup
          cache addresses were truncated to int).
        - DLL calls: arm64 stub calls StubRoutines::call_DLL_arm64 (argument conversion, async protocol, C call);
          the x86 stdcall frame / argument count check do not apply.
        - addl(esp, 4) -> oopSize (zombie block, context chain check).
        - Patching outside the stubs: patch_uncommon_call and relocIterator::callDestination/call_end use the
          literal; nmethod::makeZombie replaces the first instruction of enter with b <special handler call>.
        - restart_primitiveValue (jumped to by the block stubs) and method_entry_point are interpreter entries:
          C2d. Allocation stubs are hlt (unused, as on x86).
        Tests (+TestArm64Runtime, executed): call_sync_DLL from compiled-style code (smi args, order, balanced
        esp), inline register saving read back via SavedRegisters::fetch, megamorphic IC for nil printString
        through lookup-cache miss (C lookup) and hit (hash), plus the PIC/NLR tests. Stubs that need real
        nmethods/ICs (IC lookup, zombie, compile block, recompile, uncommon trap, deopt) are exercised in C4.
  - [x] C2d interpreter <-> compiled code transitions (interpreter/portableInterpreter_arm64.S + portableInterpreter.cpp):
        - Interpreter return points are code: 8 slots of 16 bytes [IC info: NLR target +12][b normal][b nlr]
          (return_pc(p) = base + 16p; the interpreter range is the slot table). return_from_Delta (entry frame
          marker) is the same kind of slot, so compiled code can return into entry frames.
        - delta_call_compiled(entry, recv, sp, fp, return_pc, out): saves the C callee-saved registers in a save
          area addressed by x27 (compiled code never uses x22-x27; nesting needs no TLS), runs compiled code on
          the Delta stack (esp/ebp = interpreter sp/fp, eax = receiver); the return point slots store eax/esp/ebp
          (and edi/esi for NLRs) in out (CompiledReturn) and restore C.
        - Interpreter -> compiled: send() accepts jump table entries (monomorphic compiled sends, interpreter PICs,
          megamorphic lookups) and calls call_compiled (callee pops the arguments; NLRs continue in nlr_unwind).
          call_delta from C with a jump table entry: call_compiled_from_C (entry frame as call_delta_entry).
        - Compiled -> interpreted: method_entry_point = compiled_to_interpreted (eax = receiver, ecx = methodOop):
          interpret_from_compiled runs the method with the compiled return address as its frame's return pc;
          continue_caller / nlr_unwind leave the activation when the caller is compiled code (normal return:
          continuation = return address, callee popped its arguments; NLR: continuation = NLR target of the IC
          info at the return address, frame and return address removed as continue_NLR does).
        - NLR home ids: compiled >= 0 (scope id), interpreted homes ~nof_args (same as the x86 interpreter).
        Tests (+TestArm64Runtime, hand-assembled compiled methods): C -> compiled, compiled -> interpreted
        (Object>>yourself, a 1-argument method; esp balanced), NLR compiled -> C, interpreted -> compiled through
        a patched inline cache (normal return through the return point slot, and NLR to the interpreted home),
        1000 nested round trips. Not covered yet: compiled blocks (primitiveValue / restart_primitiveValue),
        deoptimization (unpack frames), NLRs that start in the interpreter and pass compiled frames (C2e/C4).
  - [ ] C2e (part 1 done):
        - [x] Recompilation trigger: the portable interpreter counts invocations (methods called by sends, from C
              and from compiled code) and calls Recompilation::methodOop_invocation_counter_overflow at
              Interpreter::get_invocation_counter_limit() (a variable on arm64; the x86 interpreter patched its
              entry code), then continues interpreting (as x86). Gated by develop flag UseArm64Compiler (false
              until the code generator is ported, C3).
        - [x] NLRs through C into compiled code: call_C with an NLR test point checks have_nlr_through_C after
              the call (the portable primitiveValue returns from C with it set) and continues the NLR with
              eax/edi/esi from nlr_result/nlr_home/nlr_home_id (clearing the flag).
        - [x] Compiled blocks: primitive_value (interpreter) and primitiveValueN/call_delta_entry (C) call the
              block's jump table entry when the closure is compiled (the block pops its arguments and the
              closure); restart_primitiveValue (.S, for the zombie/deoptimize block stubs) jumps to compiled
              blocks or interprets block methods (interpret_block_from_compiled).
              Tested: primitiveValue1 of a compiled clean block from C; restart_primitiveValue with a compiled
              block (esp balanced).
        - [x] Deoptimization: unpack_unoptimized_frames is a return point slot (.S: IC info, normal / NLR
              entries) -> resume_deoptimized_frames (portableInterpreter.cpp): unpacks the frame array into
              interpreter frames in place (as process.cpp unpack_frame_array, with portable return points and
              word slots - the x86 version addressed stack slots as int*), then resumes the newest frame: after
              the send (the callee popped its arguments, they are in the array), redoing the send (uncommon
              traps; take_redo_the_send), or continuing the NLR (home adjusted to the unpacked frame). The
              interpreter's base frame (_base_fp) leaves the activation to its return pc whatever it is
              (compiled code / interpreter return point / return_from_Delta: all are code now).
              Not needed on arm64: deoptimized_return_from_*, redo_bytecode_after_deoptimization (x86 stubs).
        - [x] Deoptimization during C calls: C returns through lr on arm64, so call_C follows a patched return
              address (esp[-1] != its return address) explicitly, to the NLR entry if an NLR is pending.
              Tested: a hand-built deoptimized frame (frame array for an activation of a 1-argument method at
              its send) is unpacked when its callee returns, resumes after the send and returns to C.
        - [ ] Interpreted blocks through restart_primitiveValue, interpreter primitive_value with compiled
              blocks, NLRs starting in the interpreter passing compiled frames: need a running process (C4).
- [ ] C2 remaining: stubs (call_delta entry, interpreter<->compiled calls, IC miss, PICs, NLR, uncommon trap,
      recompile), nativeInstruction patching, relocations.
- [x] C3 port oldCodeGenerator (2026-10-20). The old backend (the live one) generates arm64 through the
      x86-interface MacroAssembler; the full `benchmarks` suite runs compiled (318 compilations incl.
      recompilations and uncommon-trap recompilations, exit 0).
      Run: `strongtalk +UseRecompilation +UseArm64Compiler [+PrintCompilation] benchmarks` (the
      user's .strongtalkrc has -UseRecompilation, so it must be overridden on the command line).
      - C calls: call_C -> MacroAssembler::call_C_sequence (args esp[0..7] -> x0..x7, `ldr x16,lit; blr;
        b; literal`, return address at esp[-1]); NativeCall recognizes the C-call pattern (R-20).
      - Invocation counters: nmethod::_counter_cell (C-heap int cell, address embedded in the code,
        incremented with 32-bit movl32); smi index -> word offset scaled by 2 (smi_to_word_scale);
        64-bit immediates (intptr_t/unsigned overloads); float section `% oopSize`; oopify_double.
      - **All primitive calls set the last Delta frame on arm64**: the primitive flags describe the
        x86 implementations (primitiveValueN are stubs jumping into the block, the double ops are
        assembly allocating inline); here they are C functions that call Delta code / scavenge. Without
        it, a C->Delta call started a new activation at the stack base over live frames, and
        scavenges walked stale frames.
      - nmethodScopes::getNext rounded scope offsets to 4 bytes; the section is padded to a word (8)
        -> parsed padding as a scope ("expecting a method" while inlining an existing nmethod).
      - Zombie nmethods called by the interpreter: zombie_nmethod returns compiled_redo_send (.S), which
        returns from delta_call_compiled with kind redo_send; call_compiled re-executes the send.
      Speedups vs interpreted (debug build, first column of the benchmark output; the other columns
      are fixed reference numbers): Sieve 281->34, Richards 459->210, Smopstones ~20-40x.
- [x] C4 integration (2026-10-21). The image's own VM test suites pass with compiled code, debug and
      release builds: `vmtest` (benchmarks + VMSuite class tests + dead-code tests, 4 iterations, in a
      recompiling and an interpreted configuration, heap verified after each) and `vmcompiled`
      (CompiledCodeOnly: every method compiled at lookup, no deferred uncommon branches).
      Run: `strongtalk +UseArm64Compiler vmtest` / `vmcompiled` (they set UseRecompilation themselves).
      Fixes:
      - BitVector/SimpleBitVector held bits in int but indexed with LogBitsPerWord (6): words now.
      - ReservedHeapSize default 50 MB of 32-bit words (100 MB): vmtest ran out of old space even
        interpreted (old space only grows; no automatic full GC).
      - Nested C->Delta activations start 16 words below last_Delta_sp (stub_push_margin): stub
        routines push below the caller's return pc before calling C (DLL lookup stub: ebx/ecx); on
        x86 the nested activation started at the C sp, below them.
      - Jump table entry blocks (nmethods with blocks) carved from 256K code memory chunks; one mmap
        each exhausted os_posix's 32 code areas.
      - loopOpt findRegCandidates ran off the BB list when the loop end was dead code
        ([^ 1] whileTrue in VMSuite>>dead5) - an original bug hidden without ASSERT.
      - proxy smiAt/smiAtPut: 32-bit (Win32 LONG) fields - the image's external structures use
        4-byte words (Platform wordSize = 4); they read a 64-bit long (overrunning 4-byte mallocs)
        and shifted a 32-bit value by 62 (UB: miscompiled at -O2).
      - Missing *.inline.hpp includes in ~20 files (an -O2 build did not link: callers relied on
        out-of-line copies emitted at -O0).
      Release build: `RELEASE=1 porting/stlink.sh` -> /tmp/stobj_rel (-O2, no ASSERT/DEBUG; develop
      flags stay settable since PRODUCT is not defined). Release timings (interpreted -> compiled, one
      pass, compile time included): BubbleSort 42->5, IntMM 21->3, Puzzle 209->18, Takl 80->4,
      Dictionary 478->36, Richards 46->28, DeltaBlue 153->48; Smopstones 5-12x; warm block evaluation
      ~50-100x (vmtest iterations 3-4). The debug build is ~10x slower than release.
      Open items fixed (2026-10-21, "C5"):
      - External words: pointers stored in the image's 4-byte struct fields (proxyAt:/proxyAt:put:)
        go through a 32-bit handle table (ExternalWords, proxy_prims.cpp; 0xA0000000.. handles;
        32-bit values stored as they are). +TestArm64Runtime tests it.
      - Zombie nmethod called from C: zombie_nmethod returns compiled_redo_send for return_from_Delta,
        call_compiled_from_C then interprets the nmethod's method. Delta::call_generic re-looked up a
        zombie through its own (stale) cache - fixed (clear before lookup).
      - nmethod header fields (code memory) are written in nmethodHeaderWrite scopes: 0 fault toggles.
      - Results comparison: porting/compare_results.sh files in porting/results.st at quit
        ($STRONGTALK_EVAL_FILE, system_prims.cpp) interpreted and compiled (LIMIT=invocation counter
        limit, default 1000) and diffs the RESULT lines. At the default limit (10000): identical.
        It found (interpreted too):
        - SmallInteger <-> LargeInteger conversion truncated to 32 bits (int_to_Integer, as_smi).
        - LargeInteger division by multi-digit divisors / negative operands: missing code (fatal);
          implemented (Knuth D in qr_decomposition, floor div/mod); unsigned_rem(x < y) returned y.
        - unsigned_mul looped forever on a 0 digit; cmp(0, negative) said equal (original bugs).
      - Image patches: porting/image_patches.st (symlinked as image_patches.st next to the image, or
        $STRONGTALK_PATCH_FILE) is filed in when the image first reads its command line
        (GetCommandLineA shim -> file_in_image_patches): SmallInteger>>digitsForAbs: (printString
        stopped at 10 digits) and DeltaRewriter>>normalSendNode: (to:by:do: with a variable step
        left a send with an inlined block - an original image compiler bug).
      - GC safety: primitives that keep an argument across a call that can GC must re-read it from
        the caller's Delta stack (PrimitiveArgument, block_prims.cpp: unwindprotect, blockRepeat) -
        on x86 the C parameters were these stack slots. SavedRegisters are GC roots.
      - Old space: a full GC runs after a scavenge when the remaining reservation is < 1/8 (the
        original VM left full GCs to the image); exhausting ReservedHeapSize is a clear fatal in
        release builds too (VirtualSpace::expand only asserted it).
      - Debugging aids: $STRONGTALK_NO_COMPILE ("Class>>sel" substrings / #selector, blank separated)
        excludes methods from compilation (Compiler::compile); +ZapNewSpaceAfterScavenge.
- [x] Aggressive compilation (2026-10-22, "C6"). The results comparison passes at
      InvocationCounterLimit 10, 100 and 1000 (debug and release; release also at 5). Found with a
      deterministic setup: ASLR off (posix_spawn with _POSIX_SPAWN_DISABLE_ASLR), -t,
      $STRONGTALK_FAKE_TIME (os_posix.cpp: every time query advances a fake clock by 1 ms), and a fresh
      copy of the image directory per run (the image patches rewrite source/ files). Then a
      $STRONGTALK_ONLY_COMPILE whitelist ddmin isolated DeltaCodeScope>>isInlineable, and a
      compiled-vs-interpreted differential check plus per-loop masking isolated one integer loop.
      Fixes (all original bugs unless noted):
      - BasicNode::removeUpToMerge cut the link to a branch successor before recursing into it, so a
        merge shared with live code (here the NLR merge of the sends in a loop, when OptimizeIntegerLoops
        removed the loop's non-smi `>=` send) looked private and was deleted with the outer NLR test.
        An NLR through the loop body then continued at the loop increment: `^ false` in a block
        passed to realSubScopesDo: answered true (the "DeltaScope class >> map:toHeap:" DNUs).
      - Deoptimized/materialized block closures got canonical_context() = NULL (SmallInteger 0) as
        lexical scope when the home allocates no context (nameDesc.cpp BlockValueNameDesc /
        MemoizedBlockNameDesc, blockClosureOopDesc::deoptimize); now nil, or self for copying blocks
        (as push_closure does). Recompilation asserted on the 0 receiver.
      - LoopHeaderNode::generateArrayLoopTests compared the unloaded bound register with 1 and
        overwrote the array register with its size; LoopHeaderNode::print_string looped over the
        wrong array (+PrintCode crashed).
      - Saved registers (C5) were GC roots forever: the stubs never cleared them, so stale values
        (interior pointers that look like memOops, e.g. bytecode pointers) were followed by mark-sweep
        (vmtest crashed in the deterministic setup). SavedRegistersScope in uncommon_trap and
        nmethod_invocation_counter_overflow clears them (port bug).
      - Mark-sweep phases 2 and 3 write oop roots in nmethods through the reversed pointer chains
        outside the zone's JITWriteScope: the whole collection runs in one now. Toggling W^X in the
        fault handler does not stick (the retried write faults again), so code writes need scopes
        (port bug; layout dependent: release vmtest at HEAD passed, debug crashed).
      Debugging aids: $STRONGTALK_ONLY_COMPILE (whitelist, same syntax as NO_COMPILE),
      $STRONGTALK_NO_INLINE (exact selectors excluded from inlining), $STRONGTALK_ASSERT_BACKTRACE
      (native backtrace on assertion failures), $STRONGTALK_FAKE_TIME.
      Still open:
      - InvocationCounterLimit <= 2 exhausts the jump table ("grow not implemented", jumpTable.cpp).
        At <= 5 the debug build asserts in Inliner (no subscope found for a block inlined in an
        earlier nmethod); the code falls back to the interpreted method's type feedback (release OK).
      - The startup image patches are filed in through the source DB: every launch appends them to
        source/strongtalkchange.log and rewrites the source index files of the patched classes.
      - rem: is unimplemented for LargeIntegers in the image (Number>>rem:); Integer>>bitShift: and
        Number>>raisedTo: likewise (image limitations).
- [x] x87 emulation clobbered C callee-saved registers (2026-10-23). The x87 stack lives in d8-d15,
      which are callee saved in the arm64 C ABI; nothing saved them on entry from C. The release
      +TestArm64Assembler (384 failures in fcompp/fnstsw, fprem, ftst) kept its loop values in d8-d15
      across the call. delta_call_compiled and the assembler test harness now save/restore d8-d15.
      Both builds: 89137 checks, 0 failures; vmtest, vmcompiled, results comparison pass.
