/*
 *  Call handling.
 *
 *  The main work horse functions are:
 *    - duk_handle_call(): call to a C/Ecmascript functions
 *    - duk_handle_safe_call(): make a protected C call within current activation
 *    - duk_handle_ecma_call_setup(): Ecmascript-to-Ecmascript calls, including
 *      tail calls and coroutine resume
 */

#include "duk_internal.h"

/*
 *  Arguments object creation.
 *
 *  Creating arguments objects is a bit finicky, see E5 Section 10.6 for the
 *  specific requirements.  Much of the arguments object special behavior is
 *  implemented in duk_hobject_props.c, and is enabled by the object flag
 *  DUK_HOBJECT_FLAG_SPECIAL_ARGUMENTS.
 */

static void create_arguments_object(duk_hthread *thr,
                                    duk_hobject *func,
                                    duk_hobject *varenv,
                                    int idx_argbase,            /* idx of first argument on stack */
                                    int num_stack_args) {       /* num args starting from idx_argbase */
	duk_context *ctx = (duk_context *) thr;
	duk_hobject *arg;          /* 'arguments' */
	duk_hobject *formals;      /* formals for 'func' (may be NULL if func is a C function) */
	int i_arg;
	int i_map;
	int i_mappednames;
	int i_formals;
	int i_argbase;
	int n_formals;
	int idx;
	int need_map;

	DUK_DDDPRINT("creating arguments object for func=%!iO, varenv=%!iO, idx_argbase=%d, num_stack_args=%d",
	             (duk_heaphdr *) func, (duk_heaphdr *) varenv, idx_argbase, num_stack_args);

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(func != NULL);
	DUK_ASSERT(DUK_HOBJECT_IS_NONBOUND_FUNCTION(func));
	DUK_ASSERT(varenv != NULL);
	DUK_ASSERT(idx_argbase >= 0);  /* assumed to bottom relative */
	DUK_ASSERT(num_stack_args >= 0);

	need_map = 0;

	i_argbase = idx_argbase;
	DUK_ASSERT(i_argbase >= 0);

	duk_push_hobject(ctx, func);
	duk_get_prop_stridx(ctx, -1, DUK_STRIDX_INT_FORMALS);
	formals = duk_get_hobject(ctx, -1);
	n_formals = 0;
	if (formals) {
		duk_get_prop_stridx(ctx, -1, DUK_STRIDX_LENGTH);
		n_formals = duk_require_int(ctx, -1);
		duk_pop(ctx);
	}
	duk_remove(ctx, -2);  /* leave formals on stack for later use */
	i_formals = duk_require_top_index(ctx);

	DUK_ASSERT(n_formals >= 0);
	DUK_ASSERT(formals != NULL || n_formals == 0);

	DUK_DDDPRINT("func=%!O, formals=%!O, n_formals=%d", func, formals, n_formals);

	/* [ ... formals ] */

	/*
	 *  Create required objects:
	 *    - 'arguments' object: array-like, but not an array
	 *    - 'map' object: internal object, tied to 'arguments'
	 *    - 'mappedNames' object: temporary value used during construction
	 */

	i_arg = duk_push_object_helper(ctx,
	                               DUK_HOBJECT_FLAG_EXTENSIBLE |
	                               DUK_HOBJECT_FLAG_ARRAY_PART |
	                               DUK_HOBJECT_CLASS_AS_FLAGS(DUK_HOBJECT_CLASS_ARGUMENTS),
	                               DUK_BIDX_OBJECT_PROTOTYPE);
	DUK_ASSERT(i_arg >= 0);
	arg = duk_require_hobject(ctx, -1);
	DUK_ASSERT(arg != NULL);

	i_map = duk_push_object_helper(ctx,
	                               DUK_HOBJECT_FLAG_EXTENSIBLE |
	                               DUK_HOBJECT_CLASS_AS_FLAGS(DUK_HOBJECT_CLASS_OBJECT),
	                               -1);  /* no prototype */
	DUK_ASSERT(i_map >= 0);

	i_mappednames = duk_push_object_helper(ctx,
	                                       DUK_HOBJECT_FLAG_EXTENSIBLE |
	                                       DUK_HOBJECT_CLASS_AS_FLAGS(DUK_HOBJECT_CLASS_OBJECT),
	                                       -1);  /* no prototype */
	DUK_ASSERT(i_mappednames >= 0);

	/* [... formals arguments map mappedNames] */

	DUK_DDDPRINT("created arguments related objects: "
	             "arguments at index %d -> %!O "
	             "map at index %d -> %!O "
	             "mappednames at index %d -> %!O",
	             i_arg, duk_get_hobject(ctx, i_arg),
	             i_map, duk_get_hobject(ctx, i_map),
	             i_mappednames, duk_get_hobject(ctx, i_mappednames));

	/*
	 *  Init arguments properties, map, etc.
	 */

	duk_push_int(ctx, num_stack_args);
	duk_def_prop_stridx(ctx, i_arg, DUK_STRIDX_LENGTH, DUK_PROPDESC_FLAGS_WC);

	/*
	 *  Init argument related properties
	 */

	/* step 11 */
	idx = num_stack_args - 1;
	while (idx >= 0) {
		DUK_DDDPRINT("arg idx %d, argbase=%d, argidx=%d", idx, i_argbase, i_argbase + idx);

		DUK_DDDPRINT("define arguments[%d]=arg", idx);
		duk_push_int(ctx, idx);
		duk_dup(ctx, i_argbase + idx);
		duk_def_prop(ctx, i_arg, DUK_PROPDESC_FLAGS_WEC);
		DUK_DDDPRINT("defined arguments[%d]=arg", idx);

		/* step 11.c is relevant only if non-strict (checked in 11.c.ii) */
		if (!DUK_HOBJECT_HAS_STRICT(func) && idx < n_formals) {
			DUK_ASSERT(formals != NULL);

			DUK_DDDPRINT("strict function, index within formals (%d < %d)", idx, n_formals);

			duk_get_prop_index(ctx, i_formals, idx);
			DUK_ASSERT(duk_is_string(ctx, -1));

			duk_dup(ctx, -1);  /* [... name name] */

			if (!duk_has_prop(ctx, i_mappednames)) {
				/* steps 11.c.ii.1 - 11.c.ii.4, but our internal book-keeping
				 * differs from the reference model
				 */

				/* [... name] */

				need_map = 1;

				DUK_DDDPRINT("set mappednames[%s]=%d", duk_get_string(ctx, -1), idx);
				duk_dup(ctx, -1);         /* name */
				duk_push_int(ctx, idx);   /* index */
				duk_to_string(ctx, -1);
				duk_def_prop(ctx, i_mappednames, DUK_PROPDESC_FLAGS_WEC);  /* out of spec, must be configurable */

				DUK_DDDPRINT("set map[%d]=%s", idx, duk_get_string(ctx, -1));
				duk_push_int(ctx, idx);   /* index */
				duk_to_string(ctx, -1);
				duk_dup(ctx, -2);         /* name */
				duk_def_prop(ctx, i_map, DUK_PROPDESC_FLAGS_WEC);  /* out of spec, must be configurable */
			} else {
				/* duk_has_prop() popped the second 'name' */
			}

			/* [... name] */
			duk_pop(ctx);  /* pop 'name' */
		}

		idx--;
	}

	DUK_DDDPRINT("actual arguments processed");

	/* step 12 */
	if (need_map) {
		DUK_DDDPRINT("adding 'map' and 'varenv' to arguments object");

		/* should never happen for a strict callee */
		DUK_ASSERT(!DUK_HOBJECT_HAS_STRICT(func));

		duk_dup(ctx, i_map);
		duk_def_prop_stridx(ctx, i_arg, DUK_STRIDX_INT_MAP, DUK_PROPDESC_FLAGS_NONE);  /* out of spec, don't care */

		/* The variable environment for magic variable bindings needs to be
		 * given by the caller and recorded in the arguments object.
		 *
		 * See E5 Section 10.6, the creation of setters/getters.
		 *
		 * The variable environment also provides access to the callee, so
		 * an explicit (internal) callee property is not needed.
		 */

		duk_push_hobject(ctx, varenv);
		duk_def_prop_stridx(ctx, i_arg, DUK_STRIDX_INT_VARENV, DUK_PROPDESC_FLAGS_NONE);  /* out of spec, don't care */
	}

	/* steps 13-14 */
	if (DUK_HOBJECT_HAS_STRICT(func)) {
		/*
		 *  Note: callee/caller are throwers and are not deletable etc.
		 *  They could be implemented as virtual properties, but currently
		 *  there is no support for virtual properties which are accessors
		 *  (only plain virtual properties).  This would not be difficult
		 *  to change in duk_hobject_props, but we can make the throwers
		 *  normal, concrete properties just as easily.
		 *
		 *  Note that the specification requires that the *same* thrower
		 *  built-in object is used here!  See E5 Section 10.6 main
		 *  algoritm, step 14, and Section 13.2.3 which describes the
		 *  thrower.  See test case test-arguments-throwers.js.
		 */

		DUK_DDDPRINT("strict function, setting caller/callee to throwers");

		duk_def_prop_stridx_thrower(ctx, i_arg, DUK_STRIDX_CALLER, DUK_PROPDESC_FLAGS_NONE);
		duk_def_prop_stridx_thrower(ctx, i_arg, DUK_STRIDX_CALLEE, DUK_PROPDESC_FLAGS_NONE);
	} else {
		DUK_DDDPRINT("non-strict function, setting callee to actual value");
		duk_push_hobject(ctx, func);
		duk_def_prop_stridx(ctx, i_arg, DUK_STRIDX_CALLEE, DUK_PROPDESC_FLAGS_WC);
	}

	/* set special behavior only after we're done */
	if (need_map) {
		/*
		 *  Note: special behaviors are only enabled for arguments
		 *  objects which have a parameter map (see E5 Section 10.6
		 *  main algorithm, step 12).
		 *
		 *  In particular, a non-strict arguments object with no
		 *  mapped formals does *NOT* get special behavior, even
		 *  for e.g. "caller" property.  This seems counterintuitive
		 *  but seems to be the case.
		 */

		/* cannot be strict (never mapped variables) */
		DUK_ASSERT(!DUK_HOBJECT_HAS_STRICT(func));

		DUK_DDDPRINT("enabling special behavior for arguments object");
		DUK_HOBJECT_SET_SPECIAL_ARGUMENTS(arg);
	} else {
		DUK_DDDPRINT("not enabling special behavior for arguments object");
	}

	/* nice log */
	DUK_DDDPRINT("final arguments related objects: "
	             "arguments at index %d -> %!O "
	             "map at index %d -> %!O "
	             "mappednames at index %d -> %!O",
	             i_arg, duk_get_hobject(ctx, i_arg),
	             i_map, duk_get_hobject(ctx, i_map),
	             i_mappednames, duk_get_hobject(ctx, i_mappednames));

	/* [args(n) [crud] formals arguments map mappednames] -> [args [crud] arguments] */
	duk_pop_2(ctx);
	duk_remove(ctx, -2);
}

