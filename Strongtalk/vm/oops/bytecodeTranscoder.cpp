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

#include "bytecodeTranscoder.hpp"
#include "error.hpp"

// Jump operands. Forward offsets are relative to the next instruction, backward
// offsets (whileTrue/whileFalse) to the current instruction (see interpreter_asm.asm).
enum JumpKind {
  no_jump,
  forward_byte,		// offset in lead[where]
  forward_long,		// offset in ops[where]
  backward_byte,
  backward_long,
  loop_byte,		// jump_loop_byte: lead[2] = offset to condition, lead[1] = condition to while instruction
  loop_long		// jump_loop_word: ops[1]  = offset to condition, ops[0]  = condition to while instruction
};

struct JumpInfo {
  JumpKind kind;
  int      where;
  bool     has_structure;	// ifTrue/ifFalse: lead[1] holds the if structure byte
};

static JumpInfo jump_info(Bytecodes::Code code) {
  JumpInfo j = { no_jump, 0, false };
  switch (code) {
    case Bytecodes::ifTrue_byte:
    case Bytecodes::ifFalse_byte:	j.kind = forward_byte;  j.where = 2; j.has_structure = true; break;
    case Bytecodes::ifTrue_word:
    case Bytecodes::ifFalse_word:	j.kind = forward_long;  j.where = 0; j.has_structure = true; break;
    case Bytecodes::and_byte:
    case Bytecodes::or_byte:
    case Bytecodes::jump_else_byte:	j.kind = forward_byte;  j.where = 1; break;
    case Bytecodes::and_word:
    case Bytecodes::or_word:
    case Bytecodes::jump_else_word:	j.kind = forward_long;  j.where = 0; break;
    case Bytecodes::whileTrue_byte:
    case Bytecodes::whileFalse_byte:	j.kind = backward_byte; j.where = 1; break;
    case Bytecodes::whileTrue_word:
    case Bytecodes::whileFalse_word:	j.kind = backward_long; j.where = 0; break;
    case Bytecodes::jump_loop_byte:	j.kind = loop_byte;     break;
    case Bytecodes::jump_loop_word:	j.kind = loop_long;     break;
    // primitive calls with a failure block: the last long jumps over the failure block
    case Bytecodes::prim_call_failure:
    case Bytecodes::predict_prim_call_failure:
    case Bytecodes::prim_call_self_failure:
    case Bytecodes::prim_call_failure_lookup:
    case Bytecodes::predict_prim_call_failure_lookup:
    case Bytecodes::prim_call_self_failure_lookup:
					j.kind = forward_long;  j.where = 1; break;
    default: break;
  }
  return j;
}

// The word-offset form of a byte-offset jump (used when an offset no longer fits a byte).
static Bytecodes::Code word_form(Bytecodes::Code code) {
  switch (code) {
    case Bytecodes::ifTrue_byte:	return Bytecodes::ifTrue_word;
    case Bytecodes::ifFalse_byte:	return Bytecodes::ifFalse_word;
    case Bytecodes::and_byte:		return Bytecodes::and_word;
    case Bytecodes::or_byte:		return Bytecodes::or_word;
    case Bytecodes::whileTrue_byte:	return Bytecodes::whileTrue_word;
    case Bytecodes::whileFalse_byte:	return Bytecodes::whileFalse_word;
    case Bytecodes::jump_else_byte:	return Bytecodes::jump_else_word;
    case Bytecodes::jump_loop_byte:	return Bytecodes::jump_loop_word;
    default: ShouldNotReachHere(); return code;
  }
}

