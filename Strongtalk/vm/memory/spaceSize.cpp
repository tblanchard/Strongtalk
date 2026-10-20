/* Copyright 1994, 1995 LongView Technologies L.L.C. $Revision: 1.14 $ */
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
#include "universe.inline.hpp"
#include "symbolTable.hpp"
#include "byteArrayKlass.hpp"
#include "symbolKlass.hpp"
static int scale_and_adjust(int value) {
  int result = roundTo(value * K, Universe::page_size());
  return result;
}

void spaceSizes::initialize() {
  reserved_object_size   = scale_and_adjust(ReservedHeapSize);
  eden_size              = scale_and_adjust(EdenSize);
  surv_size              = scale_and_adjust(SurvivorSize);
  old_size               = scale_and_adjust(OldSize);

  reserved_codes_size    = scale_and_adjust(ReservedCodeSize);
  code_size              = scale_and_adjust(CodeSize);

  reserved_pic_heap_size = scale_and_adjust(ReservedPICSize);
  pic_heap_size          = scale_and_adjust(PICSize);

  jump_table_size        = JumpTableSize;
}

#ifdef unused_but_maybe_useful_later

static int GetNumericEnvironmentVariable(const char* name, int factor, int def) {
  char* n = getenv(name);
  if (n) {
    int l = def;
    if (sscanf(n, "%ld", &l) == 1) {
      def = l * factor;
    } else {
      warning("%s environment variable isn't a number", name);
    }
  }
  return def;
}

static int getSize(const char* name, int def) {
  const int blockSize = 4 * K;
  int size = GetNumericEnvironmentVariable(name, 1024, def);
  return roundTo(size, blockSize);
}

static int getSize(int def) {
  const int blockSize = 4 * K;
  return roundTo(def, blockSize);
}

#endif

