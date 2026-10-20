/* Copyright 1994, 1995 LongView Technologies L.L.C. $Revision: 1.99 $ */
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
#include "klassOop.hpp"
#include "memOopKlass.hpp"
#include "associationOop.hpp"
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
#include "dispatchTable.hpp"
#include "byteArrayOop.hpp"
#include "symbolOop.hpp"
#include "key.hpp"
#include "bytecodes.hpp"
#include "methodOop.hpp"
#include "scopeDesc.hpp"
#include "costModel.hpp"
#include "prim.hpp"
#include "ncode.hpp"
#include "jumpTable.hpp"
#include "codeTable.hpp"
#include "zoneHeap.hpp"
#include "zone.hpp"
#include "nmethodScopes.hpp"
#include "os.hpp"
#include "dll.hpp"
#include "codeIterator.hpp"
#include "objArrayOop.hpp"
#include "universe.inline.hpp"
#include "vmSymbols.hpp"
#include "oop.inline.hpp"
#include "ic_iterator.hpp"
#include "frame.hpp"
#include "blockOop.hpp"
#include "blockKlass.hpp"
#include "nmethod.hpp"
#include "lookupCache.hpp"
#include "prettyPrinter.hpp"
#include "oopFactory.hpp"
#include "interpretedIC.hpp"
#include "floats.hpp"
#include "methodIterator.hpp"
#include "bootstrap.hpp"
#include "methodPrinter.hpp"
#include "mixinOop.hpp"
#include "methodKlass.hpp"
#include "bytecodeTranscoder.hpp"
#include "bootstrap.hpp"
void methodOopDesc::decay_invocation_count(double decay_factor) {
  double new_count = (double) invocation_count() / decay_factor;
  set_invocation_count((int) new_count);

  // Take care of the block methods
  CodeIterator c(this);
  do {
    switch(c.code()) {
      case Bytecodes::push_new_closure_tos_0:      // fall through
      case Bytecodes::push_new_closure_tos_1:      // fall through
      case Bytecodes::push_new_closure_tos_2:      // fall through
      case Bytecodes::push_new_closure_context_0:  // fall through
      case Bytecodes::push_new_closure_context_1:  // fall through
      case Bytecodes::push_new_closure_context_2: {
          methodOop block_method = methodOop(c.oop_at(1));
	  assert(block_method->is_method(), "must be method");
          block_method->decay_invocation_count(decay_factor);
	}
        break;
      case Bytecodes::push_new_closure_tos_n:      // fall through
      case Bytecodes::push_new_closure_context_n: {
          methodOop block_method = methodOop(c.oop_at(2));
	  assert(block_method->is_method(), "must be method");
          block_method->decay_invocation_count(decay_factor);
	}
        break;
    }
  } while (c.advance());
}

void methodOopDesc::inc_sharing_count() {
  if (sharing_count() < _sharing_count_max) {
    set_sharing_count(sharing_count()+1);
  }
}

void methodOopDesc::dec_sharing_count() {
  if (sharing_count() > 0) {
    set_sharing_count(sharing_count()-1);
  }
}

void methodOopDesc::bootstrap_object(bootstrap* st) {
  int capacity = st->current_capacity();	// must be read before any field
  memOopDesc::bootstrap_header(st);
  st->read_oop((oop*)&addr()->_debugInfo);
  st->read_oop((oop*)&addr()->_selector_or_method);
  set_counters(0, 0);
  st->read_oop((oop*)&addr()->_size_and_flags);

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

  if (VerifyFileOut) remember_loaded_code(this, code);

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
  remap_temp_info_bcis(objArrayOop(info), t);
}

void methodOopDesc::remap_temp_info_bcis(objArrayOop tempInfo, BytecodeTranscoder* t) {
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
    // smis need no store check (and cannot be verified while bootstrapping)
    *tempInfo->objs(pos)     = as_smiOop(t->map_bci((int) smiOop(begin)->value(), false));
    *tempInfo->objs(pos + 1) = as_smiOop(t->map_bci((int) smiOop(end)->value(),   true));
    pos += 2;
    for (int part = 0; part < 2; part++) {	// oop temps, float temps
      if (pos > len) return;
      pos++;	// offset
      while (pos <= len && tempInfo->obj_at(pos)->is_byteArray()) pos++;
    }
  }
}

int methodOopDesc::next_bci_from(u_char* hp) const {
  // Computes the next bci
  // hp is the interpreter 'ip' kept in the activation
  // pointing to the next code to execute.

  // Fist the next bci is computed. Note the first index is 1.
  return (hp - (u_char*) addr()) - sizeof(methodOopDesc) + 1;
}

int methodOopDesc::bci_from(u_char* hp) const {
  // We find the current bci by searching from the beginning
  return find_bci_from(next_bci_from(hp));
}


int methodOopDesc::number_of_arguments() const {
  assert(is_blockMethod() || selector()->number_of_arguments() == nofArgs(), "just checking");
  return nofArgs();
}


int methodOopDesc::number_of_stack_temporaries() const {
  int n = 1;		// temporary 0 is always there
  u_char b0 = *codes(1);// if there's more than one temporary there's an allocate temp or allocate float at the beginning
  switch (b0) {
    case Bytecodes::allocate_temp_1: n += 1; break;
    case Bytecodes::allocate_temp_2: n += 2; break;
    case Bytecodes::allocate_temp_3: n += 3; break;
    case Bytecodes::allocate_temp_n: {
        u_char b1 = *codes(2);
        n += ((b1 == 0) ? 256 : b1);
      }
      break;
    case Bytecodes::float_allocate: {
        // One additional temp (temp1) for Floats::magic + additional
	// temps allocated in pairs to match to match one float temp.
        u_char b1 = *codes(2);
	n += 1 + b1*2;
      }
      break;
  }
  return n;
}

int methodOopDesc::float_offset(int float_no) const {
  assert(0 <= float_no && float_no < number_of_float_temporaries(), "float_no out of range");
  return float_section_start_offset() - float_no*floatSlotWords - 1;
}

symbolOop methodOopDesc::enclosing_method_selector() const {
  assert(is_blockMethod(), "must be block method");
  methodOop m = parent();
  while (m->is_blockMethod()) m = m->parent();
  return m->selector();
}

