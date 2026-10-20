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
#ifndef VM_OOPS_BYTECODETRANSCODER_HPP
#define VM_OOPS_BYTECODETRANSCODER_HPP

#include "config.hpp"
#include "oopsHierarchy.hpp"
#include "allocation.hpp"
#include "util.hpp"
#include "bits.hpp"
#include "tag.hpp"
#include "bytecodes.hpp"
#include <vector>
#include <stdlib.h>

// The VM does not allow the global operator new; containers allocate on the C heap.
template <class T> struct CHeapAllocator {
  typedef T value_type;
  CHeapAllocator() {}
  template <class U> CHeapAllocator(const CHeapAllocator<U>&) {}
  T*   allocate(size_t n)        { return (T*) malloc(n * sizeof(T)); }
  void deallocate(T* p, size_t)  { free(p); }
  template <class U> bool operator==(const CHeapAllocator<U>&) const { return true; }
  template <class U> bool operator!=(const CHeapAllocator<U>&) const { return false; }
};
template <class T> using cvector = std::vector<T, CHeapAllocator<T> >;

// Method byte codes contain oop and long operands in word sized slots that are
// aligned to the word size (formats marked '*' in bytecodes.hpp). The slot size
// therefore determines the code layout:
//
//   external layout: 4-byte slots. This is the layout of methods in .bst image
//                    files and of methods produced by the image's Smalltalk
//                    compiler (DeltaHCode>>align), i.e. the 32-bit VM layout.
//   native layout:   oopSize slots (8 on 64-bit hosts).
//
// The BytecodeTranscoder converts code from one layout to another. Changing the
// slot size moves instructions, so jump offsets (and the jump_else size stored in
// if-structure bytes) are recomputed; byte-offset jumps that no longer fit are
// relaxed to their word-offset forms. A bci map (old -> new) is provided for
// other data that refers to code positions (e.g. tempInfo bci ranges).

const int external_bytecode_word_size = 4;

// Reads byte codes in a given layout.
class BytecodeSource {
 public:
  virtual int      length()            = 0;	// in bytes
  virtual int      word_size()         = 0;	// operand slot size
  virtual u_char   byte_at(int pos)    = 0;	// 0-based byte position
  virtual oop      oop_at(int pos)     = 0;	// oop operand slot at pos
  virtual intptr_t long_at(int pos)    = 0;	// long operand slot at pos (sign extended)
};

// Writes byte codes in a given layout.
class BytecodeSink {
 public:
  virtual int  word_size()                    = 0;
  virtual void put_byte(int pos, u_char b)    = 0;
  virtual void put_oop(int pos, oop value)    = 0;
  virtual void put_long(int pos, intptr_t v)  = 0;
};

class BytecodeTranscoder {
 public:
  // Decodes the source code. Returns false (with an error message) if the code is malformed.
  BytecodeTranscoder(BytecodeSource* source);

  bool        ok() const      { return _error == NULL; }
  const char* error() const   { return _error; }

  // Computes the layout for the given slot size. Must be called before the queries below.
  void layout(int word_size);
  // Computes the layout the image compiler would have generated for the code with the given
  // slot size: undoes the widening of jumps by an earlier conversion (used for file out).
  void layout_as_compiled(int word_size);

  int  length() const;			// code length in bytes (without end padding)
  int  padded_length() const;		// length rounded up to a whole number of slots

  // Writes the laid out code; the area between length() and padded_length() is filled with halt.
  void emit(BytecodeSink* sink);

  // Maps a bci (1-based byte position) of the source code to the new layout.
  // Positions inside an instruction map to the start of that instruction (end_inclusive = true)
  // or of the next instruction (end_inclusive = false).
  int  map_bci(int bci, bool end_inclusive) const;

 private:
  struct Operand {
    bool     is_oop;
    oop      oop_value;
    intptr_t long_value;
  };
  struct Instr {
    Bytecodes::Code      code;
    cvector<u_char>  lead;		// opcode and bytes before the aligned operands
    Operand              ops[3];
    int                  nops;
    cvector<u_char>  trail;		// bytes after the aligned operands
    int                  src_pos;
    int                  pos;		// position in the new layout
    int                  size;		// size in the new layout
    // jump information (instruction indices; the end of the code is index count)
    int                  target;	// forward/backward jump target, -1 if none
    int                  loop_while;	// jump_loop: index of the while instruction
  };

  cvector<Instr> _instrs;
  int                _src_length;
  int                _src_end;		// end of the decoded instructions (without padding)
  int                _word;
  const char*        _error;