// The byte-offset form of a word-offset jump; the code itself if it has none.
static Bytecodes::Code byte_form(Bytecodes::Code code) {
  switch (code) {
    case Bytecodes::ifTrue_word:	return Bytecodes::ifTrue_byte;
    case Bytecodes::ifFalse_word:	return Bytecodes::ifFalse_byte;
    case Bytecodes::and_word:		return Bytecodes::and_byte;
    case Bytecodes::or_word:		return Bytecodes::or_byte;
    case Bytecodes::whileTrue_word:	return Bytecodes::whileTrue_byte;
    case Bytecodes::whileFalse_word:	return Bytecodes::whileFalse_byte;
    case Bytecodes::jump_else_word:	return Bytecodes::jump_else_byte;
    case Bytecodes::jump_loop_word:	return Bytecodes::jump_loop_byte;
    default: return code;
  }
}

// Byte codes whose long operand only has a meaning in a running VM (code addresses).
static bool holds_runtime_address(Bytecodes::Code code) {
  if (Bytecodes::send_type(code) == Bytecodes::compiled_send) return true;
  switch (code) {
    case Bytecodes::prim_call:
    case Bytecodes::predict_prim_call:
    case Bytecodes::prim_call_self:
    case Bytecodes::prim_call_failure:
    case Bytecodes::predict_prim_call_failure:
    case Bytecodes::prim_call_self_failure:	return true;
    default:					return false;
  }
}

static bool is_dll_call(Bytecodes::Code code) {
  return code == Bytecodes::dll_call_sync || code == Bytecodes::dll_call_async;
}

void BytecodeTranscoder::format_shape(Bytecodes::Format f, int* lead, int* nops, int* trail, const char** kinds) {
  *trail = 0; *kinds = "";
  switch (f) {
    case Bytecodes::B:     *lead = 1; *nops = 0; break;
    case Bytecodes::BB:    *lead = 2; *nops = 0; break;
    case Bytecodes::BBB:   *lead = 3; *nops = 0; break;
    case Bytecodes::BBBB:  *lead = 4; *nops = 0; break;
    case Bytecodes::BBO:   *lead = 2; *nops = 1; *kinds = "O";   break;
    case Bytecodes::BBL:   *lead = 2; *nops = 1; *kinds = "L";   break;
    case Bytecodes::BO:    *lead = 1; *nops = 1; *kinds = "O";   break;
    case Bytecodes::BOO:   *lead = 1; *nops = 2; *kinds = "OO";  break;
    case Bytecodes::BLO:   *lead = 1; *nops = 2; *kinds = "LO";  break;
    case Bytecodes::BOL:   *lead = 1; *nops = 2; *kinds = "OL";  break;
    case Bytecodes::BLL:   *lead = 1; *nops = 2; *kinds = "LL";  break;
    case Bytecodes::BL:    *lead = 1; *nops = 1; *kinds = "L";   break;
    case Bytecodes::BLB:   *lead = 1; *nops = 1; *kinds = "L";   *trail = 1; break;
    case Bytecodes::BBOO:  *lead = 2; *nops = 2; *kinds = "OO";  break;
    case Bytecodes::BBLO:  *lead = 2; *nops = 2; *kinds = "LO";  break;
    case Bytecodes::BOOLB: *lead = 1; *nops = 3; *kinds = "OOL"; *trail = 1; break;
    case Bytecodes::BBS:   *lead = 2; *nops = 0; break;	// + number of bytes given by the second byte
    default:               *lead = 0; *nops = 0; break;	// UNDEF
  }
}

static inline int align_to(int pos, int word) { return (pos + word - 1) / word * word; }

int BytecodeTranscoder::instr_size(const Instr& in, int pos, int word) const {
  int n = (int) in.lead.size();
  if (in.nops == 0) return n + (int) in.trail.size();
  return align_to(pos + n, word) + in.nops * word + (int) in.trail.size() - pos;
}

