import re, os
os.chdir('/Users/todd/Projects/strongtalk-source-r36/Strongtalk/Strongtalk/vm')
def rd(p): return open(p, encoding='latin-1').read()
def wr(p, s): open(p, 'w', encoding='latin-1').write(s)
def sub(p, a, b, all=False):
    s = rd(p)
    if a not in s: print("NOT FOUND", p, a[:70]); return
    wr(p, s.replace(a, b) if all else s.replace(a, b, 1))
def add_include(p, h):
    s = rd(p)
    if f'"{h}"' in s: return
    incs = list(re.finditer(r'^#\s*include .*\n', s, re.M))
    if p.endswith('.cpp') and incs: pos = incs[-1].end()
    else:
        m = re.search(r'^#\s*define \w+\s*\n', s, re.M); pos = m.end() if m else 0
    wr(p, s[:pos] + f'#include "{h}"\n' + s[pos:])

# ---- integerOps: portable multi-precision digit primitives (Digit is 32 bits) ----
s = rd('prims/integerOps.cpp')
def replace_fn(s, name, body):
    i = s.index(f'inline Digit IntegerOps::{name}(')
    j = s.index('\n}\n', i) + 3
    return s[:i] + body + s[j:]
s = replace_fn(s, 'xpy', '''inline Digit IntegerOps::xpy(Digit x, Digit y, Digit& carry) {
  // returns (x + y + c) mod B; sets carry = (x + y + c) div B
  assert(carry <= 1, "wrong carry");
  uint64_t r = (uint64_t) x + y + carry;
  carry = (Digit) (r >> logB);
  return (Digit) r;
}
''')
s = replace_fn(s, 'xmy', '''inline Digit IntegerOps::xmy(Digit x, Digit y, Digit& carry) {
  // returns (x - y - c) mod B; sets carry = -((x - y - c) div B)
  assert(carry <= 1, "wrong carry");
  uint64_t r = (uint64_t) x - y - carry;
  carry = (Digit) (r >> logB) & 1;	// borrow
  return (Digit) r;
}
''')
s = replace_fn(s, 'axpy', '''inline Digit IntegerOps::axpy(Digit a, Digit x, Digit y, Digit& carry) {
  // returns (a*x + y + c) mod B; sets carry = (a*x + y + c) div B
  uint64_t r = (uint64_t) a * x + y + carry;	// cannot overflow: (B-1)*(B-1) + 2*(B-1) < B*B
  carry = (Digit) (r >> logB);
  return (Digit) r;
}
''')
s = replace_fn(s, 'xdy', '''inline Digit IntegerOps::xdy(Digit x, Digit y, Digit& carry) {
  // returns (carry*B + x) div y; sets carry = (carry*B + x) mod y
  assert(carry < y, "quotient would overflow");
  uint64_t n = ((uint64_t) carry << logB) | x;
  carry = (Digit) (n % y);
  return (Digit) (n / y);
}
''')
wr('prims/integerOps.cpp', s)
if 'logB' not in rd('prims/integerOps.hpp'):
    sub('prims/integerOps.hpp', 'typedef unsigned int Digit;', 'typedef unsigned int Digit;\nconst int logB = sizeof(Digit) * 8;	// B = 2^logB is the digit base')

# ---- debugPrintable: current frame pointer ----
sub('memory/debugPrintable.cpp', '  __asm { mov fp, ebp }', '  fp = (intptr_t*) __builtin_frame_address(0);', all=True)

# ---- system_prims halt ----
sub('prims/system_prims.cpp', '  __asm hlt\n', '  __builtin_debugtrap();	// was "hlt" (traps into the debugger on Win32)\n')

# ---- savedRegisters: naked x86 routine, called from generated x86 code only ----
s = rd('runtime/savedRegisters.cpp')
i = s.index('#define Naked __declspec( naked )')
j = s.index('#undef Naked') + len('#undef Naked')
s = s[:i] + '''#if defined(_MSC_VER) && defined(_M_IX86)
''' + s[i:j] + '''
#else
void SavedRegisters::save_registers() {
  // Called only from generated x86 code (see StubRoutines); the x86 backend
  // does not run on this architecture.
  fatal("SavedRegisters::save_registers is only available with the x86-32 backend");
}
#endif''' + s[j:]
wr('runtime/savedRegisters.cpp', s)
add_include('runtime/savedRegisters.cpp', 'x86_mapping.hpp')

# ---- misc compile errors ----
for p in ['compiler/node.hpp', 'compiler/node.cpp']:
    s = rd(p); s = re.sub(r'\bchar\* *(\w+::)?action\(\)', lambda m: 'const char* ' + (m.group(1) or '') + 'action()', s); wr(p, s)
s = rd('compiler/rscope.hpp'); s = re.sub(r'\bint( +)scopeID\(\) const', r'smi\1scopeID() const', s); wr('compiler/rscope.hpp', s)
add_include('oops/byteArrayOop.hpp', 'klass.hpp')
add_include('interpreter/interpreter.cpp', 'objArrayOop.hpp')
add_include('asm/assembler.cpp', 'smiOop.hpp')
for p in os.popen("grep -rlE 'width_of_string\\(char\\*' interpreter").read().split():
    s = rd(p); s = s.replace('width_of_string(char*', 'width_of_string(const char*'); wr(p, s)
sub('runtime/bootstrap.cpp', '  int no    = _fileno(stream);\n  file_size = _filelength(no);',
    '  fseek(stream, 0, SEEK_END);\n  file_size = ftell(stream);\n  fseek(stream, 0, SEEK_SET);')
# friend-only IC accessors used from relocInfo.hpp
s = rd('code/relocInfo.hpp')
if 'CompiledIC* CompiledIC_from_relocInfo(char*' not in s:
    m = re.search(r'^class relocInfo', s, re.M)
    s = s[:m.start()] + '''class CompiledIC;
class PrimitiveIC;
class CompiledDLL_Cache;
CompiledIC*        CompiledIC_from_relocInfo(char* displacement_address);
PrimitiveIC*       PrimitiveIC_from_relocInfo(char* displacement_address);
CompiledDLL_Cache* compiledDLL_Cache_from_relocInfo(char* displacement_address);

''' + s[m.start():]
    wr('code/relocInfo.hpp', s)
print("batch5 done")