void methodOopDesc::print_value_for(klassOop receiver_klass, outputStream* st) {
  outputStream* s = st ? st : stdOut;
  if (is_blockMethod()) {
    st->print("[] in ");
    enclosing_method_selector()->print_symbol_on(st);
  } else {
    selector()->print_symbol_on(s);
  }
  klassOop holder = receiver_klass->klass_part()->lookup_method_holder_for(this);
  if (holder) {
    s->print(" in ");
    holder->klass_part()->print_name_on(s);
  }
}

void methodOopDesc::print_codes() {
  ResourceMark rm;
  selector()->print_symbol_on(stdOut);
  stdOut->cr();
  MethodPrinterClosure blk(stdOut);
  MethodIterator mi(this, &blk);
  stdOut->cr();
}


void methodOopDesc::pretty_print() {
  ResourceMark rm;
  prettyPrinter::print(this);
}


symbolOop methodOopDesc::selector() const {
  if (selector_or_method()->is_symbol()) return symbolOop(selector_or_method());
  return vmSymbols::selector_for_blockMethod();
}


methodOop methodOopDesc::parent() const {
  oop t = selector_or_method();
  return t->is_method() ? methodOop(t) : NULL;
}


methodOop methodOopDesc::home() const {
  methodOop m = methodOop(this);
  while (m->is_blockMethod()) m = m->parent();
  return m;
}


byteArrayOop methodOopDesc::source() {
  return oopFactory::new_symbol("<no source>");
}


objArrayOop methodOopDesc::tempInfo() {
  return debugInfo();
}

// File out: methods are written in the external (32-bit) layout, i.e. the layout read by
// bootstrap_object. Inline caches and primitive/DLL call sites are written in their
// unlinked form, so the code does not depend on the state of this VM.

// The method's code in native layout, with all call sites unlinked (C heap copy).
class CanonicalCode {
 public:
  cvector<u_char> bytes;

  CanonicalCode(methodOop m) : bytes(m->codes(), m->codes_end()) {
    _codes = m->codes();
    CodeIterator c(m);
    do {
      if (Bytecodes::send_type(c.code()) != Bytecodes::no_send) {
        // send: original send code; the inline cache holds the selector and 0
        u_char* ic = (u_char*) c.ic();
        put_byte(c.hp(), Bytecodes::original_send_code_for(c.code()));
        put_word(ic,           (intptr_t) c.ic()->selector());
        put_word(ic + oopSize, (intptr_t) smiOop_zero);
      } else if (c.is_primitive_call()) {
        // primitive call: lookup form, the first operand is the primitive's selector
        Bytecodes::Format f = Bytecodes::format(c.code());
        put_byte(c.hp(), Bytecodes::original_primitive_call_code_for(c.code()));
        if (f == Bytecodes::BL || f == Bytecodes::BLL) {	// linked: the operand is the entry point
          primitive_desc* pdesc = primitives::lookup((fntype) c.word_at(1));
          put_word((u_char*) c.aligned_oop(1), (intptr_t) pdesc->selector());
        }
      } else if (c.is_dll_call()) {
        // DLL call: no entry point (dll name, function name, entry point, number of arguments)
        put_word((u_char*) c.dll_cache() + 2 * oopSize, 0);
      }
    } while (c.advance());
  }

 private:
  u_char* _codes;	// the method's code; positions in the copy are the same
  void put_byte(u_char* p, u_char b)       { bytes[p - _codes] = b; }
  void put_word(u_char* p, intptr_t value) { *(intptr_t*) &bytes[p - _codes] = value; }
};

// The method's code converted to the external layout.
class ExternalForm {
 public:
  CanonicalCode       canonical;
  NativeCode          native;
  BytecodeTranscoder  transcoder;

  ExternalForm(methodOop m)
    : canonical(m),
      native(&canonical.bytes[0], (int) canonical.bytes.size()),
      transcoder(&native) {
    if (!transcoder.ok()) fatal1("file out: cannot convert byte codes: %s", transcoder.error());
    // jumps widened to fit the native layout get their original form again
    transcoder.layout_as_compiled(external_bytecode_word_size);
  }

  int size_of_codes() const { return transcoder.padded_length() / external_bytecode_word_size; }
};

objArrayOop methodOopDesc::fileout_body() {
  BlockScavenge bs;
  ResourceMark rm;
  return oopFactory::new_objArray(fileout_pairs());
}

// The pairs of fileout_body (resource allocated):
// (true, SmallInteger) is a byte, (false, oop) an oop slot.
GrowableArray<oop>* methodOopDesc::fileout_pairs() {
  ExternalForm form(this);
  ExternalCodeSink sink(form.transcoder.padded_length());
  form.transcoder.emit(&sink);
  if (!form.transcoder.ok()) fatal1("file out: cannot convert byte codes: %s", form.transcoder.error());

  GrowableArray<oop>* result = new GrowableArray<oop>(2 * sink.length());
  for (int pos = 0; pos < sink.length(); ) {
    if (sink.is_oop_at(pos)) {
      result->append(falseObj);
      result->append(sink.oop_at(pos));
      pos += external_bytecode_word_size;
    } else {
      result->append(trueObj);
      result->append(as_smiOop(sink.byte_at(pos)));
      pos++;
    }
  }
  return result;
}

int methodOopDesc::fileout_size_of_codes() {
  ResourceMark rm;
  ExternalForm form(this);
  return form.size_of_codes();
}

int methodOopDesc::fileout_object_size() {
  // the header has the same number of words in both layouts
  return header_size() + fileout_size_of_codes();
}

smiOop methodOopDesc::fileout_size_and_flags() {
  int size = fileout_size_of_codes();
  return (smiOop) (intptr_t)
    ((flags() << method_flags_mask_bitno) + (nofArgs() << method_args_mask_bitno) + (size << method_size_mask_bitno));
}

objArrayOop methodOopDesc::fileout_debug_info() {
  objArrayOop info = debugInfo();
  if (info == NULL || !info->is_objArray()) return info;
  BlockScavenge bs;
  ResourceMark rm;
  ExternalForm form(this);
  objArrayOop copy = info->copy();
  remap_temp_info_bcis(copy, &form.transcoder);
  return copy;
}

