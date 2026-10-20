import re, os, collections
os.chdir('/Users/todd/Projects/strongtalk-source-r36/Strongtalk/Strongtalk/vm')

def rd(p): return open(p, encoding='latin-1').read()
def wr(p, s): open(p, 'w', encoding='latin-1').write(s)
def sub(p, a, b, count=1, regex=False):
    s = rd(p)
    if regex:
        s2 = re.sub(a, b, s, count=count)
        if s2 == s: print("NOT FOUND", p, a[:60]); return
    else:
        if a not in s: print("NOT FOUND", p, a[:60]); return
        s2 = s.replace(a, b) if count == 0 else s.replace(a, b, count)
    wr(p, s2)

# --- hand fixes ---------------------------------------------------------
sub('compiler/bitVector.hpp', '    friend class LongRegisterMask;', '    friend class LongRegisterMask;')
s = rd('compiler/bitVector.hpp')
if 'class LongRegisterMask;\n' not in s:
    m = re.search(r'\nclass \w+', s); s = s[:m.start()] + '\nclass LongRegisterMask;\nint findFirstUnused(LongRegisterMask** strings, int len, int start);\n' + s[m.start():]; wr('compiler/bitVector.hpp', s)
sub('interpreter/methodPrinter.hpp', 'void MethodPrinterClosure::print_sendtype(', 'void print_sendtype(')

# IntegerOps::and/or/xor are C++ operator keywords
for p in ['prims/integerOps.hpp', 'prims/integerOps.cpp']:
    s = rd(p)
    s = re.sub(r'\b(static void |void IntegerOps::)(and|or|xor)\b\s*\(', r'\1\2_(', s)
    wr(p, s)
for p in os.popen("grep -rlE 'IntegerOps::(and|or|xor) *\\(' .").read().split():
    s = rd(p); s = re.sub(r'IntegerOps::(and|or|xor)\s*\(', r'IntegerOps::\1_(', s); wr(p, s)

# EventMarker takes arbitrary pointers (often const char*)
s = rd('utilities/eventlog.hpp')
s = s.replace('void* p1', 'const void* p1').replace('void* p2', 'const void* p2').replace('void* p3', 'const void* p3')
wr('utilities/eventlog.hpp', s)

# uncommon trap call patching: x86 rel32 call displacement
sub('runtime/uncommonBranch.cpp', """  int* next_inst = (int*) f->pc();
  int* dest_addr = next_inst-1;
  int  dest      = *dest_addr + (int) next_inst;

  // return true if the call has been executed before
  if (dest == (int)StubRoutines::used_uncommon_trap_entry())
    return true;

  assert(dest == (int)StubRoutines::unused_uncommon_trap_entry(), "Make sure we are patching the right call");

  // patch with used_uncommon_trap
  *dest_addr = ((int)StubRoutines::used_uncommon_trap_entry()) - ((int) next_inst);""",
"""  // INTEL-SPECIFIC: the call is a rel32 call; its displacement precedes the return address
  char*    next_inst = (char*) f->pc();
  int32_t* dest_addr = ((int32_t*) next_inst) - 1;
  char*    dest      = next_inst + *dest_addr;

  // return true if the call has been executed before
  if (dest == (char*)StubRoutines::used_uncommon_trap_entry())
    return true;

  assert(dest == (char*)StubRoutines::unused_uncommon_trap_entry(), "Make sure we are patching the right call");

  // patch with used_uncommon_trap
  *dest_addr = (int32_t) ((char*)StubRoutines::used_uncommon_trap_entry() - next_inst);""")

# remaining int* frame pointers
sub('runtime/stackChunk.cpp', 'int* StackChunkBuilder::frame_pointer', 'intptr_t* StackChunkBuilder::frame_pointer')
sub('runtime/stackChunk.hpp', 'static int* frame_pointer;', 'static intptr_t* frame_pointer;')
sub('runtime/savedRegisters.cpp', 'static int* stored_frame_pointer = NULL;', 'static intptr_t* stored_frame_pointer = NULL;')
sub('runtime/process.cpp', 'int* DeltaProcess::last_Delta_fp() const {', 'intptr_t* DeltaProcess::last_Delta_fp() const {')
sub('runtime/evaluator.hpp', 'static void single_step(int* fr);', 'static void single_step(intptr_t* fr);')
sub('runtime/evaluator.cpp', r'void evaluator::single_step\(int\* ', 'void evaluator::single_step(intptr_t* ', regex=True)

