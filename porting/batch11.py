# Load the 32-bit (external layout) .bst image on 64-bit hosts
import re, os
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'Strongtalk', 'vm'))
def rd(p): return open(p, encoding='latin-1').read()
def wr(p, s): open(p, 'w', encoding='latin-1').write(s)
def sub(p, a, b):
    s = rd(p)
    if a not in s: raise SystemExit(f"NOT FOUND {p}: {a[:70]}")
    wr(p, s.replace(a, b, 1))

# --- oldGeneration: give back the unused end of the most recent allocation
sub('memory/generation.hpp', '''  oop* allocate(int size) {
    return current_space->allocate(size);
  }
''', '''  oop* allocate(int size) {
    return current_space->allocate(size);
  }

  // Gives back [from, to) if it is the end of the most recent allocation; returns false otherwise.
  bool shrink_top(oop* from, oop* to) {
    if (current_space->top() != to) return false;
    current_space->set_top(from);
    return true;
  }
''')

# --- bootstrap: allocation with an upper bound, shrink/fill afterwards
sub('runtime/bootstrap.hpp', '  bool _new_format;\n', '''  bool _new_format;

  // Objects whose size differs between the image's 32-bit layout and the native layout
  // are allocated with an upper bound and shrunk afterwards (see get_object).
  int   _current_capacity;			// capacity (words) of the object being read
  struct Filler { oop* start; int words; };
  Filler* _fillers;
  int     _number_of_fillers;
  int     _max_number_of_fillers;
  void    shrink(memOop m, int capacity, int size);
  void    add_filler(oop* start, int words);
  void    create_fillers();
''')
sub('runtime/bootstrap.hpp', '  bool new_format() const { return _new_format; }\n', '''  bool new_format() const { return _new_format; }

  // Capacity in words of the object whose bootstrap_object() is running; read it before
  // reading any field (nested objects change it).
  int  current_capacity() const { return _current_capacity; }
''')

s = rd('runtime/bootstrap.cpp')
s = s.replace('''void bootstrap::initialize_tables(int initial_table_size) {
  oop_table          = NEW_C_HEAP_ARRAY(oop, initial_table_size);''', '''void bootstrap::initialize_tables(int initial_table_size) {
  _current_capacity      = 0;
  _number_of_fillers     = 0;
  _max_number_of_fillers = 1024;
  _fillers               = NEW_C_HEAP_ARRAY(Filler, _max_number_of_fillers);
  oop_table          = NEW_C_HEAP_ARRAY(oop, initial_table_size);''', 1)