  static void format_shape(Bytecodes::Format f, int* lead, int* nops, int* trail, const char** kinds);
  int  instr_size(const Instr& in, int pos, int word) const;
  int  index_of_src_pos(int pos) const;
  int  position_of(int index) const;
  void widen(Instr& in);
  void narrow(bool while_jumps, bool other_jumps);
  void widen_other_jumps();
  bool relax();
  void fail(const char* msg) { if (_error == NULL) _error = msg; }
};

// Code in the external layout, collected from a .bst stream or from the byte/oop
// arrays given to primitiveConstructMethod. Oop operands are kept separately
// (indexed by slot); long operands are 32-bit little endian values in the bytes.
class ExternalCode : public BytecodeSource {
 private:
  cvector<u_char> _bytes;
  cvector<oop>    _oops;
  cvector<bool>   _has_oop;
 public:
  ExternalCode(int length) : _bytes(length, 0), _oops((length + 3) / 4, (oop) NULL), _has_oop((length + 3) / 4, false) {}

  void set_byte(int pos, u_char b) { _bytes[pos] = b; }
  void set_oop(int pos, oop value) { _oops[pos / 4] = value; _has_oop[pos / 4] = true; }

  bool   has_oop_at(int pos) { return pos % 4 == 0 && _has_oop[pos / 4]; }
  int    length()            { return (int) _bytes.size(); }
  int    word_size()         { return external_bytecode_word_size; }
  u_char byte_at(int pos)    { return _bytes[pos]; }
  oop    oop_at(int pos)     { return _oops[pos / 4]; }
  intptr_t long_at(int pos) {
    intptr_t raw = (intptr_t) _oops[pos / 4];
    if (_has_oop[pos / 4] && (raw & Tag_Mask) == Int_Tag) {
      // a long that was written out as a smi oop: use its (32-bit VM) bit pattern
      return (intptr_t) (int32_t) ((raw >> Tag_Size) << 2);
    }
    uint32_t v = _bytes[pos] | (_bytes[pos + 1] << 8) | (_bytes[pos + 2] << 16) | ((uint32_t) _bytes[pos + 3] << 24);
    return (intptr_t) (int32_t) v;
  }
};

// Collects code written in the external layout (the inverse of ExternalCode): bytes, with
// oop operands kept separately by slot; long operands become 32-bit little endian bytes.
class ExternalCodeSink : public BytecodeSink {
 private:
  cvector<u_char> _bytes;
  cvector<oop>    _oops;
  cvector<bool>   _has_oop;
 public:
  ExternalCodeSink(int length) : _bytes(length, 0), _oops((length + 3) / 4, (oop) NULL), _has_oop((length + 3) / 4, false) {}

  int    length() const          { return (int) _bytes.size(); }
  bool   is_oop_at(int pos) const{ return pos % 4 == 0 && _has_oop[pos / 4]; }
  u_char byte_at(int pos) const  { return _bytes[pos]; }
  oop    oop_at(int pos) const   { return _oops[pos / 4]; }

  int  word_size()                    { return external_bytecode_word_size; }
  void put_byte(int pos, u_char b)    { _bytes[pos] = b; }
  void put_oop(int pos, oop value)    { _oops[pos / 4] = value; _has_oop[pos / 4] = true; }
  void put_long(int pos, intptr_t v) {
    // the 32-bit layout cannot hold wider values (jump offsets and the like never are)
    if (v != (intptr_t) (int32_t) v) fatal("byte code operand does not fit the 32-bit layout");
    uint32_t u = (uint32_t) v;
    for (int i = 0; i < 4; i++) _bytes[pos + i] = (u_char) (u >> (8 * i));
  }
};

// Reads or writes the code of a method in native layout.
class NativeCode : public BytecodeSource, public BytecodeSink {
 private:
  u_char* _codes;
  int     _length;
 public:
  NativeCode(u_char* codes, int length) : _codes(codes), _length(length) {}

  int      length()                     { return _length; }
  int      word_size()                  { return oopSize; }
  u_char   byte_at(int pos)             { return _codes[pos]; }
  oop      oop_at(int pos)              { return *(oop*) (_codes + pos); }
  intptr_t long_at(int pos)             { return *(intptr_t*) (_codes + pos); }
  void     put_byte(int pos, u_char b)  { _codes[pos] = b; }
  void     put_oop(int pos, oop value)  { *(oop*) (_codes + pos) = value; }
  void     put_long(int pos, intptr_t v){ *(intptr_t*) (_codes + pos) = v; }
};

#endif // VM_OOPS_BYTECODETRANSCODER_HPP
