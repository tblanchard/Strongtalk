/* Copyright 1994, 1995 LongView Technologies L.L.C. $Revision: 1.53 $ */
/* Copyright (c) 2006, Sun Microsystems, Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the 
following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following 
	  disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Sun Microsystems nor the names of its contributors may be used to endorse or promote products derived 
	  from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT 
NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL 
THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE 
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE


*/


#include "config.hpp"
#include "error.hpp"
#include "oopsHierarchy.hpp"
#include "longInt.hpp"
#include "types.hpp"
#include "asserts.hpp"
#include "macros.hpp"
#include "bits.hpp"
#include "lprintf.hpp"
#include "util.hpp"
#include "allocation.hpp"
#include "ostream.hpp"
#include "tag.hpp"
#include "top.hpp"
#include "eventLog.hpp"
#include "debug.hpp"
#include "oop.hpp"
#include "iterator.hpp"
#include "growableArray.hpp"
#include "process.hpp"
#include "rSet.hpp"
#include "space.hpp"
#include "virtualspace.hpp"
#include "generation.hpp"
#include "generation.inline.hpp"
#include "spaceSize.hpp"
#include "universe.hpp"
#include "smiOop.hpp"
#include "markOop.hpp"
#include "memOop.hpp"
#include "universe.store.hpp"
#include "klass.hpp"
#include "byteArrayOop.hpp"
#include "klassOop.hpp"
#include "memOopKlass.hpp"
#include "associationOop.hpp"
#include "klassKlass.hpp"
#include "vframeOop.hpp"
#include "symbolOop.hpp"
#include "associationKlass.hpp"
#include "bytecodes.hpp"
#include "dByteArrayOop.hpp"
#include "objArrayOop.hpp"
#include "universe.inline.hpp"
#include "vmSymbols.hpp"
#include "oop.inline.hpp"
#include "ageTable.hpp"
#include "markSweep.hpp"
#include "memOop.inline.hpp"
#include "objArrayKlass.hpp"
#include "bootstrap.hpp"
#include "vframeKlass.hpp"
#include "processOop.hpp"
#include "dValueArrayOop.hpp"
#include "dValueArrayKlass.hpp"
#include "mixinOop.hpp"
#include "mixinKlass.hpp"
#include "proxyOop.hpp"
#include "runtime.hpp"
#include "proxyKlass.hpp"
#include "doubleOop.hpp"
#include "weakArrayKlass.hpp"
#include "frame.hpp"
#include "symbolTable.hpp"
#include "methodOop.hpp"
#include "smiKlass.hpp"
#include "doubleKlass.hpp"
#include "methodKlass.hpp"
#include "byteArrayKlass.hpp"
#include "symbolKlass.hpp"
#include "dByteArrayKlass.hpp"
#include "blockOop.hpp"
#include "processKlass.hpp"
#include "blockKlass.hpp"
#include <unistd.h>

// Debugging: walks the objects of old space, reports the first place where it is not parsable.
static bootstrap* the_bootstrap = NULL;
static void check_old_space() {
  oop* bottom = Universe::old_gen.bottom_mark()._point;
  oop* top    = Universe::old_gen.top_mark()._point;
  oop* p = bottom;
  oop* prev = NULL;
  int count = 0;
  while (p < top) {
    if (!oop(*p)->is_mark()) {
      lprintf("\nheap not parsable at %p (object #%d, offset %ld words)\n", p, count, (long) (p - bottom));
      if (prev != NULL) {
        memOop pm = as_memOop(prev);
        lprintf("previous object %p: size %d, klass %p method %d byteArray %d symbol %d double %d objArray %d klass %d\n",
                prev, pm->size(), (void*) pm->klass(), pm->is_method(), pm->is_byteArray(), pm->is_symbol(),
                pm->is_double(), pm->is_objArray(), pm->is_klass());
      }
      for (int i = -4; i < 6; i++) lprintf("  [%d] %p\n", i, (void*) p[i]);
      the_bootstrap->print_fillers_near(p);
      return;
    }
    prev = p;
    p += as_memOop(p)->size();
    count++;
  }
  lprintf(" [old space parsable: %d objects]", count);
}

void bootstrap::print_fillers_near(oop* p) {
  for (int i = 0; i < _number_of_fillers; i++) {
    if (_fillers[i].start >= p - 16 && _fillers[i].start <= p + 16)
      lprintf("  filler %d at %p (%+ld words), %d words\n", i, _fillers[i].start, (long) (_fillers[i].start - p), _fillers[i].words);
  }
  lprintf("  %d fillers in total\n", _number_of_fillers);
}