/* Helper for creating the arguments object and adding it to the env record
 * on top of the value stack.  This helper has a very strict dependency on
 * the shape of the input stack.
 */
static void handle_createargs_for_call(duk_hthread *thr,
                                       duk_hobject *func,
                                       duk_hobject *env,
                                       int num_stack_args) {
	duk_context *ctx = (duk_context *) thr;

	DUK_DDDPRINT("creating arguments object for function call");

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(func != NULL);
	DUK_ASSERT(env != NULL);
	DUK_ASSERT(DUK_HOBJECT_HAS_CREATEARGS(func));
	DUK_ASSERT(duk_get_top(ctx) >= num_stack_args + 1);

	/* [... arg1 ... argN envobj] */

	create_arguments_object(thr,
	                        func,
	                        env,
	                        duk_get_top(ctx) - num_stack_args - 1,    /* idx_argbase */
	                        num_stack_args);

	/* [... arg1 ... argN envobj argobj] */

	duk_def_prop_stridx(ctx,
	                    -2,
	                    DUK_STRIDX_LC_ARGUMENTS,
	                    DUK_HOBJECT_HAS_STRICT(func) ? DUK_PROPDESC_FLAGS_E :   /* strict: non-deletable, non-writable */
	                                                   DUK_PROPDESC_FLAGS_WE);  /* non-strict: non-deletable, writable */
	/* [... arg1 ... argN envobj] */
}

/*
 *  Helper for handling a "bound function" chain when a call is being made.
 *
 *  Follows the bound function chain until a non-bound function is found.
 *  Prepends the bound arguments to the value stack (at idx_func + 2),
 *  updating 'num_stack_args' in the process.  The 'this' binding is also
 *  updated if necessary (at idx_func + 1).
 *
 *  FIXME: bound function chains could be collapsed at bound function creation
 *  time so that each bound function would point directly to a non-bound
 *  function.  This would make call time handling much easier.
 */

static void handle_bound_chain_for_call(duk_hthread *thr,
                                        int idx_func,
                                        int *p_num_stack_args,   /* may be changed by call */
                                        duk_hobject **p_func) {  /* changed by call */
	duk_context *ctx = (duk_context *) thr;
	int num_stack_args;
	duk_hobject *func;
	duk_u32 sanity;

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(p_num_stack_args != NULL);
	DUK_ASSERT(p_func != NULL);
	DUK_ASSERT(*p_func != NULL);
	DUK_ASSERT(DUK_HOBJECT_HAS_BOUND(*p_func));

	num_stack_args = *p_num_stack_args;
	func = *p_func;

	sanity = DUK_HOBJECT_BOUND_CHAIN_SANITY;
	do {	
		int i, len;

		if (!DUK_HOBJECT_HAS_BOUND(func)) {
			break;
		}

		DUK_DDDPRINT("bound function encountered, ptr=%p", (void *) func);

		/* XXX: this could be more compact by accessing the internal properties
		 * directly as own properties (they cannot be inherited, and are not
		 * externally visible).
		 */

		DUK_DDDPRINT("bound function encountered, ptr=%p, num_stack_args=%d",
		             (void *) func, num_stack_args);

		/* [ ... func this arg1 ... argN ] */

		duk_get_prop_stridx(ctx, idx_func, DUK_STRIDX_INT_THIS);
		duk_replace(ctx, idx_func + 1);  /* idx_this = idx_func + 1 */

		/* [ ... func this arg1 ... argN ] */

		/* XXX: duk_get_length? */
		duk_get_prop_stridx(ctx, idx_func, DUK_STRIDX_INT_ARGS);  /* -> [ ... func this arg1 ... argN _args ] */
		duk_get_prop_stridx(ctx, -1, DUK_STRIDX_LENGTH);          /* -> [ ... func this arg1 ... argN _args length ] */
		len = duk_require_int(ctx, -1);
		duk_pop(ctx);
		for (i = 0; i < len; i++) {
			/* FIXME: very slow - better to bulk allocate a gap, and copy
			 * from args_array directly (we know it has a compact array
			 * part, etc).
			 */

			/* [ ... func this <some bound args> arg1 ... argN _args ] */
			duk_get_prop_index(ctx, -1, i);
			duk_insert(ctx, idx_func + 2 + i);  /* idx_args = idx_func + 2 */
		}
		num_stack_args += len;  /* must be updated to work properly (e.g. creation of 'arguments') */
		duk_pop(ctx);

		/* [ ... func this <bound args> arg1 ... argN ] */

		duk_get_prop_stridx(ctx, idx_func, DUK_STRIDX_INT_TARGET);
		duk_replace(ctx, idx_func);  /* replace also in stack; not strictly necessary */
		func = duk_require_hobject(ctx, idx_func);

		DUK_DDDPRINT("bound function handled, num_stack_args=%d, idx_func=%d",
		             num_stack_args, idx_func);
	} while (--sanity > 0);

	if (sanity == 0) {
		DUK_ERROR(thr, DUK_ERR_INTERNAL_ERROR, "function call bound chain sanity exceeded");
	}

	DUK_DDDPRINT("final non-bound function is: %p", (void *) func);

	DUK_ASSERT(!DUK_HOBJECT_HAS_BOUND(func));
	DUK_ASSERT(DUK_HOBJECT_HAS_COMPILEDFUNCTION(func) || DUK_HOBJECT_HAS_NATIVEFUNCTION(func));

	/* write back */
	*p_num_stack_args = num_stack_args;
	*p_func = func;
}

/*
 *  Helper for setting up var_env and lex_env of an activation,
 *  assuming it does NOT have the DUK_HOBJECT_FLAG_NEWENV flag.
 */

static void handle_oldenv_for_call(duk_hthread *thr,
                                   duk_hobject *func,
                                   duk_activation *act) {
	duk_tval *tv;

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(func != NULL);
	DUK_ASSERT(act != NULL);
	DUK_ASSERT(!DUK_HOBJECT_HAS_NEWENV(func));
	DUK_ASSERT(!DUK_HOBJECT_HAS_CREATEARGS(func));

	tv = duk_hobject_find_existing_entry_tval_ptr(func, DUK_HEAP_STRING_INT_LEXENV(thr));
	if (tv) {
		DUK_ASSERT(DUK_TVAL_IS_OBJECT(tv));
		DUK_ASSERT(DUK_HOBJECT_IS_ENV(DUK_TVAL_GET_OBJECT(tv)));
		act->lex_env = DUK_TVAL_GET_OBJECT(tv);

		tv = duk_hobject_find_existing_entry_tval_ptr(func, DUK_HEAP_STRING_INT_VARENV(thr));
		if (tv) {
			DUK_ASSERT(DUK_TVAL_IS_OBJECT(tv));
			DUK_ASSERT(DUK_HOBJECT_IS_ENV(DUK_TVAL_GET_OBJECT(tv)));
			act->var_env = DUK_TVAL_GET_OBJECT(tv);
		} else {
			act->var_env = act->lex_env;
		}
	} else {
		act->lex_env = thr->builtins[DUK_BIDX_GLOBAL_ENV];
		act->var_env = act->lex_env;
	}

	DUK_HOBJECT_INCREF(thr, act->lex_env);
	DUK_HOBJECT_INCREF(thr, act->var_env);
}

/*
 *  Determine the effective 'this' binding and coerce the current value
 *  on the valstack to the effective one (in-place, at idx_this).
 *
 *  The current this value in the valstack (at idx_this) represents either:
 *    - the caller's requested 'this' binding; or
 *    - a 'this' binding accumulated from the bound function chain
 *
 *  The final 'this' binding for the target function may still be
 *  different, and is determined as described in E5 Section 10.4.3.
 *
 *  For global and eval code (E5 Sections 10.4.1 and 10.4.2), we assume
 *  that the caller has provided the correct 'this' binding explicitly
 *  when calling, i.e.:
 *
 *    - global code: this=global object
 *    - direct eval: this=copy from eval() caller's this binding
 *    - other eval:  this=global object
 *
 *  Note: this function may cause a recursive function call with arbitrary
 *  side effects, because ToObject() may be called.
 */

static void handle_coerce_effective_this_binding(duk_hthread *thr,
                                                 duk_hobject *func,
                                                 int idx_this) {
	duk_context *ctx = (duk_context *) thr;

	if (DUK_HOBJECT_HAS_STRICT(func)) {
		DUK_DDDPRINT("this binding: strict -> use directly");
	} else {
		duk_tval *tv_this = duk_require_tval(ctx, idx_this);
		duk_hobject *obj_global;

		if (DUK_TVAL_IS_OBJECT(tv_this)) {
			DUK_DDDPRINT("this binding: non-strict, object -> use directly");
		} else if (DUK_TVAL_IS_UNDEFINED(tv_this) || DUK_TVAL_IS_NULL(tv_this)) {
			DUK_DDDPRINT("this binding: non-strict, undefined/null -> use global object");
			obj_global = thr->builtins[DUK_BIDX_GLOBAL];
			if (obj_global) {
				duk_push_hobject(ctx, obj_global);
			} else {
				/*
				 *  This may only happen if built-ins are being "torn down".
				 *  This behavior is out of specification scope.
				 */
				DUK_DPRINT("this binding: wanted to use global object, but it is NULL -> using undefined instead");
				duk_push_undefined(ctx);
			}
			duk_replace(ctx, idx_this);
		} else {
			DUK_DDDPRINT("this binding: non-strict, not object/undefined/null -> use ToObject(value)");
			duk_to_object(ctx, idx_this);  /* may have side effects */
		}
	}
}