// Checks that the file out form of the method reads back (as in bootstrap_object) into its
// unlinked native code, and that sizes and debug info agree. Returns false (and prints) if not.
bool methodOopDesc::verify_fileout() {
  BlockScavenge bs;
  ResourceMark rm;
  ExternalForm form(this);
  GrowableArray<oop>* pairs = fileout_pairs();

  // read the pairs back like bootstrap_object
  int external_length = fileout_size_of_codes() * external_bytecode_word_size;
  ExternalCode code(external_length);
  int index = 0;
  for (int i = 0; i < pairs->length(); i += 2) {
    if (index >= external_length) { index = -1; break; }
    if (pairs->at(i) == trueObj) {
      code.set_byte(index, (u_char) smiOop(pairs->at(i + 1))->value());
      index++;
    } else {
      code.set_oop(index, pairs->at(i + 1));
      index += external_bytecode_word_size;
    }
  }
  const char* problem = NULL;
  if (index != external_length) problem = "body length does not match the size";
  if (problem == NULL && fileout_object_size() != header_size() + fileout_size_of_codes()) problem = "object size";
  if (problem == NULL && get_unsigned_bitfield((long) fileout_size_and_flags(), method_size_mask_bitno, method_size_mask_size) != fileout_size_of_codes())
    problem = "size and flags";

  BytecodeTranscoder back(&code);
  if (problem == NULL && !back.ok()) problem = back.error();
  if (problem == NULL) {
    back.layout(oopSize);
    if (back.padded_length() != (int) form.canonical.bytes.size()) problem = "code size after read back";
  }
  if (problem == NULL) {
    cvector<u_char> native(back.padded_length(), 0);
    NativeCode sink(&native[0], (int) native.size());
    back.emit(&sink);
    if (!back.ok()) problem = back.error();
    else if (native != form.canonical.bytes) problem = "code differs after read back";
  }
  if (problem == NULL) {
    // bcis: native -> external (fileout_debug_info) -> native (as when read back) is the identity
    objArrayOop info = debugInfo();
    if (info != NULL && info->is_objArray()) {
      objArrayOop copy = fileout_debug_info();
      remap_temp_info_bcis(copy, &back);
      for (int i = 1; i <= info->length(); i++) {
        if (copy->obj_at(i) != info->obj_at(i)) { problem = "debug info bcis"; break; }
      }
    }
  }
  if (problem != NULL) {
    stdOut->print("file out check failed (%s): ", problem);
    if (is_blockMethod()) stdOut->print("[] in ");
    (is_blockMethod() ? enclosing_method_selector() : selector())->print_symbol_on(stdOut);
    stdOut->cr();
    return false;
  }
  return true;
}

class MethodCollector : public ObjectClosure {
 public:
  GrowableArray<methodOop>* methods;
  MethodCollector() { methods = new GrowableArray<methodOop>(50000); }
  void do_object(memOop obj) { if (obj->is_method()) methods->append(methodOop(obj)); }
};

void methodOopDesc::verify_fileout_all() {
  BlockScavenge bs;
  ResourceMark rm;
  MethodCollector collector;
  Universe::object_iterate(&collector);
  int failures = 0;
  for (int i = 0; i < collector.methods->length(); i++) {
    if (!collector.methods->at(i)->verify_fileout()) failures++;
  }
  stdOut->print_cr("file out check: %d methods, %d failures", collector.methods->length(), failures);
}

// +VerifyFileOut: the code of the methods as read from the image, to compare with their file out form.
struct LoadedMethod {
  methodOop    method;
  ExternalCode code;
  LoadedMethod(methodOop m, const ExternalCode& c) : method(m), code(c) {}
};
static cvector<LoadedMethod> loaded_methods;

void methodOopDesc::remember_loaded_code(methodOop m, const ExternalCode& code) {
  loaded_methods.push_back(LoadedMethod(m, code));
}

// Compares the file out form of the methods read from the image with their code in the image file.
// A long operand may be written as bytes or as a SmallInteger (old file outs); both are accepted.
void methodOopDesc::verify_fileout_against_image() {
  BlockScavenge bs;
  int differences = 0;
  for (int i = 0; i < (int) loaded_methods.size(); i++) {
    ResourceMark rm;
    LoadedMethod& lm = loaded_methods[i];
    ExternalCode& original = lm.code;
    GrowableArray<oop>* pairs = lm.method->fileout_pairs();
    ExternalCode written(lm.method->fileout_size_of_codes() * external_bytecode_word_size);
    int index = 0;
    for (int k = 0; k < pairs->length() && index < written.length(); k += 2) {
      if (pairs->at(k) == trueObj) { written.set_byte(index, (u_char) smiOop(pairs->at(k + 1))->value()); index++; }
      else                         { written.set_oop(index, pairs->at(k + 1)); index += external_bytecode_word_size; }
    }
    int diff_at = original.length() == written.length() ? -1 : 0;
    for (int pos = 0; diff_at < 0 && pos < original.length(); ) {
      if (pos % 4 == 0 && (original.has_oop_at(pos) || written.has_oop_at(pos))) {
        bool same;
        if (original.has_oop_at(pos) && written.has_oop_at(pos)) same = original.oop_at(pos) == written.oop_at(pos);
        else same = original.long_at(pos) == written.long_at(pos);
        if (!same) diff_at = pos;
        pos += 4;
      } else {
        if (original.byte_at(pos) != written.byte_at(pos)) diff_at = pos;
        pos++;
      }
    }
    if (diff_at >= 0) {
      if (differences++ < 20) {
        stdOut->print("file out differs from the image at byte %d (length %d, image %d): ", diff_at, written.length(), original.length());
        methodOop m = lm.method;
        if (m->is_blockMethod()) stdOut->print("[] in ");
        (m->is_blockMethod() ? m->enclosing_method_selector() : m->selector())->print_symbol_on(stdOut);
        stdOut->cr();
      }
    }
  }
  stdOut->print_cr("file out vs. image: %d methods, %d differ", (int) loaded_methods.size(), differences);
  loaded_methods.clear();
  loaded_methods.shrink_to_fit();
}

methodOopDesc::Method_Inlining_Info methodOopDesc::method_inlining_info() const {
  if (is_blockMethod()) return normal_inline;
  Method_Inlining_Info info = Method_Inlining_Info(get_unsigned_bitfield(flags(), methodInfoFlags, methodInfoSize));
  return info;
}

