/* Copyright 1994, 1995 LongView Technologies L.L.C. $Revision: 1.28 $ */
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
#include "symbolOop.hpp"
#include "oopFactory.hpp"
#include "ageTable.hpp"
#include "universe.inline.hpp"
#include "doubleOop.hpp"
#include "vmSymbols.hpp"
#include "oop.inline.hpp"
#include "doubleKlass.hpp"
#include "markSweep.hpp"
#include "memOop.inline.hpp"
oop doubleKlass::allocateObject() {
  assert(!can_inline_allocation(), "using nonstandard allocation");
  // allocate
  doubleOop obj = as_doubleOop(Universe::allocate(doubleOopDesc::object_size(), (memOop*)&doubleKlassObj));
  // header
  memOop(obj)->initialize_header(false, doubleKlassObj);
  obj->set_value(0.0);
  return obj;
}

klassOop doubleKlass::create_subclass(mixinOop mixin, Format format) {
  return NULL;
}

void set_doubleKlass_vtbl(Klass* k) {
  doubleKlass o;
  k->set_vtbl_value(o.vtbl_value());
}

void doubleKlass::oop_short_print_on(oop obj, outputStream* st) {
  assert_double(obj, "obj must be double");
  st->print("%1.10gd ", doubleOop(obj)->value());
  oop_print_value_on(obj, st);
}

void doubleKlass::oop_print_value_on(oop obj, outputStream* st) {
  assert_double(obj, "obj must be double");
  st->print("%1.10gd", doubleOop(obj)->value());
}

// since klass is tenured scavenge operations are empty.
int doubleKlass::oop_scavenge_contents(oop obj) {
  return doubleOopDesc::object_size();
}

int doubleKlass::oop_scavenge_tenured_contents(oop obj) {
  return doubleOopDesc::object_size();
}

void doubleKlass::oop_follow_contents(oop obj) {
  memOop(obj)->follow_header(); 
}

void doubleKlass::oop_layout_iterate(oop obj, ObjectLayoutClosure* blk) {
  memOop(obj)->layout_iterate_header(blk);
  blk->do_double("value", &doubleOop(obj)->addr()->_value);
}

void doubleKlass::oop_oop_iterate(oop obj, OopClosure* blk) {
  memOop(obj)->oop_iterate_header(blk);
}
