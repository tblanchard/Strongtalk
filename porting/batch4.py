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

# '= NULL' is not a pure-specifier
for d in ['compiler', 'code', 'oops', 'runtime', 'interpreter', 'memory', 'prims', 'lookup', 'recompiler', 'asm', 'utilities']:
    for f in os.listdir(d):
        if f.endswith('.hpp'):
            p = f'{d}/{f}'; s = rd(p); s2 = re.sub(r'(\)\s*(?:const\s*)?)=\s*NULL\s*;', r'\1= 0;', s)
            if s2 != s: wr(p, s2)

sub('compiler/rscope.hpp', '  int scopeID() const			{ return desc->offset(); }', '  smi scopeID() const			{ return desc->offset(); }')
sub('runtime/process.cpp', 'current = frame(current_sp, (int*) current_sp + locals + 2);', 'current = frame(current_sp, (intptr_t*) current_sp + locals + 2);')
sub('runtime/evaluator.cpp', 'void restore_hp(int* fr, int dist) {', 'void restore_hp(intptr_t* fr, int dist) {')
sub('runtime/debug.cpp', '''    if (*i->value_ptr != i->default_value)
      st->print_cr("%s=%d",  i->name, *b->value_ptr);''', '''    if (*i->value_ptr != i->default_value)
      st->print_cr("%s=%d",  i->name, *i->value_ptr);''')
sub('topIncludes/platform.hpp', '  #define _unlink    unlink\n', '  #define _unlink    unlink\n  #define _fileno    fileno\n')
for p in ['interpreter/prettyPrinter.hpp', 'interpreter/prettyPrinter.cpp']:
    s = rd(p); s = re.sub(r'\bprint\(char\* str\)', 'print(const char* str)', s); wr(p, s)
s = rd('interpreter/dispatchTable.cpp')
s = s.replace('extern "C" int* frame_breakpoint = (int*) -1;', 'extern "C" intptr_t* frame_breakpoint = (intptr_t*) -1;').replace('frame_breakpoint = (int*) -1;', 'frame_breakpoint = (intptr_t*) -1;')
wr('interpreter/dispatchTable.cpp', s)
for p in os.popen("grep -rl 'frame_breakpoint' .").read().split():
    if p.endswith(('.hpp', '.cpp')):
        s = rd(p); s = s.replace('extern "C" int* frame_breakpoint;', 'extern "C" intptr_t* frame_breakpoint;'); wr(p, s)

add_include('compiler/compiler.hpp', 'compUtils.hpp')
add_include('code/zone.hpp', 'zoneHeap.hpp')
add_include('code/stubRoutines.cpp', 'x86_mapping.hpp')
add_include('asm/assembler.hpp', 'relocInfo.hpp')
add_include('asm/assembler.cpp', 'process.hpp')

s = rd('asm/location.cpp')
s = re.sub(r'(\s*)s = NEW_RESOURCE_ARRAY\(char, (\d+)\);(\s*)sprintf\(s, ([^;]*)\);', r'\1char* buf = NEW_RESOURCE_ARRAY(char, \2);\3sprintf(buf, \4);\3s = buf;', s)
wr('asm/location.cpp', s)
print("batch4 done")
