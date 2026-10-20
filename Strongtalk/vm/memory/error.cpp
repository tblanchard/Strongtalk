/* Copyright 1994, 1995 LongView Technologies L.L.C. $Revision: 1.5 $ */
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

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

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
#include "bytecodes.hpp"
#include "nativeInstruction.hpp"
#include "runtime.hpp"
#include "compiledIC.hpp"
#include "iCache.hpp"
#include "relocInfo.hpp"
#include "codeBuffer.hpp"
#include "assembler.hpp"
#include "floats.hpp"
#include "klassOop.hpp"
#include "ncode.hpp"
#include "symbolOop.hpp"
#include "key.hpp"
#include "location.hpp"
#include "x86_mapping.hpp"
#include "nameDesc.hpp"
#include "methodOop.hpp"
#include "scopeDesc.hpp"
#include "jumpTable.hpp"
#include "codeTable.hpp"
#include "zoneHeap.hpp"
#include "zone.hpp"
#include "nmethodScopes.hpp"
#include "nmethod.hpp"
#include "lookupCache.hpp"
#include "os.hpp"
#include "dll.hpp"
#include "codeIterator.hpp"
#include "ic_iterator.hpp"
#include "interpretedIC.hpp"
#include "methodIterator.hpp"
#include "compUtils.hpp"
#include "opcode.hpp"
#include "prim.hpp"
#include "nodeBuilder.hpp"
#include "inliner.hpp"
#include "slist.hpp"
#include "compiler.hpp"
void report_vm_state() {
#ifdef DEBUG
  static bool recursive_error = false;
  if (!recursive_error) {
    recursive_error = true;
#ifdef DELTA_COMPILER
    if (theCompiler) {
      stdOut->print("\nThe error happened while compiling ");
      theCompiler->print_key(stdOut);
      stdOut->cr();
      if (CompilerDebug) {
	print_cout();
      } else {
	stdOut->print("(No compiler debug output available -- run with +CompilerDebug to get it)");
      }
      stdOut->cr();
    }
#endif // DELTA_COMPILER

    stdOut->print("\nLast 10 internal VM events:\n");
    eventLog->printPartial(10);
    recursive_error = false;
  }
#endif // DEBUG
}

void report_error(const char* title, const char* format, ...) {
  char buffer[2048];
  va_list ap;
  va_start(ap, format);
  vsprintf(buffer, format, ap);
  va_end(ap);

  stdOut->cr();
  stdOut->print_cr("[A Runtime Error Occurred]");
  stdOut->print_raw(buffer);

  if (!bootstrapping) report_vm_state();

  if (ShowMessageBoxOnError) {
    strcat(buffer, "\n\nDo you want to debug the problem?");
    if (!os::message_box(title, buffer)) os::fatalExit(-1);
  }
}

void report_assertion_failure(const char* code_str, const char* file_name, int line_no, const char* message) {
  report_error("Assertion Failure", "assert(%s, \"%s\")\n%s, %d",
	        code_str, message, file_name, line_no);
}

void report_fatal(const char* file_name, int line_no, const char* format, ...) {
  char buffer[2048];
  va_list ap;
  va_start(ap, format);
  vsprintf(buffer, format, ap);
  va_end(ap);
  report_error("Fatal Error", "Fatal: %s\n%s, %d", buffer, file_name, line_no);
}

void report_should_not_call(const char* file_name, int line_no) {
  report_error("Should Not Call This Error", "ShouldNotCall()\n%s, %d",
	       file_name, line_no);
}

void report_should_not_reach_here(const char* file_name, int line_no) {
  report_error("Should Not Reach Here Error", "ShouldNotReachHere()\n%s, %d",
	       file_name, line_no);
}

void report_subclass_responsibility(const char* file_name, int line_no) {
  report_error("Subclass Responsibility Error", "SubclassResponsibility()\n%s, %d",
	       file_name, line_no);
}

void report_unimplemented(const char* file_name, int line_no) {
  report_error("Unimplemented Error", "Unimplemented()\n%s, %d", file_name, line_no);
}