/*
 *  Helper for making various kinds of calls.
 *
 *  Call flags:
 *
 *    DUK_CALL_FLAG_PROTECTED        <-->  protected call
 *    DUK_CALL_FLAG_IGNORE_RECLIMIT  <-->  ignore C recursion limit,
 *                                         for errhandler calls
 *    DUK_CALL_FLAG_CONSTRUCTOR_CALL <-->  for 'new Foo()' calls
 *
 *  Input stack:
 *
 *    [ func this arg1 ... argN ]
 *
 *  Output stack:
 *
 *    [ retval ]         (DUK_ERR_EXEC_SUCCESS)
 *    [ errobj ]         (DUK_ERR_EXEC_ERROR (normal error), protected call)
 *    [ (unspecified) ]  (DUK_ERR_EXEC_TERM (terminal error), protected call)
 *                       (if error and not a protected call --> longjmp)
 *
 *  Even when executing a protected call, if an error happens during error
 *  handling (e.g. we run out of memory while setting up the return stack),
 *  the error is propagated to the previous catchpoint).  If no catchpoint
 *  exists, the fatal error handler is called.  Also, API errors (such as
 *  invalid indices) are thrown directly.
 *
 *  See 'execution.txt'.
 *
 *  The allowed thread states for making a call are:
 *    - thr matches heap->curr_thread, and thr is already RUNNING
 *    - thr does not match heap->curr_thread (may be NULL or other),
 *      and thr is INACTIVE (in this case, a setjmp() catchpoint is
 *      always used for thread book-keeping to work properly)
 *
 *  Like elsewhere, gotos are used to keep indent level minimal and
 *  avoiding a dozen helpers with awkward plumbing.
 *
 *  Note: setjmp() and local variables have a nasty interaction,
 *  see execution.txt; non-volatile locals modified after setjmp()
 *  call are not guaranteed to keep their value.
 */

