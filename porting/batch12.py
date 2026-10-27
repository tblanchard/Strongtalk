# primitiveConstructMethod: the image's compiler produces the external (32-bit) layout
import re, os
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'Strongtalk', 'vm'))
def rd(p): return open(p, encoding='latin-1').read()
def wr(p, s): open(p, 'w', encoding='latin-1').write(s)
def sub(p, a, b):
    s = rd(p)
    if a not in s: raise SystemExit(f"NOT FOUND {p}: {a[:70]}")
    wr(p, s.replace(a, b, 1))

sub('prims/method_prims.cpp', '''  if (objArrayOop(oops)->length() * oopSize != byteArrayOop(bytes)->length()) {''',
    '''  // the byte codes come in the external layout (4-byte operand slots), see bytecodeTranscoder.hpp
  if (objArrayOop(oops)->length() * external_bytecode_word_size != byteArrayOop(bytes)->length()) {''')
s = rd('prims/method_prims.cpp')
if '"bytecodeTranscoder.hpp"' not in s:
    incs = list(re.finditer(r'^#\s*include "[^"]*"\n', s, re.M))
    s = s[:incs[-1].end()] + '#include "bytecodeTranscoder.hpp"\n' + s[incs[-1].end():]
    wr('prims/method_prims.cpp', s)

s = rd('oops/methodKlass.cpp')
start = s.index('methodOop methodKlass::constructMethod(')
end = s.index('\n}\n', start) + 3
s = s[:start] + '''methodOop methodKlass::constructMethod(oop selector_or_method, int flags, int nofArgs, 
                                       objArrayOop debugInfo, byteArrayOop bytes, objArrayOop oops) {
  klassOop k = as_klassOop();

  // The byte codes come in the external layout (4-byte operand slots, see bytecodeTranscoder.hpp):
  // oops[i] belongs to the slot at byte offset (i-1)*4; it is an oop operand if the slot's bytes are zero.
  assert(oops->length() * external_bytecode_word_size == bytes->length(), "Invalid array sizes");
  ExternalCode code(bytes->length());
  for (int index = 1; index <= bytes->length(); index++) {
    code.set_byte(index - 1, bytes->byte_at(index));
  }
  for (int index = 1; index <= oops->length(); index++) {
    int  pos     = (index - 1) * external_bytecode_word_size;
    bool copyOop = true;
    for (int i = 0; i < external_bytecode_word_size; i++) {
      // copy oop if bytearray holds 4 consecutive aligned zeroes
      if (bytes->byte_at(pos + 1 + i) != 0) copyOop = false;
    }
    if (copyOop) {
      oop value = tenured(oops->obj_at(index));
      assert(value->is_smi() || value->is_old(), "literal must be tenured");
      code.set_oop(pos, value);
    }
  }

  // convert to the native layout
  BytecodeTranscoder t(&code);
  if (!t.ok()) {
    warning("method construction failed: %s", t.error());
    return NULL;
  }
  t.layout(oopSize);
  int code_words = t.padded_length() / oopSize;
  int obj_size   = methodOopDesc::header_size() + code_words;

  // allocate
  methodOop method = as_methodOop(Universe::allocate_tenured(obj_size));
  memOop(method)->initialize_header(has_untagged_contents(), k);

  // initialize the header
  method->set_debugInfo(objArrayOop(tenured(debugInfo)));
  method->set_selector_or_method(tenured(selector_or_method));

  method->set_invocation_count(0);
  method->set_sharing_count(0);
  method->set_size_and_flags(code_words, nofArgs, flags);

  // install the converted byte codes (also remaps the bcis in the debug info)
  method->install_codes(&t);
  if (!t.ok()) {
    warning("method construction failed: %s", t.error());
    return NULL;
  }

  assert(method->is_method(), "must be method");
  return method;
}
''' + s[end:]
if '"bytecodeTranscoder.hpp"' not in s:
    incs = list(re.finditer(r'^#\s*include "[^"]*"\n', s, re.M))
    s = s[:incs[-1].end()] + '#include "bytecodeTranscoder.hpp"\n' + s[incs[-1].end():]
wr('oops/methodKlass.cpp', s)

# ExternalCode::long_at: use the slot bytes unless the slot holds a smi (zero-valued longs share
# the "4 zero bytes" encoding of oop slots in constructMethod)
sub('oops/bytecodeTranscoder.hpp', '''    if (_has_oop[pos / 4]) {
      // a long that was written out as an oop: use its 32-bit bit pattern
      return (intptr_t) (int32_t) (uintptr_t) _oops[pos / 4];
    }''', '''    if (_has_oop[pos / 4] && _oops[pos / 4]->is_smi()) {
      // a long that was written out as a smi oop: use its (32-bit VM) bit pattern
      return (intptr_t) (int32_t) (smiOop(_oops[pos / 4])->value() << 2);
    }''')
print("batch12 done")
