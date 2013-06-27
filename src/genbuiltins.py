#!/usr/bin/python
#
#  Generate initialization data for builtins.
#
#  The init data is a bit-packed stream tailored to match the decoder
#  in duk_hthread_builtins.c.  Various bitfield sizes are used to
#  minimize the bitstream size without resorting to actual, expensive
#  compression.  The goal is to minimize the overall size of the init
#  code and the init data.
#
#  Notes on value representation
#
#    - Strings are represented either by indexing the built-in strings
#      (genstrings.py) or by emitting them verbatim
#
#    - Built-in objects are represented by their built-in index
#
#    - Native functions are represented by indexing the built-in
#      native function array
#
#  Other notes:
#
#    - 'values' and 'functions' are intentionally ordered, so that the
#      initialization order (and hence enumeration order) of keys can be
#      controlled.

# FIXME: Some algorithms need to refer to the original, unmodified built-in
# functions (like Object.toString).  These should be marked somehow here and
# slots in thr->builtins should be allocated for them.

import os
import sys
import json
import math
import struct
import optparse

import dukutil

#
#  Helpers and constants
#
#  Double constants, see http://en.wikipedia.org/wiki/Double-precision_floating-point_format.
#  Some double constants have been created with 'python-mpmath'.  The constants are in binary
#  so that the package is not needed for a normal build.
#

def create_double_constants_mpmath():
	# Just a helper to use manually
	# http://mpmath.googlecode.com/svn/trunk/doc/build/basics.html
	import mpmath

	mpmath.mp.prec = 1000  # 1000 bits

	def printhex(name, x):
		# to hex string, ready for create_double()
		hex = struct.pack('>d', float(str(x))).encode('hex')
		flt = struct.unpack('>d', hex.decode('hex'))[0]
		print '%s -> %s  (= %.20f)' % (name, hex, flt)

	printhex('DBL_E', mpmath.mpf(mpmath.e))
	printhex('DBL_LN10', mpmath.log(10))
	printhex('DBL_LN2', mpmath.log(2))
	printhex('DBL_LOG2E', mpmath.log(mpmath.e) / mpmath.log(2))
	printhex('DBL_LOG10E', mpmath.log(mpmath.e) / mpmath.log(10))
	printhex('DBL_PI', mpmath.mpf(mpmath.pi))
	printhex('DBL_SQRT1_2', mpmath.mpf(1) / mpmath.sqrt(2))
	printhex('DBL_SQRT2', mpmath.sqrt(2))

#create_double_constants_mpmath()

def create_double(x):
	return struct.unpack('>d', x.decode('hex'))[0]

DBL_NAN =                    create_double('7ff8000000000000')  # a NaN matching our "normalized NAN" definition (see duk_tval.h)
DBL_POSITIVE_INFINITY =      create_double('7ff0000000000000')  # positive infinity (unique)
DBL_NEGATIVE_INFINITY =      create_double('fff0000000000000')  # negative infinity (unique)
DBL_MAX_DOUBLE =             create_double('7fefffffffffffff')  # 'Max Double'
DBL_MIN_DOUBLE =             create_double('0000000000000001')  # 'Min subnormal positive double'
DBL_E =                      create_double('4005bf0a8b145769')  # (= 2.71828182845904509080)
DBL_LN10 =                   create_double('40026bb1bbb55516')  # (= 2.30258509299404590109)
DBL_LN2 =                    create_double('3fe62e42fefa39ef')  # (= 0.69314718055994528623)
DBL_LOG2E =                  create_double('3ff71547652b82fe')  # (= 1.44269504088896338700)
DBL_LOG10E =                 create_double('3fdbcb7b1526e50e')  # (= 0.43429448190325181667)
DBL_PI =                     create_double('400921fb54442d18')  # (= 3.14159265358979311600)
DBL_SQRT1_2 =                create_double('3fe6a09e667f3bcd')  # (= 0.70710678118654757274)
DBL_SQRT2 =                  create_double('3ff6a09e667f3bcd')  # (= 1.41421356237309514547)

# marker for 'undefined' value
UNDEFINED = {}

# default property atrributes, see E5 Section 15 beginning
LENGTH_PROPERTY_ATTRIBUTES = ""
DEFAULT_PROPERTY_ATTRIBUTES = "wc"

# encoding constants (must match duk_hthread_builtins.c)
CLASS_BITS = 4
BIDX_BITS = 6
STRIDX_BITS = 9   # FIXME: try to optimize to 8
NATIDX_BITS = 8
NUM_NORMAL_PROPS_BITS = 6
NUM_FUNC_PROPS_BITS = 6
PROP_FLAGS_BITS = 3
STRING_LENGTH_BITS = 8
STRING_CHAR_BITS = 7
LENGTH_PROP_BITS = 3
NARGS_BITS = 3
PROP_TYPE_BITS = 3

NARGS_VARARGS_MARKER = 0x07
NO_CLASS_MARKER = 0x00   # 0 = DUK_HOBJECT_CLASS_UNUSED 
NO_BIDX_MARKER = 0x3f
NO_STRIDX_MARKER = 0xff

PROP_TYPE_DOUBLE = 0
PROP_TYPE_STRING = 1
PROP_TYPE_STRIDX = 2
PROP_TYPE_BUILTIN = 3
PROP_TYPE_UNDEFINED = 4
PROP_TYPE_BOOLEAN_TRUE = 5
PROP_TYPE_BOOLEAN_FALSE = 6

# must match duk_hobject.h
PROPDESC_FLAG_WRITABLE =     (1 << 0)
PROPDESC_FLAG_ENUMERABLE =   (1 << 1)
PROPDESC_FLAG_CONFIGURABLE = (1 << 2)
PROPDESC_FLAG_ACCESSOR =     (1 << 3)

# numeric indices must match duk_hobject.h class numbers
_classnames = [
	'Unused',
	'Arguments',
	'Array',
	'Boolean',
	'Date',
	'Error',
	'Function',
	'JSON',
	'Math',
	'Number',
	'Object',
	'RegExp',
	'String',
	'global',
	'ObjEnv',
	'DecEnv',
]
_class2num = {}
for i,v in enumerate(_classnames):
	_class2num[v] = i

def classToNumber(x):
	return _class2num[x]

def internal(x):
	# zero-prefix is used to mark internal values in genstrings.py;
	# it is converted to \xFF during initialization
	return '\x00' + x

#
#  Built-in object descriptions
#