int duk_handle_call(duk_hthread *thr,
                    int num_stack_args,
                    int call_flags,
                    duk_hobject *errhandler) {  /* borrowed */
	duk_context *ctx = (duk_context *) thr;
	int entry_valstack_bottom_index;
	int entry_callstack_top;
	int entry_catchstack_top;
	int entry_call_recursion_depth;
	int need_setjmp;
	duk_hthread *entry_curr_thread;
	duk_u8 entry_thread_state;
	int idx_func;         /* valstack index of 'func' and retval (relative to entry valstack_bottom) */
	int idx_args;         /* valstack index of start of args (arg1) (relative to entry valstack_bottom) */
	int nargs;            /* # argument registers target function wants (< 0 => "as is") */
	int nregs;            /* # total registers target function wants on entry (< 0 => "as is") */
	duk_hobject *func;    /* 'func' on stack (borrowed reference) */
	duk_activation *act;
	duk_hobject *env;
	duk_jmpbuf *old_jmpbuf_ptr = NULL;
	duk_hobject *old_errhandler = NULL;
	duk_jmpbuf our_jmpbuf;
	duk_tval tv_tmp;
	int retval = DUK_ERR_EXEC_ERROR;
	int rc;

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(ctx != NULL);
	DUK_ASSERT(num_stack_args >= 0);
	DUK_ASSERT(errhandler == NULL || DUK_HOBJECT_IS_CALLABLE(errhandler));
	DUK_ASSERT_REFCOUNT_NONZERO_HEAPHDR((duk_heaphdr *) errhandler);

	/* XXX: currently NULL allocations are not supported; remove if later allowed */
	DUK_ASSERT(thr->valstack != NULL);
	DUK_ASSERT(thr->callstack != NULL);
	DUK_ASSERT(thr->catchstack != NULL);

	/*
	 *  Preliminaries, required by setjmp() handler.
	 *
	 *  Must be careful not to throw an unintended error here.
	 *
	 *  Note: careful with indices like '-x'; if 'x' is zero, it
	 *  refers to valstack_bottom.
	 */

	entry_valstack_bottom_index = (int) (thr->valstack_bottom - thr->valstack);
	entry_callstack_top = thr->callstack_top;
	entry_catchstack_top = thr->catchstack_top;
	entry_call_recursion_depth = thr->heap->call_recursion_depth;
	entry_curr_thread = thr->heap->curr_thread;  /* Note: may be NULL if first call */
	entry_thread_state = thr->state;
	idx_func = duk_normalize_index(ctx, -num_stack_args - 2);  /* idx_func must be valid, note: non-throwing! */
	idx_args = idx_func + 2;                                   /* idx_args is not necessarily valid if num_stack_args == 0 (idx_args then equals top) */

	/* Need a setjmp() catchpoint if a protected call OR if we need to
	 * do mandatory cleanup.
	 */
	need_setjmp = ((call_flags & DUK_CALL_FLAG_PROTECTED) != 0) || (thr->heap->curr_thread != thr);

	DUK_DDPRINT("duk_handle_call: thr=%p, num_stack_args=%d, "
	            "call_flags=%d (protected=%d, ignorerec=%d, constructor=%d), need_setjmp=%d, "
	            "errhandler=%p, valstack_top=%d, idx_func=%d, idx_args=%d, rec_depth=%d/%d, "
	            "entry_valstack_bottom_index=%d, entry_callstack_top=%d, entry_catchstack_top=%d, "
	            "entry_call_recursion_depth=%d, entry_curr_thread=%p, entry_thread_state=%d",
	            (void *) thr,
	            num_stack_args,
	            call_flags,
	            ((call_flags & DUK_CALL_FLAG_PROTECTED) != 0 ? 1 : 0),
	            ((call_flags & DUK_CALL_FLAG_IGNORE_RECLIMIT) != 0 ? 1 : 0),
	            ((call_flags & DUK_CALL_FLAG_CONSTRUCTOR_CALL) != 0 ? 1 : 0),
	            need_setjmp,
	            (void *) errhandler,
	            duk_get_top(ctx),
	            idx_func,
	            idx_args,
	            thr->heap->call_recursion_depth,
	            thr->heap->call_recursion_limit,
	            entry_valstack_bottom_index,
	            entry_callstack_top,
	            entry_catchstack_top,
	            entry_call_recursion_depth,
	            (void *) entry_curr_thread,
	            entry_thread_state);

#ifdef DUK_USE_DDDEBUG
	DUK_DPRINT("callstack before call setup:");
	DUK_DEBUG_DUMP_CALLSTACK(thr);
#endif

	if (idx_func < 0 || idx_args < 0) {
		/*
		 *  Since stack indices are not reliable, we can't do anything useful
		 *  here.  Invoke the existing setjmp catcher, or if it doesn't exist,
		 *  call the fatal error handler.
		 */

		DUK_ERROR(thr, DUK_ERR_API_ERROR, "invalid arguments");
	}

	/*
	 *  Setup a setjmp() catchpoint first because even the call setup
	 *  may fail.
	 */

	if (!need_setjmp) {
		DUK_DDDPRINT("don't need a setjmp catchpoint");
		goto handle_call;
	}

	old_errhandler = thr->heap->lj.errhandler;
	old_jmpbuf_ptr = thr->heap->lj.jmpbuf_ptr;

	thr->heap->lj.errhandler = errhandler;  /* may be NULL */
	thr->heap->lj.jmpbuf_ptr = &our_jmpbuf;

	if (setjmp(thr->heap->lj.jmpbuf_ptr->jb) == 0) {
		DUK_DDDPRINT("setjmp catchpoint setup complete, errhandler=%p",
		             (void *) thr->heap->lj.errhandler);
		goto handle_call;
	}

	/*
	 *  Error during setup, call, or postprocessing of the call.
	 *  The error value is in heap->lj.value1.
	 *
	 *  Note: any local variables accessed here must have their value
	 *  assigned *before* the setjmp() call, OR they must be declared
	 *  volatile.  Otherwise their value is not guaranteed to be correct.
	 *
	 *  The following are such variables:
	 *    - duk_handle_call() parameters
	 *    - entry_*
	 *    - idx_func
	 *    - idx_args
	 *
	 *  The very first thing we do is restore the previous setjmp catcher.
	 *  This means that any error in error handling will propagate outwards
	 *  instead of causing a setjmp() re-entry above.  The *only* actual
	 *  errors that should happen here are allocation errors.
	 */

	DUK_DDDPRINT("error caught during protected duk_handle_call(): %!T",
	             &thr->heap->lj.value1);

	DUK_ASSERT(thr->heap->lj.type == DUK_LJ_TYPE_THROW);
	DUK_ASSERT(thr->callstack_top >= entry_callstack_top);
	DUK_ASSERT(thr->catchstack_top >= entry_catchstack_top);

	/*
	 *  Restore previous setjmp catchpoint
	 */

	/* Note: either pointer may be NULL (at entry), so don't assert */
	DUK_DDDPRINT("restore jmpbuf_ptr: %p -> %p, errhandler: %p -> %p",
	             (thr && thr->heap ? thr->heap->lj.jmpbuf_ptr : NULL),
	             old_jmpbuf_ptr,
	             (thr && thr->heap ? thr->heap->lj.errhandler : NULL),
	             old_errhandler);

	thr->heap->lj.jmpbuf_ptr = old_jmpbuf_ptr;
	thr->heap->lj.errhandler = old_errhandler;

	if (!(call_flags & DUK_CALL_FLAG_PROTECTED)) {
		/*
		 *  Caller did not request a protected call but a setjmp
		 *  catchpoint was set up to allow cleanup.  So, clean up
		 *  and rethrow.
		 *
		 *  Note: this case happens e.g. when heap->curr_thread is
		 *  NULL on entry.
		 *
		 *  FIXME: maybe we should let the caller clean up instead.
		 */

		DUK_DDDPRINT("call is not protected -> clean up and rethrow");

#if 0  /*FIXME*/
		thr->heap->curr_thread = entry_curr_thread;  /* may be NULL */
		thr->state = entry_thread_state;

		DUK_ASSERT((thr->state == DUK_HTHREAD_STATE_INACTIVE && thr->heap->curr_thread == NULL) ||  /* first call */
		           (thr->state == DUK_HTHREAD_STATE_INACTIVE && thr->heap->curr_thread != NULL) ||  /* other call */
		           (thr->state == DUK_HTHREAD_STATE_RUNNING && thr->heap->curr_thread == thr));     /* current thread */
#endif
		/* XXX: should setjmp catcher be responsible for this instead? */
		thr->heap->call_recursion_depth = entry_call_recursion_depth;
		duk_err_longjmp(thr);
		DUK_NEVER_HERE();
	}

	duk_hthread_catchstack_unwind(thr, entry_catchstack_top);
	duk_hthread_callstack_unwind(thr, entry_callstack_top);
	thr->valstack_bottom = thr->valstack + entry_valstack_bottom_index;

	/* [ ... func this (crud) errobj ] */

	/* FIXME: is there space?  better implementation: write directly over
	 * 'func' slot to avoid valstack grow issues.
	 */
	duk_push_tval(ctx, &thr->heap->lj.value1);

	/* [ ... func this (crud) errobj ] */

	duk_replace(ctx, idx_func);
	duk_set_top(ctx, idx_func + 1);

	/* [ ... errobj ] */

	/* ensure there is internal valstack spare before we exit; this may
	 * throw an alloc error
	 */

	duk_require_valstack_resize((duk_context *) thr,
	                            (thr->valstack_top - thr->valstack) +            /* top of current func */
	                                DUK_VALSTACK_INTERNAL_EXTRA,                 /* + spare => min_new_size */
	                            1);                                              /* allow_shrink */

	/* Note: currently a second setjmp restoration is done at the target;
	 * this is OK, but could be refactored away.
	 */
	retval = DUK_ERR_EXEC_ERROR;
	goto shrink_and_finished;

 handle_call:
	/*
	 *  Thread state check and book-keeping.
	 */

	if (thr == thr->heap->curr_thread) {
		/* same thread */
		if (thr->state != DUK_HTHREAD_STATE_RUNNING) {
			/* should actually never happen, but check anyway */
			goto thread_state_error;
		}
	} else {
		/* different thread */
		DUK_ASSERT(thr->heap->curr_thread == NULL ||
		           thr->heap->curr_thread->state == DUK_HTHREAD_STATE_RUNNING);
		if (thr->state != DUK_HTHREAD_STATE_INACTIVE) {
			goto thread_state_error;
		}
		thr->heap->curr_thread = thr;
		thr->state = DUK_HTHREAD_STATE_RUNNING;

		/* Note: multiple threads may be simultaneously in the RUNNING
		 * state, but not in the same "resume chain".
		 */
	}

	DUK_ASSERT(thr->heap->curr_thread == thr);
	DUK_ASSERT(thr->state == DUK_HTHREAD_STATE_RUNNING);

	/*
	 *  C call recursion depth check, which provides a reasonable upper
	 *  bound on maximum C stack size (arbitrary C stack growth is only
	 *  possible by recursive handle_call / handle_safe_call calls).
	 */

	DUK_ASSERT(thr->heap->call_recursion_depth >= 0);
	DUK_ASSERT(thr->heap->call_recursion_depth <= thr->heap->call_recursion_limit);

	if (call_flags & DUK_CALL_FLAG_IGNORE_RECLIMIT) {
		DUK_DDPRINT("ignoring reclimit for this call (probably an errhandler call)");
	} else {	
		if (thr->heap->call_recursion_depth >= thr->heap->call_recursion_limit) {
			DUK_ERROR(thr, DUK_ERR_INTERNAL_ERROR, "maximum C call stack depth reached");
		}
		thr->heap->call_recursion_depth++;
	}

	/*
	 *  Check the function type, handle bound function chains,
	 *  and prepare parameters for the rest of the call handling.
	 *  Also figure out the effective 'this' binding, which
	 *  replaces the current value at idx_func + 1.
	 *
	 *  If the target function is a 'bound' one, follow the chain
	 *  of 'bound' functions until a non-bound function is found.
	 *  During this process, bound arguments are 'prepended' to
	 *  existing ones, and the "this" binding is overridden.
	 *  See E5 Section 15.3.4.5.1.
	 */

	if (!duk_is_callable(thr, idx_func)) {
		DUK_ERROR(ctx, DUK_ERR_TYPE_ERROR, "call target not callable");
	}
	func = duk_get_hobject(thr, idx_func);
	DUK_ASSERT(func != NULL);

	if (DUK_HOBJECT_HAS_BOUND(func)) {
		/* slow path for bound functions */
		handle_bound_chain_for_call(thr, idx_func, &num_stack_args, &func);
	}
	DUK_ASSERT(!DUK_HOBJECT_HAS_BOUND(func));
	DUK_ASSERT(DUK_HOBJECT_IS_COMPILEDFUNCTION(func) ||
	           DUK_HOBJECT_IS_NATIVEFUNCTION(func));

	handle_coerce_effective_this_binding(thr, func, idx_func + 1);
	DUK_DDDPRINT("effective 'this' binding is: %!T", duk_get_tval(ctx, idx_func + 1));

	nargs = 0;
	nregs = 0;
	if (DUK_HOBJECT_IS_COMPILEDFUNCTION(func)) {
		nargs = ((duk_hcompiledfunction *) func)->nargs;
		nregs = ((duk_hcompiledfunction *) func)->nregs;
		DUK_ASSERT(nregs >= nargs);
	} else if (DUK_HOBJECT_IS_NATIVEFUNCTION(func)) {
		/* Note: nargs (and nregs) may be negative for a native,
		 * function, which indicates that the function wants the
		 * input stack "as is" (i.e. handles "vararg" arguments).
		 */
		nargs = ((duk_hnativefunction *) func)->nargs;
		nregs = nargs;
	} else {
		/* XXX: this should be an assert */
		DUK_ERROR(thr, DUK_ERR_TYPE_ERROR, "call target not a function");
	}

	/* [ ... func this arg1 ... argN ] */

	/*
	 *  Check stack sizes and resize if necessary.
	 *
	 *  Call stack is grown by one, catch stack doesn't grow here.
	 *  Value stack may either grow or shrink, depending on the number
	 *  of func registers and the number of actual arguments.
	 */

	duk_hthread_callstack_grow(thr);

	/* if nregs >= 0, func wants args clamped to 'nargs'; else it wants
	 * all args (= 'num_stack_args')
	 */

	duk_require_valstack_resize((duk_context *) thr,
	                            (thr->valstack_bottom - thr->valstack) +         /* bottom of current func */
	                                idx_args +                                   /* bottom of new func */
	                                (nregs >= 0 ? nregs : num_stack_args) +      /* num entries of new func at entry */
	                                DUK_VALSTACK_INTERNAL_EXTRA,                 /* + spare => min_new_size */
	                            1);                                              /* allow_shrink */

	/*
	 *  Update idx_retval of current activation.
	 *
	 *  Although it might seem this is not necessary (bytecode executor
	 *  does this for Ecmascript-to-Ecmascript calls; other calls are
	 *  handled here), this turns out to be necessary for handling yield
	 *  and resume.  For them, an Ecmascript-to-native call happens, and
	 *  the Ecmascript call's idx_retval must be set for things to work.
	 */

	if (thr->callstack_top > 0) {
		/* now set unconditionally, regardless of whether current activation
		 * is native or not.
	 	 */
		(thr->callstack + thr->callstack_top - 1)->idx_retval = entry_valstack_bottom_index + idx_func;
	}

	/*
	 *  Setup a preliminary activation.
	 *
	 *  Don't touch valstack_bottom or valstack_top yet so that Duktape API
	 *  calls work normally.
	 */

	/* [ ... func this arg1 ... argN ] */

	DUK_ASSERT(thr->callstack_top < thr->callstack_size);
	act = &thr->callstack[thr->callstack_top];
	thr->callstack_top++;
	DUK_ASSERT(thr->callstack_top <= thr->callstack_size);
	DUK_ASSERT(thr->valstack_top > thr->valstack_bottom);  /* at least effective 'this' */
	DUK_ASSERT(!DUK_HOBJECT_HAS_BOUND(func));

	act->flags = 0;
	if (DUK_HOBJECT_HAS_STRICT(func)) {
		act->flags |= DUK_ACT_FLAG_STRICT;
	}
	if (call_flags & DUK_CALL_FLAG_CONSTRUCTOR_CALL) {
		act->flags |= DUK_ACT_FLAG_CONSTRUCT;
		act->flags |= DUK_ACT_FLAG_PREVENT_YIELD;
	}
	if (DUK_HOBJECT_IS_NATIVEFUNCTION(func)) {
		act->flags |= DUK_ACT_FLAG_PREVENT_YIELD;
	}
	if (call_flags & DUK_CALL_FLAG_DIRECT_EVAL) {
		act->flags |= DUK_ACT_FLAG_DIRECT_EVAL;
	}

	act->func = func;
	act->var_env = NULL;
	act->lex_env = NULL;
	act->pc = 0;
	act->idx_bottom = entry_valstack_bottom_index + idx_args;
#if 0  /* topmost activation idx_retval is considered garbage, no need to init */
	act->idx_retval = -1;  /* idx_retval is a 'caller' retval, so init to "unused" here */
#endif

	if (act->flags & DUK_ACT_FLAG_PREVENT_YIELD) {
		/* duk_hthread_callstack_unwind() will decrease this on unwind */
		thr->callstack_preventcount++;
	}

	DUK_HOBJECT_INCREF(thr, func);  /* act->func */

	/* [... func this arg1 ... argN] */

#ifdef DUK_USE_DDDEBUG
	DUK_DPRINT("pushed new activation:");
	DUK_DEBUG_DUMP_ACTIVATION(thr, thr->callstack + thr->callstack_top - 1);
#endif

	/*
	 *  Environment record creation and 'arguments' object creation.
	 *  Named function expression name binding is handled by the
	 *  compiler; the compiled function's parent env will contain
	 *  the (immutable) binding already.
	 *
	 *  This handling is now identical for C and Ecmascript functions.
	 *  C functions always have the 'NEWENV' flag set, so their
	 *  environment record initialization is delayed (which is good).
	 *
	 *  Delayed creation (on demand) is handled in duk_js_var.c.
	 */

	DUK_ASSERT(!DUK_HOBJECT_HAS_BOUND(func));  /* bound function chain has already been resolved */

	if (!DUK_HOBJECT_HAS_NEWENV(func)) {
		/* use existing env (e.g. for non-strict eval); cannot have
		 * an own 'arguments' object (but can refer to the existing one)
		 */

		DUK_ASSERT(!DUK_HOBJECT_HAS_CREATEARGS(func));

		handle_oldenv_for_call(thr, func, act);

		DUK_ASSERT(act->lex_env != NULL);
		DUK_ASSERT(act->var_env != NULL);
		goto env_done;
	}

	DUK_ASSERT(DUK_HOBJECT_HAS_NEWENV(func));

	if (!DUK_HOBJECT_HAS_CREATEARGS(func)) {
		/* no need to create environment record now; leave as NULL */
		DUK_ASSERT(act->lex_env == NULL);
		DUK_ASSERT(act->var_env == NULL);
		goto env_done;
	}

	/* third arg: absolute index (to entire valstack) of idx_bottom of new activation */
	env = duk_create_activation_environment_record(thr, func, act->idx_bottom);
	DUK_ASSERT(env != NULL);
	
	/* [... func this arg1 ... argN envobj] */

	DUK_ASSERT(DUK_HOBJECT_HAS_CREATEARGS(func));
	handle_createargs_for_call(thr, func, env, num_stack_args);

	/* [... func this arg1 ... argN envobj] */

	act->lex_env = env;
	act->var_env = env;
	DUK_HOBJECT_INCREF(thr, env);
	DUK_HOBJECT_INCREF(thr, env);  /* XXX: incref by count (2) directly */
	duk_pop(ctx);

 env_done:
	/* [... func this arg1 ... argN] */

	/*
	 *  Setup value stack: clamp to 'nargs', fill up to 'nregs'
	 */

	/* XXX: replace with a single operation */

	if (nregs >= 0) {
		duk_set_top(ctx, idx_args + nargs);  /* clamp anything above nargs */
		duk_set_top(ctx, idx_args + nregs);  /* extend with undefined */
	} else {
		/* 'func' wants stack "as is" */
	}

#ifdef DUK_USE_DDDEBUG
	DUK_DPRINT("callstack after call setup:");
	DUK_DEBUG_DUMP_CALLSTACK(thr);
#endif

	/*
	 *  Determine call type; then setup activation and call
	 */

	if (DUK_HOBJECT_IS_COMPILEDFUNCTION(func)) {
		goto ecmascript_call;
	} else {
		goto native_call;
	}
	DUK_NEVER_HERE();

	/*
	 *  Native (C) call
	 */

 native_call:
	/*
	 *  Shift to new valstack_bottom.
	 */

	thr->valstack_bottom = thr->valstack_bottom + idx_args;
	/* keep current valstack_top */
	DUK_ASSERT(thr->valstack_bottom >= thr->valstack);
	DUK_ASSERT(thr->valstack_top >= thr->valstack_bottom);
	DUK_ASSERT(thr->valstack_end >= thr->valstack_top);
	DUK_ASSERT(((duk_hnativefunction *) func)->func != NULL);

	/* [... func this | arg1 ... argN] ('this' must precede new bottom) */

	/*
	 *  Actual function call and return value check.
	 *
	 *  Return values:
	 *    0    success, no return value (default to 'undefined')
	 *    1    success, one return value on top of stack
	 *  < 0    error, throw a "magic" error
	 *  other  invalid
	 */

	rc = ((duk_hnativefunction *) func)->func((duk_context *) thr);

	if (rc < 0) {
		duk_error_throw_from_negative_rc(thr, rc);
		DUK_NEVER_HERE();
	} else if (rc > 1) {
		DUK_ERROR(thr, DUK_ERR_API_ERROR, "c function returned invalid rc");
	}
	DUK_ASSERT(rc == 0 || rc == 1);

	/*
	 *  Unwind stack(s) and shift back to old valstack_bottom.
	 */

	DUK_ASSERT(thr->catchstack_top == entry_catchstack_top);
	DUK_ASSERT(thr->callstack_top == entry_callstack_top + 1);

#if 0  /* should be no need to unwind */
	duk_hthread_catchstack_unwind(thr, entry_catchstack_top);
#endif
	duk_hthread_callstack_unwind(thr, entry_callstack_top);

	thr->valstack_bottom = thr->valstack + entry_valstack_bottom_index;
	/* keep current valstack_top */

	DUK_ASSERT(thr->valstack_bottom >= thr->valstack);
	DUK_ASSERT(thr->valstack_top >= thr->valstack_bottom);
	DUK_ASSERT(thr->valstack_end >= thr->valstack_top);
	DUK_ASSERT(thr->valstack_top - thr->valstack_bottom >= idx_func + 1);

	/*
	 *  Manipulate value stack so that return value is on top
	 *  (pushing an 'undefined' if necessary).
	 */

	/* XXX: should this happen in the callee's activation or after unwinding? */
	if (rc == 0) {
		duk_require_stack(ctx, 1);
		duk_push_undefined(ctx);
	}
	/* [... func this (crud) retval] */

	DUK_DDDPRINT("native call retval -> %!T (rc=%d)", duk_get_tval(ctx, -1), rc);

	duk_replace(ctx, idx_func);
	duk_set_top(ctx, idx_func + 1);

	/* [... retval] */

	/* ensure there is internal valstack spare before we exit */

	duk_require_valstack_resize((duk_context *) thr,
	                            (thr->valstack_top - thr->valstack) +            /* top of current func */
	                                DUK_VALSTACK_INTERNAL_EXTRA,                 /* + spare => min_new_size */
	                            1);                                              /* allow_shrink */


	/*
	 *  Shrink checks and return with success.
	 */

	retval = DUK_ERR_EXEC_SUCCESS;
	goto shrink_and_finished;	

	/*
	 *  Ecmascript call
	 */

 ecmascript_call:

	/*
	 *  Shift to new valstack_bottom.
	 */

	thr->valstack_bottom = thr->valstack_bottom + idx_args;
	/* keep current valstack_top */
	DUK_ASSERT(thr->valstack_bottom >= thr->valstack);
	DUK_ASSERT(thr->valstack_top >= thr->valstack_bottom);
	DUK_ASSERT(thr->valstack_end >= thr->valstack_top);

	/* [... func this | arg1 ... argN] ('this' must precede new bottom) */

	/*
	 *  Bytecode executor call.
	 *
	 *  Execute bytecode, handling any recursive function calls and
	 *  thread resumptions.  Returns when execution would return from
	 *  the entry level activation.  When the executor returns, a
	 *  single return value is left on the stack top.
	 *
	 *  The only possible longjmp() is an error (DUK_LJ_TYPE_THROW),
	 *  other types are handled internally by the executor.
	 *
	 */

	DUK_DDDPRINT("entering bytecode execution");
	duk_js_execute_bytecode(thr);
	DUK_DDDPRINT("returned from bytecode execution");

	/*
	 *  Unwind stack(s) and shift back to old valstack_bottom.
	 */

	DUK_ASSERT(thr->callstack_top == entry_callstack_top + 1);

	duk_hthread_catchstack_unwind(thr, entry_catchstack_top);
	duk_hthread_callstack_unwind(thr, entry_callstack_top);

	thr->valstack_bottom = thr->valstack + entry_valstack_bottom_index;
	/* keep current valstack_top */

	DUK_ASSERT(thr->valstack_bottom >= thr->valstack);
	DUK_ASSERT(thr->valstack_top >= thr->valstack_bottom);
	DUK_ASSERT(thr->valstack_end >= thr->valstack_top);
	DUK_ASSERT(thr->valstack_top - thr->valstack_bottom >= idx_func + 1);

	/*
	 *  Manipulate value stack so that return value is on top.
	 */

	/* [... func this (crud) retval] */

	duk_replace(ctx, idx_func);
	duk_set_top(ctx, idx_func + 1);

	/* [... retval] */

	/* ensure there is internal valstack spare before we exit */

	duk_require_valstack_resize((duk_context *) thr,
	                            (thr->valstack_top - thr->valstack) +            /* top of current func */
	                                DUK_VALSTACK_INTERNAL_EXTRA,                 /* + spare => min_new_size */
	                            1);                                              /* allow_shrink */

	/*
	 *  Shrink checks and return with success.
	 */

	retval = DUK_ERR_EXEC_SUCCESS;
	goto shrink_and_finished;	

 shrink_and_finished:
	/* these are "soft" shrink checks, whose failures are ignored */
	/* XXX: would be nice if fast path was inlined */
	duk_hthread_catchstack_shrink_check(thr);
	duk_hthread_callstack_shrink_check(thr);
	goto finished;

 finished:
	if (need_setjmp) {
		/* Note: either pointer may be NULL (at entry), so don't assert;
		 * this is now done potentially twice, which is OK
		 */
		DUK_DDDPRINT("restore jmpbuf_ptr: %p -> %p, errhandler: %p -> %p (possibly already done)",
		             (thr && thr->heap ? thr->heap->lj.jmpbuf_ptr : NULL),
		             old_jmpbuf_ptr,
		             (thr && thr->heap ? thr->heap->lj.errhandler : NULL),
		             old_errhandler);
		thr->heap->lj.jmpbuf_ptr = old_jmpbuf_ptr;
		thr->heap->lj.errhandler = old_errhandler;

		/* These are just convenience "wiping" of state */
		thr->heap->lj.type = DUK_LJ_TYPE_UNKNOWN;
		thr->heap->lj.iserror = 0;

		/* FIXME: what about side effects here? finalizer runs should be shielded
		 * from errors so even out-of-memory should not be an issue here.
		 */
		DUK_TVAL_SET_TVAL(&tv_tmp, &thr->heap->lj.value1);
		DUK_TVAL_SET_UNDEFINED_UNUSED(&thr->heap->lj.value1);
		DUK_TVAL_DECREF(thr, &tv_tmp);

		DUK_TVAL_SET_TVAL(&tv_tmp, &thr->heap->lj.value2);
		DUK_TVAL_SET_UNDEFINED_UNUSED(&thr->heap->lj.value2);
		DUK_TVAL_DECREF(thr, &tv_tmp);

		DUK_DDDPRINT("setjmp catchpoint torn down");
	}

	thr->heap->curr_thread = entry_curr_thread;  /* may be NULL */
	thr->state = entry_thread_state;

	DUK_ASSERT((thr->state == DUK_HTHREAD_STATE_INACTIVE && thr->heap->curr_thread == NULL) ||  /* first call */
	           (thr->state == DUK_HTHREAD_STATE_INACTIVE && thr->heap->curr_thread != NULL) ||  /* other call */
	           (thr->state == DUK_HTHREAD_STATE_RUNNING && thr->heap->curr_thread == thr));     /* current thread */
	
	thr->heap->call_recursion_depth = entry_call_recursion_depth;

	return retval;

 thread_state_error:
	DUK_ERROR(thr, DUK_ERR_TYPE_ERROR, "invalid thread state for call (%d)", thr->state);
	DUK_NEVER_HERE();
	return DUK_ERR_EXEC_ERROR;  /* never executed */
}