void methodOopDesc::set_method_inlining_info(Method_Inlining_Info info) {
  if (is_blockMethod()) return;
  set_flags(set_unsigned_bitfield(flags(), methodInfoFlags, methodInfoSize, info));
}

methodOopDesc::Block_Info methodOopDesc::block_info() const {
  assert(is_blockMethod(), "must be a block");
  return Block_Info(get_unsigned_bitfield(flags(), blockInfoFlags, blockInfoSize));
}

bool methodOopDesc::in_context_allocation(int bci) const {
  CodeIterator c(methodOop(this), bci);
  return c.code_type() == Bytecodes::new_context;
}

class BlockFinderClosure : public SpecializedMethodClosure {
public:
  bool hasBlock;
  BlockFinderClosure() { hasBlock = false; }
  void allocate_closure(AllocationType type, int nofArgs, methodOop meth) { hasBlock = true; }
};

bool methodOopDesc::hasNestedBlocks() const {
  // should be a bit in the methodOop -- fix this, Robert (delete class above)
  BlockFinderClosure cl;
  MethodIterator it(methodOop(this), &cl);
  return cl.hasBlock;
}


// The following two functions map context numbers (as used in the interpreter
// to access temps in enclosing scopes) to source-level lexical distances, and
// vice versa.
// Definitions: context no = number of indirections through contexts needed to
//			     access temporary (0 -> temp is in current context)
//		lex. dist. = difference in nesting levels between two scopes;
//			     e.g., distance between a scope and its immediately
//			     enclosing scope is 1

int methodOopDesc::lexicalDistance(int contextNo) {
  methodOop m = this;
  int c = -1;
  int d = -1;
  while (c < contextNo) {
    if (m->allocatesInterpretedContext()) c++;
    m = m->parent();
    d++;
  };
  return d;
}


int methodOopDesc::contextNo(int lexicalDistance) {
  methodOop m = this;
  int c = -1;
  int d = -1;
  while (d < lexicalDistance) {
    if (m->allocatesInterpretedContext()) c++;
    m = m->parent();
    d++;
  }
  return c;
}

int methodOopDesc::context_chain_length() const {
  int length = 0;
  for (methodOop method = methodOop(this); method; method = method->parent()) {
    if (method->allocatesInterpretedContext())
      length++;
  }
  return length;
}

void methodOopDesc::clear_inline_caches() {
  // %skim the cream:
  //    if the method is not customized it has never been executed.
  if (!is_customized()) return;

  CodeIterator c(this);
  do {
    InterpretedIC* ic = c.ic();
    if (ic) {
      ic->clear();
    } else {
      // Call it for blocks
      switch(c.code()) {
      case Bytecodes::push_new_closure_tos_0:      // fall through
      case Bytecodes::push_new_closure_tos_1:      // fall through
      case Bytecodes::push_new_closure_tos_2:      // fall through
      case Bytecodes::push_new_closure_context_0:  // fall through
      case Bytecodes::push_new_closure_context_1:  // fall through
      case Bytecodes::push_new_closure_context_2: {
          methodOop block_method = methodOop(c.oop_at(1));
	  assert(block_method->is_method(), "must be method");
	  block_method->clear_inline_caches();
        }
        break;
      case Bytecodes::push_new_closure_tos_n:      // fall through
      case Bytecodes::push_new_closure_context_n: {
          methodOop block_method = methodOop(c.oop_at(2));
  	  assert(block_method->is_method(), "must be method");
	  block_method->clear_inline_caches();
        }
        break;
      }
    }
  } while (c.advance());
}

void methodOopDesc::cleanup_inline_caches() {
  // %skim the cream:
  //    if the method is not customized it has never been executed.
  if (!is_customized()) return;

  CodeIterator c(this);
  do {
    InterpretedIC* ic = c.ic();
    if (ic) {
      ic->cleanup();
    } else {
      methodOop bm = c.block_method();
      if (bm) {
        bm->cleanup_inline_caches();
      }
    }
  } while (c.advance());
}


bool methodOopDesc::was_never_executed() {
  // skim the cream: if the method is not customized it has never been ecexuted.
  if (!is_customized()) return true;

  // return true if method looks like it was never executed
  if (invocation_count() != 0 || sharing_count() != 0) return false;
  CodeIterator c(this);
  do {
    InterpretedIC* ic = c.ic();
    if (ic && !ic->is_empty()) return false;
  } while (c.advance());
  return true;
}

int methodOopDesc::estimated_inline_cost(klassOop receiverKlass) {
  // the result of this calculation should be cached in the method; 8 bits are enough
  CodeIterator c(this);
  int cost = 0;
  do {
    cost += CostModel::cost_for(c.code());
    switch (c.code()) {
      case Bytecodes::push_new_closure_context_0:
      case Bytecodes::push_new_closure_context_1:
      case Bytecodes::push_new_closure_context_2:
      case Bytecodes::push_new_closure_tos_0:
      case Bytecodes::push_new_closure_tos_1:
      case Bytecodes::push_new_closure_tos_2: {
        methodOop m = methodOop(c.oop_at(1));
	assert(m->is_method(), "must be method");
	cost += m->estimated_inline_cost(receiverKlass);
	break;
      }
      case Bytecodes::push_new_closure_tos_n:
      case Bytecodes::push_new_closure_context_n: {
        methodOop m = methodOop(c.oop_at(2));
	assert(m->is_method(), "must be method");
	cost += m->estimated_inline_cost(receiverKlass);
	break;
      }
    }
    extern bool SuperSendsAreAlwaysInlined;
    if (Bytecodes::is_super_send(c.code()) && SuperSendsAreAlwaysInlined && receiverKlass) {
      klassOop mh = receiverKlass->klass_part()->lookup_method_holder_for(this);
      methodOop superMethod = mh ? lookupCache::compile_time_super_lookup(mh, selector()) : NULL;
      if (superMethod) cost += superMethod->estimated_inline_cost(receiverKlass);
    }
  } while (c.advance());
  return cost;
}


int methodOopDesc::find_bci_from(int nbci) const {
  CodeIterator c(methodOop(this));
  int prev_bci = 1;
  do {
    if (c.bci() == nbci) return prev_bci; 
    prev_bci = c.bci();
  } while (c.advance());
  return -1;
}


int methodOopDesc::next_bci(int bci) const {
  CodeIterator c(methodOop(this), bci);
  c.advance();
  return c.bci();
}