bi_global = {
	# internal prototype: implementation specific
	#	Smjs: Object.prototype
	#	Rhino: Object.prototype
	#	V8: *not* Object.prototype, but prototype chain includes Object.prototype
	# external prototype: apparently not set
	# external constructor: apparently not set
	# internal class: implemented specific
	#	Smjs: 'global'
	#	Rhino: 'global'
	#	V8: 'global'

	# E5 Sections B.2.1 and B.2.2 describe non-standard properties which are
	# included below but flagged as extensions.

	'internal_prototype': 'bi_object_prototype',
	'class': 'global',

	'values': [
		{ 'name': 'NaN',			'value': DBL_NAN,		'attributes': '' },
		{ 'name': 'Infinity',			'value': DBL_POSITIVE_INFINITY, 'attributes': '' },
		{ 'name': 'undefined',			'value': UNDEFINED,		'attributes': '' },	# marker value

		{ 'name': 'Object',			'value': { 'type': 'builtin', 'id': 'bi_object_constructor' } },
		{ 'name': 'Function',			'value': { 'type': 'builtin', 'id': 'bi_function_constructor' } },
		{ 'name': 'Array',			'value': { 'type': 'builtin', 'id': 'bi_array_constructor' } },
		{ 'name': 'String',			'value': { 'type': 'builtin', 'id': 'bi_string_constructor' } },
		{ 'name': 'Boolean',			'value': { 'type': 'builtin', 'id': 'bi_boolean_constructor' } },
		{ 'name': 'Number',			'value': { 'type': 'builtin', 'id': 'bi_number_constructor' } },
		{ 'name': 'Date',			'value': { 'type': 'builtin', 'id': 'bi_date_constructor' } },
		{ 'name': 'RegExp',			'value': { 'type': 'builtin', 'id': 'bi_regexp_constructor' } },
		{ 'name': 'Error',			'value': { 'type': 'builtin', 'id': 'bi_error_constructor' } },
		{ 'name': 'EvalError',			'value': { 'type': 'builtin', 'id': 'bi_eval_error_constructor' } },
		{ 'name': 'RangeError',			'value': { 'type': 'builtin', 'id': 'bi_range_error_constructor' } },
		{ 'name': 'ReferenceError',		'value': { 'type': 'builtin', 'id': 'bi_reference_error_constructor' } },
		{ 'name': 'SyntaxError',		'value': { 'type': 'builtin', 'id': 'bi_syntax_error_constructor' } },
		{ 'name': 'TypeError',			'value': { 'type': 'builtin', 'id': 'bi_type_error_constructor' } },
		{ 'name': 'URIError',			'value': { 'type': 'builtin', 'id': 'bi_uri_error_constructor' } },
		{ 'name': 'Math',			'value': { 'type': 'builtin', 'id': 'bi_math' } },
		{ 'name': 'JSON',			'value': { 'type': 'builtin', 'id': 'bi_json' } },

		# DUK specific
		{ 'name': '__duk__',			'value': { 'type': 'builtin', 'id': 'bi_duk' } },
	],
	'functions': [
		{ 'name': 'eval',			'native': 'duk_builtin_global_object_eval', 			'length': 1 },
		{ 'name': 'parseInt',			'native': 'duk_builtin_global_object_parse_int',		'length': 2 },
		{ 'name': 'parseFloat',			'native': 'duk_builtin_global_object_parse_float',		'length': 1 },
		{ 'name': 'isNaN',			'native': 'duk_builtin_global_object_is_nan',			'length': 1 },
		{ 'name': 'isFinite',			'native': 'duk_builtin_global_object_is_finite',		'length': 1 },
		{ 'name': 'decodeURI',			'native': 'duk_builtin_global_object_decode_uri',		'length': 1 },
		{ 'name': 'decodeURIComponent',		'native': 'duk_builtin_global_object_decode_uri_component',	'length': 1 },
		{ 'name': 'encodeURI',			'native': 'duk_builtin_global_object_encode_uri',		'length': 1 },
		{ 'name': 'encodeURIComponent',		'native': 'duk_builtin_global_object_encode_uri_component', 	'length': 1 },

		# Non-standard extensions: E5 Sections B.2.1 and B.2.2
		#
		# 'length' is not specified explicitly in E5 but it follows the
		# general argument count rule.  V8 also agrees on the lengths.

		{ 'name': 'escape',			'native': 'duk_builtin_global_object_escape',			'length': 1,	'section_b': True },
		{ 'name': 'unescape',			'native': 'duk_builtin_global_object_unescape',                 'length': 1,	'section_b': True },

		# Non-standard extensions from the web (not comprehensive)
		#
		#   print:  common even outside browsers (smjs: length = 0)
		#   alert:  common in browsers (Chromium: length = 0)

		{ 'name': 'print',			'native': 'duk_builtin_global_object_print',			'length': 0,	'varargs': True,	'browser': True },
		{ 'name': 'alert',			'native': 'duk_builtin_global_object_alert',			'length': 0,	'varargs': True,	'browser': True },

		# XXX: built-ins which are nice for compatibility?  E.g. 'print'

		# XXX: built-in stuff here for browser-like methods (setTimeout & co,
		# alert, etc)?
	],
}

bi_global_env = {
	'class': 'ObjEnv',

	'values': [
		{ 'name': internal('target'),		'value': { 'type': 'builtin', 'id': 'bi_global' },	'attributes': '' },
	],
	'functions': [],
}

bi_object_constructor = {
	'internal_prototype': 'bi_function_prototype',
	'external_prototype': 'bi_object_prototype',
	'class': 'Function',
	'name': 'Object',

	'length': 1,
	'native': 'duk_builtin_object_constructor',
	'callable': True,
	'constructable': True,

	'values': [],
	'functions': [
		{ 'name': 'getPrototypeOf',		'native': 'duk_builtin_object_constructor_get_prototype_of',			'length': 1 },
		{ 'name': 'getOwnPropertyDescriptor',	'native': 'duk_builtin_object_constructor_get_own_property_descriptor',		'length': 2 },
		{ 'name': 'getOwnPropertyNames',	'native': 'duk_builtin_object_constructor_get_own_property_names', 		'length': 1 },
		{ 'name': 'create',			'native': 'duk_builtin_object_constructor_create',				'length': 2 },
		{ 'name': 'defineProperty',		'native': 'duk_builtin_object_constructor_define_property',			'length': 3 },
		{ 'name': 'defineProperties',		'native': 'duk_builtin_object_constructor_define_properties',			'length': 2 },
		{ 'name': 'seal',			'native': 'duk_builtin_object_constructor_seal',				'length': 1 },
		{ 'name': 'freeze',			'native': 'duk_builtin_object_constructor_freeze',				'length': 1 },
		{ 'name': 'preventExtensions',		'native': 'duk_builtin_object_constructor_prevent_extensions',			'length': 1 },
		{ 'name': 'isSealed',			'native': 'duk_builtin_object_constructor_is_sealed',				'length': 1 },
		{ 'name': 'isFrozen',			'native': 'duk_builtin_object_constructor_is_frozen',				'length': 1 },
		{ 'name': 'isExtensible',		'native': 'duk_builtin_object_constructor_is_extensible',			'length': 1 },
		{ 'name': 'keys',			'native': 'duk_builtin_object_constructor_keys',				'length': 1 },
	],
}

bi_object_prototype = {
	# internal prototype is null
	'external_constructor': 'bi_object_constructor',
	'values': [],
	'class': 'Object',

	'functions': [
		{ 'name': 'toString',			'native': 'duk_builtin_object_prototype_to_string',			'length': 0 },
		{ 'name': 'toLocaleString',		'native': 'duk_builtin_object_prototype_to_locale_string',		'length': 0 },
		{ 'name': 'valueOf',			'native': 'duk_builtin_object_prototype_value_of',			'length': 0 },
		{ 'name': 'hasOwnProperty',		'native': 'duk_builtin_object_prototype_has_own_property',		'length': 1 },
		{ 'name': 'isPrototypeOf',		'native': 'duk_builtin_object_prototype_is_prototype_of',		'length': 1 },
		{ 'name': 'propertyIsEnumerable',	'native': 'duk_builtin_object_prototype_property_is_enumerable',	'length': 1 },
	],
}

bi_function_constructor = {
	'internal_prototype': 'bi_function_prototype',
	'external_prototype': 'bi_function_prototype',
	'class': 'Function',
	'name': 'Function',

	'length': 1,
	'varargs': True,
	'native': 'duk_builtin_function_constructor',
	'callable': True,
	'constructable': True,

	'values': [],
	'functions': [],
}

# Note, unlike other prototype objects, Function.prototype is itself
# a Function and callable.  When invoked, it accepts any arguments
# and returns undefined.  It cannot be called as a constructor.
# See E5 Section 15.3.4.
bi_function_prototype = {
	'internal_prototype': 'bi_object_prototype',
	'external_constructor': 'bi_function_constructor',
	'class': 'Function',
	'name': '',  # FIXME: what does the spec say?

	'length': 0,
	'native': 'duk_builtin_function_prototype',
	'callable': True,
	'constructable': False,  # Note: differs from other global Function classed objects (matches e.g. V8 behavior).

	'values': [],
	'functions': [
		{ 'name': 'toString',			'native': 'duk_builtin_function_prototype_to_string',		'length': 1 },
		{ 'name': 'apply',			'native': 'duk_builtin_function_prototype_apply',		'length': 2 },
		{ 'name': 'call',			'native': 'duk_builtin_function_prototype_call',		'length': 1,	'varargs': True },
		{ 'name': 'bind',			'native': 'duk_builtin_function_prototype_bind',		'length': 1,	'varargs': True },
	],
}

bi_array_constructor = {
	'internal_prototype': 'bi_function_prototype',
	'external_prototype': 'bi_array_prototype',
	'class': 'Function',
	'name': 'Array',

	'length': 1,
	'varargs': True,
	'native': 'duk_builtin_array_constructor',
	'callable': True,
	'constructable': True,

	'values': [],
	'functions': [
		{ 'name': 'isArray',			'native': 'duk_builtin_array_constructor_is_array',		'length': 1 },
	],
}