/*
 *  Manipulate value stack so that exactly 'num_stack_rets' return
 *  values are at 'idx_retbase' in every case, assuming there are
 *  'rc' return values on top of stack.
 *
 *  This is a bit tricky, because the called C function operates in
 *  the same activation record and may have e.g. popped the stack
 *  empty (below idx_retbase).
 */

static void safe_call_adjust_valstack(duk_hthread *thr, int idx_retbase, int num_stack_rets, int num_actual_rets) {
	duk_context *ctx = (duk_context *) thr;
	int idx_rcbase;

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(idx_retbase >= 0);
	DUK_ASSERT(num_stack_rets >= 0);
	DUK_ASSERT(num_actual_rets >= 0);

	idx_rcbase = duk_get_top(ctx) - num_actual_rets;  /* base of known return values */

	DUK_DDDPRINT("adjust valstack after func call: "
	             "num_stack_rets=%d, num_actual_rets=%d, stack_top=%d, idx_retbase=%d, idx_rcbase=%d",
	             num_stack_rets, num_actual_rets, duk_get_top(ctx), idx_retbase, idx_rcbase);

	DUK_ASSERT(idx_rcbase >= 0);  /* caller must check */

	/* ensure space for final configuration (idx_retbase + num_stack_rets) and
	 * intermediate configurations
	 */
	duk_require_stack_top(ctx,
	                      (idx_rcbase > idx_retbase ? idx_rcbase : idx_retbase) +
	                      num_stack_rets);

	/* chop extra retvals away / extend with undefined */
	duk_set_top(ctx, idx_rcbase + num_stack_rets);

	if (idx_rcbase >= idx_retbase) {
		int count = idx_rcbase - idx_retbase;
		int i;

		DUK_DDDPRINT("elements at/after idx_retbase have enough to cover func retvals "
		             "(idx_retbase=%d, idx_rcbase=%d)", idx_retbase, idx_rcbase);

		/* nuke values at idx_retbase to get the first retval (initially
		 * at idx_rcbase) to idx_retbase
		 */

		DUK_ASSERT(count >= 0);

		for (i = 0; i < count; i++) {
			/* XXX: inefficient; block remove primitive */
			duk_remove(ctx, idx_retbase);
		}
	} else {
		int count = idx_retbase - idx_rcbase;
		int i;

		DUK_DDDPRINT("not enough elements at/after idx_retbase to cover func retvals "
		             "(idx_retbase=%d, idx_rcbase=%d)", idx_retbase, idx_rcbase);

		/* insert 'undefined' values at idx_rcbase to get the
		 * return values to idx_retbase
		 */

		DUK_ASSERT(count > 0);

		for (i = 0; i < count; i++) {
			/* XXX: inefficient; block insert primitive */
			duk_push_undefined(ctx);
			duk_insert(ctx, idx_rcbase);
		}
	}
}

