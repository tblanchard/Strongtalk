/* Copyright 1994 - 1996 LongView Technologies L.L.C. $Revision: 1.50 $ */
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
#ifndef VM_MEMORY_VMSYMBOLS_HPP_HPP
#define VM_MEMORY_VMSYMBOLS_HPP_HPP

#include "oopsHierarchy.hpp"
#include "symbolOop.hpp"


#define VMSYMBOLS(template) \
  template(smi_overflow,                   "SmallIntegerOverflow")       \
  template(division_by_zero,               "DivisionByZero")             \
  template(receiver_has_wrong_type,        "ReceiverHasWrongType")       \
  template(division_not_exact,             "DivisionNotExact")           \
  template(first_argument_has_wrong_type,  "FirstArgumentHasWrongType")  \
  template(second_argument_has_wrong_type, "SecondArgumentHasWrongType") \
  template(third_argument_has_wrong_type,  "ThirdArgumentHasWrongType")  \
  template(fourth_argument_has_wrong_type, "FourthArgumentHasWrongType") \
  template(fifth_argument_has_wrong_type,  "FifthArgumentHasWrongType")  \
  template(sixth_argument_has_wrong_type,  "SixthArgumentHasWrongType")  \
  template(seventh_argument_has_wrong_type,"SixthArgumentHasWrongType")  \
  template(eighth_argument_has_wrong_type, "SeventhArgumentHasWrongType")\
  template(ninth_argument_has_wrong_type,  "EighthArgumentHasWrongType") \
  template(tenth_argument_has_wrong_type,  "NinthArgumentHasWrongType")  \
  template(argument_has_wrong_type,  	   "ArgumentHasWrongType")       \
  template(argument_is_invalid,            "ArgumentIsInvalid")          \
  template(conversion_failed,		"ConversionFailed")		\
  template(not_found,			"NotFound")			\
  template(plus,                         "+")                      	\
  template(minus,                        "-")                      	\
  template(multiply,                     "*")                      	\
  template(divide,                       "//")                     	\
  template(mod,                          "\\\\")                   	\
  template(point_at,                     "@")                      	\
  template(equal,                        "=")                      	\
  template(not_equal,                    "~=")                     	\
  template(less_than,                    "<")                      	\
  template(less_than_or_equal,           "<=")                     	\
  template(greater_than,                 ">")                      	\
  template(greater_than_or_equal,        ">=")                     	\
  template(bitAnd_,                      "bitAnd:")                	\
  template(bitOr_,                       "bitOr:")                 	\
  template(bitXor_,                      "bitXor:")                	\
  template(bitShift_,                    "bitShift:")              	\
  template(bitInvert,                    "bitInvert")              	\
  template(and_,  	                 "and:")       			\
  template(or_,				 "or:")  	           	\
  template(and_op,  	                 "&&")        			\
  template(or_op,				 "||")    	         	\
  template(and1,  	                 "&")        			\
  template(or1,				 "|")  	 	          	\
  template(not_op,  	                 "not")        			\
  template(xor_,  	                 "xor:")       			\
  template(eqv_,  	                 "eqv:")       			\
  template(at,                           "at:")                    	\
  template(at_put,                       "at:put:")                	\
  template(size  ,                       "size")                   	\
  template(double_equal,                 "==")                     	\
  template(double_tilde,                 "~~")                     	\
  template(out_of_bounds,                "OutOfBounds")                       \
  template(index_not_valid,              "IndexNotValid")                     \
  template(not_yet_implemented,          "NotYetImplemented")                 \
  template(indexable,                    "Indexable")                         \
  template(not_oops,                     "ReceiverNotOops")                   \
  template(not_clonable,                 "ReceiverNotClonable")               \
  template(value_out_of_range,           "ValueOutOfRange")                   \
  template(method_construction_failed,   "MethodConstructionFailed")          \
  template(primitive_not_defined,        "PrimitiveNotDefined")               \
  template(smi_conversion_failed,        "SmallIntegerConversionFailed")      \
  template(not_klass,                    "NotAClass")                         \
  template(selector_for_blockMethod,     "block")                             \
  template(not_indexable,                "NotIndexable")                      \
  template(to_by_do,                     "to:by:do:")                         \
  template(value,                        "value")                             \
  template(value_,                       "value:")                            \
  template(if_true,			 "ifTrue:")                           \
  template(if_true_false,                "ifTrue:ifFalse:")                   \
  template(if_false,                     "ifFalse:")                          \
  template(if_false_true,                "ifFalse:ifTrue:")                   \
  template(while_true,			 "whileTrue")                         \
  template(while_true_,                  "whileTrue:")                        \
  template(while_false,                  "whileFalse")                        \
  template(while_false_,                 "whileFalse:")                       \
  template(empty_queue,                  "EmptyQueue:")                       \
  template(is_absent,                    "IsAbsent")                          \
  template(in_scheduler,                 "InScheduler")                       \
  template(in_async_dll,                 "InAsyncDLL")                        \
  template(not_in_scheduler,             "NotInScheduler")                    \
  template(initialized,                  "Initialized")                       \
  template(yielded,                      "Yielded")                           \
  template(running,                      "Running")                           \
  template(stopped,                      "Stopped")                           \
  template(preempted,                    "Preempted")                         \
  template(completed,                    "Completed")                         \
  template(dead,                         "Dead")                              \
  template(aborted,                      "Aborted")                           \
  template(boolean_error,                "BooleanError")                      \
  template(lookup_error,                 "LookupError")                       \
  template(primitive_lookup_error,       "PrimitiveLookupError")              \
  template(DLL_lookup_error,             "DLLLookupError")                    \
  template(NLR_error,                    "NLRError")                          \
  template(float_error,                  "FloatError")                        \
  template(stack_overflow,               "StackOverflow")                     \
  template(is_installed,                 "IsInstalled")                       \
  template(failed,                       "Failed")                            \
  template(abs,                          "abs")                               \
  template(negated,                      "negated")                           \
  template(squared,                      "squared")                           \
  template(sqrt,                         "sqrt")                              \
  template(sin,                          "sin")                               \
  template(cos,                          "cos")                               \
  template(tan,                          "tan")                               \
  template(exp,                          "exp")                               \
  template(ln,                           "ln")                                \
  template(as_float,                     "asFloat")                           \
  template(as_float_value,               "asFloatValue")                      \
  template(negative_size,                "NegativeSize")                      \
  template(not_active,                   "NotActive")                         \
  template(primitive_disabled_in_product,"PrimitiveDisabledInProduct")        \
  template(activation_is_invalid,        "ActivationIsInvalid")               \
  template(external_activation,          "ExternalActivation")                \
  template(mem_klass,                    "Oops")                              \
  template(association_klass,            "GlobalAssociation")                 \
  template(blockClosure_klass,           "Block")                             \
  template(byteArray_klass,              "ByteArray")                         \
  template(symbol_klass,                 "Symbol")                            \
  template(context_klass,                "Context")                           \
  template(doubleByteArray_klass,        "DoubleByteArray")                   \
  template(doubleValueArray_klass,       "FloatValueArray")                   \
  template(double_klass,                 "Float")                             \
  template(klass_klass,                  "Class")                             \
  template(method_klass,                 "Method")                            \
  template(mixin_klass,                  "Mixin")                             \
  template(objArray_klass,               "Array")                             \
  template(weakArray_klass,              "WeakArray")                         \
  template(process_klass,                "Process")                           \
  template(vframe_klass,                 "Activation")                        \
  template(proxy_klass,                  "Proxy")                             \
  template(smi_klass,                    "SmallInteger")                      \
								              \
  /* for compiler's type prediction */					      \
  template(error,      "error")        					      \
  template(error_,     "error:")       					      \
  template(subclassResponsibility,      "subclassResponsibility")	      \


