/* Copyright 1994, 1995 LongView Technologies L.L.C. $Revision: 1.26 $ */
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
#ifndef VM_OOPS_OBJARRAYKLASS_HPP_HPP
#define VM_OOPS_OBJARRAYKLASS_HPP_HPP

// This header is self-contained. The inline methods below access
// objArrayOopDesc (objArrayOop(obj)->length() at oop_size(),
// objArrayOopDesc::header_size() at oop_header_size()), so the
// objArrayOopDesc class must be complete here rather than relying on
// every including translation unit to order its includes correctly.
#include "oopsHierarchy.hpp"
#include "types.hpp"
#include "oop.hpp"
#include "asserts.hpp"
#include "smiOop.hpp"
#include "memOop.hpp"
#include "klass.hpp"
#include "klassOop.hpp"
#include "objArrayOop.hpp"
#include "memOopKlass.hpp"

void set_objArrayKlass_vtbl(Klass* k);
class objArrayKlass: public memOopKlass {
 public:
  // allocation properties
  bool can_inline_allocation() const { return false; }

  // creation operation
  oop allocateObjectSize(int size);

  // creates invocation
  klassOop create_subclass(mixinOop mixin, Format format);

  static klassOop create_class(klassOop super_class, mixinOop mixin);

  // Format
  Format format() { return objArray_klass; }

  // allocated an object array for an interpreter PIC.
  static objArrayOop allocate_tenured_pic(int size);

  // Return the oop size for a objArrayOop
  int object_size(int size) const	{ return non_indexable_size() + 1 + size; }

  // Layout
  int length_offset() const		{ return non_indexable_size(); }
  int array_offset() const		{ return non_indexable_size() + 1; }

  friend void set_objArrayKlass_vtbl(Klass* k);

  const char* name() const { return "objArray"; }

  // ALL FUNCTIONS BELOW THIS POINT ARE DISPATCHED FROM AN OOP 
  
  // size operation
  int oop_size(oop obj) const { return object_size(objArrayOop(obj)->length()); }

  // Memory operations
  int  oop_scavenge_contents(oop obj);
  int  oop_scavenge_tenured_contents(oop obj);
  void oop_follow_contents(oop obj);

  // testers
  bool oop_is_objArray()  const { return true; }
  bool oop_is_indexable() const { return true; }

  void oop_short_print_on(oop obj, outputStream* st);

  // iterators
  void oop_oop_iterate(oop obj, OopClosure* blk);
  void oop_layout_iterate(oop obj, ObjectLayoutClosure* blk);

  // Sizing
  int oop_header_size() const { return objArrayOopDesc::header_size(); }
};


#endif // VM_OOPS_OBJARRAYKLASS_HPP_HPP
