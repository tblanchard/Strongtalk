# 64-bit fixes found by Xcode's -Wint-to-pointer-cast / -Wshorten-64-to-32 / range warnings
import re, os
os.chdir('/Users/todd/Projects/strongtalk-source-r36/Strongtalk/Strongtalk/vm')
def rd(p): return open(p, encoding='latin-1').read()
def wr(p, s): open(p, 'w', encoding='latin-1').write(s)
def sub(p, a, b, all=False):
    s = rd(p)
    if a not in s: print("NOT FOUND", p, a[:70]); return
    wr(p, s.replace(a, b) if all else s.replace(a, b, 1))

# set_words/copy_words operate on int arrays (ageTable, bitVector, rSet); forwarding to the
# oop-sized copy/set routines overran the arrays on 64-bit hosts.
sub('memory/util.hpp', '''inline void copy_words(int* from, int* to, int count) {
  copy_oops((oop*) from, (oop*) to, count);
}
inline void set_words(int* from, int count, int value = 0) {
  set_oops((oop*) from, count, (oop) value);
}''', '''// Note: these operate on int-sized elements (not oop-sized words).
inline void copy_words(int* from, int* to, int count) {
  for (int i = 0; i < count; i++) to[i] = from[i];
}
inline void set_words(int* from, int count, int value = 0) {
  for (int i = 0; i < count; i++) from[i] = value;
}''')

# byte array comparison: the length field is an oop (word), the data is compared in 32-bit chunks
for p, unit in [('oops/byteArrayOop.cpp', 'char'), ('oops/dByteArrayOop.cpp', 'doubleByte')]:
    s = rd(p)
    s = re.sub(r'''  const unsigned int\* a = \((?:const )?unsigned int\*\) length_addr\(\);
  const unsigned int\* b = \((?:const )?unsigned int\*\) arg->length_addr\(\);

  // Get the word sizes of the arays
  int a_size = roundTo\(smiOop\(\*a\+\+\)->value\(\) \* sizeof\((\w+)\), sizeof\(int\)\) / sizeof\(int\);
  int b_size = roundTo\(smiOop\(\*b\+\+\)->value\(\) \* sizeof\(\w+\), sizeof\(int\)\) / sizeof\(int\);''',
    lambda m: f'''  // (the length field is a smiOop word; the bodies are compared in 32-bit chunks)
  const oop* a_length = (const oop*) length_addr();
  const oop* b_length = (const oop*) arg->length_addr();
  const unsigned int* a = (const unsigned int*) (a_length + 1);
  const unsigned int* b = (const unsigned int*) (b_length + 1);

  // Get the 32-bit chunk sizes of the arrays
  int a_size = (int) (roundTo(smiOop(*a_length)->value() * sizeof({m.group(1)}), sizeof(int)) / sizeof(int));
  int b_size = (int) (roundTo(smiOop(*b_length)->value() * sizeof({m.group(1)}), sizeof(int)) / sizeof(int));''', s)
    wr(p, s)

# debugger helpers: addresses typed in the debugger are words, not ints
s = rd('memory/debugPrintable.cpp')
for f in ['pv', 'pp', 'pp_short', 'pk']:
    s = re.sub(r'\bvoid ' + f + r'\(int p\)', f'void {f}(intptr_t p)', s)
s = s.replace('nmethod* find(int addr) {', 'nmethod* find(intptr_t addr) {')
s = s.replace('methodOop findm(int hp) {', 'methodOop findm(intptr_t hp) {')
s = s.replace('void ph(int hp)	{', 'void ph(intptr_t hp)	{').replace('void pm(int m) {', 'void pm(intptr_t m) {')
wr('memory/debugPrintable.cpp', s)
for p in os.popen("grep -rlE '\\b(pv|pp|pp_short|pk|ph|pm|find|findm)\\(int ' --include='*.hpp' .").read().split():
    s = rd(p)
    s = re.sub(r'\b(void|nmethod\*|methodOop)(\s+)(pv|pp|pp_short|pk|ph|pm|find|findm)\(int (\w+)\)', r'\1\2\3(intptr_t \4)', s)
    wr(p, s)

sub('oops/methodOop.hpp', '''    addr()->_size_and_flags = (smiOop)
      ((flags << method_flags_mask_bitno)''', '''    addr()->_size_and_flags = (smiOop) (intptr_t)
      ((flags << method_flags_mask_bitno)''')

sub('runtime/evaluator.cpp', '''  int address;
  oop obj;
  unsigned int length;
  if (sscanf(current(), "0x%x%n", &address, &length) == 1 && strlen(current()) == length) {''', '''  uintptr_t address;
  oop obj;
  int length;
  if (sscanf(current(), "0x%lx%n", &address, &length) == 1 && strlen(current()) == (size_t) length) {''')

# a Digit (32 bits) always fits in a 64-bit smi; int() overflowed for digits >= 2^31
s = rd('prims/integerOps.cpp')
s = s.replace('return as_smiOop(-int(_first_digit));', 'return as_smiOop(-smi(_first_digit));')
s = s.replace('return as_smiOop( int(_first_digit));', 'return as_smiOop( smi(_first_digit));')
wr('prims/integerOps.cpp', s)

# compiler constant folding works on tagged smi values (words)
s = rd('compiler/node.cpp')
s = s.replace('''    int c1 = (int)((ConstPReg*)_src)->constant;
    int c2 = (int)operConst();
    int res;''', '''    intptr_t c1 = (intptr_t)((ConstPReg*)_src)->constant;
    intptr_t c2 = (intptr_t)operConst();
    intptr_t res;''')
s = s.replace('void BranchNode::eliminateBranch(int op1, int op2, int res) {', 'void BranchNode::eliminateBranch(intptr_t op1, intptr_t op2, intptr_t res) {')
wr('compiler/node.cpp', s)
sub('compiler/node.hpp', 'void		eliminateBranch(int op1, int op2, int res);', 'void		eliminateBranch(intptr_t op1, intptr_t op2, intptr_t res);')

# event log parameters may be ints or pointers
s = rd('utilities/eventLog.hpp')
s = s.replace('#define LOG_EVENT1(name,p1)       eventLog->log(name, (void*)(p1))', '#define LOG_EVENT1(name,p1)       eventLog->log(name, (void*)(intptr_t)(p1))')
s = s.replace('#define LOG_EVENT2(name,p1,p2)    eventLog->log(name, (void*)(p1), (void*)(p2))', '#define LOG_EVENT2(name,p1,p2)    eventLog->log(name, (void*)(intptr_t)(p1), (void*)(intptr_t)(p2))')
s = s.replace('#define LOG_EVENT3(name,p1,p2,p3) eventLog->log(name, (void*)(p1), (void*)(p2), (void*)(p3))', '#define LOG_EVENT3(name,p1,p2,p3) eventLog->log(name, (void*)(intptr_t)(p1), (void*)(intptr_t)(p2), (void*)(intptr_t)(p3))')
wr('utilities/eventLog.hpp', s)
sub('memory/generation.cpp', 'EventMarker em("expanding heap by %d", (void*)size);', 'EventMarker em("expanding heap by %d", (void*)(intptr_t)size);')
print("batch9 done")