class ExpressionStackMapper : public MethodClosure {
 private:
   GrowableArray<int>* mapping;
   int target_bci;

   void map_push() { map_push(bci()); }

   void map_push(int b)  {
     // lprintf("push(%d)", bci);
     if (b >= target_bci) {
       abort();
     } else {
       mapping->push(b);
     }
   }

   void map_pop() {
     if (bci() >= target_bci) {
       abort();
     } else {
       // lprintf("pop(%d)", bci());
       mapping->pop();
     }
   }

   void map_send(bool has_receiver, int number_of_arguments) {
     if (has_receiver) map_pop();
     for(int i = 0; i < number_of_arguments; i++) map_pop();
     map_push();
   }

 public:
  ExpressionStackMapper(GrowableArray<int>* mapping, int target_bci) {
    this->mapping    = mapping;
    this->target_bci = target_bci;
  }

  void push_self()				{ map_push(); }
  void push_tos()				{ map_push(); }  
  void push_literal(oop obj)			{ map_push(); }
  void push_argument(int no)			{ map_push(); }
  void push_temporary(int no)			{ map_push(); }
  void push_temporary(int no, int context)	{ map_push(); }
  void push_instVar(int offset)	                { map_push(); }
  void push_instVar_name(symbolOop name)	{ map_push(); }
  void push_classVar(associationOop assoc)	{ map_push(); }
  void push_classVar_name(symbolOop name)	{ map_push(); }

  void push_global(associationOop obj)		{ map_push(); }

  void pop()					{ map_pop(); }

  void normal_send(InterpretedIC* ic)		{ map_send(true,  ic->selector()->number_of_arguments()); }
  void self_send(InterpretedIC* ic)		{ map_send(false, ic->selector()->number_of_arguments()); }
  void super_send(InterpretedIC* ic)		{ map_send(false, ic->selector()->number_of_arguments()); }

  void double_equal()				{ map_send(true, 1); }
  void double_not_equal()			{ map_send(true, 1); }

  void method_return(int nofArgs)		{ map_pop(); }
  void nonlocal_return(int nofArgs)		{ map_pop(); }

  void allocate_closure(AllocationType type, int nofArgs, methodOop meth) {
    if (type == tos_as_scope) map_pop(); 
    map_push();
  }

  // nodes
  void if_node(IfNode* node);
  void cond_node(CondNode* node);
  void while_node(WhileNode* node);
  void primitive_call_node(PrimitiveCallNode* node);
  void dll_call_node(DLLCallNode* node);

  // call backs to ignore
  void allocate_temporaries(int nofTemps)                          {}
  void store_temporary(int no)                                     {}
  void store_temporary(int no, int context)                        {}
  void store_instVar(int offset)                                   {}
  void store_instVar_name(symbolOop name)                          {}
  void store_classVar(associationOop assoc)                        {}
  void store_classVar_name(symbolOop name)                         {}

  void store_global(associationOop obj)                            {} 
  void allocate_context(int nofTemps, bool forMethod = false)      {}
  void set_self_via_context()                                      {}
  void copy_self_into_context()                                    {}
  void copy_argument_into_context(int argNo, int no)               {}
  void zap_scope()                                                 {}
  void predict_prim_call(primitive_desc* pdesc, int failure_start) {}
  void float_allocate(int nofFloatTemps, int nofFloatExprs)	   {}
  void float_floatify(Floats::Function f, int tof)		   { map_pop(); }
  void float_move(int tof, int from)				   {}
  void float_set(int tof, doubleOop value)			   {}
  void float_nullary(Floats::Function f, int tof)		   {}
  void float_unary(Floats::Function f, int tof)			   {}
  void float_binary(Floats::Function f, int tof)		   {}
  void float_unaryToOop(Floats::Function f, int tof)		   { map_push(); }
  void float_binaryToOop(Floats::Function f, int tof)		   { map_push(); }
};


void ExpressionStackMapper::if_node(IfNode* node) {
  if (node->includes(target_bci)) {
    if (node->then_code()->includes(target_bci)) {
      map_pop();
      MethodIterator i(node->then_code(), this);
    } else if (node->else_code() && node->else_code()->includes(target_bci)) {
      map_pop();
      MethodIterator i(node->else_code(), this);
    }
    abort();
  } else {
    map_pop();
    if (node->produces_result())
      map_push(node->begin_bci());
  }
}


void ExpressionStackMapper::cond_node(CondNode* node) {
  if (node->includes(target_bci)) {
    if (node->expr_code()->includes(target_bci)) {
      map_pop();
      MethodIterator i(node->expr_code(), this);
    }
    abort();
  } else {
    map_pop();
    map_push(node->begin_bci());
  }
}


void ExpressionStackMapper::while_node(WhileNode* node) {
  if (node->includes(target_bci)) {
    if (node->expr_code()->includes(target_bci))
      MethodIterator i(node->expr_code(), this);
    else if (node->body_code() && node->body_code()->includes(target_bci))
      MethodIterator i(node->body_code(), this);
    abort();
  }
}


void ExpressionStackMapper::primitive_call_node(PrimitiveCallNode* node) {
  int nofArgsToPop = node->number_of_parameters();
  for(int i = 0; i < nofArgsToPop; i++) map_pop();

  map_push();
  if (node->failure_code() && node->failure_code()->includes(target_bci)) {
    MethodIterator i(node->failure_code(), this);
  }
}


void ExpressionStackMapper::dll_call_node(DLLCallNode* node) {
  int nargs = node->nofArgs();
  for (int index = 0; index < nargs; index++)
    map_pop();
}


GrowableArray<int>* methodOopDesc::expression_stack_mapping(int bci) {
  GrowableArray<int>* mapping = new GrowableArray<int>(10);
  ExpressionStackMapper blk(mapping, bci);
  MethodIterator i(this, &blk);

  // reverse the mapping so the top of the expression stack is first
  // %todo:
  //    move reverse to GrowableArray

  GrowableArray<int>* result = new GrowableArray<int>(mapping->length());
  for (int index = mapping->length() - 1; index >= 0; index--) {
    result->push(mapping->at(index));
  }
  return result;
}


