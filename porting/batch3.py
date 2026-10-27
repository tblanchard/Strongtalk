import re, os
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'Strongtalk', 'vm'))
def rd(p): return open(p, encoding='latin-1').read()
def wr(p, s): open(p, 'w', encoding='latin-1').write(s)
def sub(p, a, b, all=False):
    s = rd(p)
    if a not in s: print("NOT FOUND", p, a[:70]); return
    wr(p, s.replace(a, b) if all else s.replace(a, b, 1))
def add_include(p, h, top=False):
    s = rd(p)
    if f'"{h}"' in s: return
    incs = list(re.finditer(r'^#\s*include .*\n', s, re.M))
    if p.endswith('.cpp') and incs and not top: pos = incs[-1].end()
    else:
        m = re.search(r'^#\s*define \w+\s*\n', s, re.M); pos = m.end() if m else 0
    wr(p, s[:pos] + f'#include "{h}"\n' + s[pos:])

# make headers self-contained
for p, h in [('oops/symbolOop.hpp', 'byteArrayOop.hpp'), ('oops/blockKlass.hpp', 'blockOop.hpp'),
             ('interpreter/interpretedIC.hpp', 'ic_iterator.hpp'), ('compiler/compiler.hpp', 'jumpTable.hpp'),
             ('code/zone.hpp', 'codeTable.hpp'), ('code/scopeDesc.hpp', 'key.hpp'),
             ('code/inliningdb.hpp', 'types.hpp'), ('interpreter/floats.hpp', 'assembler.hpp')]:
    add_include(p, h)
s = rd('runtime/debug.hpp')
if 'class outputStream;' not in s:
    m = re.search(r'^#\s*define \w+\s*\n', s, re.M); wr('runtime/debug.hpp', s[:m.end()] + '\nclass outputStream;\n' + s[m.end():])

sub('compiler/scope.hpp', '  int		scopeID() const 			{ return _scopeID; }', '  smi		scopeID() const 			{ return _scopeID; }')
sub('compiler/rscope.hpp', '  virtual int scopeID() const				{ ShouldNotCallThis(); return 0; }', '  virtual smi scopeID() const				{ ShouldNotCallThis(); return 0; }')
sub('utilities/lprintf.cpp', '  va_end(ap);\n  DEBUG_EXCEPTION\n', '  va_end(ap);\n  DEBUG_EXCEPTION;\n')

# frame pointers
sub('runtime/process.cpp', 'static int*        cur_fp;', 'static intptr_t*   cur_fp;')
s = rd('runtime/evaluator.cpp')
s = s.replace('int* saved_frame;', 'intptr_t* saved_frame;').replace('bool patch_last_delta_frame(int* fr, int* dist) {', 'bool patch_last_delta_frame(intptr_t* fr, int* dist) {')
wr('runtime/evaluator.cpp', s)
for p in ['interpreter/dispatchTable.hpp', 'interpreter/dispatchTable.cpp']:
    s = rd(p); s = re.sub(r'(intercept_for_(?:next|return|step)\w*)\(int\* fr\)', r'\1(intptr_t* fr)', s); wr(p, s)

s = rd('runtime/debug.cpp')
s = s.replace('''  for (intFlag* i = &intTable[0]; i->name; i++) {
    if (*b->value_ptr != b->default_value)''', '''  for (intFlag* i = &intTable[0]; i->name; i++) {
    if (*i->value_ptr != i->default_value)''')
wr('runtime/debug.cpp', s)
sub('runtime/bootstrap.hpp', '  char* file_name;', '  const char* file_name;')
s = rd('prims/objArray_prims.cpp'); wr('prims/objArray_prims.cpp', re.sub(r'if \(smiOop\((\w+)\) <= 0\)', r'if (smiOop(\1)->value() <= 0)', s))
sub('memory/symbolTable.cpp', '  const char* s = name;\n  char* end = s + len;', '  const char* s = name;\n  const char* end = s + len;')
sub('asm/location.cpp', 'const char* Location::name() const {\n  char* s;', 'const char* Location::name() const {\n  const char* s;')

# printing functions take const format strings
for p in ['utilities/ostream.hpp', 'utilities/ostream.cpp', 'utilities/lprintf.hpp', 'utilities/lprintf.cpp']:
    if os.path.exists(p):
        s = rd(p); s = re.sub(r'\bchar\* *(format|str|s|fmt)\b(?=[,)])', r'const char* \1', s); s = s.replace('const const', 'const'); wr(p, s)

# --- pointer truncation in the x86 backend ----------------------------------
x86_files = ('asm/', 'compiler/codeGenerator.cpp', 'compiler/oldCodeGenerator.cpp', 'interpreter/interpreter.cpp',
             'interpreter/floats.cpp', 'code/stubRoutines.cpp', 'code/compiledPIC.cpp', 'code/jumpTable.cpp',
             'code/compiledIC.cpp', 'code/nativeInstruction', 'prims/dll', 'runtime/uncommonBranch.cpp')
def operand_end(t, i):
    # end index of a C++ postfix expression starting at t[i]
    def group(j, o, c):
        d = 0
        while j < len(t):
            if t[j] == o: d += 1
            elif t[j] == c:
                d -= 1
                if d == 0: return j + 1
            j += 1
        return j
    while t[i] == ' ': i += 1
    if t[i] == '(':
        i = group(i, '(', ')')
    elif t[i] in '&*-~!':
        return operand_end(t, i + 1)
    else:
        m = re.match(r'[\w:]+', t[i:])
        if not m: raise ValueError(t[i:i+20])
        i += m.end()
    while True:
        if t[i:i+1] == '(': i = group(i, '(', ')')
        elif t[i:i+1] == '[': i = group(i, '[', ']')
        elif t[i:i+2] == '->' or t[i:i+1] == '.':
            i += 2 if t[i] == '-' else 1
            m = re.match(r'\w+', t[i:]); i += m.end()
        else: return i
edits = {}
for line in open('/tmp/stlogs/uniq_errors.txt'):
    m = re.match(r"([^:]+):(\d+):(\d+): error: cast from pointer to smaller type '(int|unsigned int)' loses information", line)
    if m: edits.setdefault(m.group(1), set()).add((int(m.group(2)), int(m.group(3))))
n = 0
for f, locs in edits.items():
    lines = rd(f).split('\n')
    for l, c in sorted(locs, key=lambda x: (x[0], -x[1])):
        t = lines[l-1]; i = c - 1
        printing = 'print' in t
        conv = '(intptr_t)' if (printing or not f.startswith(x86_files)) else None
        if t.startswith('int(', i) or t.startswith('int (', i):
            j = t.index('(', i)
            lines[l-1] = t[:i] + ('intptr_t' if conv else 'x86_imm32') + t[j:]; n += 1
        elif re.match(r'\((unsigned )?int\) ?', t[i:]):
            m2 = re.match(r'\((unsigned )?int\) ?', t[i:]); k = i + m2.end()
            try: e = operand_end(t, k)
            except Exception as ex: print("UNHANDLED", f, l, t.strip()[:90]); continue
            if conv: lines[l-1] = t[:i] + '(intptr_t) ' + t[k:]
            else:    lines[l-1] = t[:i] + 'x86_imm32(' + t[k:e] + ')' + t[e:]
            n += 1
        else:
            print("UNHANDLED", f, l, c, t.strip()[:80])
    wr(f, '\n'.join(lines))
print("casts rewritten:", n)