bootstrap::bootstrap(const char* name) {
  the_bootstrap = this;
  file_name = name;
  _has_error = false;
  open_file();
  if (!has_error()) {
    initialize_tables(file_size > 0 ? (file_size / 32) : (10 * K));
    parse_file();
    close_file();
    if (VerifyBeforeGC) { check_old_space(); lprintf(" [verifying heap after bootstrap"); Universe::verify(); lprintf("]"); }
    if (VerifyFileOut) methodOopDesc::verify_fileout_against_image();
    Universe  ::cleanup_after_bootstrap();
  }
}

void bootstrap::initialize_tables(int initial_table_size) {
  _current_capacity      = 0;
  _number_of_fillers     = 0;
  _max_number_of_fillers = 1024;
  _fillers               = NEW_C_HEAP_ARRAY(Filler, _max_number_of_fillers);
  oop_table          = NEW_C_HEAP_ARRAY(oop, initial_table_size);
  max_number_of_oops = initial_table_size;
  number_of_oops     = 0;
  position           = 0;
}

bootstrap::~bootstrap() {
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
  if (TraceBootstrap) lprintf("  shrink %p: capacity %d size %d top %p\n", start, capacity, size, Universe::old_gen.top_mark()._point);
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
}

oop bootstrap::at(int index) {
  if (index < 0 || index > number_of_oops)
    error("bootstrap oop table overflow");
  return oop_table[index];
}

void bootstrap::add(oop obj) {
  if (number_of_oops >= max_number_of_oops) {
    int new_size = max_number_of_oops  * 2;
    printf("Expanding boot table to %d\n", new_size);
    oop* new_oop_table = NEW_C_HEAP_ARRAY(oop, new_size);
    for(int index = 0; index < max_number_of_oops; index++)
      new_oop_table[index] = oop_table[index];
   
    max_number_of_oops = new_size;
    FreeHeap(oop_table);
    oop_table = new_oop_table;
  }
  oop_table[number_of_oops++] = obj;
}

void bootstrap::open_file() {
  stream = fopen(file_name, "rb");
  if (stream == NULL) {
    _has_error = true;
    lprintf("\nCould not open file (%s) for reading!\n", file_name);
    exit(-1);
  }
  fseek(stream, 0, SEEK_END);
  file_size = ftell(stream);
  fseek(stream, 0, SEEK_SET);
}

char bootstrap::get_char() {
  position++;
  return getc(stream);
}

int bootstrap::get_integer() {
  position++;
  int result = getc(stream);
  if (result == EOF) fatal("end of file");
  assert(result >= 0, "must be positive");
  if (result < 128 ) return result;
  return get_integer() * 128 + result - 128;
}

void bootstrap::parse_file() {
  // Check version number
  int version_number = get_integer();

  if (version_number > 100) {
    _new_format = true;
    lprintf(", n");
    version_number -= 100;
  } else {
    _new_format = false;
  }

  if (Bytecodes::version() == version_number) {

  } else {
    lprintf("\n");
    lprintf("Bytecode version conflict\n");
    lprintf(" excpected: %d\n", Bytecodes::version());
    lprintf(" received:  %d\n", version_number);
    exit(-1);
  }

  Universe::_systemDictionaryObj         = objArrayOop(get_object());
  nilObj                                 = memOop(get_object());
  trueObj                                = memOop(get_object());
  falseObj                               = memOop(get_object());
  smiKlassObj                            = klassOop(get_object());
  Universe::_memOopKlassObj              = klassOop(get_object());
  Universe::_objArrayKlassObj            = klassOop(get_object());
  Universe::_byteArrayKlassObj           = klassOop(get_object());
  symbolKlassObj                         = klassOop(get_object());
  doubleKlassObj                         = klassOop(get_object());
  Universe::_methodKlassObj              = klassOop(get_object());
  Universe::_associationKlassObj         = klassOop(get_object());
  zeroArgumentBlockKlassObj              = klassOop(get_object());
  oneArgumentBlockKlassObj               = klassOop(get_object());
  twoArgumentBlockKlassObj               = klassOop(get_object());
  threeArgumentBlockKlassObj             = klassOop(get_object());
  fourArgumentBlockKlassObj              = klassOop(get_object());
  fiveArgumentBlockKlassObj              = klassOop(get_object());
  sixArgumentBlockKlassObj               = klassOop(get_object());
  sevenArgumentBlockKlassObj             = klassOop(get_object());
  eightArgumentBlockKlassObj             = klassOop(get_object());
  nineArgumentBlockKlassObj              = klassOop(get_object());
  contextKlassObj                        = klassOop(get_object());
  Universe::_asciiCharacters             = objArrayOop(get_object());
  Universe::_vframeKlassObj              = HasActivationClass ? klassOop(get_object()) : klassOop(nilObj);

  create_fillers();
}

void bootstrap::insert_symbol(memOop obj) {
  if (Universe::symbol_table->is_present(symbolOop(obj))) {
    lprintf("Symbol ");
    symbolOop(obj)->print_value();
    lprintf(" already present in symbol_table!\n");
  } else {
    Universe::symbol_table->add_symbol(symbolOop(obj));
  }
}