static void lookup_primitive_and_patch(u_char* p, u_char byte) {
  assert((intptr_t)p % oopSize == 0, "first instruction supposed to be aligned");
  *p = byte;	// patch byte
  p += oopSize;	// advance to primitive name
  //(*(symbolOop*)p)->print_symbol_on();
  *(intptr_t*)p = (intptr_t)primitives::lookup(*(symbolOop*)p)->fn();
}

bool methodOopDesc::is_primitiveMethod() const {
  char b = *codes();
  switch (*codes()) {
    case Bytecodes::predict_prim_call:
      return true;
    case Bytecodes::predict_prim_call_failure:
      return true;
    case Bytecodes::predict_prim_call_lookup:
      lookup_primitive_and_patch(codes(), Bytecodes::predict_prim_call);
      return true;
    case Bytecodes::predict_prim_call_failure_lookup:
      lookup_primitive_and_patch(codes(), Bytecodes::predict_prim_call_failure);
      return true;
    default:
      return false;
  }
}


Bytecodes::Code methodOopDesc::special_primitive_code() const {
  assert(is_special_primitiveMethod(), "should only be called for special primitive methods");
  Bytecodes::Code code = Bytecodes::Code(*codes(2));
  assert(Bytecodes::send_type(code) == Bytecodes::predicted_send, "code or bytecode table inconsistent");
  return code;
}


methodOop methodOopDesc::methodOop_from_hcode(u_char* hp) {
  methodOop method = methodOop(as_memOop(Universe::object_start((oop*) hp)));
  assert(method->is_method(), "must be method");
  assert(method->codes() <= hp && hp < method->codes() + method->size_of_codes() * sizeof(oop),
  	 "h-code pointer not contained in method");
  return method;
}


int methodOopDesc::end_bci() const {
  int last_entry = this->size_of_codes() * oopSize;
  for (int index = 0; index < oopSize; index++)
    if (byte_at(last_entry-index) != Bytecodes::halt) return last_entry+1-index;
  fatal("should never reach the point");
  return 0;
}


InterpretedIC* methodOopDesc::ic_at(int bci) const {
   CodeIterator iterator(methodOop(this), bci);
   return iterator.ic();
}

methodOop methodOopDesc::block_method_at(int bci) {
  CodeIterator c(methodOop(this), bci);
  switch(c.code()) {
    case Bytecodes::push_new_closure_tos_0:      // fall through
    case Bytecodes::push_new_closure_tos_1:      // fall through
    case Bytecodes::push_new_closure_tos_2:      // fall through
    case Bytecodes::push_new_closure_context_0:  // fall through
    case Bytecodes::push_new_closure_context_1:  // fall through
    case Bytecodes::push_new_closure_context_2: {
        methodOop block_method = methodOop(c.oop_at(1));
	assert(block_method->is_method(), "must be method");
	return block_method;
      }
      break;
    case Bytecodes::push_new_closure_tos_n:      // fall through
    case Bytecodes::push_new_closure_context_n: {
        methodOop block_method = methodOop(c.oop_at(2));
	assert(block_method->is_method(), "must be method");
	return block_method;
      }
      break;
   }
  return NULL;
}

int methodOopDesc::bci_for_block_method(methodOop inner) {
  CodeIterator c(this);
  do {
    if (inner == block_method_at(c.bci()))
      return c.bci();
  } while (c.advance());
  ShouldNotReachHere();
  return 0;
}

void methodOopDesc::print_inlining_database_on(outputStream* st) {
  if (is_blockMethod()) {
    methodOop o = parent();
    o->print_inlining_database_on(st);
    st->print(" %d", o->bci_for_block_method(this));
  } else {
    selector()->print_symbol_on(st);
  }
}

// ContextMethodIterator is used in number_of_context_temporaries to
// get information about context allocation
class ContextMethodIterator : public SpecializedMethodClosure {
 private:
  enum { sentinel = -1 };
  int  count;
  bool _self_in_context;

 public:
  ContextMethodIterator() {
    count = sentinel;
    _self_in_context = false;
  }

  bool self_in_context() { return _self_in_context; }

  int number_of_context_temporaries() {
    assert(count != sentinel, "number_of_context_temporaries not set");
    return count;
  }

  void allocate_context(int nofTemps, bool forMethod) {
    assert(count == sentinel, "make sure it is not called more than one");
    count = nofTemps;
  }

  void copy_self_into_context() {
    _self_in_context = true; 
  }
};

int methodOopDesc::number_of_context_temporaries(bool* self_in_context) {
  // Use this for debugging only
  assert(allocatesInterpretedContext(), "can only be called if method allocates context");
  ContextMethodIterator blk;
  MethodIterator i(this, &blk);
  if (self_in_context) *self_in_context = blk.self_in_context();
  return blk.number_of_context_temporaries();
}

void methodOopDesc::customize_for(klassOop klass, mixinOop mixin) {
  assert(!is_customized() ||  klass != mixin->primary_invocation(), "should not recustomize to the same class"); 
  CodeIterator c(this);
  do {
    InterpretedIC* ic = c.ic();
    if (ic) ic->clear_without_deallocation_pic();
    switch(c.code()) {

      case Bytecodes::push_classVar_name:
      case Bytecodes::store_classVar_pop_name:
      case Bytecodes::store_classVar_name:
	c.customize_class_var_code(klass);
        break;

      case Bytecodes::push_classVar:
      case Bytecodes::store_classVar_pop:
      case Bytecodes::store_classVar:
	c.recustomize_class_var_code(mixin->primary_invocation(), klass);
        break;

      case Bytecodes::push_instVar_name:
      case Bytecodes::store_instVar_pop_name:
      case Bytecodes::store_instVar_name: 
      case Bytecodes::return_instVar_name:
	c.customize_inst_var_code(klass);
        break;

      case Bytecodes::push_instVar:
      case Bytecodes::store_instVar_pop:
      case Bytecodes::store_instVar: 
      case Bytecodes::return_instVar:
	c.recustomize_inst_var_code(mixin->primary_invocation(), klass);
	break;

      case Bytecodes::push_new_closure_tos_0:      // fall through
      case Bytecodes::push_new_closure_tos_1:      // fall through
      case Bytecodes::push_new_closure_tos_2:      // fall through
      case Bytecodes::push_new_closure_context_0:  // fall through
      case Bytecodes::push_new_closure_context_1:  // fall through
      case Bytecodes::push_new_closure_context_2: {
          methodOop block_method = methodOop(c.oop_at(1));
	  assert(block_method->is_method(), "must be method");
	  block_method->customize_for(klass, mixin);
	}
        break;
      case Bytecodes::push_new_closure_tos_n:      // fall through
      case Bytecodes::push_new_closure_context_n: {
          methodOop block_method = methodOop(c.oop_at(2));
	  assert(block_method->is_method(), "must be method");
	  block_method->customize_for(klass, mixin);
	}
        break;
    }
  } while (c.advance());
  // set customized flag

  int new_flags = addNth(flags(), isCustomizedFlag);
  set_size_and_flags(size_of_codes(), nofArgs(), new_flags);
}