bi_array_prototype = {
	'internal_prototype': 'bi_object_prototype',
	'external_constructor': 'bi_array_constructor',
	'class': 'Array',

	# An array prototype is an Array itself.  It has a length property initialized to 0,
	# with property attributes: writable, non-configurable, non-enumerable.  The attributes
	# are not specified very explicitly for the prototype, but are given for Array instances
	# in E5 Section 15.4.5.2 (and this matches the behavior of e.g. V8).

	'length': 0,
	'length_attributes': 'w',

	'values': [],
	'functions': [
		{ 'name': 'toString',			'native': 'duk_builtin_array_prototype_to_string',		'length': 0 },
		{ 'name': 'toLocaleString',		'native': 'duk_builtin_array_prototype_to_locale_string',	'length': 0 },
		{ 'name': 'concat',			'native': 'duk_builtin_array_prototype_concat',			'length': 1,	'varargs': True },
		{ 'name': 'join',			'native': 'duk_builtin_array_prototype_join',			'length': 1 },
		{ 'name': 'pop',			'native': 'duk_builtin_array_prototype_pop',			'length': 0 },
		{ 'name': 'push',			'native': 'duk_builtin_array_prototype_push',			'length': 1,	'varargs': True },
		{ 'name': 'reverse',			'native': 'duk_builtin_array_prototype_reverse',		'length': 0 },
		{ 'name': 'shift',			'native': 'duk_builtin_array_prototype_shift',			'length': 0 },
		{ 'name': 'slice',			'native': 'duk_builtin_array_prototype_slice',			'length': 2 },
		{ 'name': 'sort',			'native': 'duk_builtin_array_prototype_sort',			'length': 1 },
		{ 'name': 'splice',			'native': 'duk_builtin_array_prototype_splice',			'length': 2,	'varargs': True },
		{ 'name': 'unshift',			'native': 'duk_builtin_array_prototype_unshift',		'length': 1,	'varargs': True },
		{ 'name': 'indexOf',			'native': 'duk_builtin_array_prototype_index_of',		'length': 1,	'varargs': True },
		{ 'name': 'lastIndexOf',		'native': 'duk_builtin_array_prototype_last_index_of',		'length': 1,	'varargs': True },
		{ 'name': 'every',			'native': 'duk_builtin_array_prototype_every',			'length': 1,	'nargs': 2 },
		{ 'name': 'some',			'native': 'duk_builtin_array_prototype_some',			'length': 1,	'nargs': 2 },
		{ 'name': 'forEach',			'native': 'duk_builtin_array_prototype_for_each',		'length': 1,	'nargs': 2 },
		{ 'name': 'map',			'native': 'duk_builtin_array_prototype_map',			'length': 1,	'nargs': 2 },
		{ 'name': 'filter',			'native': 'duk_builtin_array_prototype_filter',			'length': 1,	'nargs': 2 },
		{ 'name': 'reduce',			'native': 'duk_builtin_array_prototype_reduce',			'length': 1,	'varargs': True },
		{ 'name': 'reduceRight',		'native': 'duk_builtin_array_prototype_reduce_right',		'length': 1,	'varargs': True },
	],
}

bi_string_constructor = {
	'internal_prototype': 'bi_function_prototype',
	'external_prototype': 'bi_string_prototype',
	'class': 'Function',
	'name': 'String',

	'length': 1,
	'varargs': True,
	'native': 'duk_builtin_string_constructor',
	'callable': True,
	'constructable': True,

	'values': [],
	'functions': [
		{ 'name': 'fromCharCode',		'native': 'duk_builtin_string_constructor_from_char_code',	'length': 1,	'varargs': True },
	],
}

bi_string_prototype = {
	'internal_prototype': 'bi_object_prototype',
	'external_constructor': 'bi_string_constructor',
	'class': 'String',

	# String prototype is a String instance and must have length value 0.
	# This is supplied by the String instance virtual properties and does
	# not need to be included in init data.
	#
	# Unlike Array.prototype.length, String.prototype.length has the default
	# 'length' attributes of built-in objects: non-writable, non-enumerable,
	# non-configurable.

	#'length': 0,  # omitted; non-writable, non-enumerable, non-configurable

	'values': [
		# Internal empty string value.  Note that this value is not writable
		# which prevents a String instance's internal value also from being
		# written with standard methods.  The internal code creating String
		# instances has no such issues.
		{ 'name': internal('value'),            'value': '',	'attributes': '' },
	],
	'functions': [
		{ 'name': 'toString',			'native': 'duk_builtin_string_prototype_to_string',		'length': 0 },
		{ 'name': 'valueOf',			'native': 'duk_builtin_string_prototype_value_of',		'length': 0 },
		{ 'name': 'charAt',			'native': 'duk_builtin_string_prototype_char_at',		'length': 1 },
		{ 'name': 'charCodeAt',			'native': 'duk_builtin_string_prototype_char_code_at',		'length': 1 },
		{ 'name': 'concat',			'native': 'duk_builtin_string_prototype_concat',		'length': 1,	'varargs': True },
		{ 'name': 'indexOf',			'native': 'duk_builtin_string_prototype_index_of',		'length': 1,	'nargs': 2 },
		{ 'name': 'lastIndexOf',		'native': 'duk_builtin_string_prototype_last_index_of',		'length': 1,	'nargs': 2 },
		{ 'name': 'localeCompare',		'native': 'duk_builtin_string_prototype_locale_compare',	'length': 1 },
		{ 'name': 'match',			'native': 'duk_builtin_string_prototype_match',			'length': 1 },
		{ 'name': 'replace',			'native': 'duk_builtin_string_prototype_replace',		'length': 2 },
		{ 'name': 'search',			'native': 'duk_builtin_string_prototype_search',		'length': 1 },
		{ 'name': 'slice',			'native': 'duk_builtin_string_prototype_slice',			'length': 2 },
		{ 'name': 'split',			'native': 'duk_builtin_string_prototype_split',			'length': 2 },
		{ 'name': 'substring',			'native': 'duk_builtin_string_prototype_substring',		'length': 2 },
		{ 'name': 'toLowerCase',		'native': 'duk_builtin_string_prototype_to_lower_case',		'length': 0 },
		{ 'name': 'toLocaleLowerCase',		'native': 'duk_builtin_string_prototype_to_locale_lower_case',	'length': 0 },
		{ 'name': 'toUpperCase',		'native': 'duk_builtin_string_prototype_to_upper_case',		'length': 0 },
		{ 'name': 'toLocaleUpperCase',		'native': 'duk_builtin_string_prototype_to_locale_upper_case',	'length': 0 },
		{ 'name': 'trim',			'native': 'duk_builtin_string_prototype_trim',			'length': 0 },

		# Non-standard extension: E5 Section B.2.3

		{ 'name': 'substr',			'native': 'duk_builtin_string_prototype_substr',		'length': 2,	'section_b': True },
	],
}

bi_boolean_constructor = {
	'internal_prototype': 'bi_function_prototype',
	'external_prototype': 'bi_boolean_prototype',
	'class': 'Function',
	'name': 'Boolean',

	'length': 1,
	'native': 'duk_builtin_boolean_constructor',
	'callable': True,
	'constructable': True,

	'values': [],
	'functions': [],
}

bi_boolean_prototype = {
	'internal_prototype': 'bi_object_prototype',
	'external_constructor': 'bi_boolean_constructor',
	'class': 'Boolean',

	'values': [
		# Internal false boolean value.  Note that this value is not writable
		# which prevents a Boolean instance's internal value also from being
		# written with standard methods.  The internal code creating Boolean
		# instances has no such issues.
		{ 'name': internal('value'),            'value': False,		'attributes': '' },
	],
	'functions': [
		{ 'name': 'toString',			'native': 'duk_builtin_boolean_prototype_to_string',		'length': 0 },
		{ 'name': 'valueOf',			'native': 'duk_builtin_boolean_prototype_value_of',		'length': 0 },
	],
}

