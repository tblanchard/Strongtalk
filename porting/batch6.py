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
def proto_before_class(p, cls, protos):
    s = rd(p)
    if protos[0] in s.replace('friend ', '@@'): return
    m = re.search(r'^class ' + cls + r'\b', s, re.M)
    wr(p, s[:m.start()] + '\n'.join(protos) + '\n' + s[m.start():])

proto_before_class('oops/smiKlass.hpp', 'smiKlass', ['void set_smiKlass_vtbl(Klass* k);'])
for f in os.listdir('oops'):   # all set_*_vtbl friends
    if f.endswith('Klass.hpp'):
        p = 'oops/' + f; s = rd(p)
        for m in re.finditer(r'friend void (set_\w+_vtbl)\(Klass\* k\);', s):
            if f'\nvoid {m.group(1)}(Klass* k);' not in s:
                c = re.search(r'^class \w+', s, re.M)
                s = s[:c.start()] + f'void {m.group(1)}(Klass* k);\n' + s[c.start():]
        wr(p, s)

sub('prims/system_prims.cpp', '  proxyOop(resultProxy)->set_pointer(func);', '  proxyOop(resultProxy)->set_pointer((void*) func);')
sub('prims/integerOps.cpp', 'static const int   logB = sizeof(Digit)*8;\n', '')
add_include('oops/klass.hpp', 'smiOop.hpp')
sub('interpreter/prettyPrinter.cpp', '   nameNode(char* str) : leafNode(0,0) {', '   nameNode(const char* str) : leafNode(0,0) {')
for p in ['interpreter/prettyPrinter.hpp', 'interpreter/prettyPrinter.cpp']:
    s = rd(p); s = s.replace('width_of_string(char* str)', 'width_of_string(const char* str)'); wr(p, s)
add_include('interpreter/interpreter.cpp', 'blockOop.hpp')
add_include('compiler/codeGenerator.cpp', 'blockOop.hpp')
s = rd('compiler/primInliner.cpp')
if 'static inline bool equal(' not in s:
    incs = list(re.finditer(r'^#\s*include .*\n', s, re.M))
    s = s[:incs[-1].end()] + '\nstatic inline bool equal(const char* s, const char* t) { return strcmp(s, t) == 0; }\n' + s[incs[-1].end():]
    wr('compiler/primInliner.cpp', s)
sub('compiler/oldCodeGenerator.cpp', 'call_C((char*)(&pdesc->fn()), relocInfo::prim_type,', 'call_C((char*)(pdesc->fn()), relocInfo::prim_type,')
sub('compiler/defUse.cpp', '	char* glob = global ? "global" : "local";', '	const char* glob = global ? "global" : "local";')
sub('compiler/codeGenerator.cpp', '_masm->movl(Address(context_reg, contextOopDesc::parent_byte_offset()), NULL);', '_masm->movl(Address(context_reg, contextOopDesc::parent_byte_offset()), (int) 0);')
sub('code/inliningdb.cpp', '  const char* name = NEW_RESOURCE_ARRAY(char, 1024);', '  char* name = NEW_RESOURCE_ARRAY(char, 1024);')
sub('asm/assembler.cpp', '    char* s = (title == NULL) ? "" : title;', '    const char* s = (title == NULL) ? "" : title;')
add_include('asm/x86_mapping.hpp', 'location.hpp')
add_include('asm/assembler.hpp', 'codeBuffer.hpp')
for p, h in [('code/nmethod.hpp', 'ncode.hpp')]:
    add_include(p, h)
# computeEscapingBlocks / compiling strings
for p in os.popen("grep -rlE 'computeEscapingBlocks' compiler").read().split():
    s = rd(p); s = re.sub(r'(computeEscapingBlocks\([^)]*?)char\* (\w+)\)', r'\1const char* \2)', s); wr(p, s)
s = rd('compiler/compiler.cpp'); s = re.sub(r'\bchar\* compiling\b', 'const char* compiling', s); wr('compiler/compiler.cpp', s)

# generalize for-scope hoister to class-pointer loop variables
src = rd('/tmp/fixforscope.py')
src = src.replace(r"pat=re.compile(r'for\s*\(\s*(int|unsigned int|unsigned|smi|u_char\*|char\*|oop\*)\s+'+v+r'\b')",
                  r"pat=re.compile(r'for\s*\(\s*(int|unsigned int|unsigned|smi|[A-Za-z_]\w*\s*\*|[A-Za-z_]\w*)\s+'+v+r'\b')")
src = src.replace("if len(v)>6: continue", "if len(v)>12: continue")
wr('/tmp/fixforscope.py', src)
print('batch6 done')
