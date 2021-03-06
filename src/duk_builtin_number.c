/*
 *  Number built-ins
 */

/* FIXME: needs to be refactored when exact parsing / string conversion
 * primitives are implemented.
 */

#include "duk_internal.h"

static double push_this_number_plain(duk_context *ctx) {
	duk_hobject *h;

	/* Number built-in accepts a plain number or a Number object (whose
	 * internal value is operated on).  Other types cause TypeError.
	 */

	duk_push_this(ctx);
	if (duk_is_number(ctx, -1)) {
		DUK_DDDPRINT("plain number value: %!T", duk_get_tval(ctx, -1));
		goto done;
	}
	h = duk_get_hobject(ctx, -1);
	if (!h || 
	    (DUK_HOBJECT_GET_CLASS_NUMBER(h) != DUK_HOBJECT_CLASS_NUMBER)) {
		DUK_DDDPRINT("unacceptable this value: %!T", duk_get_tval(ctx, -1));
		DUK_ERROR((duk_hthread *) ctx, DUK_ERR_TYPE_ERROR, "expected a number");
	}
	duk_get_prop_stridx(ctx, -1, DUK_STRIDX_INT_VALUE);
	DUK_ASSERT(duk_is_number(ctx, -1));
	DUK_DDDPRINT("number object: %!T, internal value: %!T", duk_get_tval(ctx, -2), duk_get_tval(ctx, -1));
	duk_remove(ctx, -2);

 done:
	return duk_get_number(ctx, -1);
}

int duk_builtin_number_constructor(duk_context *ctx) {
	int nargs;
	duk_hobject *h_this;

	/*
	 *  The Number constructor uses ToNumber(arg) for number coercion
	 *  (coercing an undefined argument to NaN).  However, if the
	 *  argument is not given at all, +0 must be used instead.  To do
	 *  this, a vararg function is used.
	 */

	nargs = duk_get_top(ctx);
	if (nargs == 0) {
		duk_push_int(ctx, 0);
	}
	duk_to_number(ctx, 0);
	duk_set_top(ctx, 1);
	DUK_ASSERT_TOP(ctx, 1);

	if (!duk_is_constructor_call(ctx)) {
		return 1;
	}

	/*
	 *  E5 Section 15.7.2.1 requires that the constructed object
	 *  must have the original Number.prototype as its internal
	 *  prototype.  However, since Number.prototype is non-writable
	 *  and non-configurable, this doesn't have to be enforced here:
	 *  The default object (bound to 'this') is OK, though we have
	 *  to change its class.
	 *
	 *  Internal value set to ToNumber(arg) or +0; if no arg given,
	 *  ToNumber(undefined) = NaN, so special treatment is needed
	 *  (above).  String internal value is immutable.
	 */

	/* FIXME: helper */
	duk_push_this(ctx);
	h_this = duk_get_hobject(ctx, -1);
	DUK_ASSERT(h_this != NULL);
	DUK_HOBJECT_SET_CLASS_NUMBER(h_this, DUK_HOBJECT_CLASS_NUMBER);

	DUK_ASSERT(h_this->prototype == ((duk_hthread *) ctx)->builtins[DUK_BIDX_NUMBER_PROTOTYPE]);
	DUK_ASSERT(DUK_HOBJECT_GET_CLASS_NUMBER(h_this) == DUK_HOBJECT_CLASS_NUMBER);
	DUK_ASSERT(DUK_HOBJECT_HAS_EXTENSIBLE(h_this));

	duk_dup(ctx, 0);  /* -> [ val obj val ] */
	duk_def_prop_stridx(ctx, -2, DUK_STRIDX_INT_VALUE, DUK_PROPDESC_FLAGS_NONE);
	return 0;  /* no return value -> don't replace created value */
}

int duk_builtin_number_prototype_value_of(duk_context *ctx) {
	(void) push_this_number_plain(ctx);
	return 1;
}

int duk_builtin_number_prototype_to_string(duk_context *ctx) {
	int radix;
	int n2s_flags;

	(void) push_this_number_plain(ctx);
	if (duk_is_undefined(ctx, 0)) {
		radix = 10;
	} else {
		radix = duk_to_int_check_range(ctx, 0, 2, 36);
	}
	DUK_DDDPRINT("radix=%d", radix);

	n2s_flags = 0;

	duk_numconv_stringify(ctx,
	                      radix /*radix*/,
	                      0 /*digits*/,
	                      n2s_flags /*flags*/);
	return 1;
}