#define VMSYMBOL_POSTFIX  _enum
#define VMSYMBOL_ENUM_NAME(name) name##_enum

#define VMSYMBOL_ENUM(name,string) VMSYMBOL_ENUM_NAME(name),

enum {
  VMSYMBOLS(VMSYMBOL_ENUM)
  terminating_enum
};

#define VMSYMBOL_DECL(name, string) \
  static symbolOop name () { return vm_symbols[VMSYMBOL_ENUM_NAME(name)]; }

extern "C" symbolOop vm_symbols[];

class vmSymbols : AllStatic {
  friend void vmSymbols_init();
 public:
  static void initialize();
  typedef class symbolOopDesc* symbolOop;
  static symbolOop smi_overflow() { return vm_symbols[smi_overflow_enum]; }
  static symbolOop division_by_zero() { return vm_symbols[division_by_zero_enum]; }
  static symbolOop receiver_has_wrong_type() { return vm_symbols[receiver_has_wrong_type_enum]; }
  static symbolOop division_not_exact() { return vm_symbols[division_not_exact_enum]; }
  static symbolOop first_argument_has_wrong_type() { return vm_symbols[first_argument_has_wrong_type_enum]; }
  static symbolOop second_argument_has_wrong_type() { return vm_symbols[second_argument_has_wrong_type_enum]; }
  static symbolOop third_argument_has_wrong_type() { return vm_symbols[third_argument_has_wrong_type_enum]; }
  static symbolOop fourth_argument_has_wrong_type() { return vm_symbols[fourth_argument_has_wrong_type_enum]; }
  static symbolOop fifth_argument_has_wrong_type() { return vm_symbols[fifth_argument_has_wrong_type_enum]; }
  static symbolOop sixth_argument_has_wrong_type() { return vm_symbols[sixth_argument_has_wrong_type_enum]; }
  static symbolOop seventh_argument_has_wrong_type() { return vm_symbols[seventh_argument_has_wrong_type_enum]; }
  static symbolOop eighth_argument_has_wrong_type() { return vm_symbols[eighth_argument_has_wrong_type_enum]; }
  static symbolOop ninth_argument_has_wrong_type() { return vm_symbols[ninth_argument_has_wrong_type_enum]; }
  static symbolOop tenth_argument_has_wrong_type() { return vm_symbols[tenth_argument_has_wrong_type_enum]; }
  static symbolOop argument_has_wrong_type() { return vm_symbols[argument_has_wrong_type_enum]; }
  static symbolOop argument_is_invalid() { return vm_symbols[argument_is_invalid_enum]; }
  static symbolOop conversion_failed() { return vm_symbols[conversion_failed_enum]; }
  static symbolOop not_found() { return vm_symbols[not_found_enum]; }
  static symbolOop plus() { return vm_symbols[plus_enum]; }
  static symbolOop minus() { return vm_symbols[minus_enum]; }
  static symbolOop multiply() { return vm_symbols[multiply_enum]; }
  static symbolOop divide() { return vm_symbols[divide_enum]; }
  static symbolOop mod() { return vm_symbols[mod_enum]; }
  static symbolOop point_at() { return vm_symbols[point_at_enum]; }
  static symbolOop equal() { return vm_symbols[equal_enum]; }
  static symbolOop not_equal() { return vm_symbols[not_equal_enum]; }
  static symbolOop less_than() { return vm_symbols[less_than_enum]; }
  static symbolOop less_than_or_equal() { return vm_symbols[less_than_or_equal_enum]; }
  static symbolOop greater_than() { return vm_symbols[greater_than_enum]; }
  static symbolOop greater_than_or_equal() { return vm_symbols[greater_than_or_equal_enum]; }
  static symbolOop bitAnd_() { return vm_symbols[bitAnd__enum]; }
  static symbolOop bitOr_() { return vm_symbols[bitOr__enum]; }
  static symbolOop bitXor_() { return vm_symbols[bitXor__enum]; }
  static symbolOop bitShift_() { return vm_symbols[bitShift__enum]; }
  static symbolOop bitInvert() { return vm_symbols[bitInvert_enum]; }
  static symbolOop and_() { return vm_symbols[and__enum]; }
  static symbolOop or_() { return vm_symbols[or__enum]; }
  static symbolOop and_op() { return vm_symbols[and_op_enum]; }
  static symbolOop or_op() { return vm_symbols[or_op_enum]; }
  static symbolOop and1() { return vm_symbols[and1_enum]; }
  static symbolOop or1() { return vm_symbols[or1_enum]; }
  static symbolOop not_op() { return vm_symbols[not_op_enum]; }
  static symbolOop xor_() { return vm_symbols[xor__enum]; }
  static symbolOop eqv_() { return vm_symbols[eqv__enum]; }
  static symbolOop at() { return vm_symbols[at_enum]; }
  static symbolOop at_put() { return vm_symbols[at_put_enum]; }
  static symbolOop size() { return vm_symbols[size_enum]; }
  static symbolOop double_equal() { return vm_symbols[double_equal_enum]; }
  static symbolOop double_tilde() { return vm_symbols[double_tilde_enum]; }
  static symbolOop out_of_bounds() { return vm_symbols[out_of_bounds_enum]; }
  static symbolOop index_not_valid() { return vm_symbols[index_not_valid_enum]; }
  static symbolOop not_yet_implemented() { return vm_symbols[not_yet_implemented_enum]; }
  static symbolOop indexable() { return vm_symbols[indexable_enum]; }
  static symbolOop not_oops() { return vm_symbols[not_oops_enum]; }
  static symbolOop not_clonable() { return vm_symbols[not_clonable_enum]; }
  static symbolOop value_out_of_range() { return vm_symbols[value_out_of_range_enum]; }
  static symbolOop method_construction_failed() { return vm_symbols[method_construction_failed_enum]; }
  static symbolOop primitive_not_defined() { return vm_symbols[primitive_not_defined_enum]; }
  static symbolOop smi_conversion_failed() { return vm_symbols[smi_conversion_failed_enum]; }
  static symbolOop not_klass() { return vm_symbols[not_klass_enum]; }
  static symbolOop selector_for_blockMethod() { return vm_symbols[selector_for_blockMethod_enum]; }
  static symbolOop not_indexable() { return vm_symbols[not_indexable_enum]; }
  static symbolOop to_by_do() { return vm_symbols[to_by_do_enum]; }
  static symbolOop value() { return vm_symbols[value_enum]; }
  static symbolOop value_() { return vm_symbols[value__enum]; }
  static symbolOop if_true() { return vm_symbols[if_true_enum]; }
  static symbolOop if_true_false() { return vm_symbols[if_true_false_enum]; }
  static symbolOop if_false() { return vm_symbols[if_false_enum]; }
  static symbolOop if_false_true() { return vm_symbols[if_false_true_enum]; }
  static symbolOop while_true() { return vm_symbols[while_true_enum]; }
  static symbolOop while_true_() { return vm_symbols[while_true__enum]; }
  static symbolOop while_false() { return vm_symbols[while_false_enum]; }
  static symbolOop while_false_() { return vm_symbols[while_false__enum]; }
  static symbolOop empty_queue() { return vm_symbols[empty_queue_enum]; }
  static symbolOop is_absent() { return vm_symbols[is_absent_enum]; }
  static symbolOop in_scheduler() { return vm_symbols[in_scheduler_enum]; }
  static symbolOop in_async_dll() { return vm_symbols[in_async_dll_enum]; }
  static symbolOop not_in_scheduler() { return vm_symbols[not_in_scheduler_enum]; }
  static symbolOop initialized() { return vm_symbols[initialized_enum]; }
  static symbolOop yielded() { return vm_symbols[yielded_enum]; }
  static symbolOop running() { return vm_symbols[running_enum]; }
  static symbolOop stopped() { return vm_symbols[stopped_enum]; }
  static symbolOop preempted() { return vm_symbols[preempted_enum]; }
  static symbolOop completed() { return vm_symbols[completed_enum]; }
  static symbolOop dead() { return vm_symbols[dead_enum]; }
  static symbolOop aborted() { return vm_symbols[aborted_enum]; }
  static symbolOop boolean_error() { return vm_symbols[boolean_error_enum]; }
  static symbolOop lookup_error() { return vm_symbols[lookup_error_enum]; }
  static symbolOop primitive_lookup_error() { return vm_symbols[primitive_lookup_error_enum]; }
  static symbolOop DLL_lookup_error() { return vm_symbols[DLL_lookup_error_enum]; }
  static symbolOop NLR_error() { return vm_symbols[NLR_error_enum]; }
  static symbolOop float_error() { return vm_symbols[float_error_enum]; }
  static symbolOop stack_overflow() { return vm_symbols[stack_overflow_enum]; }
  static symbolOop is_installed() { return vm_symbols[is_installed_enum]; }
  static symbolOop failed() { return vm_symbols[failed_enum]; }
  static symbolOop abs() { return vm_symbols[abs_enum]; }
  static symbolOop negated() { return vm_symbols[negated_enum]; }
  static symbolOop squared() { return vm_symbols[squared_enum]; }
  static symbolOop sqrt() { return vm_symbols[sqrt_enum]; }
  static symbolOop sin() { return vm_symbols[sin_enum]; }
  static symbolOop cos() { return vm_symbols[cos_enum]; }
  static symbolOop tan() { return vm_symbols[tan_enum]; }
  static symbolOop exp() { return vm_symbols[exp_enum]; }
  static symbolOop ln() { return vm_symbols[ln_enum]; }
  static symbolOop as_float() { return vm_symbols[as_float_enum]; }
  static symbolOop as_float_value() { return vm_symbols[as_float_value_enum]; }
  static symbolOop negative_size() { return vm_symbols[negative_size_enum]; }
  static symbolOop not_active() { return vm_symbols[not_active_enum]; }
  static symbolOop primitive_disabled_in_product() { return vm_symbols[primitive_disabled_in_product_enum]; }
  static symbolOop activation_is_invalid() { return vm_symbols[activation_is_invalid_enum]; }
  static symbolOop external_activation() { return vm_symbols[external_activation_enum]; }
  static symbolOop mem_klass() { return vm_symbols[mem_klass_enum]; }
  static symbolOop association_klass() { return vm_symbols[association_klass_enum]; }
  static symbolOop blockClosure_klass() { return vm_symbols[blockClosure_klass_enum]; }
  static symbolOop byteArray_klass() { return vm_symbols[byteArray_klass_enum]; }
  static symbolOop symbol_klass() { return vm_symbols[symbol_klass_enum]; }
  static symbolOop context_klass() { return vm_symbols[context_klass_enum]; }
  static symbolOop doubleByteArray_klass() { return vm_symbols[doubleByteArray_klass_enum]; }
  static symbolOop doubleValueArray_klass() { return vm_symbols[doubleValueArray_klass_enum]; }
  static symbolOop double_klass() { return vm_symbols[double_klass_enum]; }
  static symbolOop klass_klass() { return vm_symbols[klass_klass_enum]; }
  static symbolOop method_klass() { return vm_symbols[method_klass_enum]; }
  static symbolOop mixin_klass() { return vm_symbols[mixin_klass_enum]; }
  static symbolOop objArray_klass() { return vm_symbols[objArray_klass_enum]; }
  static symbolOop weakArray_klass() { return vm_symbols[weakArray_klass_enum]; }
  static symbolOop process_klass() { return vm_symbols[process_klass_enum]; }
  static symbolOop vframe_klass() { return vm_symbols[vframe_klass_enum]; }
  static symbolOop proxy_klass() { return vm_symbols[proxy_klass_enum]; }
  static symbolOop smi_klass() { return vm_symbols[smi_klass_enum]; }
  static symbolOop error() { return vm_symbols[error_enum]; }
  static symbolOop error_() { return vm_symbols[error__enum]; }
  static symbolOop subclassResponsibility() { return vm_symbols[subclassResponsibility_enum]; }

  // operations for memory management.
  static void follow_contents();
  static void switch_pointers(oop from, oop to);
  static void relocate();
  static void verify();
};

#undef VMSYMBOL_DECL
#undef VMSYMBOL_ENUM

// for primitive failures
inline oop markSymbol(symbolOop sym) {
  assert(sym->is_mem(), "must have mem tag");
  return oop((char*) sym - Mem_Tag + Mark_Tag);
}

inline symbolOop unmarkSymbol(oop sym) {
  assert(sym->is_mark(), "must have mark tag");
  oop res = oop((char*) sym - Mark_Tag + Mem_Tag);
  assert(res->is_symbol(), "must be symbol");
  return symbolOop(res);
}


#endif // VM_MEMORY_VMSYMBOLS_HPP_HPP