/* Copyright 1994, 1995 LongView Technologies L.L.C. $Revision: 1.13 $ */
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
#ifndef VM_RUNTIME_BOOTSTRAP_HPP_HPP
#define VM_RUNTIME_BOOTSTRAP_HPP_HPP

// bootstrap parses and allocates memOops from a text file

class bootstrap: CHeapObj {
 private:
  // Tables
  oop* oop_table;
  int number_of_oops;
  int max_number_of_oops;

  int position;

  bool _new_format;

  // Objects whose size differs between the image's 32-bit layout and the native layout
  // are allocated with an upper bound and shrunk afterwards (see get_object).
  int   _current_capacity;			// capacity (words) of the object being read
  struct Filler { oop* start; int words; };
  Filler* _fillers;
  int     _number_of_fillers;
  int     _max_number_of_fillers;
  void    shrink(memOop m, int capacity, int size);
  void    add_filler(oop* start, int words);
  void    create_fillers();
 public:
  void    print_fillers_near(oop* p);	// debugging
 private:

  void initialize_tables(int initial_table_size);

  void add(oop obj);
  oop  at(int index);

  // File
  const char* file_name;
  FILE* stream;
  int   file_size;
  char get_char();
  int  get_integer();

  // Error handline
  bool _has_error;

  // Parsing
  void open_file();
  void parse_file();
  oop  get_object();
  void close_file();
  void insert_symbol(memOop obj);
 public:
  bootstrap(const char *name);
  ~bootstrap();

  bool has_error() { return _has_error; }

  void read_mark(markOop* mark_addr);
  void read_oop(oop* oop_addr) { *oop_addr = get_object(); }

  char       read_byte()       { return get_char(); }
  doubleByte read_doubleByte() { return (doubleByte) get_integer(); }
  long       read_long()       { return get_integer(); } 
  double     read_double();

  bool new_format() const { return _new_format; }

  // Capacity in words of the object whose bootstrap_object() is running; read it before
  // reading any field (nested objects change it).
  int  current_capacity() const { return _current_capacity; }

  bool is_byte() { return getc(stream) == '4'; }
};


#endif // VM_RUNTIME_BOOTSTRAP_HPP_HPP