int duk_builtin_number_prototype_to_locale_string(duk_context *ctx) {
	/* FIXME: just use toString() for now; permitted although not recommended.
	 * nargs==1, so radix is passed to toString().
	 */
	return duk_builtin_number_prototype_to_string(ctx);
}

/*
 *  toFixed(), toExponential(), toPrecision()
 */

/* FIXME: shared helper for toFixed(), toExponential(), toPrecision()? */

int duk_builtin_number_prototype_to_fixed(duk_context *ctx) {
	int frac_digits;
	double d;
	int c;
	int n2s_flags;

	frac_digits = duk_to_int_check_range(ctx, 0, 0, 20);
	d = push_this_number_plain(ctx);

	c = DUK_FPCLASSIFY(d);
	if (c == DUK_FP_NAN || c == DUK_FP_INFINITE) {
		goto use_to_string;
	}

	if (d >= 1.0e21 || d <= -1.0e21) {
		goto use_to_string;
	}

	n2s_flags = DUK_N2S_FLAG_FIXED_FORMAT |
	            DUK_N2S_FLAG_FRACTION_DIGITS;

	duk_numconv_stringify(ctx,
	                      10 /*radix*/,
	                      frac_digits /*digits*/,
	                      n2s_flags /*flags*/);
	return 1;

 use_to_string:
	DUK_ASSERT_TOP(ctx, 2);
	duk_to_string(ctx, -1);
	return 1;
}

int duk_builtin_number_prototype_to_exponential(duk_context *ctx) {
	int frac_undefined;
	int frac_digits;
	double d;
	int c;
	int n2s_flags;

	d = push_this_number_plain(ctx);

	frac_undefined = duk_is_undefined(ctx, 0);
	duk_to_int(ctx, 0);  /* for side effects */

	c = DUK_FPCLASSIFY(d);
	if (c == DUK_FP_NAN || c == DUK_FP_INFINITE) {
		goto use_to_string;
	}

	frac_digits = duk_to_int_check_range(ctx, 0, 0, 20);

	n2s_flags = DUK_N2S_FLAG_FORCE_EXP |
	           (frac_undefined ? 0 : DUK_N2S_FLAG_FIXED_FORMAT);

	duk_numconv_stringify(ctx,
	                      10 /*radix*/,
	                      frac_digits + 1 /*leading digit + fractions*/,
	                      n2s_flags /*flags*/);
	return 1;

 use_to_string:
	DUK_ASSERT_TOP(ctx, 2);
	duk_to_string(ctx, -1);
	return 1;
}

int duk_builtin_number_prototype_to_precision(duk_context *ctx) {
	/* The specification has quite awkward order of coercion and
	 * checks for toPrecision().  The operations below are a bit
	 * reordered, within constraints of observable side effects.
	 */

	double d;
	int prec;
	int c;
	int n2s_flags;

	DUK_ASSERT_TOP(ctx, 1);

	d = push_this_number_plain(ctx);
	if (duk_is_undefined(ctx, 0)) {
		goto use_to_string;
	}
	DUK_ASSERT_TOP(ctx, 2);

	duk_to_int(ctx, 0);  /* for side effects */

	c = DUK_FPCLASSIFY(d);
	if (c == DUK_FP_NAN || c == DUK_FP_INFINITE) {
		goto use_to_string;
	}

	prec = duk_to_int_check_range(ctx, 0, 1, 21);

	n2s_flags = DUK_N2S_FLAG_FIXED_FORMAT |
	            DUK_N2S_FLAG_NO_ZERO_PAD;

	duk_numconv_stringify(ctx,
	                      10 /*radix*/,
	                      prec /*digits*/,
	                      n2s_flags /*flags*/);
	return 1;

 use_to_string:
	/* Used when precision is undefined; also used for NaN (-> "NaN"),
	 * and +/- infinity (-> "Infinity", "-Infinity").
	 */

	DUK_ASSERT_TOP(ctx, 2);
	duk_to_string(ctx, -1);
	return 1;
}

