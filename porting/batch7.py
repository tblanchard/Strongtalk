import re, os
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'Strongtalk', 'vm'))
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

# asm headers: make include order explicit
sub('asm/assembler.hpp', '#include "codeBuffer.hpp"\n#include "relocInfo.hpp"\n', '#include "relocInfo.hpp"\n#include "codeBuffer.hpp"\n')
add_include('asm/codeBuffer.hpp', 'relocInfo.hpp')
add_include('asm/x86_mapping.hpp', 'assembler.hpp')
add_include('runtime/savedRegisters.cpp', 'assembler.hpp')

# x86_imm32 for function pointers
sub('topIncludes/platform.hpp', 'inline int32_t x86_imm32(const volatile void* p) { return x86_imm32((intptr_t) p); }\n',
    '''inline int32_t x86_imm32(const volatile void* p) { return x86_imm32((intptr_t) p); }
template<class R, class... A> inline int32_t x86_imm32(R (*f)(A...))      { return x86_imm32((intptr_t) f); }
template<class R, class... A> inline int32_t x86_imm32(R (*f)(A..., ...)) { return x86_imm32((intptr_t) f); }
''')

for p, h in [('compiler/codeGenerator.cpp', 'objArrayOop.hpp'), ('compiler/oldCodeGenerator.cpp', 'objArrayOop.hpp'),
             ('compiler/oldCodeGenerator.cpp', 'blockOop.hpp'), ('runtime/shell.cpp', 'timer.hpp'),
             ('runtime/shell.cpp', 'bootstrap.hpp'), ('runtime/shell.cpp', 'process.hpp'),
             ('runtime/sweeper.cpp', 'klassOop.hpp'), ('runtime/sweeper.cpp', 'klass.hpp'),
             ('code/nmethod.hpp', 'key.hpp'), ('code/nmethod.hpp', 'nmethodScopes.hpp')]:
    add_include(p, h)

# const strings
s = rd('code/inliningdb.cpp'); s = s.replace('  const char* name = NEW_RESOURCE_ARRAY(char, 1024);', '  char* name = NEW_RESOURCE_ARRAY(char, 1024);'); wr('code/inliningdb.cpp', s)
sub('compiler/defUse.cpp', '    char* prop = ok ? "" : " couldn\'t propagate";', '    const char* prop = ok ? "" : " couldn\'t propagate";')
for p in ['compiler/compUtils.hpp', 'compiler/compUtils.cpp']:
    s = rd(p); s = s.replace('report_block(Node* s, BlockPReg* blk, char* what)', 'report_block(Node* s, BlockPReg* blk, const char* what)'); wr(p, s)
for p in ['compiler/preg.hpp', 'compiler/preg.cpp']:
    s = rd(p); s = re.sub(r'\bchar\* (PReg::)?safeName\(\) const', lambda m: 'const char* ' + (m.group(1) or '') + 'safeName() const', s); wr(p, s)
for p in ['interpreter/bytecodes.hpp', 'interpreter/bytecodes.cpp']:
    s = rd(p)
    s = s.replace('static char*		_name[number_of_codes];', 'static const char*	_name[number_of_codes];')
    s = s.replace('static char*		name		(Code code)	{ return _name[code]; }', 'static const char*	name		(Code code)	{ return _name[code]; }')
    s = s.replace('char*			Bytecodes::_name[number_of_codes];', 'const char*		Bytecodes::_name[number_of_codes];')
    wr(p, s)
s = rd('interpreter/prettyPrinter.cpp')
lines = s.split('\n')
for i in [253, 284]:   # 0-based index of 'char* str;' members assigned from const char*
    if lines[i].strip() == 'char* str;': lines[i] = lines[i].replace('char* str;', 'const char* str;')
wr('interpreter/prettyPrinter.cpp', '\n'.join(lines))

# BranchOpCode negation: 'not' is a C++ operator keyword
s = rd('compiler/primInliner.cpp'); s = re.sub(r'\bnot\((\w+)\)', r'negated(\1)', s)
s = s.replace('static BranchOpCode not(BranchOpCode cond) {', 'static BranchOpCode negated(BranchOpCode cond) {'); wr('compiler/primInliner.cpp', s)

# function pointers -> void*
sub('prims/system_prims.cpp', '  proxyOop(result)->set_pointer(res);', '  proxyOop(result)->set_pointer((void*) res);')
s = rd('compiler/oldCodeGenerator.cpp')
s = s.replace('call_C((char*)(&pdesc->fn()),', 'call_C((char*)(pdesc->fn()),')
s = s.replace('theMacroAssm->movl(Address(context, contextOopDesc::parent_byte_offset()), NULL);', 'theMacroAssm->movl(Address(context, contextOopDesc::parent_byte_offset()), (int) 0);')
s = s.replace('''static oop oopify_float() {
  double x;
  __asm fstp x							// get top of FPU stack''', '''static oop oopify_float() {
  double x;
#if defined(_MSC_VER) && defined(_M_IX86)
  __asm fstp x							// get top of FPU stack
#else
  // called from generated x86 code with the value on the x87 FPU stack
  fatal("oopify_float is only available with the x86-32 backend");
  x = 0.0;
#endif''')
wr('compiler/oldCodeGenerator.cpp', s)
print('batch7 done')