BytecodeTranscoder::BytecodeTranscoder(BytecodeSource* src) {
  _error      = NULL;
  _word       = 0;
  _src_length = src->length();
  int w       = src->word_size();

  // Trailing halts are only padding up to a whole slot; they are recreated by emit().
  int end = _src_length;
  while (end > 0 && src->byte_at(end - 1) == Bytecodes::halt) end--;
  _src_end = end;

  // Decode
  int pos = 0;
  while (pos < end) {
    Instr in;
    in.code = Bytecodes::Code(src->byte_at(pos));
    Bytecodes::Format f = Bytecodes::format(in.code);
    if (f == Bytecodes::UNDEF) { fail("undefined byte code"); return; }
    int lead, nops, trail; const char* kinds;
    format_shape(f, &lead, &nops, &trail, &kinds);
    if (f == Bytecodes::BBS) {
      if (pos + 1 >= end) { fail("truncated byte code"); return; }
      int n = src->byte_at(pos + 1);
      lead += (n == 0 ? 256 : n);
    }
    in.src_pos = pos;
    in.nops    = nops;
    in.target  = -1;
    in.loop_while = -1;
    for (int i = 0; i < lead; i++) in.lead.push_back(src->byte_at(pos + i));
    int next = pos + lead;
    if (nops > 0) {
      int a = align_to(pos + lead, w);
      for (int i = 0; i < nops; i++) {
        Operand& op = in.ops[i];
        op.is_oop     = kinds[i] == 'O';
        op.oop_value  = NULL;
        op.long_value = 0;
        if (op.is_oop) op.oop_value  = src->oop_at(a + i * w);
        else           op.long_value = src->long_at(a + i * w);
      }
      next = a + nops * w;
    }
    for (int i = 0; i < trail; i++) in.trail.push_back(src->byte_at(next + i));
    next += trail;
    if (next > _src_length) { fail("truncated byte code"); return; }
    if (next > end) _src_end = end = next;	// last instruction ends in (former) padding

    if (w != oopSize && holds_runtime_address(in.code)) {
      fail("byte code with a runtime address (compiled send / resolved primitive) in external code");
      return;
    }
    if (is_dll_call(in.code)) in.ops[2].long_value = 0;	// cached entry point; looked up again on first call

    _instrs.push_back(in);
    pos = next;
  }

  // Resolve jump targets to instruction indices
  for (int i = 0; i < (int) _instrs.size(); i++) {
    Instr&   in   = _instrs[i];
    JumpInfo j    = jump_info(in.code);
    int      next = (i + 1 < (int) _instrs.size()) ? _instrs[i + 1].src_pos : end;
    int      dest = -1;
    switch (j.kind) {
      case no_jump:       continue;
      case forward_byte:  dest = next + in.lead[j.where]; break;
      case forward_long:  dest = next + (int) in.ops[j.where].long_value; break;
      case backward_byte: dest = in.src_pos - in.lead[j.where]; break;
      case backward_long: dest = in.src_pos - (int) in.ops[j.where].long_value; break;
      case loop_byte:
      case loop_long: {
        int cond_offset = j.kind == loop_byte ? in.lead[2] : (int) in.ops[1].long_value;
        int end_offset  = j.kind == loop_byte ? in.lead[1] : (int) in.ops[0].long_value;
        dest = next + cond_offset;
        in.loop_while = index_of_src_pos(dest + end_offset);
        if (in.loop_while < 0) { fail("jump_loop does not refer to an instruction"); return; }
        break;
      }
    }
    in.target = index_of_src_pos(dest);
    if (in.target < 0) { fail("jump into the middle of an instruction"); return; }
  }
}

// Index of the instruction starting at pos (the number of instructions for the end of code), -1 otherwise.
int BytecodeTranscoder::index_of_src_pos(int pos) const {
  if (pos == _src_end) return (int) _instrs.size();
  int lo = 0, hi = (int) _instrs.size() - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if      (_instrs[mid].src_pos < pos) lo = mid + 1;
    else if (_instrs[mid].src_pos > pos) hi = mid - 1;
    else return mid;
  }
  return -1;
}

int BytecodeTranscoder::position_of(int index) const {
  if (index >= (int) _instrs.size()) return length();
  return _instrs[index].pos;
}

