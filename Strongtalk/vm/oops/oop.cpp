/* Copyright 1994, 1995 LongView Technologies L.L.C. $Revision: 1.19 $ */
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
#include "symbolOop.hpp"
#include "klassOop.hpp"
#include "universe.inline.hpp"
#include "vmSymbols.hpp"
#include "oop.inline.hpp"
// Called during bootstrapping for computing vtbl values see (create_*Klass)
oopDesc::oopDesc() {
 if (!bootstrapping) ShouldNotCallThis();
}

void oopDesc::print_value_on(outputStream* st) {
  if (is_mark()) {
    markOop(this)->print_on(st);
  } else if (is_smi()) {
    smiOop(this)->print_on(st);
  } else {
#ifdef ASSERT
    // In the debug version unused space is cleared after scavenge.
    // This means if we try printing an oop pointing to unused space
    // its klass() is NULL.
    // The following hack can print such oops.
    if (klass()->addr() == NULL) {
      st->print("Wrong Oop(0x%lx)", this);
    } else
#endif
    blueprint()->oop_print_value_on(this, st);
  }
}

void oopDesc::print_on(outputStream* st) {
  if (is_mark()) {
    markOop(this)->print_on(st);
  } else if (is_smi()) {
    smiOop(this)->print_on(st);
  } else {
    memOop(this)->print_on(st);
  } 
}

void oopDesc::print()       { print_on(stdOut);       }
void oopDesc::print_value() { print_value_on(stdOut); }

char* oopDesc::print_string() {
  stringStream* st = new stringStream(50);
  print_on(st);
  return st->as_string();
}

char* oopDesc::print_value_string() {
  stringStream* st = new stringStream(50);
  print_value_on(st);
  return st->as_string();
}