# define KLASS_CASE(func)  func(klassOop(m)->klass_part()); klassOop(m)->bootstrap_object(this); break;
# define OBJECT_CASE(cast) cast(m)->bootstrap_object(this); break;
# define OBJECT_ERROR(str) fatal(str); break;
# define SYMBOL_CASE(cast) cast(m)->bootstrap_object(this); insert_symbol(cast(m)); break;


oop bootstrap::get_object() {
  char type = get_char();
  
  if (type == '0') {
    int v = get_integer();
    if (TraceBootstrap) lprintf("@%d i %d\n", position, v);
    return as_smiOop(v);
  }
  if (type == '-') { 
    int v = get_integer();
    if (TraceBootstrap) lprintf("@%d i %d\n", position, -v);
    return as_smiOop(-v);
  }
  if (type == '3') {
    int v = get_integer();
    if (TraceBootstrap) lprintf("@%d r %d\n", position, v);
    return at(v);
  }

  // The size is in words of the image's (32-bit) layout. Objects with untagged contents
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

  if (TraceBootstrap) lprintf("@%d %c size %d #%d = %p\n", position, type, size, number_of_oops, m);

  add(m);
  _current_capacity = capacity;
  switch (type) {
    // Classes
    case 'A': KLASS_CASE(set_klassKlass_vtbl)
    case 'B': KLASS_CASE(set_smiKlass_vtbl)
    case 'C': KLASS_CASE(set_memOopKlass_vtbl)
    case 'D': KLASS_CASE(set_byteArrayKlass_vtbl)
    case 'E': KLASS_CASE(set_doubleByteArrayKlass_vtbl)
    case 'F': KLASS_CASE(set_objArrayKlass_vtbl)
    case 'G': KLASS_CASE(set_symbolKlass_vtbl)
    case 'H': KLASS_CASE(set_doubleKlass_vtbl)	// note: non_indexable_size is fixed below
    case 'I': KLASS_CASE(set_associationKlass_vtbl)
    case 'J': KLASS_CASE(set_methodKlass_vtbl)
    case 'K': KLASS_CASE(set_blockClosureKlass_vtbl)
    case 'L': KLASS_CASE(set_contextKlass_vtbl)
    case 'M': KLASS_CASE(set_proxyKlass_vtbl)
    case 'N': KLASS_CASE(set_mixinKlass_vtbl)
    case 'O': KLASS_CASE(set_weakArrayKlass_vtbl)
    case 'P': KLASS_CASE(set_processKlass_vtbl)
    case 'Q': KLASS_CASE(set_doubleValueArrayKlass_vtbl)
    case 'R': KLASS_CASE(set_vframeKlass_vtbl)
    // Objects
    case 'a': OBJECT_ERROR("klass")
    case 'b': OBJECT_ERROR("smi")
    case 'c': OBJECT_CASE(memOop);
    case 'd': OBJECT_CASE(byteArrayOop);
    case 'e': OBJECT_CASE(doubleByteArrayOop);
    case 'f': OBJECT_CASE(objArrayOop)
    case 'g': SYMBOL_CASE(symbolOop);
    case 'h': OBJECT_CASE(doubleOop);
    case 'i': OBJECT_CASE(associationOop);
    case 'j': OBJECT_CASE(methodOop);
    case 'k': OBJECT_ERROR("blockClosure")
    case 'l': OBJECT_ERROR("context")
    case 'm': OBJECT_ERROR("proxy")
    case 'n': OBJECT_CASE(mixinOop)
    case 'o': OBJECT_ERROR("weakArrayOop")
    case 'p': OBJECT_CASE(processOop)
    default: fatal("unknown object type");
  }

  if (type == 'H') {
    // the image gives the 32-bit size of Double instances (header + 2 words)
    klassOop(m)->klass_part()->set_non_indexable_size(doubleOopDesc::header_size());
  }
  if (capacity != size || type == 'h') {
    // doubles: the Double class may still be being read (its size is fixed above when done)
    shrink(m, capacity, type == 'h' ? doubleOopDesc::object_size() : m->size());
  } else if (m->size() != size) {
    // all other objects must have the same number of words in both layouts
    fatal3("bootstrap: object of type '%c' has %d words in the image but needs %d", type, size, m->size());
  }

  return m;
}

void bootstrap::close_file() {
  fclose(stream);
}

void bootstrap::read_mark(markOop* mark_addr) {
  char type = get_char();
  markOop m;
  if      (type == '1') m = markOopDesc::untagged_prototype();
  else if (type == '2') m = markOopDesc::tagged_prototype();
  else fatal("expecting a markup");
  *mark_addr = m;
}

double bootstrap::read_double() {
  double value;
  unsigned char* str = (unsigned char*) &value;
  for (int index = 0; index < 8; index++) {
    position++;
    str[index] = getc(stream);
  }
  return value; 
}