int BytecodeTranscoder::length() const {
  if (_instrs.empty()) return 0;
  return _instrs.back().pos + _instrs.back().size;
}

int BytecodeTranscoder::padded_length() const {
  return align_to(length(), _word);
}

// Converts byte-offset jumps whose offset no longer fits a byte; returns true if anything changed.
bool BytecodeTranscoder::relax() {
  bool changed = false;
  for (int i = 0; i < (int) _instrs.size(); i++) {
    Instr&   in = _instrs[i];
    JumpInfo j  = jump_info(in.code);
    int      next = in.pos + in.size;
    bool     fits = true;
    if (j.kind == forward_byte)  fits = (position_of(in.target) - next) <= 255;
    if (j.kind == backward_byte) fits = (in.pos - position_of(in.target)) <= 255;
    if (j.kind == loop_byte)     fits = (position_of(in.target) - next) <= 255 &&
                                        (position_of(in.loop_while) - position_of(in.target)) <= 255;
    if (fits) continue;
    widen(in);
    changed = true;
  }
  return changed;
}

// Gives a byte-offset jump its word-offset form (offsets are recomputed by emit()).
void BytecodeTranscoder::widen(Instr& in) {
  JumpInfo        j         = jump_info(in.code);
  Bytecodes::Code word_code = word_form(in.code);
  cvector<u_char> lead;
  lead.push_back((u_char) word_code);
  if (j.has_structure) lead.push_back(in.lead[1]);
  in.code = word_code;
  in.lead = lead;
  in.nops = (j.kind == loop_byte) ? 2 : 1;
  for (int k = 0; k < in.nops; k++) {
    in.ops[k].is_oop     = false;
    in.ops[k].oop_value  = NULL;
    in.ops[k].long_value = 0;
  }
}

static inline bool is_while_jump(Bytecodes::Code code) {
  return code == Bytecodes::whileTrue_byte || code == Bytecodes::whileFalse_byte ||
         code == Bytecodes::whileTrue_word || code == Bytecodes::whileFalse_word;
}

// Gives the selected jumps their byte-offset form (offsets are recomputed by emit()).
void BytecodeTranscoder::narrow(bool while_jumps, bool other_jumps) {
  for (int i = 0; i < (int) _instrs.size(); i++) {
    Instr&          in        = _instrs[i];
    Bytecodes::Code byte_code = byte_form(in.code);
    if (byte_code == in.code) continue;
    if (!(is_while_jump(in.code) ? while_jumps : other_jumps)) continue;
    JumpInfo j = jump_info(in.code);
    cvector<u_char> lead;
    lead.push_back((u_char) byte_code);
    if (j.has_structure) lead.push_back(in.lead[1]);	// if structure byte
    lead.push_back(0);					// offset
    if (j.kind == loop_long) lead.push_back(0);		// jump_loop: second offset
    in.code = byte_code;
    in.lead = lead;
    in.nops = 0;
  }
}

// Gives all jumps other than while jumps their word-offset form.
void BytecodeTranscoder::widen_other_jumps() {
  for (int i = 0; i < (int) _instrs.size(); i++) {
    JumpKind k = jump_info(_instrs[i].code).kind;
    if ((k == forward_byte || k == loop_byte) && !is_while_jump(_instrs[i].code)) widen(_instrs[i]);
  }
}

void BytecodeTranscoder::layout_as_compiled(int word_size) {
  // As DeltaHCodeGenerator: a while jump has the byte-offset form if its offset fits (while:jumpTo:);
  // the other jumps of a method all have the byte-offset form, or, if one does not fit, all the
  // word-offset form (longJumps).
  cvector<Instr> original = _instrs;
  narrow(true, true);
  layout(word_size);
  for (int i = 0; i < (int) _instrs.size(); i++) {
    Bytecodes::Code code = _instrs[i].code;
    if (byte_form(code) != code && !is_while_jump(code)) {
      // a short jump did not fit: long jumps
      _instrs = original;
      narrow(true, false);
      widen_other_jumps();
      layout(word_size);
      return;
    }
  }
}