bi_number_constructor = {
	'internal_prototype': 'bi_function_prototype',
	'external_prototype': 'bi_number_prototype',
	'class': 'Function',
	'name': 'Number',

	'length': 1,
	'varargs': True,
	'native': 'duk_builtin_number_constructor',
	'callable': True,
	'constructable': True,

	'values': [
		{ 'name': 'MAX_VALUE',			'value': DBL_MAX_DOUBLE,		'attributes': '' },
		{ 'name': 'MIN_VALUE',			'value': DBL_MIN_DOUBLE,		'attributes': '' },
		{ 'name': 'NaN',			'value': DBL_NAN,			'attributes': '' },
		{ 'name': 'POSITIVE_INFINITY',		'value': DBL_POSITIVE_INFINITY,		'attributes': '' },
		{ 'name': 'NEGATIVE_INFINITY',		'value': DBL_NEGATIVE_INFINITY,		'attributes': '' },
	],
	'functions': [],
}

bi_number_prototype = {
	'internal_prototype': 'bi_object_prototype',
	'external_constructor': 'bi_number_constructor',
	'class': 'Number',

	'values': [
		# Internal 0.0 number value.  Note that this value is not writable
		# which prevents a Number instance's internal value also from being
		# written with standard methods.  The internal code creating Number
		# instances has no such issues.
		{ 'name': internal('value'),            'value': 0.0,		'attributes': '' }
	],
	'functions': [
		{ 'name': 'toString',			'native': 'duk_builtin_number_prototype_to_string',		'length': 1 },
		{ 'name': 'toLocaleString',		'native': 'duk_builtin_number_prototype_to_locale_string',	'length': 1 },
		{ 'name': 'valueOf',			'native': 'duk_builtin_number_prototype_value_of',		'length': 0 },
		{ 'name': 'toFixed',			'native': 'duk_builtin_number_prototype_to_fixed',		'length': 1 },
		{ 'name': 'toExponential',		'native': 'duk_builtin_number_prototype_to_exponential',	'length': 1 },
		{ 'name': 'toPrecision',		'native': 'duk_builtin_number_prototype_to_precision',		'length': 1 },
	],
}

bi_date_constructor = {
	'internal_prototype': 'bi_function_prototype',
	'external_prototype': 'bi_date_prototype',
	'class': 'Function',
	'name': 'Date',

	'length': 7,
	'varargs': True,
	'native': 'duk_builtin_date_constructor',
	'callable': True,
	'constructable': True,

	'values': [
	],
	'functions': [
		{ 'name': 'parse',			'native': 'duk_builtin_date_constructor_parse',			'length': 1 },
		{ 'name': 'UTC',			'native': 'duk_builtin_date_constructor_utc',			'length': 7,	'varargs': True },
		{ 'name': 'now',			'native': 'duk_builtin_date_constructor_now',			'length': 0 },
	],
}

bi_date_prototype = {
	'internal_prototype': 'bi_object_prototype',
	'external_constructor': 'bi_date_constructor',
	'class': 'Date',

	# The Date prototype is an instance of Date with [[PrimitiveValue]] NaN.

	# Setters with optional arguments must be varargs functions because
	# they must detect the number of parameters actually given (cannot
	# assume parameters not given are undefined).

	# Date.prototype.valueOf() and Date.prototype.getTime() have identical
	# behavior so they share the same C function, but have different
	# function instances.

	'values': [
		# Internal date value (E5 Section 15.9.5).
		#
		# Note: the value is writable, as you can e.g. do the following (V8):
		#  > Date.prototype.toString()
		#  'Invalid Date'
		#  > Date.prototype.setYear(2010)
		#  1262296800000
		#  > Date.prototype.toString()
		#  'Fri Jan 01 2010 00:00:00 GMT+0200 (EET)'

		{ 'name': internal('value'),            'value': DBL_NAN,	'attributes': 'w' }
	],
	'functions': [
		{ 'name': 'toString',			'native': 'duk_builtin_date_prototype_to_string',		'length': 0 },
		{ 'name': 'toDateString',		'native': 'duk_builtin_date_prototype_to_date_string',		'length': 0 },
		{ 'name': 'toTimeString',		'native': 'duk_builtin_date_prototype_to_time_string',		'length': 0 },
		{ 'name': 'toLocaleString',		'native': 'duk_builtin_date_prototype_to_locale_string',	'length': 0 },
		{ 'name': 'toLocaleDateString',		'native': 'duk_builtin_date_prototype_to_locale_date_string',	'length': 0 },
		{ 'name': 'toLocaleTimeString',		'native': 'duk_builtin_date_prototype_to_locale_time_string',	'length': 0 },
		{ 'name': 'valueOf',			'native': 'duk_builtin_date_prototype_value_of',		'length': 0 },
		{ 'name': 'getTime',			'native': 'duk_builtin_date_prototype_value_of',		'length': 0 },  # Native function shared on purpose
		{ 'name': 'getFullYear',		'native': 'duk_builtin_date_prototype_get_full_year',		'length': 0 },
		{ 'name': 'getUTCFullYear',		'native': 'duk_builtin_date_prototype_get_utc_full_year',	'length': 0 },
		{ 'name': 'getMonth',			'native': 'duk_builtin_date_prototype_get_month',		'length': 0 },
		{ 'name': 'getUTCMonth',		'native': 'duk_builtin_date_prototype_get_utc_month',		'length': 0 },
		{ 'name': 'getDate',			'native': 'duk_builtin_date_prototype_get_date',		'length': 0 },
		{ 'name': 'getUTCDate',			'native': 'duk_builtin_date_prototype_get_utc_date',		'length': 0 },
		{ 'name': 'getDay',			'native': 'duk_builtin_date_prototype_get_day',			'length': 0 },
		{ 'name': 'getUTCDay',			'native': 'duk_builtin_date_prototype_get_utc_day',		'length': 0 },
		{ 'name': 'getHours',			'native': 'duk_builtin_date_prototype_get_hours',		'length': 0 },
		{ 'name': 'getUTCHours',		'native': 'duk_builtin_date_prototype_get_utc_hours',		'length': 0 },
		{ 'name': 'getMinutes',			'native': 'duk_builtin_date_prototype_get_minutes',		'length': 0 },
		{ 'name': 'getUTCMinutes',		'native': 'duk_builtin_date_prototype_get_utc_minutes',		'length': 0 },
		{ 'name': 'getSeconds',			'native': 'duk_builtin_date_prototype_get_seconds',		'length': 0 },
		{ 'name': 'getUTCSeconds',		'native': 'duk_builtin_date_prototype_get_utc_seconds',		'length': 0 },
		{ 'name': 'getMilliseconds',		'native': 'duk_builtin_date_prototype_get_milliseconds',	'length': 0 },
		{ 'name': 'getUTCMilliseconds',		'native': 'duk_builtin_date_prototype_get_utc_milliseconds',	'length': 0 },
		{ 'name': 'getTimezoneOffset',		'native': 'duk_builtin_date_prototype_get_timezone_offset',	'length': 0 },
		{ 'name': 'setTime',			'native': 'duk_builtin_date_prototype_set_time',		'length': 1 },
		{ 'name': 'setMilliseconds',		'native': 'duk_builtin_date_prototype_set_milliseconds',	'length': 1 },
		{ 'name': 'setUTCMilliseconds',		'native': 'duk_builtin_date_prototype_set_utc_milliseconds',	'length': 1 },
		{ 'name': 'setSeconds',			'native': 'duk_builtin_date_prototype_set_seconds',		'length': 2,	'varargs': True },
		{ 'name': 'setUTCSeconds',		'native': 'duk_builtin_date_prototype_set_utc_seconds',		'length': 2,	'varargs': True },
		{ 'name': 'setMinutes',			'native': 'duk_builtin_date_prototype_set_minutes',		'length': 3,	'varargs': True },
		{ 'name': 'setUTCMinutes',		'native': 'duk_builtin_date_prototype_set_utc_minutes',		'length': 3,	'varargs': True },
		{ 'name': 'setHours',			'native': 'duk_builtin_date_prototype_set_hours',		'length': 4,	'varargs': True },
		{ 'name': 'setUTCHours',		'native': 'duk_builtin_date_prototype_set_utc_hours',		'length': 4,	'varargs': True },
		{ 'name': 'setDate',			'native': 'duk_builtin_date_prototype_set_date',		'length': 1 },
		{ 'name': 'setUTCDate',			'native': 'duk_builtin_date_prototype_set_utc_date',		'length': 1 },
		{ 'name': 'setMonth',			'native': 'duk_builtin_date_prototype_set_month',		'length': 2,	'varargs': True },
		{ 'name': 'setUTCMonth',		'native': 'duk_builtin_date_prototype_set_utc_month',		'length': 2,	'varargs': True },
		{ 'name': 'setFullYear',		'native': 'duk_builtin_date_prototype_set_full_year',		'length': 3,	'varargs': True },
		{ 'name': 'setUTCFullYear',		'native': 'duk_builtin_date_prototype_set_utc_full_year',	'length': 3,	'varargs': True },
		{ 'name': 'toUTCString',		'native': 'duk_builtin_date_prototype_to_utc_string',		'length': 0 },
		{ 'name': 'toISOString',		'native': 'duk_builtin_date_prototype_to_iso_string',		'length': 0 },
		{ 'name': 'toJSON',			'native': 'duk_builtin_date_prototype_to_json',			'length': 1 },

		# Non-standard extensions: E5 Section B.2.4, B.2.5, B.2.6
		#
		# 'length' values are not given explicitly but follows the general rule.
		# The lengths below agree with V8.

		{ 'name': 'getYear',			'native': 'duk_builtin_date_prototype_get_year',		'length': 0,	'section_b': True },
		{ 'name': 'setYear',			'native': 'duk_builtin_date_prototype_set_year',		'length': 1,	'section_b': True },

		# Note: toGMTString() is required to initially be the same Function object as the initial
		# Date.prototype.toUTCString.  In other words: Date.prototype.toGMTString === Date.prototype.toUTCString --> true.
		# This is implemented as a special post-tweak in duk_hthread_builtins.c, so the property is not included here.
		#
		# Note that while Smjs respects the requirement in E5 Section B.2.6, V8 does not.

		#{ 'name': 'toGMTString',		'native': 'duk_builtin_date_prototype_to_gmt_string',		'length': 0,	'section_b': True },

	],
}