/*
 *  Make a "C protected call" within the current activation.
 *
 *  The allowed thread states for making a call are the same as for
 *  duk_handle_call().
 *
 *  Note that like duk_handle_call(), even if this call is protected,
 *  there are a few situations where the current (pre-entry) setjmp
 *  catcher (or a fatal error handler if no such catcher exists) is
 *  invoked:
 *
 *    - Blatant API argument errors (e.g. num_stack_args is invalid,
 *      so we can't form a reasonable return stack)
 *
 *    - Errors during error handling, e.g. failure to reallocate
 *      space in the value stack due to an alloc error
 *
 *  Such errors propagate outwards, ultimately to the fatal error
 *  handler if nothing else.
 */

/* FIXME: bump preventcount by one for the duration of this call? */

int duk_handle_safe_call(duk_hthread *thr,
                         duk_safe_call_function func,
                         int num_stack_args,
                         int num_stack_rets,
                         duk_hobject *errhandler) {
	duk_context *ctx = (duk_context *) thr;
	int entry_valstack_bottom_index;
	int entry_callstack_top;
	int entry_catchstack_top;
	int entry_call_recursion_depth;
	duk_hthread *entry_curr_thread;
	duk_u8 entry_thread_state;
	duk_jmpbuf *old_jmpbuf_ptr = NULL;
	duk_hobject *old_errhandler = NULL;
	duk_jmpbuf our_jmpbuf;
	duk_tval tv_tmp;
	int idx_retbase;
	int retval;
	int rc;

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(ctx != NULL);
	DUK_ASSERT(errhandler == NULL || DUK_HOBJECT_IS_CALLABLE(errhandler));
	DUK_ASSERT_REFCOUNT_NONZERO_HEAPHDR((duk_heaphdr *) errhandler);

	/* Note: careful with indices like '-x'; if 'x' is zero, it refers to bottom */
	entry_valstack_bottom_index = (int) (thr->valstack_bottom - thr->valstack);
	entry_callstack_top = thr->callstack_top;
	entry_catchstack_top = thr->catchstack_top;
	entry_call_recursion_depth = thr->heap->call_recursion_depth;
	entry_curr_thread = thr->heap->curr_thread;  /* Note: may be NULL if first call */
	entry_thread_state = thr->state;
	idx_retbase = duk_get_top(ctx) - num_stack_args;  /* Note: not a valid stack index if num_stack_args == 0 */

	/* Note: cannot portably debug print a function pointer, hence 'func' not printed! */
	DUK_DDPRINT("duk_handle_safe_call: thr=%p, num_stack_args=%d, num_stack_rets=%d, "
	            "errhandler=%p, valstack_top=%d, idx_retbase=%d, rec_depth=%d/%d, "
	            "entry_valstack_bottom_index=%d, entry_callstack_top=%d, entry_catchstack_top=%d, "
	            "entry_call_recursion_depth=%d, entry_curr_thread=%p, entry_thread_state=%d",
	            (void *) thr, num_stack_args, num_stack_rets,
	            (void *) errhandler, duk_get_top(ctx), idx_retbase, thr->heap->call_recursion_depth,
	            thr->heap->call_recursion_limit, entry_valstack_bottom_index,
	            entry_callstack_top, entry_catchstack_top, entry_call_recursion_depth,
	            entry_curr_thread, entry_thread_state);

	if (idx_retbase < 0) {
		/*
		 *  Since stack indices are not reliable, we can't do anything useful
		 *  here.  Invoke the existing setjmp catcher, or if it doesn't exist,
		 *  call the fatal error handler.
		 */

		DUK_ERROR(thr, DUK_ERR_API_ERROR, "invalid arguments");
	}

	/* setjmp catchpoint setup */

	old_errhandler = thr->heap->lj.errhandler;
	old_jmpbuf_ptr = thr->heap->lj.jmpbuf_ptr;

	thr->heap->lj.errhandler = errhandler;  /* may be NULL */
	thr->heap->lj.jmpbuf_ptr = &our_jmpbuf;

	if (setjmp(thr->heap->lj.jmpbuf_ptr->jb) == 0) {
		goto handle_call;
	}

	/*
	 *  Error during call.  The error value is at heap->lj.value1.
	 *
	 *  Careful with variable accesses here; must be assigned to before
	 *  setjmp() or be declared volatile.  See duk_handle_call().
	 *
	 *  The following are such variables:
	 *    - duk_handle_safe_call() parameters
	 *    - entry_*
	 *    - idx_retbase
	 *
	 *  The very first thing we do is restore the previous setjmp catcher.
	 *  This means that any error in error handling will propagate outwards
	 *  instead of causing a setjmp() re-entry above.  The *only* actual
	 *  errors that should happen here are allocation errors.
	 */

	DUK_DDDPRINT("error caught during protected duk_handle_safe_call()");

	DUK_ASSERT(thr->heap->lj.type == DUK_LJ_TYPE_THROW);
	DUK_ASSERT(thr->callstack_top >= entry_callstack_top);
	DUK_ASSERT(thr->catchstack_top >= entry_catchstack_top);

	/* Note: either pointer may be NULL (at entry), so don't assert;
	 * these are now restored twice which is OK.
	 */
	thr->heap->lj.jmpbuf_ptr = old_jmpbuf_ptr;
	thr->heap->lj.errhandler = old_errhandler;

	duk_hthread_catchstack_unwind(thr, entry_catchstack_top);
	duk_hthread_callstack_unwind(thr, entry_callstack_top);
	thr->valstack_bottom = thr->valstack + entry_valstack_bottom_index;

	/* [ ... | (crud) ] */

	/* FIXME: space in valstack?  see discussion in duk_handle_call. */
	duk_push_tval(ctx, &thr->heap->lj.value1);

	/* [ ... | (crud) errobj ] */

	DUK_ASSERT(duk_get_top(ctx) >= 1);  /* at least errobj must be on stack */

	/* check that the valstack has space for the final amount and any
	 * intermediate space needed; this is unoptimal but should be safe
	 */
	duk_require_stack_top(ctx, idx_retbase + num_stack_rets);  /* final configuration */
	duk_require_stack(ctx, num_stack_rets);

	safe_call_adjust_valstack(thr, idx_retbase, num_stack_rets, 1);  /* 1 = num actual 'return values' */

	/* [ ... | ] or [ ... | errobj (M * undefined)] where M = num_stack_rets - 1 */

#ifdef DUK_USE_DDDEBUG
	DUK_DDPRINT("protected safe_call error handling finished, thread dump:");
	DUK_DEBUG_DUMP_HTHREAD(thr);
#endif

	retval = DUK_ERR_EXEC_ERROR;
	goto shrink_and_finished;

	/*
	 *  Handle call (inside setjmp)
	 */

 handle_call:

	DUK_DDDPRINT("safe_call setjmp catchpoint setup complete, errhandler=%p",
	             (void *) thr->heap->lj.errhandler);

	/*
	 *  Thread state check and book-keeping.
	 */

	if (thr == thr->heap->curr_thread) {
		/* same thread */
		if (thr->state != DUK_HTHREAD_STATE_RUNNING) {
			/* should actually never happen, but check anyway */
			goto thread_state_error;
		}
	} else {
		/* different thread */
		DUK_ASSERT(thr->heap->curr_thread == NULL ||
		           thr->heap->curr_thread->state == DUK_HTHREAD_STATE_RUNNING);
		if (thr->state != DUK_HTHREAD_STATE_INACTIVE) {
			goto thread_state_error;
		}
		thr->heap->curr_thread = thr;
		thr->state = DUK_HTHREAD_STATE_RUNNING;

		/* Note: multiple threads may be simultaneously in the RUNNING
		 * state, but not in the same "resume chain".
		 */
	}

	DUK_ASSERT(thr->heap->curr_thread == thr);
	DUK_ASSERT(thr->state == DUK_HTHREAD_STATE_RUNNING);

	/*
	 *  Recursion limit check.
	 *
	 *  Note: there is no need for an "ignore recursion limit" flag
	 *  for duk_handle_safe_call now.
	 */

	DUK_ASSERT(thr->heap->call_recursion_depth >= 0);
	DUK_ASSERT(thr->heap->call_recursion_depth <= thr->heap->call_recursion_limit);
	if (thr->heap->call_recursion_depth >= thr->heap->call_recursion_limit) {
		DUK_ERROR(thr, DUK_ERR_INTERNAL_ERROR, "maximum C call stack depth reached");
	}
	thr->heap->call_recursion_depth++;

	/*
	 *  Valstack spare check
	 */

	duk_require_stack(ctx, 0);  /* internal spare */

	/*
	 *  Make the C call
	 */

	rc = func(ctx);

	DUK_DDDPRINT("safe_call, func rc=%d", rc);

	/*
	 *  Valstack manipulation for results
	 */

	/* we're running inside the caller's activation, so no change in call/catch stack or valstack bottom */
	DUK_ASSERT(thr->callstack_top == entry_callstack_top);
	DUK_ASSERT(thr->catchstack_top == entry_catchstack_top);
	DUK_ASSERT(thr->valstack_bottom - thr->valstack == entry_valstack_bottom_index);
	DUK_ASSERT(thr->valstack_bottom >= thr->valstack);
	DUK_ASSERT(thr->valstack_top >= thr->valstack_bottom);
	DUK_ASSERT(thr->valstack_end >= thr->valstack_top);

	if (rc < 0) {
		duk_error_throw_from_negative_rc(thr, rc);
	}
	DUK_ASSERT(rc >= 0);

	if (duk_get_top(ctx) < rc) {
		DUK_ERROR(thr, DUK_ERR_API_ERROR, "not enough stack values for safe_call rc");
	}

	safe_call_adjust_valstack(thr, idx_retbase, num_stack_rets, rc);

	/* Note: no need from callstack / catchstack shrink check */
	retval = DUK_ERR_EXEC_SUCCESS;
	goto finished;

 shrink_and_finished:
	/* these are "soft" shrink checks, whose failures are ignored */
	/* XXX: would be nice if fast path was inlined */
	duk_hthread_catchstack_shrink_check(thr);
	duk_hthread_callstack_shrink_check(thr);
	goto finished;

 finished:
	/* Note: either pointer may be NULL (at entry), so don't assert */
	thr->heap->lj.jmpbuf_ptr = old_jmpbuf_ptr;
	thr->heap->lj.errhandler = old_errhandler;

	/* These are just convenience "wiping" of state */
	thr->heap->lj.type = DUK_LJ_TYPE_UNKNOWN;
	thr->heap->lj.iserror = 0;

	/* FIXME: what about side effects here? finalizer runs should be shielded
	 * from errors so even out-of-memory should not be an issue here.
	 */
	DUK_TVAL_SET_TVAL(&tv_tmp, &thr->heap->lj.value1);
	DUK_TVAL_SET_UNDEFINED_UNUSED(&thr->heap->lj.value1);
	DUK_TVAL_DECREF(thr, &tv_tmp);

	DUK_TVAL_SET_TVAL(&tv_tmp, &thr->heap->lj.value2);
	DUK_TVAL_SET_UNDEFINED_UNUSED(&thr->heap->lj.value2);
	DUK_TVAL_DECREF(thr, &tv_tmp);

	DUK_DDDPRINT("setjmp catchpoint torn down");

	thr->heap->curr_thread = entry_curr_thread;  /* may be NULL */
	thr->state = entry_thread_state;

	DUK_ASSERT((thr->state == DUK_HTHREAD_STATE_INACTIVE && thr->heap->curr_thread == NULL) ||  /* first call */
	           (thr->state == DUK_HTHREAD_STATE_INACTIVE && thr->heap->curr_thread != NULL) ||  /* other call */
	           (thr->state == DUK_HTHREAD_STATE_RUNNING && thr->heap->curr_thread == thr));     /* current thread */

	thr->heap->call_recursion_depth = entry_call_recursion_depth;

	/* stack discipline consistency check */
	DUK_ASSERT(duk_get_top(ctx) == idx_retbase + num_stack_rets);

	return retval;

 thread_state_error:
	DUK_ERROR(thr, DUK_ERR_TYPE_ERROR, "invalid thread state for safe_call (%d)", thr->state);
	DUK_NEVER_HERE();
	return DUK_ERR_EXEC_ERROR;  /* never executed */
}