s = s.replace('''bootstrap::~bootstrap() {
  FreeHeap(oop_table);
}''', '''bootstrap::~bootstrap() {
  FreeHeap(oop_table);
  FreeHeap(_fillers);
}

void bootstrap::add_filler(oop* start, int words) {
  if (_number_of_fillers == _max_number_of_fillers) {
    Filler* f = NEW_C_HEAP_ARRAY(Filler, _max_number_of_fillers * 2);
    for (int i = 0; i < _number_of_fillers; i++) f[i] = _fillers[i];
    FreeHeap(_fillers);
    _fillers = f;
    _max_number_of_fillers *= 2;
  }
  _fillers[_number_of_fillers].start = start;
  _fillers[_number_of_fillers].words = words;
  _number_of_fillers++;
}

// The unused end of an object allocated with an upper bound is given back if the object
// was the last allocation, otherwise it becomes a (garbage) filler object, so that old
// space stays parsable.
void bootstrap::shrink(memOop m, int capacity, int size) {
  oop* start = (oop*) m->addr();
  if (size > capacity) fatal2("bootstrap: object needs %d words, only %d allocated", size, capacity);
  if (size == capacity) return;
  if (Universe::old_gen.shrink_top(start + size, start + capacity)) return;
  if (capacity - size < 3) fatal("bootstrap: gap too small for a filler object");
  add_filler(start + size, capacity - size);
}

// Fillers are empty-looking object arrays; they can only be created once the Array class is known.
void bootstrap::create_fillers() {
  klassOop k = Universe::objArrayKlassObj();
  assert(k->klass_part()->non_indexable_size() == memOopDesc::header_size(), "unexpected Array layout");
  for (int i = 0; i < _number_of_fillers; i++) {
    oop* p     = _fillers[i].start;
    int  words = _fillers[i].words;
    p[0] = (oop) markOopDesc::tagged_prototype();
    p[1] = (oop) k;
    p[2] = (oop) as_smiOop(words - 3);
    for (int j = 3; j < words; j++) p[j] = smiOop_zero;
  }
  if (_number_of_fillers > 0)
    lprintf(" (%d filler objects)", _number_of_fillers);
}''', 1)
s = s.replace('''  Universe::_vframeKlassObj              = HasActivationClass ? klassOop(get_object()) : klassOop(nilObj);
}''', '''  Universe::_vframeKlassObj              = HasActivationClass ? klassOop(get_object()) : klassOop(nilObj);

  create_fillers();
}''', 1)
s = s.replace('''  int    size = get_integer();
  memOop m    = as_memOop(Universe::allocate_tenured(size));

  // Clear eventual padding area for byteArray, symbol, doubleByteArray.
  m->raw_at_put(size-1, smiOop_zero);
''', '''  // The size is in words of the image's (32-bit) layout. Objects with untagged contents
  // need fewer native words (bytes are packed 8 per word, a double takes one word);
  // methods may need more after their byte codes are converted to the native layout.
  int size     = get_integer();
  int capacity = size;
  switch (type) {
    case 'd': case 'e': case 'g': case 'h': capacity = size + 3;     break;	// +3: room for a filler
    case 'j':                               capacity = 2 * size + 3; break;
  }
  memOop m = as_memOop(Universe::allocate_tenured(capacity));

  // Clear the object (padding areas of byteArray, symbol, doubleByteArray must be zero).
  set_oops((oop*) m->addr(), capacity, smiOop_zero);
''', 1)
s = s.replace('''  add(m);
  switch (type) {''', '''  add(m);
  _current_capacity = capacity;
  switch (type) {''', 1)
s = s.replace('''    case 'H': KLASS_CASE(set_doubleKlass_vtbl)''', '''    case 'H': KLASS_CASE(set_doubleKlass_vtbl)	// note: non_indexable_size is fixed below''', 1)
s = s.replace('''    default: fatal("unknown object type");
  }

  return m;''', '''    default: fatal("unknown object type");
  }

  if (type == 'H') {
    // the image gives the 32-bit size of Double instances (header + 2 words)
    klassOop(m)->klass_part()->set_non_indexable_size(doubleOopDesc::header_size());
  }
  if (capacity != size || type == 'h') shrink(m, capacity, m->size());

  return m;''', 1)
wr('runtime/bootstrap.cpp', s)
for h in ['doubleOop.hpp', 'markOop.hpp', 'generation.hpp', 'universe.hpp', 'util.hpp']:
    s = rd('runtime/bootstrap.cpp')
    if f'"{h}"' not in s:
        incs = list(re.finditer(r'^#\s*include "[^"]*"\n', s, re.M))
        s = s[:incs[-1].end()] + f'#include "{h}"\n' + s[incs[-1].end():]
        wr('runtime/bootstrap.cpp', s)