bi_regexp_constructor = {
	'internal_prototype': 'bi_function_prototype',
	'external_prototype': 'bi_regexp_prototype',
	'class': 'Function',
	'name': 'RegExp',

	'length': 2,
	'native': 'duk_builtin_regexp_constructor',
	'callable': True,
	'constructable': True,

	'values': [],
	'functions': [],
}

bi_regexp_prototype = {
	'internal_prototype': 'bi_object_prototype',
	'external_constructor': 'bi_regexp_constructor',
	'class': 'RegExp',

	'values': [
		# RegExp internal value should match that of new RegExp() (E5 Sections 15.10.6
		# and 15.10.7), i.e. a bytecode sequence that matches an empty string.
		# The compiled regexp bytecode for that is embedded here, and must match the
		# defines in duk_regexp.h.
		#
		# Note that the property attributes are non-default.

		{
			# Compiled bytecode, must match duk_regexp.h.
			'name': internal('bytecode'),
			'value': unichr(0) +		# flags (none)
			         unichr(2) +		# nsaved == 2
			         unichr(1),		# DUK_REOP_MATCH
			'attributes': '',
		},
		{
			# An object created as new RegExp('') should have the escaped source
			# '(?:)' (E5 Section 15.10.4.1).  However, at least V8 and Smjs seem
			# to have an empty string here.

			'name': 'source',
			'value': '(?:)',
			'attributes': '',
		},
		{
			'name': 'global',
			'value': False,
			'attributes': '',
		},
		{
			'name': 'ignoreCase',
			'value': False,
			'attributes': '',
		},
		{
			'name': 'multiline',
			'value': False,
			'attributes': '',
		},
		{
			# 'lastIndex' is writable, even in the RegExp.prototype object.
			# This matches at least V8.

			'name': 'lastIndex',
			'value': 0,
			'attributes': 'w',
		},

	],
	'functions': [
		{ 'name': 'exec',			'native': 'duk_builtin_regexp_prototype_exec',		'length': 1 },
		{ 'name': 'test',			'native': 'duk_builtin_regexp_prototype_test',		'length': 1 },
		{ 'name': 'toString',			'native': 'duk_builtin_regexp_prototype_to_string',	'length': 0 },
	],
}

bi_error_constructor = {
	'internal_prototype': 'bi_function_prototype',
	'external_prototype': 'bi_error_prototype',
	'class': 'Function',
	'name': 'Error',

	'length': 1,
	'native': 'duk_builtin_error_constructor',
	'callable': True,
	'constructable': True,

	'values': [],
	'functions': [],
}

bi_error_prototype = {
	'internal_prototype': 'bi_object_prototype',
	'external_constructor': 'bi_error_constructor',
	'class': 'Error',

	'values': [
		# Property attributes:
		#
		# 'message' is writable and deletable.  This matches the default
		# attributes of 'wc'.  V8 and Smjs both match this.
		#
		# 'name' is writable and deletable.  This matches the default
		# attributes too.  Smjs behaves like this, but in V8 'name' is
		# non-writable:
		#
		#  > Object.getOwnPropertyDescriptor(Error.prototype, 'name')
		#  { value: 'Error',
		#    writable: false,
		#    enumerable: false,
		#    configurable: false }
		#
		# We go with the standard attributes ("wc").

		{ 'name': 'name',			'value': 'Error' },
		{ 'name': 'message',			'value': '' },
	],
	'functions': [
		{ 'name': 'toString',			'native': 'duk_builtin_error_prototype_to_string',		'length': 0 },
	],
}

# NOTE: Error subclass prototypes have an empty 'message' property, even
# though one is inherited already from Error prototype (E5 Section 15.11.7.10).
#
# V8 does not respect this: Error subclasses ("native Errors" in E5 spec)
# do not have a 'message' property at all.  Also, in V8 their 'name' property
# is not writable and configurable as E5 requires.

bi_eval_error_constructor = {
	'internal_prototype': 'bi_function_prototype',
	'external_prototype': 'bi_eval_error_prototype',
	'class': 'Function',
	'name': 'EvalError',

	'length': 1,
	'native': 'duk_builtin_eval_error_constructor',
	'callable': True,
	'constructable': True,

	'values': [],
	'functions': [],
}

bi_eval_error_prototype = {
	'internal_prototype': 'bi_error_prototype',
	'external_constructor': 'bi_eval_error_constructor',
	'class': 'Error',

	'values': [
		{ 'name': 'name',			'value': 'EvalError' },
		{ 'name': 'message',			'value': '' },
	],
	'functions': [],
}

bi_range_error_constructor = {
	'internal_prototype': 'bi_function_prototype',
	'external_prototype': 'bi_range_error_prototype',
	'class': 'Function',
	'name': 'RangeError',

	'length': 1,
	'native': 'duk_builtin_range_error_constructor',
	'callable': True,
	'constructable': True,

	'values': [],
	'functions': [],
}

bi_range_error_prototype = {
	'internal_prototype': 'bi_error_prototype',
	'external_constructor': 'bi_range_error_constructor',
	'class': 'Error',

	'values': [
		{ 'name': 'name',			'value': 'RangeError' },
		{ 'name': 'message',			'value': '' },
	],
	'functions': [],
}

bi_reference_error_constructor = {
	'internal_prototype': 'bi_function_prototype',
	'external_prototype': 'bi_reference_error_prototype',
	'class': 'Function',
	'name': 'ReferenceError',

	'length': 1,
	'native': 'duk_builtin_reference_error_constructor',
	'callable': True,
	'constructable': True,

	'values': [],
	'functions': [],
}

bi_reference_error_prototype = {
	'internal_prototype': 'bi_error_prototype',
	'external_constructor': 'bi_reference_error_constructor',
	'class': 'Error',

	'values': [
		{ 'name': 'name',			'value': 'ReferenceError' },
		{ 'name': 'message',			'value': '' },
	],
	'functions': [],
}

bi_syntax_error_constructor = {
	'internal_prototype': 'bi_function_prototype',
	'external_prototype': 'bi_syntax_error_prototype',
	'class': 'Function',
	'name': 'SyntaxError',

	'length': 1,
	'native': 'duk_builtin_syntax_error_constructor',
	'callable': True,
	'constructable': True,

	'values': [],
	'functions': [],
}