/*
 *  Helper for handling an Ecmascript-to-Ecmascript call or an Ecmascript
 *  function (initial) __duk__.resume().
 *
 *  Compared to normal calls handled by duk_handle_call(), there are a
 *  bunch of differences:
 *
 *    - the call is never protected, and current errhandler is not changed
 *    - there is no C recursion depth increase (hence an "ignore recursion
 *      limit" flag is not applicable)
 *    - instead of making the call, this helper just performs the thread
 *      setup and returns; the bytecode executor then restarts execution
 *      internally
 *    - ecmascript functions are never 'vararg' functions (they access
 *      varargs through the 'arguments' object)
 *
 *  The callstack of the target contains an earlier Ecmascript call in case
 *  of an Ecmascript-to-Ecmascript call (whose idx_retval is updated), or
 *  is empty in case of an initial __duk__.resume().
 */

void duk_handle_ecma_call_setup(duk_hthread *thr,
                                int num_stack_args,
                                int call_flags) {
	duk_context *ctx = (duk_context *) thr;
	int entry_valstack_bottom_index;
	int idx_func;         /* valstack index of 'func' and retval (relative to entry valstack_bottom) */
	int idx_args;         /* valstack index of start of args (arg1) (relative to entry valstack_bottom) */
	int nargs;            /* # argument registers target function wants (< 0 => never for ecma calls) */
	int nregs;            /* # total registers target function wants on entry (< 0 => never for ecma calls) */
	duk_hobject *func;    /* 'func' on stack (borrowed reference) */
	duk_activation *act;
	duk_hobject *env;

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(ctx != NULL);
	DUK_ASSERT(!((call_flags & DUK_CALL_FLAG_IS_RESUME) != 0 && (call_flags & DUK_CALL_FLAG_IS_TAILCALL) != 0));

	/* XXX: assume these? */
	DUK_ASSERT(thr->valstack != NULL);
	DUK_ASSERT(thr->callstack != NULL);
	DUK_ASSERT(thr->catchstack != NULL);

	/* no need to handle thread state book-keeping here */
	DUK_ASSERT((call_flags & DUK_CALL_FLAG_IS_RESUME) != 0 ||
	           (thr->state == DUK_HTHREAD_STATE_RUNNING &&
	            thr->heap->curr_thread == thr));

	/* if a tailcall:
	 *   - an Ecmascript activation must be on top of the callstack
	 *   - there cannot be any active catchstack entries
	 */
#ifdef DUK_USE_ASSERTIONS
	if (call_flags & DUK_CALL_FLAG_IS_TAILCALL) {
		int our_callstack_index;
		int i;

		DUK_ASSERT(thr->callstack_top >= 1);
		our_callstack_index = thr->callstack_top - 1;
		DUK_ASSERT(our_callstack_index >= 0 && our_callstack_index < thr->callstack_size);
		DUK_ASSERT(thr->callstack[our_callstack_index].func != NULL);
		DUK_ASSERT(DUK_HOBJECT_IS_COMPILEDFUNCTION(thr->callstack[our_callstack_index].func));

		/* now checks entire callstack, would suffice to check just the top entry */
		for (i = 0; i < thr->catchstack_top; i++) {
			DUK_ASSERT(thr->catchstack[i].callstack_index < our_callstack_index);
		}
	}
#endif  /* DUK_USE_ASSERTIONS */

	entry_valstack_bottom_index = (int) (thr->valstack_bottom - thr->valstack);
	idx_func = duk_normalize_index(thr, -num_stack_args - 2);
	idx_args = idx_func + 2;

	DUK_DDPRINT("handle_ecma_call_setup: thr=%p, "
	            "num_stack_args=%d, call_flags=%d (resume=%d, tailcall=%d), "
	            "idx_func=%d, idx_args=%d, entry_valstack_bottom_index=%d",
	            (void *) thr,
	            num_stack_args,
	            call_flags,
	            ((call_flags & DUK_CALL_FLAG_IS_RESUME) != 0 ? 1 : 0),
	            ((call_flags & DUK_CALL_FLAG_IS_TAILCALL) != 0 ? 1 : 0),
	            idx_func,
	            idx_args,
	            entry_valstack_bottom_index);

#ifdef DUK_USE_DDDEBUG
	DUK_DPRINT("callstack before call setup:");
	DUK_DEBUG_DUMP_CALLSTACK(thr);
#endif

	if (idx_func < 0 || idx_args < 0) {
		/* XXX: assert? compiler is responsible for this never happening */
		DUK_ERROR(thr, DUK_ERR_API_ERROR, "invalid func index");
	}

	/*
	 *  Check the function type, handle bound function chains,
	 *  and prepare parameters for the rest of the call handling.
	 *  Also figure out the effective 'this' binding, which replaces
	 *  the current value at idx_func + 1.
	 *
	 *  If the target function is a 'bound' one, follow the chain
	 *  of 'bound' functions until a non-bound function is found.
	 *  During this process, bound arguments are 'prepended' to
	 *  existing ones, and the "this" binding is overridden.
	 *  See E5 Section 15.3.4.5.1.
	 */

	if (!duk_is_callable(thr, idx_func)) {
		DUK_ERROR(ctx, DUK_ERR_TYPE_ERROR, "call target not callable");
	}
	func = duk_get_hobject(thr, idx_func);
	DUK_ASSERT(func != NULL);

	if (DUK_HOBJECT_HAS_BOUND(func)) {
		/* slow path for bound functions */
		handle_bound_chain_for_call(thr, idx_func, &num_stack_args, &func);
	}
	DUK_ASSERT(!DUK_HOBJECT_HAS_BOUND(func));
	DUK_ASSERT(DUK_HOBJECT_IS_COMPILEDFUNCTION(func));  /* caller must ensure this */

	handle_coerce_effective_this_binding(thr, func, idx_func + 1);
	DUK_DDDPRINT("effective 'this' binding is: %!T", duk_get_tval(ctx, idx_func + 1));

	nargs = ((duk_hcompiledfunction *) func)->nargs;
	nregs = ((duk_hcompiledfunction *) func)->nregs;
	DUK_ASSERT(nregs >= nargs);

	/* [ ... func this arg1 ... argN ] */

	/*
	 *  Preliminary activation record and valstack manipulation.
	 *  The concrete actions depend on whether the we're dealing
	 *  with a tailcall (reuse an existing activation), a resume,
	 *  or a normal call.
	 *
	 *  The basic actions, in varying order, are:
	 *
	 *    - Check stack size for call handling
	 *    - Grow call stack if necessary (non-tail-calls)
	 *    - Update current activation (idx_retval) if necessary
	 *      (non-tail, non-resume calls)
	 *    - Move start of args (idx_args) to valstack bottom
	 *      (tail calls)
	 *
	 *  Don't touch valstack_bottom or valstack_top yet so that Duktape API
	 *  calls work normally.
	 */

	/* XXX: some overlapping code; cleanup */

	if (call_flags & DUK_CALL_FLAG_IS_TAILCALL) {
#ifdef DUK_USE_REFERENCE_COUNTING
		duk_hobject *tmp;
#endif
		duk_tval *tv1, *tv2;
		duk_tval tv_tmp;
		int i;

		DUK_DDDPRINT("is tailcall, reusing activation at callstack top, at index %d",
		             thr->callstack_top - 1);

		DUK_ASSERT(thr->callstack_top <= thr->callstack_size);
		act = thr->callstack + thr->callstack_top - 1;

		DUK_ASSERT(!DUK_HOBJECT_HAS_BOUND(func));
		DUK_ASSERT(!DUK_HOBJECT_HAS_NATIVEFUNCTION(func));
		DUK_ASSERT(DUK_HOBJECT_HAS_COMPILEDFUNCTION(func));

		/* Note: since activation is still reachable, refcount manipulation
		 * must be very careful to avoid side effect issues.  Also, 'act'
		 * must be looked up again (finalizer calls may reallocate).
		 */

		/* XXX: a 'raw' decref + explicit refzero check afterwards would be
		 * very useful here.
		 */

#ifdef DUK_USE_REFERENCE_COUNTING
		tmp = act->var_env;
#endif
		act->var_env = NULL;
#ifdef DUK_USE_REFERENCE_COUNTING
		DUK_HOBJECT_DECREF(thr, tmp);  /* side effects */
		act = thr->callstack + thr->callstack_top - 1;
#endif

#ifdef DUK_USE_REFERENCE_COUNTING
		tmp = act->lex_env;
#endif
		act->lex_env = NULL;
#ifdef DUK_USE_REFERENCE_COUNTING
		DUK_HOBJECT_DECREF(thr, tmp);  /* side effects */
		act = thr->callstack + thr->callstack_top - 1;
#endif

		DUK_DDDPRINT("tailcall -> decref func");
#ifdef DUK_USE_REFERENCE_COUNTING
		tmp = act->func;
		DUK_ASSERT(tmp != NULL);
		DUK_ASSERT(DUK_HOBJECT_IS_COMPILEDFUNCTION(tmp));
#endif
		act->func = func;  /* don't want an intermediate exposed state with func == NULL */
		act->pc = 0;       /* don't want an intermediate exposed state with invalid pc */
#ifdef DUK_USE_REFERENCE_COUNTING
		DUK_HOBJECT_INCREF(thr, func);
		DUK_HOBJECT_DECREF(thr, tmp);  /* side effects */
		act = thr->callstack + thr->callstack_top - 1;
#endif

		act->flags = (DUK_HOBJECT_HAS_STRICT(func) ?
		              DUK_ACT_FLAG_STRICT | DUK_ACT_FLAG_TAILCALLED :
	        	      DUK_ACT_FLAG_TAILCALLED);
	
		/* act->func: already updated */
		/* act->var_env: already NULLed */
		/* act->lex_env: already NULLed */
		/* act->pc: already zeroed */
		act->idx_bottom = entry_valstack_bottom_index;  /* tail call -> reuse current "frame" */
		DUK_ASSERT(nregs >= 0);
#if 0  /* topmost activation idx_retval is considered garbage, no need to init */
		act->idx_retval = -1;  /* idx_retval is a 'caller' retval, so init to "unused" here */
#endif

		/*
		 *  Manipulate valstack so that args are on the current bottom and the
		 *  previous caller's 'this' binding (which is the value preceding the
		 *  current bottom) is replaced with the new 'this' binding:
		 *
		 *       [ ... this_old | (crud) func this_new arg1 ... argN ]
		 *  -->  [ ... this_new | arg1 ... argN ]
		 *
		 *  For tailcalling to work properly, the valstack bottom must not grow
		 *  here; otherwise crud would accumulate on the valstack.
		 */

		tv1 = thr->valstack_bottom - 1;
		tv2 = thr->valstack_bottom + idx_func + 1;
		DUK_ASSERT(tv1 >= thr->valstack && tv1 < thr->valstack_top);  /* tv1 is -below- valstack_bottom */
		DUK_ASSERT(tv2 >= thr->valstack_bottom && tv2 < thr->valstack_top);
		DUK_TVAL_SET_TVAL(&tv_tmp, tv1);
		DUK_TVAL_SET_TVAL(tv1, tv2);
		DUK_TVAL_INCREF(thr, tv1);
		DUK_TVAL_DECREF(thr, &tv_tmp);  /* side effects */
		
		for (i = 0; i < idx_args; i++) {
			/* XXX: block removal API primitive */
			/* Note: 'func' is popped from valstack here, but it is
			 * already reachable from the activation.
			 */
			duk_remove(ctx, 0);
		}
		idx_func = 0;  /* really 'not applicable' anymore, should not be referenced after this */
		idx_args = 0;

		/* [ ... this_new | arg1 ... argN ] */

		/* now we can also do the valstack resize check */

		duk_require_valstack_resize((duk_context *) thr,
		                            (thr->valstack_bottom - thr->valstack) +         /* bottom of current func */
		                                idx_args +                                   /* bottom of new func (always 0 here) */
		                                nregs +                                      /* num entries of new func at entry */
		                                DUK_VALSTACK_INTERNAL_EXTRA,                 /* + spare => min_new_size */
		                            1);                                              /* allow_shrink */
	} else {
		DUK_DDDPRINT("not a tailcall, pushing a new activation to callstack, to index %d",
		             thr->callstack_top);

		duk_hthread_callstack_grow(thr);

		/* func wants args clamped to 'nargs' */

		duk_require_valstack_resize((duk_context *) thr,
		                            (thr->valstack_bottom - thr->valstack) +         /* bottom of current func */
		                                idx_args +                                   /* bottom of new func */
		                                nregs +                                      /* num entries of new func at entry */
		                                DUK_VALSTACK_INTERNAL_EXTRA,                 /* + spare => min_new_size */
		                            1);                                              /* allow_shrink */

		if (call_flags & DUK_CALL_FLAG_IS_RESUME) {
			DUK_DDDPRINT("is resume -> no update to current activation (may not even exist)");
		} else {
			DUK_DDDPRINT("update to current activation idx_retval");
			DUK_ASSERT(thr->callstack_top < thr->callstack_size);
			DUK_ASSERT(thr->callstack_top >= 1);
			act = thr->callstack + thr->callstack_top - 1;
			DUK_ASSERT(act->func != NULL);
			DUK_ASSERT(DUK_HOBJECT_IS_COMPILEDFUNCTION(act->func));
			act->idx_retval = entry_valstack_bottom_index + idx_func;
		}

		DUK_ASSERT(thr->callstack_top < thr->callstack_size);
		act = &thr->callstack[thr->callstack_top];
		thr->callstack_top++;
		DUK_ASSERT(thr->callstack_top <= thr->callstack_size);

		DUK_ASSERT(!DUK_HOBJECT_HAS_BOUND(func));
		DUK_ASSERT(!DUK_HOBJECT_HAS_NATIVEFUNCTION(func));
		DUK_ASSERT(DUK_HOBJECT_HAS_COMPILEDFUNCTION(func));

		act->flags = (DUK_HOBJECT_HAS_STRICT(func) ?
		              DUK_ACT_FLAG_STRICT :
	        	      0);
		act->func = func;
		act->var_env = NULL;
		act->lex_env = NULL;
		act->pc = 0;
		act->idx_bottom = entry_valstack_bottom_index + idx_args;
		DUK_ASSERT(nregs >= 0);
#if 0  /* topmost activation idx_retval is considered garbage, no need to init */
		act->idx_retval = -1;  /* idx_retval is a 'caller' retval, so init to "unused" here */
#endif

		DUK_HOBJECT_INCREF(thr, func);  /* act->func */
	}

	/* [... func this arg1 ... argN]  (not tail call)
	 * [this | arg1 ... argN]         (tail call)
	 *
	 * idx_args updated to match
	 */

#ifdef DUK_USE_DDDEBUG
	DUK_DPRINT("pushed new activation:");
	DUK_DEBUG_DUMP_ACTIVATION(thr, thr->callstack + thr->callstack_top - 1);
#endif

	/*
	 *  Environment record creation and 'arguments' object creation.
	 *  Named function expression name binding is handled by the
	 *  compiler; the compiled function's parent env will contain
	 *  the (immutable) binding already.
	 *
	 *  Delayed creation (on demand) is handled in duk_js_var.c.
	 */

	DUK_ASSERT(!DUK_HOBJECT_HAS_BOUND(func));  /* bound function chain has already been resolved */

	if (!DUK_HOBJECT_HAS_NEWENV(func)) {
		/* use existing env (e.g. for non-strict eval); cannot have
		 * an own 'arguments' object (but can refer to the existing one)
		 */

		handle_oldenv_for_call(thr, func, act);

		DUK_ASSERT(act->lex_env != NULL);
		DUK_ASSERT(act->var_env != NULL);
		goto env_done;
	}

	DUK_ASSERT(DUK_HOBJECT_HAS_NEWENV(func));

	if (!DUK_HOBJECT_HAS_CREATEARGS(func)) {
		/* no need to create environment record now; leave as NULL */
		DUK_ASSERT(act->lex_env == NULL);
		DUK_ASSERT(act->var_env == NULL);
		goto env_done;
	}

	/* third arg: absolute index (to entire valstack) of idx_bottom of new activation */
	env = duk_create_activation_environment_record(thr, func, act->idx_bottom);
	DUK_ASSERT(env != NULL);

	/* [... arg1 ... argN envobj] */

	DUK_ASSERT(DUK_HOBJECT_HAS_CREATEARGS(func));
	handle_createargs_for_call(thr, func, env, num_stack_args);

	/* [... arg1 ... argN envobj] */

	act->lex_env = env;
	act->var_env = env;
	DUK_HOBJECT_INCREF(thr, act->lex_env);
	DUK_HOBJECT_INCREF(thr, act->var_env);
	duk_pop(ctx);

 env_done:
	/* [... arg1 ... argN] */

	/*
	 *  Setup value stack: clamp to 'nargs', fill up to 'nregs'
	 */

	/* XXX: replace with a single operation */

	DUK_ASSERT(nregs >= 0);
	duk_set_top(ctx, idx_args + nargs);  /* clamp anything above nargs */
	duk_set_top(ctx, idx_args + nregs);  /* extend with undefined */

#ifdef DUK_USE_DDDEBUG
	DUK_DPRINT("callstack after call setup:");
	DUK_DEBUG_DUMP_CALLSTACK(thr);
#endif

	/*
	 *  Shift to new valstack_bottom.
	 */

	thr->valstack_bottom = thr->valstack_bottom + idx_args;
	/* keep current valstack_top */
	DUK_ASSERT(thr->valstack_bottom >= thr->valstack);
	DUK_ASSERT(thr->valstack_top >= thr->valstack_bottom);
	DUK_ASSERT(thr->valstack_end >= thr->valstack_top);

	/*
	 *  Return to bytecode executor, which will resume execution from
	 *  the topmost activation.
	 */
}

