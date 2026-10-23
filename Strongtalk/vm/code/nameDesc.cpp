/* Copyright 1994 - 1996, LongView Technologies L.L.C. $Revision: 1.19 $ */
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
#include "bytecodes.hpp"
#include "stackChunk.hpp"
#include "iCache.hpp"
#include "location.hpp"
#include "nativeInstruction.hpp"
#include "runtime.hpp"
#include "compiledIC.hpp"
#include "relocInfo.hpp"
#include "codeBuffer.hpp"
#include "assembler.hpp"
#include "x86_mapping.hpp"
#include "nameDesc.hpp"
#include "frame.hpp"
#include "symbolOop.hpp"
#include "methodOop.hpp"
#include "blockOop.hpp"
#include "blockKlass.hpp"
#include "blockOop.inline.hpp"
#include "vframe.hpp"
#include "oopFactory.hpp"
#include "scopeDesc.hpp"
#ifdef DELTA_COMPILER


void LocationNameDesc::print() {
  stdOut->print("@%s (%d)", location().name(), offset);
}

void ValueNameDesc::print() {
  stdOut->print("=");
  value()->print_value();
  stdOut->print(" (%d)", offset);
}

void BlockValueNameDesc::print() {
  stdOut->print("[=");
  block_method()->print_value();
  stdOut->print("]");
  stdOut->print(" (%d)", offset);
}

void MemoizedBlockNameDesc::print() {
  stdOut->print("[@%s =", location().name());
  block_method()->print_value();
  stdOut->print("]");
  stdOut->print(" (%d)", offset);
}

void IllegalNameDesc::print() {
  stdOut->print("###illegal###");
  stdOut->print(" (%d)", offset);
}

bool LocationNameDesc::equal(NameDesc* other) const {
  if (other->isLocation()) {
    return location() == ((LocationNameDesc*) other)->location();
  }
  return false;
}

bool ValueNameDesc::equal(NameDesc* other) const {
  if (other->isValue()) {
    return value() == ((ValueNameDesc*) other)->value();
  }
  return false;
}

bool BlockValueNameDesc::equal(NameDesc* other) const {
  if (other->isBlockValue()) {
    return block_method() == ((BlockValueNameDesc*) other)->block_method()
        && parent_scope()->s_equivalent(((BlockValueNameDesc*) other)->parent_scope());
  }
  return false;
}

bool MemoizedBlockNameDesc::equal(NameDesc* other) const {
  if (other->isMemoizedBlock()) {
    return location()     == ((MemoizedBlockNameDesc*) other)->location()
        && block_method() == ((MemoizedBlockNameDesc*) other)->block_method()
        && parent_scope()->s_equivalent(((MemoizedBlockNameDesc*) other)->parent_scope());
  }
  return false;
}

bool IllegalNameDesc::equal(NameDesc* other) const {
  return other->isIllegal();
}

extern "C" oop allocateBlock(smiOop nofArgs);

// The lexical scope of a block materialized during deoptimization, as the interpreter's
// push_closure bytecodes set it: self for a copying block, otherwise the context or nil
// (canonical_context() answers NULL -- SmallInteger 0 -- if the home allocates no context).
static contextOop materialized_lexical_scope(methodOop block_method, compiledVFrame* vf) {
  if (block_method->block_info() == methodOopDesc::expects_self) return contextOop(vf->receiver());
  contextOop con = vf->canonical_context();
  return con == NULL ? contextOop(nilObj) : con;
}

oop BlockValueNameDesc::value(const frame* fr) const {
  // create a block closure
  if (MaterializeEliminatedBlocks || StackChunkBuilder::is_deoptimizing()) {
    blockClosureOop blk = blockClosureOop(
	 allocateBlock(as_smiOop(block_method()->number_of_arguments())));
    blk->set_method(block_method());

    compiledVFrame* vf = compiledVFrame::new_vframe(fr, parent_scope(), 0);
    blk->set_lexical_scope(materialized_lexical_scope(block_method(), vf));
    return blk;
  } else {
    ResourceMark rm;
    stringStream st(50);
    st.print("eliminated [] in ");
    block_method()->home()->selector()->print_symbol_on(&st);
    return oopFactory::new_symbol(st.as_string());
  }
}

oop MemoizedBlockNameDesc::value(const frame* fr) const {
  // check is the block has been created
  compiledVFrame* vf = fr 
                     ? compiledVFrame::new_vframe(fr, parent_scope(), 0)
		     : NULL;

  oop stored_value = compiledVFrame::resolve_location(location(), vf);
  if (stored_value != uncreatedBlockValue()) {
    return stored_value;
  }
  
  // otherwise do the same as for a BlockValueNameDesc
  if (MaterializeEliminatedBlocks || StackChunkBuilder::is_deoptimizing()) {
    blockClosureOop blk = blockClosureOop(
	 allocateBlock(as_smiOop(block_method()->number_of_arguments())));
    blk->set_method(block_method());

    blk->set_lexical_scope(materialized_lexical_scope(block_method(), vf));
    return blk;
  } else {
    ResourceMark rm;
    stringStream st(50);
    st.print("memoized [] in ");
    block_method()->home()->selector()->print_symbol_on(&st);
    return oopFactory::new_symbol(st.as_string());
  }
}

#endif