void methodOopDesc::uncustomize_for(mixinOop mixin) {
  // Skim the cream
  if (!is_customized()) return;

  klassOop klass = mixin->primary_invocation();
  assert(klass->is_klass(), "primary invocation muyst be present");

  CodeIterator c(this);
  do {
    InterpretedIC* ic = c.ic();
    if (ic) ic->clear_without_deallocation_pic();
    switch(c.code()) {
      case Bytecodes::push_classVar:
      case Bytecodes::store_classVar_pop:
      case Bytecodes::store_classVar:
	c.uncustomize_class_var_code(mixin->primary_invocation());
        break;

      case Bytecodes::push_instVar:
      case Bytecodes::store_instVar_pop:
      case Bytecodes::store_instVar: 
      case Bytecodes::return_instVar:
	c.uncustomize_inst_var_code(mixin->primary_invocation());
	break;

      case Bytecodes::push_new_closure_tos_0:      // fall through
      case Bytecodes::push_new_closure_tos_1:      // fall through
      case Bytecodes::push_new_closure_tos_2:      // fall through
      case Bytecodes::push_new_closure_context_0:  // fall through
      case Bytecodes::push_new_closure_context_1:  // fall through
      case Bytecodes::push_new_closure_context_2: {
          methodOop block_method = methodOop(c.oop_at(1));
	  assert(block_method->is_method(), "must be method");
	  block_method->uncustomize_for(mixin);
	}
        break;
      case Bytecodes::push_new_closure_tos_n:      // fall through
      case Bytecodes::push_new_closure_context_n: {
          methodOop block_method = methodOop(c.oop_at(2));
	  assert(block_method->is_method(), "must be method");
	  block_method->uncustomize_for(mixin);
	}
        break;
    }
  } while (c.advance());
  // set customized flag
  int new_flags = subNth(flags(), isCustomizedFlag);
  set_size_and_flags(size_of_codes(), nofArgs(), new_flags);
}

methodOop methodOopDesc::copy_for_customization() const {
  // Copy this method
  int  len   = size();
  oop* clone = Universe::allocate_tenured(len);
  oop* to    = clone;
  oop* from  = (oop*) addr();
  oop* end   = to + len;
  while (to < end) *to++ = *from++;

  // Do the deep copy
  methodOop new_method = methodOop(as_memOop(clone));
  CodeIterator c(new_method);
  do {
    switch(c.code()) {
      case Bytecodes::push_new_closure_tos_0:      // fall through
      case Bytecodes::push_new_closure_tos_1:      // fall through
      case Bytecodes::push_new_closure_tos_2:      // fall through
      case Bytecodes::push_new_closure_context_0:  // fall through
      case Bytecodes::push_new_closure_context_1:  // fall through
      case Bytecodes::push_new_closure_context_2: {
          methodOop block_method = methodOop(c.oop_at(1));
	  assert(block_method->is_method(), "must be method");
	  methodOop new_block_method = block_method->copy_for_customization();
	  new_block_method->set_selector_or_method(new_method);
          Universe::store(c.aligned_oop(1), new_block_method);
	}
        break;
      case Bytecodes::push_new_closure_tos_n:      // fall through
      case Bytecodes::push_new_closure_context_n: {
          methodOop block_method = methodOop(c.oop_at(2));
	  assert(block_method->is_method(), "must be method");
	  methodOop new_block_method = block_method->copy_for_customization();
	  new_block_method->set_selector_or_method(new_method);
          Universe::store(c.aligned_oop(2), new_block_method);
	}
        break;
    }
  } while (c.advance());
  return new_method;
}

void methodOopDesc::verify_context(contextOop con) {
  // Check if we should expect a context
  if (!activation_has_context()) {
    warning("Activation has no context (0x%lx).", con);
  }
  // Check the static vs. dynamic chain length
  if (context_chain_length() != con->chain_length()) {
    warning("Wong context chain length (got %d expected %d)",
            con->chain_length(), context_chain_length());
  }
  // Check the context has no forward reference
  if (con->unoptimized_context() != NULL) {
    warning("Context is optimized (0x%lx).", con);
  }
}

// Traverses over the method including the blocks inside
class TransitiveMethodClosure: public MethodClosure {
 public:
  void if_node(IfNode* node);
  void cond_node(CondNode* node);
  void while_node(WhileNode* node);
  void primitive_call_node(PrimitiveCallNode* node);
  void dll_call_node(DLLCallNode* node);
 
 public:
  virtual void inlined_send(symbolOop selector) {} 

 public:
  void allocate_temporaries(int nofTemps)					{}
  void push_self()								{}
  void push_tos()								{}
  void push_literal(oop obj)							{}
  void push_argument(int no)							{}
  void push_temporary(int no)							{}
  void push_temporary(int no, int context)					{}
  void push_instVar(int offset)							{}
  void push_instVar_name(symbolOop name)					{}
  void push_classVar(associationOop assoc)					{}
  void push_classVar_name(symbolOop name)					{}
  void push_global(associationOop obj)						{}
  void store_temporary(int no)							{}
  void store_temporary(int no, int context)					{}
  void store_instVar(int offset)						{}
  void store_instVar_name(symbolOop name)					{}
  void store_classVar(associationOop assoc)					{}
  void store_classVar_name(symbolOop name)					{}
  void store_global(associationOop obj)						{}
  void pop()									{}
  void normal_send(InterpretedIC* ic) 						{}
  void self_send  (InterpretedIC* ic) 						{}
  void super_send (InterpretedIC* ic) 						{}
  void double_equal()								{}
  void double_not_equal()							{}
  void method_return(int nofArgs)						{}
  void nonlocal_return(int nofArgs)						{}
  void allocate_closure(AllocationType type, int nofArgs, methodOop meth);
  void allocate_context(int nofTemps, bool forMethod)				{}
  void set_self_via_context()							{}
  void copy_self_into_context()							{}
  void copy_argument_into_context(int argNo, int no)				{}
  void zap_scope()								{}
  void predict_prim_call(primitive_desc* pdesc, int failure_start)		{}
  void float_allocate(int nofFloatTemps, int nofFloatExprs)			{}
  void float_floatify(Floats::Function f, int fno)				{}
  void float_move(int fno, int from)						{}
  void float_set(int fno, doubleOop value)					{}
  void float_nullary(Floats::Function f, int fno)				{}
  void float_unary(Floats::Function f, int fno)					{}
  void float_binary(Floats::Function f, int fno)				{}
  void float_unaryToOop(Floats::Function f, int fno)				{}
  void float_binaryToOop(Floats::Function f, int fno)				{}
};