bi_syntax_error_prototype = {
	'internal_prototype': 'bi_error_prototype',
	'external_constructor': 'bi_syntax_error_constructor',
	'class': 'Error',

	'values': [
		{ 'name': 'name',			'value': 'SyntaxError' },
		{ 'name': 'message',			'value': '' },
	],
	'functions': [],
}

bi_type_error_constructor = {
	'internal_prototype': 'bi_function_prototype',
	'external_prototype': 'bi_type_error_prototype',
	'class': 'Function',
	'name': 'TypeError',

	'length': 1,
	'native': 'duk_builtin_type_error_constructor',
	'callable': True,
	'constructable': True,

	'values': [],
	'functions': [],
}

bi_type_error_prototype = {
	'internal_prototype': 'bi_error_prototype',
	'external_constructor': 'bi_type_error_constructor',
	'class': 'Error',

	'values': [
		{ 'name': 'name',			'value': 'TypeError' },
		{ 'name': 'message',			'value': '' },
	],
	'functions': [],
}

bi_uri_error_constructor = {
	'internal_prototype': 'bi_function_prototype',
	'external_prototype': 'bi_uri_error_prototype',
	'class': 'Function',
	'name': 'URIError',

	'length': 1,
	'native': 'duk_builtin_uri_error_constructor',
	'callable': True,
	'constructable': True,

	'values': [],
	'functions': [],
}

bi_uri_error_prototype = {
	'internal_prototype': 'bi_error_prototype',
	'external_constructor': 'bi_uri_error_constructor',
	'class': 'Error',

	'values': [
		{ 'name': 'name',			'value': 'URIError' },
		{ 'name': 'message',			'value': '' },
	],
	'functions': [],
}

bi_math = {
	'internal_prototype': 'bi_object_prototype',
	# apparently no external 'prototype' property
	# apparently no external 'constructor' property
	'class': 'Math',

	'values': [
		{ 'name': 'E',				'value': DBL_E,			'attributes': '' },
		{ 'name': 'LN10',			'value': DBL_LN10,		'attributes': '' },
		{ 'name': 'LN2',			'value': DBL_LN2,		'attributes': '' },
		{ 'name': 'LOG2E',			'value': DBL_LOG2E,		'attributes': '' },
		{ 'name': 'LOG10E',			'value': DBL_LOG10E,		'attributes': '' },
		{ 'name': 'PI',				'value': DBL_PI,		'attributes': '' },
		{ 'name': 'SQRT1_2',			'value': DBL_SQRT1_2,		'attributes': '' },
		{ 'name': 'SQRT2',			'value': DBL_SQRT2,		'attributes': '' },
	],
	'functions': [
		{ 'name': 'abs',			'native': 'duk_builtin_math_object_abs',		'length': 1 },
		{ 'name': 'acos',			'native': 'duk_builtin_math_object_acos',		'length': 1 },
		{ 'name': 'asin',			'native': 'duk_builtin_math_object_asin',		'length': 1 },
		{ 'name': 'atan',			'native': 'duk_builtin_math_object_atan',		'length': 1 },
		{ 'name': 'atan2',			'native': 'duk_builtin_math_object_atan2',		'length': 2 },
		{ 'name': 'ceil',			'native': 'duk_builtin_math_object_ceil',		'length': 1 },
		{ 'name': 'cos',			'native': 'duk_builtin_math_object_cos',		'length': 1 },
		{ 'name': 'exp',			'native': 'duk_builtin_math_object_exp',		'length': 1 },
		{ 'name': 'floor',			'native': 'duk_builtin_math_object_floor',		'length': 1 },
		{ 'name': 'log',			'native': 'duk_builtin_math_object_log',		'length': 1 },
		{ 'name': 'max',			'native': 'duk_builtin_math_object_max',		'length': 2,	'varargs': True },
		{ 'name': 'min',			'native': 'duk_builtin_math_object_min',		'length': 2,	'varargs': True },
		{ 'name': 'pow',			'native': 'duk_builtin_math_object_pow',		'length': 2 },
		{ 'name': 'random',			'native': 'duk_builtin_math_object_random',		'length': 0 },
		{ 'name': 'round',			'native': 'duk_builtin_math_object_round',		'length': 1 },
		{ 'name': 'sin',			'native': 'duk_builtin_math_object_sin',		'length': 1 },
		{ 'name': 'sqrt',			'native': 'duk_builtin_math_object_sqrt',		'length': 1 },
		{ 'name': 'tan',			'native': 'duk_builtin_math_object_tan',		'length': 1 },
	],
}

bi_json = {
	'internal_prototype': 'bi_object_prototype',
	# apparently no external 'prototype' property
	# apparently no external 'constructor' property
	'class': 'JSON',

	'values': [],
	'functions': [
		{ 'name': 'parse',			'native': 'duk_builtin_json_object_parse',		'length': 2 },
		{ 'name': 'stringify',			'native': 'duk_builtin_json_object_stringify',		'length': 3 },
	],
}

# E5 Section 13.2.3
bi_type_error_thrower = {
	'internal_prototype': 'bi_function_prototype',
	'class': 'Function',
	'name': 'ThrowTypeError',  # FIXME: matches V8

	'length': 0,
	'native': 'duk_builtin_type_error_thrower',
	'callable': True,
	'constructable': False,  # This is not clearly specified, but [[Construct]] is not set in E5 Section 13.2.3.

	'values': [],
	'functions': [],
}

bi_duk = {
	'internal_prototype': 'bi_object_prototype',
	'class': 'Object',

	'values': [
		# Note: 'version' and 'build' are added from parameter file.
		# They are intentionally non-writable and non-configurable now.
	],
	'functions': [
		# FIXME: 'yield' is a bad method name, since yield is a reserved word
		{ 'name': 'print',			'native': 'duk_builtin_duk_object_print',		'length': 1,	'varargs': True },
		{ 'name': 'addr',			'native': 'duk_builtin_duk_object_addr',                'length': 1 },
		{ 'name': 'refc',			'native': 'duk_builtin_duk_object_refc',                'length': 1 },
		{ 'name': 'gc',				'native': 'duk_builtin_duk_object_gc',			'length': 1 },
		{ 'name': 'getFinalizer',		'native': 'duk_builtin_duk_object_get_finalizer',	'length': 1 },
		{ 'name': 'setFinalizer',		'native': 'duk_builtin_duk_object_set_finalizer',	'length': 2 },
		{ 'name': 'spawn',			'native': 'duk_builtin_duk_object_spawn',		'length': 1 },
		{ 'name': 'yield',			'native': 'duk_builtin_duk_object_yield',		'length': 2 },
		{ 'name': 'resume',			'native': 'duk_builtin_duk_object_resume',		'length': 3 },
		{ 'name': 'curr',			'native': 'duk_builtin_duk_object_curr',		'length': 0 },
		{ 'name': 'time',			'native': 'duk_builtin_duk_object_time',		'length': 0 },
		{ 'name': 'enc',			'native': 'duk_builtin_duk_object_enc',			'length': 2 },
		{ 'name': 'dec',			'native': 'duk_builtin_duk_object_dec',			'length': 2 },
		{ 'name': 'sleep',			'native': 'duk_builtin_duk_object_sleep',		'length': 1 },
	],
}

# non-standard built-in prototype object
# FIXME: where to keep reachable? not global object?
bi_thread_prototype = {
	'internal_prototype': 'bi_object_prototype',
	# 'external_constructor': '',  # FIXME: __duk__.Thread?
	'class': 'Object',

	# Note: we don't keep up with the E5 "convention" that prototype objects
	# are some faux instances of their type (e.g. Date.prototype is a Date
	# instance).
	#
	# Also, we don't currently have a "constructor" property because there is
	# no explicit constructor object.

	'values': [
	],
	'functions': [
		{ 'name': 'toString',			'native': 'duk_builtin_thread_prototype_to_string',	'length': 0 },
	],
}

# This is an Error *instance* used to avoid allocation when a "double error" occurs.
# The object is "frozen and sealed" to avoid code accidentally modifying the instance.
# This is important because the error is rethrown as is.

bi_double_error = {
	'internal_prototype': 'bi_error_prototype',
	'class': 'Error',
	'extensible': False,

	# Note: this is the only non-extensible built-in, so there is special
	# post-tweak in duk_hthread_builtins.c to handle this.

	'values': [
		{ 'name': 'name',			'value': 'DoubleError',				'attributes': '' },
		{ 'name': 'message',                    'value': 'error in error handling', 		'attributes': '' },
	],
	'functions': [
	],
}

