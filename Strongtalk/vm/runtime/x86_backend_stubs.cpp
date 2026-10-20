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

// Stand-ins for the x86-32 interpreter and its assembly glue
// (interpreter/interpreter_asm.asm, runtime/callBackStubs.asm).
//
// On 64-bit hosts there is no interpreter yet: the hcode interpreter only exists
// as x86-32 assembly. These definitions let the VM link; reaching any of them
// is a fatal error. Replace them with a portable interpreter (port option 2).

#include "platform.hpp"
#ifndef STRONGTALK_X86_BACKEND

#include <stdio.h>
#include <stdlib.h>

static void x86_backend_unavailable(const char* name) {
  fprintf(stderr, "Strongtalk: '%s' requires the x86-32 interpreter, which is not available on this architecture\n", name);
  abort();
}

#define X86_STUB(name) extern "C" void name() { x86_backend_unavailable(#name); }


X86_STUB(deoptimized_return_from_dll_call)
X86_STUB(deoptimized_return_from_dll_call_restore)
X86_STUB(deoptimized_return_from_primitive_call_with_failure_block)
X86_STUB(deoptimized_return_from_primitive_call_with_failure_block_restore)
X86_STUB(deoptimized_return_from_primitive_call_without_failure_block)
X86_STUB(deoptimized_return_from_primitive_call_without_failure_block_restore)
X86_STUB(deoptimized_return_from_send_with_receiver)
X86_STUB(deoptimized_return_from_send_with_receiver_pop)
X86_STUB(deoptimized_return_from_send_with_receiver_pop_restore)
X86_STUB(deoptimized_return_from_send_with_receiver_restore)
X86_STUB(deoptimized_return_from_send_without_receiver)
X86_STUB(deoptimized_return_from_send_without_receiver_pop)
X86_STUB(deoptimized_return_from_send_without_receiver_pop_restore)
X86_STUB(deoptimized_return_from_send_without_receiver_restore)
X86_STUB(handleCCallBackStub)
X86_STUB(handlePascalCallBackStub)
X86_STUB(illegal)
X86_STUB(interpreter_case_100)
X86_STUB(interpreter_case_101)
X86_STUB(interpreter_case_102)
X86_STUB(interpreter_case_103)
X86_STUB(interpreter_case_105)
X86_STUB(interpreter_case_106)
X86_STUB(interpreter_case_107)
X86_STUB(interpreter_case_108)
X86_STUB(interpreter_case_109)
X86_STUB(interpreter_case_110)
X86_STUB(interpreter_case_111)
X86_STUB(interpreter_case_112)
X86_STUB(interpreter_case_113)
X86_STUB(interpreter_case_114)
X86_STUB(interpreter_case_115)
X86_STUB(interpreter_case_116)
X86_STUB(interpreter_case_117)
X86_STUB(interpreter_case_118)
X86_STUB(interpreter_case_119)
X86_STUB(interpreter_case_120)
X86_STUB(interpreter_case_121)
X86_STUB(interpreter_case_122)
X86_STUB(interpreter_case_123)
X86_STUB(interpreter_case_124)
X86_STUB(interpreter_case_125)
X86_STUB(interpreter_case_126)
X86_STUB(interpreter_case_127)
X86_STUB(interpreter_case_140)
X86_STUB(interpreter_case_141)
X86_STUB(interpreter_case_142)
X86_STUB(interpreter_case_143)
X86_STUB(interpreter_case_156)
X86_STUB(interpreter_case_157)
X86_STUB(interpreter_case_158)
X86_STUB(interpreter_case_159)
X86_STUB(interpreter_case_172)
X86_STUB(interpreter_case_173)
X86_STUB(interpreter_case_177)
X86_STUB(interpreter_case_179)
X86_STUB(interpreter_case_184)
X86_STUB(interpreter_case_185)
X86_STUB(interpreter_case_193)
X86_STUB(interpreter_case_195)
X86_STUB(interpreter_case_200)
X86_STUB(interpreter_case_201)
X86_STUB(interpreter_case_238)
X86_STUB(interpreter_case_239)
X86_STUB(interpreter_case_67)
X86_STUB(interpreter_case_68)
X86_STUB(interpreter_case_69)
X86_STUB(interpreter_case_70)
X86_STUB(interpreter_case_71)
X86_STUB(interpreter_case_83)
X86_STUB(interpreter_case_84)
X86_STUB(interpreter_case_85)
X86_STUB(interpreter_case_86)
X86_STUB(interpreter_case_87)
X86_STUB(interpreter_case_96)
X86_STUB(interpreter_case_97)
X86_STUB(interpreter_case_98)
X86_STUB(interpreter_case_99)
X86_STUB(redo_bytecode_after_deoptimization)
X86_STUB(single_step_stub)
X86_STUB(unpack_unoptimized_frames)

#endif // STRONGTALK_X86_BACKEND
