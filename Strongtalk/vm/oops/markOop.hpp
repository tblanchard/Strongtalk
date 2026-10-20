/* Copyright 1994 - 1996 LongView Technologies L.L.C. $Revision: 1.24 $ */
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
#ifndef VM_OOPS_MARKOOP_HPP
#define VM_OOPS_MARKOOP_HPP

#include "oop.hpp"
// Bit-format of markOop (most significant bit first):
//  sentinel:1 near_death:1 tagged_contents:1 age:7 hash:N tag:2 = BitsPerWord bits
//  where N = BitsPerWord - 12 (20 bits on 32-bit hosts, 52 bits on 64-bit hosts).
//  - sentinel is needed during pointer reversal in garbage collection 
//    to distinguish markOops from roots of oops. markOops are used termination
//    elements in the pointer list.
//    The sentinel must be the most significant bit: is_oop_root() relies on it
//    being clear in every (user space) heap address.
//    During normal execution the sentinel is 1 but the but may be used for
//    marking during special vm operations like dependency checking.
//  - near_death is set by the memory system iff weak pointers keep the object alive.
//  - tagged_contents indicates the is no untagged data in the object.
//  - age contains the age of the object when residing in the new generation.
//    (during garbage collection, age is used to store the size of the object).
//    PLEASE DO NOT CHANGE THE AGE FIELD since the garbage collector relies on the size.
//  - hash contains the identity hash value.
//  - tag contains the special mark tag

// Mask of the low 'bits' bits of a word (usable in constant expressions).
constexpr uintptr_t markOop_mask(int bits) { return bits >= BitsPerWord ? ~uintptr_t(0) : (uintptr_t(1) << bits) - 1; }

class markOopDesc: public oopDesc {
 private:
  typedef uintptr_t word;
  word value()  const { return (word) this; }
  friend   smi assign_hash(markOop& m);

  static constexpr int  no_hash               = 0;
  static constexpr int  first_hash            = 1;

  static constexpr int  sentinel_bits         = 1;
  static constexpr int  near_death_bits       = 1;
  static constexpr int  tagged_contents_bits  = 1;
  static constexpr int  age_bits              = 7;
  static constexpr int  hash_bits             = BitsPerWord - sentinel_bits - near_death_bits - tagged_contents_bits - age_bits - Tag_Size;

  static constexpr int  hash_shift            = Tag_Size;
  static constexpr int  age_shift             = hash_bits             + hash_shift;
  static constexpr int  tagged_contents_shift = age_bits              + age_shift;
  static constexpr int  near_death_shift      = tagged_contents_bits  + tagged_contents_shift;
  static constexpr int  sentinel_shift        = near_death_bits       + near_death_shift;
  static_assert(sentinel_shift == BitsPerWord - 1, "sentinel must be the most significant bit");

  static constexpr word hash_mask                     = markOop_mask(hash_bits);
  static constexpr word hash_mask_in_place            = hash_mask << hash_shift;
  static constexpr word age_mask                      = markOop_mask(age_bits);
  static constexpr word age_mask_in_place             = age_mask << age_shift;
  static constexpr word tagged_contents_mask          = markOop_mask(tagged_contents_bits);
  static constexpr word tagged_contents_mask_in_place = tagged_contents_mask << tagged_contents_shift;
  static constexpr word near_death_mask               = markOop_mask(near_death_bits);
  static constexpr word near_death_mask_in_place      = near_death_mask << near_death_shift;
  static constexpr word sentinel_mask                 = markOop_mask(sentinel_bits);
  static constexpr word sentinel_mask_in_place        = sentinel_mask << sentinel_shift;

  static constexpr word no_hash_in_place           = word(no_hash)    << hash_shift;
  static constexpr word first_hash_in_place        = word(first_hash) << hash_shift;
  static constexpr word untagged_contents_in_place = word(1)          << tagged_contents_shift;

  static constexpr word sentinel_is_place = word(1) << sentinel_shift;
 public:
  static constexpr int max_age = (int) age_mask;
  // accessors
  bool    has_sentinel()      const { return maskBits(value(), sentinel_mask_in_place) != 0; }
  markOop set_sentinel()      const { return markOop( sentinel_is_place | value()); } 
  markOop clear_sentinel()    const { return markOop(~sentinel_is_place & value()); }

  bool has_tagged_contents()  const { return maskBits(value(), tagged_contents_mask_in_place) != 0; }

  bool    is_near_death()     const { return maskBits(value(), near_death_mask_in_place) != 0; }
  markOop set_near_death()    const { return markOop( near_death_mask_in_place | value()); } 
  markOop clear_near_death()  const { return markOop(~near_death_mask_in_place & value()); }

  // klass invalidation (via sentinel bit)
  bool    is_klass_invalid()  const { return !has_sentinel();  }
  markOop set_klass_invalid() const { return clear_sentinel(); }
  markOop clear_klass_invalid() const { return set_sentinel(); }

  // tells if context has forward reference to an unoptimized context(via sentinel bit)
  static uintptr_t context_forward_bit_mask() { return near_death_mask_in_place; }
  bool    has_context_forward() const { return is_near_death();  }
  markOop set_context_forward() const { return set_near_death(); }

  // notification queue check
  bool    is_queued()    const { return !has_sentinel();  }
  markOop set_queued()   const { return clear_sentinel(); }
  markOop clear_queued() const { return set_sentinel();   }

  // age operations
  int        age()            const { return (int) (maskBits(value(), age_mask_in_place)  >> age_shift); }
  markOop    set_age(int v)   const {
    assert((v & ~age_mask) == 0, "shouldn't overflow field");
    return markOop((value() & ~age_mask_in_place) | ((word(v) & age_mask) << age_shift));
  }
  markOop    incr_age()       const { return age() == max_age ? markOop(this) : set_age(age() + 1); }

  // hash operations
  smi        hash()           const { return (smi) (maskBits(value(), hash_mask_in_place) >> hash_shift); }
  markOop    set_hash(smi v)  const {
    if ((v & hash_mask) == 0) v = first_hash; // avoid no_hash
    markOop val = markOop((value() & ~hash_mask_in_place) | ((word(v) & hash_mask) << hash_shift));
    assert(val->hash() != no_hash, "should have hash now");
    return val;
  }
  bool       has_valid_hash() const { return hash() != no_hash; }

  // markOop prototypes
  static markOop tagged_prototype() {
    return markOop(sentinel_is_place | no_hash_in_place | Mark_Tag);
  }
  static markOop untagged_prototype() {
    return markOop(sentinel_is_place | untagged_contents_in_place | no_hash_in_place | Mark_Tag);
  }

  // badOop
  static markOop bad() {
    return markOop(sentinel_is_place | first_hash_in_place | Mark_Tag);
  }

  friend smi hash_markOop(markOop& m) {
    smi v = m->hash();
    return v == no_hash ? assign_hash(m) : v;
  }

  // printing
  void print_on(outputStream* st);

  static smi masked_hash(smi v) { return (smi) (word(v) & hash_mask); }
};

# define badOop markOopDesc::bad()

// tells whether p is a root to an oop or a markOop
// used during pointer reversal during GC.
inline bool is_oop_root(oop* p) { return !markOop(p)->has_sentinel(); }

#endif // VM_OOPS_MARKOOP_HPP
