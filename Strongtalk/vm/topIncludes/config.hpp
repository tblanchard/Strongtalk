/* Copyright 1994, 1995 LongView Technologies L.L.C. $Revision: 1.2 $ */
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
#ifndef VM_TOPINCLUDES_CONFIG_HPP_HPP
#define VM_TOPINCLUDES_CONFIG_HPP_HPP

#include "platform.hpp"

#include <stdint.h>
#include <sys/types.h>

// A VM word is the size of a pointer (oops, smis and mark words are all one word).
// Originally 4 (x86-32); 8 on 64-bit targets such as arm64.
const int BitsPerByte = 8;
const int BytesPerWord = sizeof(void*);
const int BitsPerWord = BitsPerByte * BytesPerWord;

static_assert(sizeof(void*) == sizeof(intptr_t), "pointer and intptr_t must be the same size");

const int LogBitsPerByte = 3;
const int LogBytesPerWord = sizeof(void*) == 8 ? 3 : 2;
const int LogBitsPerWord = LogBitsPerByte + LogBytesPerWord;

// A smi holds the untagged value of a SmallInteger; it is pointer sized so
// that a tagged smiOop (value << Tag_Size) fits in a word.
typedef intptr_t smi;

typedef unsigned short doubleByte;
typedef uint16_t       uint16;
typedef int16_t        int16;
typedef uint32_t       uint32;





#endif // VM_TOPINCLUDES_CONFIG_HPP_HPP