# for-scope with a frame loop variable
sub('runtime/frame.cpp', """  for (frame s = sender(); !s.is_delta_frame(); s = s.sender()) ;
  return s;""", """  frame s;
  for (s = sender(); !s.is_delta_frame(); s = s.sender()) ;
  return s;""")

sub('runtime/debug.cpp', 'inline bool str_equal(char* s, char* q, int len) {', 'inline bool str_equal(const char* s, const char* q, int len) {')
sub('runtime/bootstrap.cpp', '#include <io.h>\n', '#include <unistd.h>\n')
sub('recompiler/recompile.cpp', '    char* msg = oop(method)->is_smi()', '    const char* msg = oop(method)->is_smi()')
sub('prims/objArray_prims.cpp', '  if (smiOop(from) <= 0)', '  if (smiOop(from)->value() <= 0)')
sub('memory/printlayout.cpp', 'PrintObjectClosure::PrintObjectClosure(outputStream* st = NULL) {', 'PrintObjectClosure::PrintObjectClosure(outputStream* st) {')

# process_asm.cpp: per its own comment, unused (x86 fast process switch); keep for reference only
s = rd('runtime/process_asm.cpp')
if '#if 0 // unused' not in s:
    i = s.index('/********** This file is not used')
    wr('runtime/process_asm.cpp', s[:i] + '#if 0 // unused x86 code, kept for reference\n' + s[i:] + '\n#endif\n')

# --- missing includes ------------------------------------------------------
flags = set(re.findall(r'(?:develop|product|notproduct)\((\w+)', rd('runtime/debug.hpp')))
hdr = {'vmSymbols': 'vmSymbols.hpp', 'stdOut': 'ostream.hpp', 'oopFactory': 'oopFactory.hpp',
       'Bytecodes': 'bytecodes.hpp', 'Floats': 'floats.hpp', 'lookupCache': 'lookupCache.hpp',
       'os': 'os.hpp', 'CodeIterator': 'codeIterator.hpp', 'InliningDatabase': 'inliningdb.hpp',
       'findNMethod': 'nmethod.hpp', 'GrowableArray': 'growableArray.hpp', 'ValueObj': 'allocation.hpp',
       'CHeapObj': 'allocation.hpp', 'ResourceObj': 'allocation.hpp', 'vframe': 'vframe.hpp',
       'NativeCall': 'nativeInstruction.hpp', 'NativeMov': 'nativeInstruction.hpp',
       'NativeTest': 'nativeInstruction.hpp', 'objArrayOopDesc': 'objArrayOop.hpp',
       'deltaVFrame': 'vframe.hpp', 'primitives': 'prim.hpp', 'oopSize': 'util.hpp', 'byte_size': 'util.hpp'}
need = collections.defaultdict(set)
pats = [r"use of undeclared identifier '(\w+)'", r"unknown type name '(\w+)'", r"no template named '(\w+)'",
        r"expected class name", r"member access into incomplete type '(\w+)'",
        r"incomplete type '(\w+)' named", r"cannot initialize .* type '(\w+) \*' with an rvalue of type '(\w+) \*'",
        r"cannot initialize return object of type 'oop' \(aka 'oopDesc \*'\) with an rvalue of type '\w+' \(aka '(\w+) \*'\)"]
for line in open('/tmp/stlogs/uniq_errors.txt'):
    f = line.split(':')[0]
    for p in pats:
        m = re.search(p, line)
        if not m: continue
        for g in (m.groups() or ()):
            if g in flags: need[f].add('debug.hpp')
            elif g in hdr: need[f].add(hdr[g])
for f in ['utilities/eventlog.hpp', 'memory/space.hpp']: need[f].add('allocation.hpp')
need['prims/dll.hpp'].add('nativeInstruction.hpp')
need['runtime/vframe.hpp'].add('growableArray.hpp')

added = 0
for f, hs in need.items():
    if not os.path.exists(f): continue
    s = rd(f)
    base = os.path.basename(f)
    hs = [h for h in sorted(hs) if h != base and f'"{h}"' not in s]
    if not hs: continue
    inc = ''.join(f'#include "{h}"\n' for h in hs)
    incs = list(re.finditer(r'^#\s*include .*\n', s, re.M))
    if f.endswith('.cpp') and incs:
        pos = incs[-1].end()
    else:
        m = re.search(r'^#\s*define \w+\s*\n', s, re.M)   # after include guard
        pos = m.end() if m else 0
    s = s[:pos] + inc + s[pos:]
    wr(f, s); added += len(hs)
print("includes added:", added)