#
#  Built-ins table.  The ordering determines ordering for the DUK_BIDX_XXX constants.
#

builtins = [
	{ 'id': 'bi_global',				'info': bi_global },
	{ 'id': 'bi_global_env',			'info': bi_global_env },
	{ 'id': 'bi_object_constructor',		'info': bi_object_constructor },
	{ 'id': 'bi_object_prototype',			'info': bi_object_prototype },
	{ 'id': 'bi_function_constructor',		'info': bi_function_constructor },
	{ 'id': 'bi_function_prototype',		'info': bi_function_prototype },
	{ 'id': 'bi_array_constructor',			'info': bi_array_constructor },
	{ 'id': 'bi_array_prototype',			'info': bi_array_prototype },
	{ 'id': 'bi_string_constructor',		'info': bi_string_constructor },
	{ 'id': 'bi_string_prototype',			'info': bi_string_prototype },
	{ 'id': 'bi_boolean_constructor',		'info': bi_boolean_constructor },
	{ 'id': 'bi_boolean_prototype',			'info': bi_boolean_prototype },
	{ 'id': 'bi_number_constructor',		'info': bi_number_constructor },
	{ 'id': 'bi_number_prototype',			'info': bi_number_prototype },
	{ 'id': 'bi_date_constructor',			'info': bi_date_constructor },
	{ 'id': 'bi_date_prototype',			'info': bi_date_prototype },
	{ 'id': 'bi_regexp_constructor',		'info': bi_regexp_constructor },
	{ 'id': 'bi_regexp_prototype',			'info': bi_regexp_prototype },
	{ 'id': 'bi_error_constructor',			'info': bi_error_constructor },
	{ 'id': 'bi_error_prototype',			'info': bi_error_prototype },
	{ 'id': 'bi_eval_error_constructor',		'info': bi_eval_error_constructor },
	{ 'id': 'bi_eval_error_prototype',		'info': bi_eval_error_prototype },
	{ 'id': 'bi_range_error_constructor',		'info': bi_range_error_constructor },
	{ 'id': 'bi_range_error_prototype',		'info': bi_range_error_prototype },
	{ 'id': 'bi_reference_error_constructor',	'info': bi_reference_error_constructor },
	{ 'id': 'bi_reference_error_prototype',		'info': bi_reference_error_prototype },
	{ 'id': 'bi_syntax_error_constructor',		'info': bi_syntax_error_constructor },
	{ 'id': 'bi_syntax_error_prototype',		'info': bi_syntax_error_prototype },
	{ 'id': 'bi_type_error_constructor',		'info': bi_type_error_constructor },
	{ 'id': 'bi_type_error_prototype',		'info': bi_type_error_prototype },
	{ 'id': 'bi_uri_error_constructor',		'info': bi_uri_error_constructor },
	{ 'id': 'bi_uri_error_prototype',		'info': bi_uri_error_prototype },
	{ 'id': 'bi_math',				'info': bi_math },
	{ 'id': 'bi_json',				'info': bi_json },
	{ 'id': 'bi_type_error_thrower',		'info': bi_type_error_thrower },
	{ 'id': 'bi_duk',				'info': bi_duk },
	{ 'id': 'bi_thread_prototype',			'info': bi_thread_prototype },
	{ 'id': 'bi_double_error',                      'info': bi_double_error },
]

builtin_indexes = {}

idx = 0
for bi in builtins:
	builtin_indexes[bi['id']] = idx
	idx += 1

#
#  Functions to generate the init bitstream and headers/sources
#

native_func_hash = {}
native_func_list = []

# array workaround for Python scope
count_builtins = [0]
count_normal_props = [0]
count_function_props = [0]

def get_native_funcs(bi):
	if bi.has_key('native'):
		native_func = bi['native']
		native_func_hash[native_func] = -1

	for funspec in bi['functions']:
		if funspec.has_key('native'):
			native_func = funspec['native']
			native_func_hash[native_func] = -1

def number_native_funcs():
	k = native_func_hash.keys()
	k.sort()
	idx = 0
	for i in k:
		native_func_hash[i] = idx
		native_func_list.append(i)
		idx += 1

def encode_property_flags(flags):
	# Note: must match duk_hobject.h

	res = 0
	nflags = 0
	if 'w' in flags:
		nflags += 1
		res = res | PROPDESC_FLAG_WRITABLE
	if 'e' in flags:
		nflags += 1
		res = res | PROPDESC_FLAG_ENUMERABLE
	if 'c' in flags:
		nflags += 1
		res = res | PROPDESC_FLAG_CONFIGURABLE

	# There are no accessor properties now, so DUK_PROPDESC_FLAG_ACCESSOR
	# is always zero now

	if nflags != len(flags):
		raise Exception('unsupported flags: %s' % repr(flags))

	return res

def generate_properties_data_for_builtin(opts, be, bi):
	count_builtins[0] += 1

	if bi.has_key('internal_prototype'):
		be.bits(builtin_indexes[bi['internal_prototype']], BIDX_BITS)
	else:
		be.bits(NO_BIDX_MARKER, BIDX_BITS)

	if bi.has_key('external_prototype'):
		be.bits(builtin_indexes[bi['external_prototype']], BIDX_BITS)
	else:
		be.bits(NO_BIDX_MARKER, BIDX_BITS)

	if bi.has_key('external_constructor'):
		be.bits(builtin_indexes[bi['external_constructor']], BIDX_BITS)
	else:
		be.bits(NO_BIDX_MARKER, BIDX_BITS)

	# Filter values and functions
	values = []
	for valspec in bi['values']:
		if valspec.has_key('section_b') and valspec['section_b'] and not opts.ext_section_b:
			continue
		if valspec.has_key('browser') and valspec['browser'] and not opts.ext_browser_like:
			continue
		values.append(valspec)

	functions = []
	for valspec in bi['functions']:
		if valspec.has_key('section_b') and valspec['section_b'] and not opts.ext_section_b:
			continue
		if valspec.has_key('browser') and valspec['browser'] and not opts.ext_browser_like:
			continue
		functions.append(valspec)

	be.bits(len(values), NUM_NORMAL_PROPS_BITS)

	for valspec in values:
		count_normal_props[0] += 1

		# NOTE: we rely on there being less than 256 built-in strings
		stridx = duk_strings.real_name_to_index[valspec['name']]
		val = valspec['value']

		be.bits(stridx, STRIDX_BITS)

		if valspec['name'] == 'length':
			default_attrs = LENGTH_PROPERTY_ATTRIBUTES
		else:
			default_attrs = DEFAULT_PROPERTY_ATTRIBUTES
		attrs = default_attrs
		if valspec.has_key('attributes'):
			attrs = valspec['attributes']

		if attrs != default_attrs:
			#print 'non-default attributes: %s -> %r (default %r)' % (valspec['name'], attrs, default_attrs)
			be.bits(1, 1)  # flag: have custom attributes
			be.bits(encode_property_flags(attrs), PROP_FLAGS_BITS)
		else:
			be.bits(0, 1)  # flag: no custom attributes

		if isinstance(val, bool):
			if val == True:
				be.bits(PROP_TYPE_BOOLEAN_TRUE, PROP_TYPE_BITS)
			else:
				be.bits(PROP_TYPE_BOOLEAN_FALSE, PROP_TYPE_BITS)
		elif val == UNDEFINED:
			be.bits(PROP_TYPE_UNDEFINED, PROP_TYPE_BITS)
		elif isinstance(val, (float, int)):
			be.bits(PROP_TYPE_DOUBLE, PROP_TYPE_BITS)
			val = float(val)

			# encoding of double must match target architecture byte order
			bo = opts.byte_order
			if bo == 'big':
				data = struct.pack('>d', val)	# 01234567
			elif bo == 'little':
				data = struct.pack('<d', val)	# 76543210
			elif bo == 'middle':	# arm
				data = struct.pack('<d', val)	# 32107654
				data = data[4:8] + data[0:4]
			else:
				raise Exception('unsupported byte order: %s' % repr(bo))

			if len(data) != 8:
				raise Exception('internal error')
			be.string(data)
		elif isinstance(val, str) or isinstance(val, unicode):
			if isinstance(val, unicode):
				# Note: non-ASCII characters will not currently work,
				# because bits/char is too low.
				val = val.encode('utf-8')

			if duk_strings.real_name_to_index.has_key(val):
				# String value is in built-in string table -> encode
				# using a string index.  This saves some space,
				# especially for the 'name' property of errors
				# ('EvalError' etc).

				stridx = duk_strings.real_name_to_index[val]
				be.bits(PROP_TYPE_STRIDX, PROP_TYPE_BITS)
				be.bits(stridx, STRIDX_BITS)
			else:
				# Not in string table -> encode as raw 7-bit value

				be.bits(PROP_TYPE_STRING, PROP_TYPE_BITS)
				be.bits(len(val), STRING_LENGTH_BITS)
				for i in xrange(len(val)):
					t = ord(val[i])
					be.bits(t, STRING_CHAR_BITS)
		elif isinstance(val, dict):
			if val['type'] == 'builtin':
				be.bits(PROP_TYPE_BUILTIN, PROP_TYPE_BITS)
				be.bits(builtin_indexes[val['id']], BIDX_BITS)
			else:
				raise Exception('unsupported value: %s' % repr(val))
		else:
			raise Exception('unsupported value: %s' % repr(val))

	be.bits(len(functions), NUM_FUNC_PROPS_BITS)

	for funspec in functions:
		count_function_props[0] += 1

		# NOTE: we rely on there being less than 256 built-in strings
		# and built-in native functions

		stridx = duk_strings.real_name_to_index[funspec['name']]
		be.bits(stridx, STRIDX_BITS)

		natidx = native_func_hash[funspec['native']]
		be.bits(natidx, NATIDX_BITS)

		length = funspec['length']
		be.bits(length, LENGTH_PROP_BITS)

		if funspec.has_key('varargs'):
			be.bits(1, 1)  # flag: non-default nargs
			be.bits(NARGS_VARARGS_MARKER, NARGS_BITS)
		elif funspec.has_key('nargs'):
			be.bits(1, 1)  # flag: non-default nargs
			be.bits(funspec['nargs'], NARGS_BITS)
		else:
			be.bits(0, 1)  # flag: default nargs OK