void BytecodeTranscoder::layout(int word_size) {
  _word = word_size;
  do {
    int pos = 0;
    for (int i = 0; i < (int) _instrs.size(); i++) {
      _instrs[i].pos  = pos;
      _instrs[i].size = instr_size(_instrs[i], pos, word_size);
      pos += _instrs[i].size;
    }
  } while (relax());
}

void BytecodeTranscoder::emit(BytecodeSink* sink) {
  assert(sink->word_size() == _word, "layout was computed for another slot size");
  for (int i = 0; i < (int) _instrs.size(); i++) {
    Instr    in   = _instrs[i];	// copy; jump fields are patched below
    JumpInfo j    = jump_info(in.code);
    int      next = in.pos + in.size;

    switch (j.kind) {
      case no_jump:       break;
      case forward_byte:  in.lead[j.where] = (u_char) (position_of(in.target) - next); break;
      case forward_long:  in.ops[j.where].long_value = position_of(in.target) - next; break;
      case backward_byte: in.lead[j.where] = (u_char) (in.pos - position_of(in.target)); break;
      case backward_long: in.ops[j.where].long_value = in.pos - position_of(in.target); break;
      case loop_byte:
        in.lead[2] = (u_char) (position_of(in.target) - next);
        in.lead[1] = (u_char) (position_of(in.loop_while) - position_of(in.target));
        break;
      case loop_long:
        in.ops[1].long_value = position_of(in.target) - next;
        in.ops[0].long_value = position_of(in.loop_while) - position_of(in.target);
        break;
    }

    if (j.has_structure && isSet(in.lead[1], 1)) {
      // has an else branch: the high nibble is the size of the jump_else instruction preceding it
      // (a size of 16 is encoded as 0, see IfNode)
      int else_jump = in.target - 1;
      Bytecodes::Code c = _instrs[else_jump].code;
      if (else_jump < 0 || (c != Bytecodes::jump_else_byte && c != Bytecodes::jump_else_word)) {
        fail("if with else branch is not followed by a jump_else");
      } else {
        int size = _instrs[else_jump].size;
        in.lead[1] = (u_char) ((in.lead[1] & 0x0f) | ((size & 0x0f) << 4));
      }
    }

    int pos = in.pos;
    for (int k = 0; k < (int) in.lead.size(); k++) sink->put_byte(pos++, in.lead[k]);
    if (in.nops > 0) {
      int a = align_to(pos, _word);
      while (pos < a) sink->put_byte(pos++, Bytecodes::halt);	// alignment padding
      for (int k = 0; k < in.nops; k++) {
        if (in.ops[k].is_oop) sink->put_oop(pos, in.ops[k].oop_value);
        else                  sink->put_long(pos, in.ops[k].long_value);
        pos += _word;
      }
    }
    for (int k = 0; k < (int) in.trail.size(); k++) sink->put_byte(pos++, in.trail[k]);
    assert(pos == next, "instruction size mismatch");
  }
  for (int pos = length(); pos < padded_length(); pos++) sink->put_byte(pos, Bytecodes::halt);
}

int BytecodeTranscoder::map_bci(int bci, bool end_inclusive) const {
  int pos = bci - 1;
  if (pos < 0) return bci;
  if (_instrs.empty() || pos >= _src_end) return length() + 1;
  // last instruction starting at or before pos
  int lo = 0, hi = (int) _instrs.size() - 1;
  while (lo < hi) {
    int mid = (lo + hi + 1) / 2;
    if (_instrs[mid].src_pos <= pos) lo = mid; else hi = mid - 1;
  }
  if (_instrs[lo].src_pos == pos || end_inclusive) return _instrs[lo].pos + 1;
  return position_of(lo + 1) + 1;
}