void TransitiveMethodClosure::allocate_closure(AllocationType type, int nofArgs, methodOop meth) {
  MethodIterator iter(meth, this);
}

void TransitiveMethodClosure::if_node(IfNode* node) {
  inlined_send(node->selector());
  MethodIterator iter(node->then_code(), this);
  if (node->else_code() != NULL) {
    MethodIterator iter(node->else_code(), this);
  }
}

void TransitiveMethodClosure::cond_node(CondNode* node) {
  inlined_send(node->selector());
  MethodIterator iter(node->expr_code(), this);
}

void TransitiveMethodClosure::while_node(WhileNode* node) {
  inlined_send(node->selector());
  MethodIterator iter(node->expr_code(), this);
  if (node->body_code() != NULL) {
    MethodIterator iter(node->body_code(), this);
  }
}

void TransitiveMethodClosure::primitive_call_node(PrimitiveCallNode* node) {
  inlined_send(node->name());
  if (node->failure_code() != NULL) {
    MethodIterator iter(node->failure_code(), this);
  }
}

void TransitiveMethodClosure::dll_call_node(DLLCallNode* node) {
  inlined_send(node->function_name());
  if (node->failure_code() != NULL) {
    MethodIterator iter(node->failure_code(), this);
  }
}

class ReferencedInstVarNamesClosure: public TransitiveMethodClosure {
 private:
  mixinOop mixin;

  void collect(int offset) {
    symbolOop name = mixin->primary_invocation()->klass_part()->inst_var_name_at(offset);
    if (name) result->append(name);
  }

  void collect(symbolOop name) {
    result->append(name);
  }

 public:
  void push_instVar(int offset)           { collect(offset); }
  void push_instVar_name(symbolOop name)  { collect(name);   }
  void store_instVar(int offset)          { collect(offset); }
  void store_instVar_name(symbolOop name) { collect(name);   }
 public:
  ReferencedInstVarNamesClosure(int size, mixinOop mixin) {
    this->result = new GrowableArray<oop>(size);
    this->mixin  = mixin;
  }
  GrowableArray<oop>* result; 
};

objArrayOop methodOopDesc::referenced_instance_variable_names(mixinOop mixin) const {
  ResourceMark rm;
  ReferencedInstVarNamesClosure blk(20, mixin);
  MethodIterator(methodOop(this), &blk);
  return oopFactory::new_objArray(blk.result);
}

class ReferencedClassVarNamesClosure: public TransitiveMethodClosure {
 private:
  void collect(symbolOop name) { result->append(name); }
 public:
  void push_classVar(associationOop assoc)  { collect(assoc->key()); }
  void push_classVar_name(symbolOop name)   { collect(name);         }

  void store_classVar(associationOop assoc) { collect(assoc->key()); }
  void store_classVar_name(symbolOop name)  { collect(name);         }

 public:
  ReferencedClassVarNamesClosure(int size) {
    result = new GrowableArray<oop>(size);
  }
  GrowableArray<oop>* result; 
};

objArrayOop methodOopDesc::referenced_class_variable_names() const {
  ResourceMark rm;
  ReferencedClassVarNamesClosure blk(20);
  MethodIterator(methodOop(this), &blk);
  return oopFactory::new_objArray(blk.result);
}

class ReferencedGlobalsClosure: public TransitiveMethodClosure {
 private:
  void collect(symbolOop selector) { result->append(selector); }
 public:
  void push_global(associationOop obj)	{ collect(obj->key());}
  void store_global(associationOop obj)	{ collect(obj->key());}

 public:
  ReferencedGlobalsClosure(int size) {
    result = new GrowableArray<oop>(size);
  }
  GrowableArray<oop>* result; 
};

objArrayOop methodOopDesc::referenced_global_names() const {
  ResourceMark rm;
  ReferencedGlobalsClosure blk(20);
  MethodIterator(methodOop(this), &blk);
  return oopFactory::new_objArray(blk.result);
}

class SendersClosure: public TransitiveMethodClosure {
 private:
  void collect(symbolOop selector) { result->append(selector); }

  void float_op(Floats::Function f) {
    if (Floats::has_selector_for(f)) {
      collect(Floats::selector_for(f));
    }
  }
 public:
  void inlined_send(symbolOop selector) { collect(selector);       }
  void normal_send(InterpretedIC* ic)   { collect(ic->selector()); }
  void self_send  (InterpretedIC* ic)   { collect(ic->selector()); }
  void super_send (InterpretedIC* ic)   { collect(ic->selector()); }
  void double_equal()                   { collect(vmSymbols::double_equal());     }
  void double_not_equal()               { collect(vmSymbols::double_tilde());     }

  void float_floatify(Floats::Function f,    int fno) { float_op(f); }
  void float_nullary(Floats::Function f,     int fno) { float_op(f); }
  void float_unary(Floats::Function f,       int fno) { float_op(f); }
  void float_binary(Floats::Function f,      int fno) { float_op(f); }
  void float_unaryToOop(Floats::Function f,  int fno) { float_op(f); }
  void float_binaryToOop(Floats::Function f, int fno) { float_op(f); }

 public:
  SendersClosure(int size) {
    result = new GrowableArray<oop>(size);
  }
  GrowableArray<oop>* result; 
};

objArrayOop methodOopDesc::senders() const {
  ResourceMark rm;
  SendersClosure blk(20);
  MethodIterator(methodOop(this), &blk);
  return oopFactory::new_objArray(blk.result);
}
