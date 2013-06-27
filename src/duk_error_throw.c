/*
 *  Create and throw an Ecmascript error object based on a code and a message.
 *
 *  Used when we throw errors internally.  Ecmascript generated error objects
 *  are created by Ecmascript code, and the throwing is handled by the bytecode
 *  executor.
 */

#include "duk_internal.h"

/*
 *  Helper for calling errhandler.
 *
 *  'thr' must be the currently active thread; the errhandler is called
 *  in its context.  The valstack of 'thr' must have the error value on
 *  top, and will be replaced by another error value based on the return
 *  value of the errhandler.
 *
 *  The helper calls duk_handle_call() recursively in protected mode, but
 *  without an error handler.  Before that call happens, no longjmps
 *  should happen; as a consequence, we must assume that the valstack
 *  contains enough temporary space for arguments and such.
 *
 *  If the errhandler call causes an error to be thrown, that error will
 *  (silently) replace the original error now.  This would be easy to
 *  change and even to signal to the caller.
 *
 *  Note: since further longjmp()s may occur while calling the errhandler
 *  (for many reasons, e.g. a labeled 'break' inside the handler), the
 *  caller can make no assumptions on the thr->heap->lj state after the
 *  call.  This is currently not an issue, because the lj state is only
 *  written after the errhandler finishes.
 */

static void call_errhandler(duk_hthread *thr) {
	int call_flags;
	int rc;

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(thr->heap != NULL);

	if (!thr->heap->lj.errhandler) {
		DUK_DDDPRINT("no errhandler, return");
		return;
	}

	/* FIXME: assert/require for valstack space */

	/* [ ... errval ] */

	DUK_DDDPRINT("errhandler is %p", (void *) thr->heap->lj.errhandler);
	DUK_DDDPRINT("errhandler dump: %!O", (duk_heaphdr *) thr->heap->lj.errhandler);

	duk_push_hobject((duk_context *) thr, thr->heap->lj.errhandler);
	duk_insert((duk_context *) thr, -2);  /* -> [ ... errhandler errval ] */
	duk_push_undefined((duk_context *) thr);
	duk_insert((duk_context *) thr, -2);  /* -> [ ... errhandler undefined(= this) errval ] */

	/* [ ... errhandler undefined errval ] */

	/*
	 *  DUK_CALL_FLAG_IGNORE_RECLIMIT causes duk_handle_call() to ignore C
	 *  recursion depth limit (and won't increase it either).  This is
	 *  dangerous, but useful because it allows an errhandler to run even
	 *  if the original error is caused by C recursion depth limit.  Because
	 *  errhandler is NULL in the errhandler call, the errhandler call
	 *  can't cause the same situation to occur again.
	 *
	 *  We ignore errors now: a success return and an error value both
	 *  replace the original error value.  (This would be easy to change.)
	 */

	call_flags = DUK_CALL_FLAG_PROTECTED |
	             DUK_CALL_FLAG_IGNORE_RECLIMIT;  /* protected, ignore reclimit, not constructor */

	rc = duk_handle_call(thr,
	                     1,          /* num args */
	                     call_flags, /* call_flags */
	                     NULL);      /* errhandler */
	rc = rc;  /* no need to check now: both success and error are OK */

	/* [ ... errval ] */
}

/*
 *  Create and throw an error
 *
 *  Push an error object on top of the stack, possibly call an errhandler,
 *  and finally longjmp.
 *
 *  If an error occurs while we're dealing with the current error, we might
 *  enter an infinite recursion loop.  This is prevented by detecting a
 *  "double fault" through the heap->handling_error flag; the recursion
 *  then stops at the second level.
 */

