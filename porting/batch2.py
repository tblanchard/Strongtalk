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

for p, h in [('memory/allocation.hpp', 'bits.hpp'), ('runtime/systemAverage.cpp', 'compiler.hpp'),
             ('prims/vframe_prims.cpp', 'methodOop.hpp'), ('prims/method_prims.cpp', 'blockKlass.hpp'),
             ('interpreter/interpreter.cpp', 'interpretedIC.hpp'), ('interpreter/floats.hpp', 'smiOop.hpp'),
             ('code/zone.hpp', 'jumpTable.hpp'), ('code/zone.cpp', 'blockOop.hpp'),
             ('code/stubRoutines.cpp', 'jumpTable.hpp'), ('code/nameDesc.cpp', 'scopeDesc.hpp'),
             ('code/inliningdb.cpp', 'klassOop.hpp')]:
    add_include(p, h)

# platform: POSIX names
sub('topIncludes/platform.hpp', '  #define _vsnprintf vsnprintf\n', '  #define _vsnprintf vsnprintf\n  #define _unlink    unlink\n')
sub('topIncludes/platform.hpp', '#include <strings.h>\n', '#include <strings.h>\n#ifndef _WIN32\n#include <unistd.h>\n#endif\n')

# x86 code emitters: an address or oop embedded as a 32-bit immediate. Only valid when
# the VM runs on x86-32; fail loudly instead of silently truncating on 64-bit hosts.
sub('topIncludes/platform.hpp', '#endif // VM_TOPINCLUDES_PLATFORM_HPP', '''// Convert a word (address, oop, ...) to an x86 32-bit immediate/displacement.
inline int32_t x86_imm32(intptr_t value) {
  if (value != (intptr_t) (int32_t) value) abort();   // does not fit: x86-32 code cannot reference it
  return (int32_t) value;
}
inline int32_t x86_imm32(const volatile void* p) { return x86_imm32((intptr_t) p); }

#endif // VM_TOPINCLUDES_PLATFORM_HPP''')

# os interface: frame pointers are words, strings are const
for p in ['runtime/os.hpp', 'runtime/os.cpp']:
    s = rd(p)
    s = s.replace('fetch_top_frame(Thread* thread, int** sp, int** fp, char** pc)', 'fetch_top_frame(Thread* thread, intptr_t** sp, intptr_t** fp, char** pc)')
    s = s.replace('message_box(char* title, char* message)', 'message_box(const char* title, const char* message)')
    wr(p, s)

sub('runtime/debug.cpp', '''  for (intFlag* i = &intTable[0]; i->name; i++)
    st->print_cr("%s=%d",  i->name, *b->value_ptr);''', '''  for (intFlag* i = &intTable[0]; i->name; i++)
    st->print_cr("%s=%d",  i->name, *i->value_ptr);''')
sub('runtime/bootstrap.hpp', '  bootstrap(char *name);', '  bootstrap(const char *name);')
s = rd('prims/objArray_prims.cpp'); wr('prims/objArray_prims.cpp', s.replace('if (smiOop(from) <= 0)', 'if (smiOop(from)->value() <= 0)'))
sub('oops/byteArrayKlass.hpp', 'void initialize_object(byteArrayOop obj, char* value, int len);', 'void initialize_object(byteArrayOop obj, const char* value, int len);')
sub('oops/byteArrayKlass.cpp', 'void byteArrayKlass::initialize_object(byteArrayOop obj, char* value, int len){', 'void byteArrayKlass::initialize_object(byteArrayOop obj, const char* value, int len){')
sub('memory/symbolTable.cpp', '  char* s = name;', '  const char* s = name;')
sub('interpreter/prettyPrinter.cpp', ' void PrintParams::parameter(byteArrayOop name, int index);', ' void parameter(byteArrayOop name, int index);')
sub('compiler/slist.cpp', '''  for (GenericSListElem* e = headL(); e && e->dataL() != p;''', '''  GenericSListElem* e;
  for (e = headL(); e && e->dataL() != p;''')
sub('asm/location.cpp', 'char* Location::name() const {', 'const char* Location::name() const {')
sub('asm/assembler.cpp', 'char* Register::name() const {', 'const char* Register::name() const {')

# jump table entries hold an x86 rel32 jump
sub('code/jumpTable.cpp', 'fill_entry(jump_instruction, dest - (int) state_addr(), nmethod_entry);',
    'fill_entry(jump_instruction, (char*) (dest - (char*) state_addr()), nmethod_entry);	// INTEL-SPECIFIC: relative jump')

# compiled PIC (x86 code): embedded words are 32-bit immediates
s = rd('code/compiledPIC.cpp')
s = s.replace('static inline void  put_disp(char*& p, char* d)	{ put_word(p, (int)(d - p - sizeof(int))); }',
              'static inline void  put_disp(char*& p, char* d)	{ put_word(p, x86_imm32(d - p - sizeof(int))); }')
s = re.sub(r'put_word\(p, int\(([^;]*)\)\);', r'put_word(p, x86_imm32(\1));', s)
wr('code/compiledPIC.cpp', s)
print("done")