# --- methods: convert the byte codes to the native layout
sub('oops/methodOop.cpp', '''  st->read_oop((oop*)&addr()->_size_and_flags);

  for(int index = 1; index <= size_of_codes() * oopSize; )
    if (st->is_byte()) {
      byte_at_put(index, st->read_byte());
      index++;
    } else {
      st->read_oop((oop*) codes(index));
      index += oopSize;
    }
}''', '''  st->read_oop((oop*)&addr()->_size_and_flags);

  // The image holds the byte codes in the external (32-bit) layout (4-byte operand slots).
  int external_length = size_of_codes() * external_bytecode_word_size;
  ExternalCode code(external_length);
  for (int index = 0; index < external_length; ) {
    if (st->is_byte()) {
      code.set_byte(index, st->read_byte());
      index++;
    } else {
      oop value;
      st->read_oop(&value);
      code.set_oop(index, value);
      index += external_bytecode_word_size;
    }
  }

  BytecodeTranscoder t(&code);
  if (!t.ok()) fatal1("bootstrap: cannot convert byte codes: %s", t.error());
  t.layout(oopSize);
  if (header_size() + t.padded_length() / oopSize > capacity)
    fatal("bootstrap: method does not fit after byte code conversion");
  install_codes(&t);
}

// Installs the code of a laid out transcoder (native layout); the method must have room for it.
// Bci ranges in the temp info are remapped to the new code positions.
void methodOopDesc::install_codes(BytecodeTranscoder* t) {
  NativeCode sink(codes(), t->padded_length());
  t->emit(&sink);
  if (!t->ok()) fatal1("cannot convert byte codes: %s", t->error());
  set_size_of_code(t->padded_length() / oopSize);
  remap_temp_info(t);
}

// tempInfo format (see TempDecoder::decode):
//   name* offset name* offset name* offset name* (begin end offset name* offset name*)*
// The begin/end pairs are bcis.
void methodOopDesc::remap_temp_info(BytecodeTranscoder* t) {
  oop info = debugInfo();
  if (info == NULL || !info->is_objArray()) return;
  objArrayOop tempInfo = objArrayOop(info);
  int len = tempInfo->length();
  int pos = 1;
  // skip parameters and the three global sections
  while (pos <= len && tempInfo->obj_at(pos)->is_byteArray()) pos++;
  for (int section = 0; section < 3; section++) {
    if (pos > len) return;
    pos++;	// offset
    while (pos <= len && tempInfo->obj_at(pos)->is_byteArray()) pos++;
  }
  // inlined scopes
  while (pos + 1 <= len) {
    oop begin = tempInfo->obj_at(pos);
    oop end   = tempInfo->obj_at(pos + 1);
    if (!begin->is_smi() || !end->is_smi()) return;
    tempInfo->obj_at_put(pos,     as_smiOop(t->map_bci((int) smiOop(begin)->value(), false)));
    tempInfo->obj_at_put(pos + 1, as_smiOop(t->map_bci((int) smiOop(end)->value(),   true)));
    pos += 2;
    for (int part = 0; part < 2; part++) {	// oop temps, float temps
      if (pos > len) return;
      pos++;	// offset
      while (pos <= len && tempInfo->obj_at(pos)->is_byteArray()) pos++;
    }
  }
}''')
sub('oops/methodOop.cpp', '''void methodOopDesc::bootstrap_object(bootstrap* st) {
  memOopDesc::bootstrap_header(st);''', '''void methodOopDesc::bootstrap_object(bootstrap* st) {
  int capacity = st->current_capacity();	// must be read before any field
  memOopDesc::bootstrap_header(st);''')
s = rd('oops/methodOop.cpp')
if '"bytecodeTranscoder.hpp"' not in s:
    incs = list(re.finditer(r'^#\s*include "[^"]*"\n', s, re.M))
    s = s[:incs[-1].end()] + '#include "bytecodeTranscoder.hpp"\n#include "bootstrap.hpp"\n' + s[incs[-1].end():]
    wr('oops/methodOop.cpp', s)

sub('oops/methodOop.hpp', '''  void set_size_of_code(int size) {''', '''  // Installs converted byte codes (see bytecodeTranscoder.hpp)
  void install_codes(class BytecodeTranscoder* t);
  void remap_temp_info(class BytecodeTranscoder* t);

  void set_size_of_code(int size) {''')

# --- IfNode: a jump_else size of 16 (64-bit word form) is encoded as 0
sub('interpreter/methodIterator.cpp', '''  else_jump_size              = structure >> 4;
''', '''  else_jump_size              = structure >> 4;
  if (has_else_branch && else_jump_size == 0) else_jump_size = 16;	// 16 does not fit the nibble
''')
print("batch11 done")