#ifdef DUK_USE_VERBOSE_ERRORS
void duk_err_create_and_throw(duk_hthread *thr, duk_u32 code, const char *msg, const char *filename, int line) {
#else
void duk_err_create_and_throw(duk_hthread *thr, duk_u32 code) {
#endif
	duk_context *ctx = (duk_context *) thr;
	int double_error = thr->heap->handling_error;

#ifdef DUK_USE_VERBOSE_ERRORS
	DUK_DDPRINT("duk_err_create_and_throw(): code=%d, msg=%s, filename=%s, line=%d",
	             code, msg ? msg : "null", filename ? filename : "null", line);
#else
	DUK_DDPRINT("duk_err_create_and_throw(): code=%d", code);
#endif

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(ctx != NULL);

	thr->heap->handling_error = 1;

	/*
	 *  Create and push an error object onto the top of stack.
	 *  If a "double error" occurs, use a fixed error instance
	 *  to avoid further trouble.
	 */

	/* FIXME: if attempt to push beyond allocated valstack, this double fault
	 * handling fails miserably.  We should really write the double error
	 * directly to thr->heap->lj.value1 and avoid valstack use entirely.
	 */

	if (double_error) {
		if (thr->builtins[DUK_BIDX_DOUBLE_ERROR]) {
			DUK_DPRINT("double fault detected -> push built-in fixed 'double error' instance");
			duk_push_hobject(ctx, thr->builtins[DUK_BIDX_DOUBLE_ERROR]);
		} else {
			DUK_DPRINT("double fault detected; there is no built-in fixed 'double error' instance "
			           "-> push the error code as a number");
			duk_push_int(ctx, code);
		}
	} else {
		/* Error object is augmented at its creation here. */
		duk_require_stack(ctx, 1);
#ifdef DUK_USE_VERBOSE_ERRORS
		duk_push_new_error_object(ctx, code, msg);
#else
		duk_push_new_error_object(ctx, code, NULL);
#endif
	}

	/*
	 *  Call errhandler (unless error is an alloc error)
	 *
	 *  Note: must back up the current jmpbuf if it is the shared bytecode
	 *  executor one (handled internally by the helper).
	 */

	if (double_error || code == DUK_ERR_ALLOC_ERROR) {
		DUK_DPRINT("alloc or double error: skip calling errhandler to avoid further trouble");
	} else {
		call_errhandler(thr);
	}

	/*
	 *  Finally, longjmp
	 */

	thr->heap->handling_error = 0;

	duk_err_setup_heap_ljstate(thr, DUK_LJ_TYPE_THROW);

	DUK_DDDPRINT("THROW ERROR (INTERNAL): %!iT, %!iT",
	             &thr->heap->lj.value1, &thr->heap->lj.value2);

	duk_err_longjmp(thr);
	DUK_NEVER_HERE();
}

/*
 *  Helper for C function call negative return values.
 */

void duk_error_throw_from_negative_rc(duk_hthread *thr, int rc) {
	const char *msg;
	int code;

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(rc < 0);

	/* FIXME: this generates quite large code - perhaps select the error
	 * class based on the code and then just use the error 'name'?
	 */

	code = -rc;

	switch (rc) {
	case DUK_RET_UNIMPLEMENTED_ERROR:  msg = "unimplemented"; break;
	case DUK_RET_UNSUPPORTED_ERROR:    msg = "unsupported"; break;
	case DUK_RET_INTERNAL_ERROR:       msg = "internal error"; break;
	case DUK_RET_ALLOC_ERROR:          msg = "alloc error"; break;
	case DUK_RET_ASSERTION_ERROR:      msg = "assertion error"; break;
	case DUK_RET_API_ERROR:            msg = "api error"; break;
	case DUK_RET_UNCAUGHT_ERROR:       msg = "uncaught error"; break;
	case DUK_RET_ERROR:                msg = "error"; break;
	case DUK_RET_EVAL_ERROR:           msg = "eval error"; break;
	case DUK_RET_RANGE_ERROR:          msg = "range error"; break;
	case DUK_RET_REFERENCE_ERROR:      msg = "reference error"; break;
	case DUK_RET_SYNTAX_ERROR:         msg = "syntax error"; break;
	case DUK_RET_TYPE_ERROR:           msg = "type error"; break;
	case DUK_RET_URI_ERROR:            msg = "uri error"; break;
	default:                           msg = "unknown error"; break;
	}

	msg = msg;  /* suppress warning */

	DUK_ERROR(thr, code, "%s (rc %d)", msg, rc);
	DUK_NEVER_HERE();
}