def generate_creation_data_for_builtin(opts, be, bi):
	class_num = classToNumber(bi['class'])
	be.bits(class_num, CLASS_BITS)

	if bi.has_key('length'):
		be.bits(1, 1)  # flag: have length
		be.bits(bi['length'], LENGTH_PROP_BITS)
	else:
		be.bits(0, 1)  # flag: no length

	# This is a very unfortunate format; 'length' property of a top
	# level object may be non-standard.  However, this is only the
	# case for the Array prototype, whose 'length' property has the
	# attributes expected of an Array instance.  This is handled
	# with custom code in duk_hthread_builtins.c

	len_attrs = LENGTH_PROPERTY_ATTRIBUTES
	if bi.has_key('length_attributes'):
		len_attrs = bi['length_attributes']

	if len_attrs != LENGTH_PROPERTY_ATTRIBUTES:
		if bi['class'] != 'Array':  # Array.prototype is the only one with this class
			raise Exception('non-default length attribute for unexpected object')

	# For 'Function' classed objects, emit the native function stuff.
	# Unfortunately this is more or less a copy of what we do for
	# function properties now.  This should be addressed if a rework
	# on the init format is done.

	if bi['class'] == 'Function':
		length = bi['length']

		natidx = native_func_hash[bi['native']]
		be.bits(natidx, NATIDX_BITS)

		stridx = duk_strings.real_name_to_index[bi['name']]
		be.bits(stridx, STRIDX_BITS)

		if bi.has_key('varargs'):
			be.bits(1, 1)  # flag: non-default nargs
			be.bits(NARGS_VARARGS_MARKER, NARGS_BITS)
		elif bi.has_key('nargs'):
			be.bits(1, 1)  # flag: non-default nargs
			be.bits(bi['nargs'], NARGS_BITS)
		else:
			be.bits(0, 1)  # flag: default nargs OK

	# All Function-classed global level objects are callable
	# (have [[Call]]) but not all are consturactlr (have
	# [[Construct]]).  Flag that.

	if bi['class'] == 'Function':
		assert(bi.has_key('callable'))
		assert(bi['callable'] == True)

		if bi.has_key('constructable') and bi['constructable'] == True:
			be.bits(1, 1)	# flag: constructable
		else:
			be.bits(0, 1)	# flag: not constructable

def generate_builtin_init_data(opts):
	be = dukutil.BitEncoder()

	for bi in builtins:
		get_native_funcs(bi['info'])
	number_native_funcs()

	# First, emit the control data required for creating correct
	# objects.

	for bi in builtins:
		generate_creation_data_for_builtin(opts, be, bi['info'])

	# Then, emit object properties.

	for bi in builtins:
		generate_properties_data_for_builtin(opts, be, bi['info'])

	return be.getByteString()

def write_native_func_array(genc):
	genc.emitLine('/* native functions: %d */' % len(native_func_list))
	genc.emitLine('duk_c_function duk_builtin_native_functions[] = {')
	for i in native_func_list:
		genc.emitLine('\t(duk_c_function) %s,' % i)
	genc.emitLine('};')

def generate_define_names(id):
	t1 = id.upper().split('_')
	t2 = '_'.join(t1[1:])  # bi_foo_bar -> FOO_BAR
	return 'DUK_BIDX_' + t2, 'DUK_BUILTIN_' + t2

#
#  Main
#

if __name__ == '__main__':
	parser = optparse.OptionParser()
	parser.add_option('--buildinfo', dest='buildinfo')
	parser.add_option('--strings-py', dest='strings_py')
	parser.add_option('--out-header', dest='out_header')
	parser.add_option('--out-source', dest='out_source')
	parser.add_option('--out-bin', dest='out_bin')
	parser.add_option('--byte-order', dest='byte_order')
	parser.add_option('--ext-section-b', dest='ext_section_b', default=True)
	parser.add_option('--ext-browser-like', dest='ext_browser_like', default=True)
	(opts, args) = parser.parse_args()

	import imp
	duk_strings = imp.load_source('duk_strings', opts.strings_py)

	f = open(opts.buildinfo, 'rb')
	params = dukutil.json_decode(f.read().strip())
	bi_duk['values'].insert(0, { 'name': 'version', 'value': params['version'], 'attributes': '' })
	bi_duk['values'].insert(1, { 'name': 'build', 'value': params['build'], 'attributes': '' })
	f.close()

	data = generate_builtin_init_data(opts)

	# write raw data file
	f = open(opts.out_bin, 'wb')
	f.write(data)
	f.close()

	# write C source file
	genc = dukutil.GenerateC()
	genc.emitHeader('genbuiltins.py')
	genc.emitLine('#include "duk_internal.h"')
	genc.emitLine('')
	write_native_func_array(genc)
	genc.emitLine('')
	genc.emitArray(data, 'duk_builtins_data')

	f = open(opts.out_source, 'wb')
	f.write(genc.getString())
	f.close()

	# write C header file
	genc = dukutil.GenerateC()
	genc.emitHeader('genbuiltins.py')
	genc.emitLine('#ifndef __DUK_BUILTINS_H')
	genc.emitLine('#define __DUK_BUILTINS_H 1')
	genc.emitLine('')
	genc.emitLine('extern duk_c_function duk_builtin_native_functions[];')
	genc.emitLine('')
	genc.emitLine('extern char duk_builtins_data[];')
	genc.emitLine('')
	genc.emitDefine('DUK_BUILTINS_DATA_LENGTH', len(data))
	genc.emitLine('')
	for idx,t in enumerate(builtins):
		def_name1, def_name2 = generate_define_names(t['id'])
		genc.emitDefine(def_name1, idx)
	genc.emitLine('')
	genc.emitDefine('DUK_NUM_BUILTINS', len(builtins))
	genc.emitLine('')
	genc.emitLine('#endif  /* __DUK_BUILTINS_H */')

	f = open(opts.out_header, 'wb')
	f.write(genc.getString())
	f.close()

	print '%d bytes of built-in init data, %d built-in objects, %d normal props, %d func props' % \
		(len(data), count_builtins[0], count_normal_props[0], count_function_props[0])


