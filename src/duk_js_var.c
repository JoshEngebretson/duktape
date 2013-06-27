/*
 *  Identifier access and function closure handling.
 *
 *  Provides the primitives for slow path identifier accesses: GETVAR,
 *  PUTVAR, DELVAR, etc.  The fast path, direct register accesses, should
 *  be used for most identifier accesses.  Consequently, these slow path
 *  primitives should be optimized for maximum compactness.
 *
 *  Ecmascript environment records (declarative and object) are represented
 *  as internal objects with control keys.  Environment records have a
 *  parent record ("outer environment reference") which is represented by
 *  the implicit prototype for technical reasons (in other words, it is a
 *  convenient field).  The prototype chain is not followed in the ordinary
 *  sense for variable lookups.
 *
 *  See identifier-design.txt and identifier-algorithms.txt for more details
 *  on identifier handling, and function-objects.txt for details on what
 *  function instances are expected to look like.
 *
 *  Care must be taken to avoid duk_tval pointer invalidation caused by
 *  e.g. value stack or object resizing.
 *
 *  FIXME: properties for function instances could be initialized much more
 *  efficiently by creating a property allocation for a certain size and
 *  filling in keys and values directly (and INCREFing both with "bulk incref"
 *  primitives.
 *
 *  FIXME: duk_hobject_getprop() and duk_hobject_putprop() calls are a bit
 *  awkward (especially because they follow the prototype chain); rework
 *  if "raw" own property helpers are added.
 */

#include "duk_internal.h"

/*
 *  Local result type for get_identifier_reference() lookup.
 */

typedef struct {
	duk_hobject *holder;      /* for object-bound identifiers */
	duk_tval *value;          /* for register-bound identifiers */
	duk_tval *this_binding;
	duk_hobject *env;
} duk_id_lookup_result;

/*
 *  Create a new function object based on a "template function"
 *  which contains compiled bytecode, constants, etc, but lacks
 *  a lexical environment.
 *
 *  Ecmascript requires that each created closure is a separate
 *  object, with its own set of editable properties.  However,
 *  structured property values (such as the formal arguments
 *  list and the variable map) are shared.  Also the bytecode,
 *  constants, and inner functions are shared.
 *
 *  See E5 Section 13.2 for detailed requirements on the function
 *  objects; there are no similar requirements for function
 *  "templates" which are an implementation dependent feature.
 *  Also see function-objects.txt for a discussion on the function
 *  instance properties provided by this implementation.
 *
 *  Notes:
 *
 *   * Order of internal properties should match frequency of
 *     use, since the properties will be linearly scanned on
 *     lookup (functions usually don't have enough properties
 *     to warrant a hash part).
 *
 *   * The created closure is independent of its template;
 *     they do share the same 'data' buffer object, but the
 *     template object itself can be freed even if the closure
 *     object remains reachable.
 */

/* FIXME: need to recheck the resulting Function instance objects for
 * compliance to E5 Section 13.2.
 */

static void increase_data_inner_refcounts(duk_hthread *thr, duk_hcompiledfunction *f) {
	duk_tval *tv, *tv_end;
	duk_hobject **funcs, **funcs_end;

	DUK_ASSERT(f->data != NULL);  /* compiled functions must be created 'atomically' */

	tv = DUK_HCOMPILEDFUNCTION_GET_CONSTS_BASE(f);
	tv_end = DUK_HCOMPILEDFUNCTION_GET_CONSTS_END(f);
	while (tv < tv_end) {
		DUK_TVAL_INCREF(thr, tv);
		tv++;
	}

	funcs = DUK_HCOMPILEDFUNCTION_GET_FUNCS_BASE(f);
	funcs_end = DUK_HCOMPILEDFUNCTION_GET_FUNCS_END(f);
	while (funcs < funcs_end) {
		DUK_HEAPHDR_INCREF(thr, (duk_heaphdr *) *funcs);
		funcs++;
	}
}

/* Push a new closure on the stack.
 *
 * Note: if fun_temp has NEWENV, i.e. a new lexical and variable
 * declaration is created when the function is called, only
 * outer_lex_env matters (outer_var_env is ignored and may or
 * may not be same as outer_lex_env).
 */
void duk_js_push_closure(duk_hthread *thr,
                         duk_hcompiledfunction *fun_temp,
                         duk_hobject *outer_var_env,
                         duk_hobject *outer_lex_env) {
	duk_context *ctx = (duk_context *) thr;
	duk_hcompiledfunction *fun_clos;
	duk_u16 proplist[] = { DUK_STRIDX_INT_VARMAP,
	                       DUK_STRIDX_INT_FORMALS,
	                       DUK_STRIDX_INT_SOURCE,
	                       DUK_STRIDX_NAME,
	                       DUK_STRIDX_INT_PC2LINE,
	                       DUK_STRIDX_INT_FILENAME };  /* order: most frequent to least frequent */
	int i;
	duk_u32 len_value;

	DUK_ASSERT(fun_temp != NULL);
	DUK_ASSERT(fun_temp->data != NULL);
	DUK_ASSERT(fun_temp->funcs != NULL);
	DUK_ASSERT(fun_temp->bytecode != NULL);
	DUK_ASSERT(outer_var_env != NULL);
	DUK_ASSERT(outer_lex_env != NULL);

	duk_push_new_compiledfunction(ctx);
	duk_push_hobject(ctx, &fun_temp->obj);  /* -> [ ... closure template ] */

	fun_clos = (duk_hcompiledfunction *) duk_get_hobject(ctx, -2);  /* FIXME: duk_get_hcompiledfunction */
	DUK_ASSERT(DUK_HOBJECT_IS_COMPILEDFUNCTION((duk_hobject *) fun_clos));
	DUK_ASSERT(fun_clos != NULL);
	DUK_ASSERT(fun_clos->data == NULL);
	DUK_ASSERT(fun_clos->funcs == NULL);
	DUK_ASSERT(fun_clos->bytecode == NULL);

	fun_clos->data = fun_temp->data;
	fun_clos->funcs = fun_temp->funcs;
	fun_clos->bytecode = fun_temp->bytecode;

	/* Note: all references inside 'data' need to get their refcounts
	 * upped too.  This is the case because refcounts are decreased
	 * through every function referencing 'data' independently.
	 */

	DUK_HBUFFER_INCREF(thr, fun_clos->data);
	increase_data_inner_refcounts(thr, fun_temp);

	fun_clos->nregs = fun_temp->nregs;
	fun_clos->nargs = fun_temp->nargs;

	DUK_ASSERT(fun_clos->data != NULL);
	DUK_ASSERT(fun_clos->funcs != NULL);
	DUK_ASSERT(fun_clos->bytecode != NULL);

	/* XXX: or copy from template? */
	DUK_HOBJECT_SET_PROTOTYPE(thr, &fun_clos->obj, thr->builtins[DUK_BIDX_FUNCTION_PROTOTYPE]);  /* contains incref */

	/*
	 *  Init/assert flags, copying them where appropriate.
	 *  Some flags (like NEWENV) are processed separately below.
	 */

	/* FIXME: copy flags using a mask */

	DUK_ASSERT(DUK_HOBJECT_HAS_EXTENSIBLE(&fun_clos->obj));
	DUK_HOBJECT_SET_CONSTRUCTABLE(&fun_clos->obj);  /* Note: not set in template (has no "prototype") */
	DUK_ASSERT(DUK_HOBJECT_HAS_CONSTRUCTABLE(&fun_clos->obj));
	DUK_ASSERT(!DUK_HOBJECT_HAS_BOUND(&fun_clos->obj));
	DUK_ASSERT(DUK_HOBJECT_HAS_COMPILEDFUNCTION(&fun_clos->obj));
	DUK_ASSERT(!DUK_HOBJECT_HAS_NATIVEFUNCTION(&fun_clos->obj));
	DUK_ASSERT(!DUK_HOBJECT_HAS_THREAD(&fun_clos->obj));
	/* DUK_HOBJECT_FLAG_ARRAY_PART: don't care */
	if (DUK_HOBJECT_HAS_STRICT(&fun_temp->obj)) {
		DUK_HOBJECT_SET_STRICT(&fun_clos->obj);
	}
	/* DUK_HOBJECT_FLAG_NEWENV: handled below */
	DUK_ASSERT(!DUK_HOBJECT_HAS_NAMEBINDING(&fun_clos->obj));
	if (DUK_HOBJECT_HAS_CREATEARGS(&fun_temp->obj)) {
		DUK_HOBJECT_SET_CREATEARGS(&fun_clos->obj);
	}
	DUK_ASSERT(!DUK_HOBJECT_HAS_SPECIAL_ARRAY(&fun_clos->obj));
	DUK_ASSERT(!DUK_HOBJECT_HAS_SPECIAL_STRINGOBJ(&fun_clos->obj));
	DUK_ASSERT(!DUK_HOBJECT_HAS_SPECIAL_ARGUMENTS(&fun_clos->obj));

	/*
	 *  Setup environment record properties based on the template
	 *  and its flags.
	 *
	 *  If DUK_HOBJECT_HAS_NEWENV(fun_temp) is true, the environment
	 *  records represent identifiers "outside" the function; the
	 *  "inner" environment records are created on demand.  Otherwise,
	 *  the environment records are those that will be directly used
	 *  (e.g. for declarations).
	 *
	 *  _lexenv is always set; _varenv defaults to _lexenv if missing,
	 *  so _varenv is only set if _lexenv != _varenv.
	 *
	 *  This is relatively complex, see doc/identifier-handling.txt.
	 */

	if (DUK_HOBJECT_HAS_NEWENV(&fun_temp->obj)) {
		DUK_HOBJECT_SET_NEWENV(&fun_clos->obj);

		if (DUK_HOBJECT_HAS_NAMEBINDING(&fun_temp->obj)) {
			duk_hobject *proto;
			duk_hobject *env;

			/*
			 *  Named function expression, name needs to be bound
			 *  in an intermediate environment record.  The "outer"
			 *  lexical/variable environment will thus be:
			 *
			 *  a) { funcname: <func>, _prototype: outer_lex_env }
			 *  b) { funcname: <func>, _prototype:  <globalenv> }  (if outer_lex_env missing)
			 */

			DUK_ASSERT(duk_has_prop_stridx(ctx, -1, DUK_STRIDX_NAME));  /* required if NAMEBINDING set */

			if (outer_lex_env) {
				proto = outer_lex_env;
			} else {
				proto = thr->builtins[DUK_BIDX_GLOBAL_ENV];
			}

			/* -> [ ... closure template env ] */
			(void) duk_push_new_object_helper(ctx,
	   		                                  DUK_HOBJECT_FLAG_EXTENSIBLE |
			                                  DUK_HOBJECT_CLASS_AS_FLAGS(DUK_HOBJECT_CLASS_DECENV),
			                                  -1);  /* no prototype, updated below */

			duk_get_prop_stridx(ctx, -2, DUK_STRIDX_NAME);       /* -> [ ... closure template env funcname ] */
			duk_dup(ctx, -4);                                    /* -> [ ... closure template env funcname closure ] */
			duk_def_prop(ctx, -3, DUK_PROPDESC_FLAGS_NONE);      /* -> [ ... closure template env ] */

			env = duk_get_hobject(ctx, -1);
			DUK_ASSERT(env != NULL);
			DUK_ASSERT(proto != NULL);
			DUK_HOBJECT_SET_PROTOTYPE(thr, env, proto);  /* increases 'proto' refcount */

			/* [ ... closure template env ] */

			duk_def_prop_stridx(ctx, -3, DUK_STRIDX_INT_LEXENV, DUK_PROPDESC_FLAGS_WC);
			/* since closure has NEWENV, never define DUK_STRIDX_INT_VARENV, as it
			 * will be ignored anyway
			 */

			/* [ ... closure template ] */
		} else {
			/*
			 *  Other cases (function declaration, anonymous function expression,
			 *  strict direct eval code).  The "outer" environment will be whatever
			 *  the caller gave us.
			 */

			duk_push_hobject(ctx, outer_lex_env);  /* -> [ ... closure template env ] */
			duk_def_prop_stridx(ctx, -3, DUK_STRIDX_INT_LEXENV, DUK_PROPDESC_FLAGS_WC);
			/* since closure has NEWENV, never define DUK_STRIDX_INT_VARENV, as it
			 * will be ignored anyway
			 */

			/* [ ... closure template ] */
		}
	} else {
		/*
		 *  Function gets no new environment when called.  This is the
		 *  case for global code, indirect eval code, and non-strict
		 *  direct eval code.  There is no direct correspondence to the
		 *  E5 specification, as global/eval code is not exposed as a
		 *  function.
		 */

		DUK_ASSERT(!DUK_HOBJECT_HAS_NAMEBINDING(&fun_temp->obj));

		duk_push_hobject(ctx, outer_lex_env);  /* -> [ ... closure template env ] */
		duk_def_prop_stridx(ctx, -3, DUK_STRIDX_INT_LEXENV, DUK_PROPDESC_FLAGS_WC);

		if (outer_var_env != outer_lex_env) {
			duk_push_hobject(ctx, outer_var_env);  /* -> [ ... closure template env ] */
			duk_def_prop_stridx(ctx, -3, DUK_STRIDX_INT_VARENV, DUK_PROPDESC_FLAGS_WC);
		}
	}
#ifdef DUK_USE_DDDEBUG
	duk_get_prop_stridx(ctx, -2, DUK_STRIDX_INT_VARENV);
	duk_get_prop_stridx(ctx, -3, DUK_STRIDX_INT_LEXENV);
	DUK_DDDPRINT("closure varenv -> %!ipT, lexenv -> %!ipT", duk_get_tval(ctx, -2), duk_get_tval(ctx, -1));
	duk_pop_2(ctx);
#endif

	/*
	 *  Copy some internal properties directly
	 */

	/* [ ... closure template ] */

	DUK_DDDPRINT("copying properties: closure=%!iT, template=%!iT", duk_get_tval(ctx, -2), duk_get_tval(ctx, -1));

	for (i = 0; i < sizeof(proplist) / sizeof(duk_u16); i++) {
		int stridx = (int) proplist[i];
		if (duk_get_prop_stridx(ctx, -1, stridx)) {
			/* [ ... closure template val ] */
			DUK_DDDPRINT("copying property, stridx=%d -> found", stridx);
			duk_def_prop_stridx(ctx, -3, stridx, DUK_PROPDESC_FLAGS_WC);
		} else {
			DUK_DDDPRINT("copying property, stridx=%d -> not found", stridx);
			duk_pop(ctx);
		}
	}

	/*
	 *  "length" maps to number of formals (E5 Section 13.2) for
	 *  function declarations/expressions (non-bound functions).
	 *  Note that 'nargs' is NOT necessarily equal to the number
	 *  of arguments.
	 */

	/* [ ... closure template ] */

	len_value = 0;

	/* FIXME: use helper for size optimization */
	if (duk_get_prop_stridx(ctx, -2, DUK_STRIDX_INT_FORMALS)) {
		/* [ ... closure template formals ] */
		DUK_ASSERT(duk_has_prop_stridx(ctx, -1, DUK_STRIDX_LENGTH));
		duk_get_prop_stridx(ctx, -1, DUK_STRIDX_LENGTH);
		DUK_ASSERT(duk_is_number(ctx, -1));
		len_value = duk_to_int(ctx, -1);
		duk_pop_2(ctx);
	} else {
		duk_pop(ctx);  /* FIXME: this is tedious.. another wrapper function? */
		/* FIXME: what to do if _formals is not empty but compiler has optimized
		 * it away -- read length from an explicit property then?
		 */
	}

	duk_push_int(ctx, len_value);  /* [ ... closure template len_value ] */
	duk_def_prop_stridx(ctx, -3, DUK_STRIDX_LENGTH, DUK_PROPDESC_FLAGS_NONE);

	/*
	 *  "prototype" is, by default, a fresh object with the "constructor"
	 *  property.
	 *
	 *  Note that this creates a circular reference for every function
	 *  instance (closure) which prevents refcount-based collection of
	 *  function instances.
	 *
	 *  FIXME: Try to avoid creating the default prototype object, because
	 *  many functions are not used as constructors and the default
	 *  prototype is unnecessary.  Perhaps it could be created on-demand
	 *  when it is first accessed?
	 */

	/* [ ... closure template ] */

	duk_push_new_object(ctx);  /* -> [ ... closure template newobj ] */
	duk_dup(ctx, -3);          /* -> [ ... closure template newobj closure ] */
	duk_def_prop_stridx(ctx, -2, DUK_STRIDX_CONSTRUCTOR, DUK_PROPDESC_FLAGS_WC);  /* -> [ ... closure template newobj ] */
	duk_def_prop_stridx(ctx, -3, DUK_STRIDX_PROTOTYPE, DUK_PROPDESC_FLAGS_W);     /* -> [ ... closure template ] */

	/*
	 *  "arguments" and "caller" must be mapped to throwers for
	 *  strict mode and bound functions (E5 Section 15.3.5).
	 *
	 *  FIXME: This is expensive to have for every strict function instance.
	 *  Try to implement as virtual properties or on-demand created properties.
	 */

	/* [ ... closure template ] */

	if (DUK_HOBJECT_HAS_STRICT(&fun_clos->obj)) {
		duk_def_prop_stridx_thrower(ctx, -2, DUK_STRIDX_CALLER, DUK_PROPDESC_FLAGS_NONE);
		duk_def_prop_stridx_thrower(ctx, -2, DUK_STRIDX_LC_ARGUMENTS, DUK_PROPDESC_FLAGS_NONE);
	}

	/*
	 *  "name" is a non-standard property found in at least V8, Rhino, smjs.
	 *  For Rhino and smjs it is non-writable, non-enumerable, and non-configurable;
	 *  for V8 it is writable, non-enumerable, non-configurable.  It is also defined
	 *  for an anonymous function expression in which case the value is an empty string.
	 *
	 *  FIXME: make optional?  costs something per function.
	 */

	/* [ ... closure template ] */

	if (duk_get_prop_stridx(ctx, -1, DUK_STRIDX_NAME)) {
		/* [ ... closure template name ] */
		DUK_ASSERT(duk_is_string(ctx, -1));
	} else {
		/* [ ... closure template undefined ] */
		duk_pop(ctx);
		duk_push_hstring_stridx(ctx, DUK_STRIDX_EMPTY_STRING);
	}
	duk_def_prop_stridx(ctx, -3, DUK_STRIDX_NAME, DUK_PROPDESC_FLAGS_NONE);  /* -> [ ... closure template ] */

	/*
	 *  Some assertions (E5 Section 13.2).
	 */

	DUK_ASSERT(DUK_HOBJECT_GET_CLASS_NUMBER(&fun_clos->obj) == DUK_HOBJECT_CLASS_FUNCTION);
	DUK_ASSERT(fun_clos->obj.prototype == thr->builtins[DUK_BIDX_FUNCTION_PROTOTYPE]);
	DUK_ASSERT(DUK_HOBJECT_HAS_EXTENSIBLE(&fun_clos->obj));
	DUK_ASSERT(duk_has_prop_stridx(ctx, -2, DUK_STRIDX_LENGTH) != 0);
	DUK_ASSERT(duk_has_prop_stridx(ctx, -2, DUK_STRIDX_PROTOTYPE) != 0);
	DUK_ASSERT(duk_has_prop_stridx(ctx, -2, DUK_STRIDX_NAME) != 0);  /* non-standard */
	DUK_ASSERT(!DUK_HOBJECT_HAS_STRICT(&fun_clos->obj) ||
	           duk_has_prop_stridx(ctx, -2, DUK_STRIDX_CALLER) != 0);
	DUK_ASSERT(!DUK_HOBJECT_HAS_STRICT(&fun_clos->obj) ||
	           duk_has_prop_stridx(ctx, -2, DUK_STRIDX_LC_ARGUMENTS) != 0);

	/*
	 *  Finish
	 */
	
	/* [ ... closure template ] */

	DUK_DDDPRINT("created function instance: template=%!iT -> closure=%!iT",
	             duk_get_tval(ctx, -1), duk_get_tval(ctx, -2));

	duk_pop(ctx);

	/* [ ... closure ] */
}

/*
 *  Delayed activation environment record initialization (for functions
 *  with NEWENV).
 *
 *  The non-delayed initialization is handled by duk_handle_call().
 */

/* shared helper */
duk_hobject *duk_create_activation_environment_record(duk_hthread *thr,
                                                      duk_hobject *func,
                                                      duk_u32 idx_bottom) {
	duk_context *ctx = (duk_context *) thr;
	duk_hobject *env;
	duk_hobject *parent;
	duk_tval *tv;

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(func != NULL);

	tv = duk_hobject_find_existing_entry_tval_ptr(func, DUK_HEAP_STRING_INT_LEXENV(thr));
	if (tv) {
		DUK_ASSERT(DUK_TVAL_IS_OBJECT(tv));
		DUK_ASSERT(DUK_HOBJECT_IS_ENV(DUK_TVAL_GET_OBJECT(tv)));
		parent = DUK_TVAL_GET_OBJECT(tv);
	} else {
		parent = thr->builtins[DUK_BIDX_GLOBAL_ENV];
	}

	(void) duk_push_new_object_helper(ctx,
	                                  DUK_HOBJECT_FLAG_EXTENSIBLE |
	                                  DUK_HOBJECT_CLASS_AS_FLAGS(DUK_HOBJECT_CLASS_DECENV),
	                                  -1);  /* no prototype, updated below */
	env = duk_require_hobject(ctx, -1);
	DUK_ASSERT(env != NULL);
	DUK_HOBJECT_SET_PROTOTYPE(thr, env, parent);  /* parent env is the prototype, updates refcounts */

	/* open scope information, for compiled functions only */

	if (DUK_HOBJECT_IS_COMPILEDFUNCTION(func)) {
		/* FIXME: duk_push_hthread etc -> macros at least */
		duk_push_hobject(ctx, (duk_hobject *) thr);
		duk_def_prop_stridx(ctx, -2, DUK_STRIDX_INT_THREAD, DUK_PROPDESC_FLAGS_WEC);
		duk_push_hobject(ctx, (duk_hobject *) func);
		duk_def_prop_stridx(ctx, -2, DUK_STRIDX_INT_CALLEE, DUK_PROPDESC_FLAGS_WEC);
		duk_push_int(ctx, idx_bottom);  /* FIXME: type */
		duk_def_prop_stridx(ctx, -2, DUK_STRIDX_INT_REGBASE, DUK_PROPDESC_FLAGS_WEC);
	}

	return env;
}

void duk_js_init_activation_environment_records_delayed(duk_hthread *thr,
                                                        duk_activation *act) {
	duk_context *ctx = (duk_context *) thr;
	duk_hobject *func;
	duk_hobject *env;

	func = act->func;
	DUK_ASSERT(func != NULL);
	DUK_ASSERT(!DUK_HOBJECT_HAS_BOUND(func));  /* bound functions are never in act->func */

	/*
	 *  Delayed initialization only occurs for 'NEWENV' functions.
	 */

	DUK_ASSERT(DUK_HOBJECT_HAS_NEWENV(func));
	DUK_ASSERT(act->lex_env == NULL);
	DUK_ASSERT(act->var_env == NULL);

	env = duk_create_activation_environment_record(thr, func, act->idx_bottom);
	DUK_ASSERT(env != NULL);

	DUK_DDDPRINT("created delayed fresh env: %!ipO", env);
#ifdef DUK_USE_DDDEBUG
	{
		duk_hobject *p = env;
		while (p) {
			DUK_DDDPRINT("  -> %!ipO", p);
			p = p->prototype;
		}
	}
#endif

	act->lex_env = env;
	act->var_env = env;
	DUK_HOBJECT_INCREF(thr, env);  /* FIXME: incref by count (here 2 times) */
	DUK_HOBJECT_INCREF(thr, env);

	duk_pop(ctx);
}

/*
 *  Closing environment records.
 *
 *  The environment record MUST be closed with the thread where its activation
 *  is.  In other words (if 'env' is open):
 *
 *    - 'thr' must match _env.thread
 *    - 'func' must match _env.callee
 *    - 'regbase' must match _env.regbase
 *
 *  These are not looked up from the env to minimize code size.
 *
 *  FIXME: should access the own properties directly instead of using the API
 */

void duk_js_close_environment_record(duk_hthread *thr, duk_hobject *env, duk_hobject *func, int regbase) {
	duk_context *ctx = (duk_context *) thr;
	int i;

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(env != NULL);
	DUK_ASSERT(func != NULL);

	if (!DUK_HOBJECT_IS_DECENV(env) || DUK_HOBJECT_HAS_ENVRECCLOSED(env)) {
		DUK_DDDPRINT("environment record not a declarative record, or already closed: %!iO", env);
		return;
	}

	DUK_DDDPRINT("closing environment record: %!iO, func: %!iO, regbase: %d", env, func, regbase);

	duk_push_hobject(ctx, env);

	/* assertions: env must be closed in the same thread as where it runs */
#ifdef DUK_USE_ASSERTIONS
	{
		/* [... env] */

		if (duk_get_prop_stridx(ctx, -1, DUK_STRIDX_INT_CALLEE)) {
			DUK_ASSERT(duk_is_object(ctx, -1));
			DUK_ASSERT(duk_get_hobject(ctx, -1) == (duk_hobject *) func);
		}
		duk_pop(ctx);

		if (duk_get_prop_stridx(ctx, -1, DUK_STRIDX_INT_THREAD)) {
			DUK_ASSERT(duk_is_object(ctx, -1));
			DUK_ASSERT(duk_get_hobject(ctx, -1) == (duk_hobject *) thr);
		}
		duk_pop(ctx);

		if (duk_get_prop_stridx(ctx, -1, DUK_STRIDX_INT_REGBASE)) {
			DUK_ASSERT(duk_is_number(ctx, -1));
			DUK_ASSERT(duk_get_number(ctx, -1) == (double) regbase);
		}
		duk_pop(ctx);

		/* [... env] */
	}
#endif

	if (DUK_HOBJECT_IS_COMPILEDFUNCTION(func)) {
		duk_hobject *varmap;
		duk_hstring *key;
		duk_tval *tv;
		int regnum;

		/* FIXME: additional conditions when to close variables? we don't want to do it
		 * unless the environment may have "escaped" (referenced in a function closure).
		 * With delayed environments, the existence is probably good enough of a check.
		 */

		/* FIXME: any way to detect faster whether something needs to be closed?
		 * We now look up _callee and then skip the rest.
		 */

		/* Note: we rely on the _varmap having a bunch of nice properties, like:
		 *  - being compacted and unmodified during this process
		 *  - not containing an array part
		 *  - having correct value types
		 */

		/* [... env] */

		if (!duk_get_prop_stridx(ctx, -1, DUK_STRIDX_INT_CALLEE)) {
			DUK_DDDPRINT("env has no callee property, nothing to close; re-delete the control properties just in case");
			duk_pop(ctx);
			goto skip_varmap;
		}

		/* [... env callee] */

		if (!duk_get_prop_stridx(ctx, -1, DUK_STRIDX_INT_VARMAP)) {
			DUK_DDDPRINT("callee has no varmap property, nothing to close; delete the control properties");
			duk_pop_2(ctx);
			goto skip_varmap;
		}
		varmap = duk_require_hobject(ctx, -1);
		DUK_ASSERT(varmap != NULL);

		DUK_DDDPRINT("varmap: %!O", varmap);

		/* [... env callee varmap] */

		DUK_DDDPRINT("copying bound register values, %d bound regs", varmap->e_used);

		for (i = 0; i < varmap->e_used; i++) {
			key = DUK_HOBJECT_E_GET_KEY(varmap, i);
			DUK_ASSERT(key != NULL);   /* assume keys are compacted */

			DUK_ASSERT(!DUK_HOBJECT_E_SLOT_IS_ACCESSOR(varmap, i));  /* assume plain values */

			tv = DUK_HOBJECT_E_GET_VALUE_TVAL_PTR(varmap, i);
			DUK_ASSERT(DUK_TVAL_IS_NUMBER(tv));  /* assume value is a number */
			regnum = (int) DUK_TVAL_GET_NUMBER(tv);
			DUK_ASSERT(regnum >= 0 && regnum < ((duk_hcompiledfunction *) func)->nregs);  /* regnum is sane */
			DUK_ASSERT(thr->valstack + regbase + regnum >= thr->valstack);
			DUK_ASSERT(thr->valstack + regbase + regnum < thr->valstack_top);

			/* XXX: slightly awkward */
			duk_push_hstring(ctx, key);
			duk_push_tval(ctx, thr->valstack + regbase + regnum);
			DUK_DDDPRINT("closing identifier '%s' -> reg %d, value %!T",
			             duk_get_string(ctx, -2), regnum, duk_get_tval(ctx, -1));

			/* [... env callee varmap key val] */

			/* if property already exists, overwrites silently */
			duk_def_prop(ctx, -5, DUK_PROPDESC_FLAGS_WE);  /* writable but not deletable */
		}

		duk_pop_2(ctx);

		/* [... env] */
	}

 skip_varmap:

	/* [... env] */

	duk_del_prop_stridx(ctx, -1, DUK_STRIDX_INT_CALLEE);
	duk_del_prop_stridx(ctx, -1, DUK_STRIDX_INT_THREAD);
	duk_del_prop_stridx(ctx, -1, DUK_STRIDX_INT_REGBASE);

	duk_pop(ctx);

	DUK_HOBJECT_SET_ENVRECCLOSED(env);

	DUK_DDDPRINT("environment record after being closed: %!O", env);
}

/*
 *  GETIDREF: a GetIdentifierReference-like helper.
 *
 *  Provides a parent traversing lookup and a single level lookup
 *  (for HasBinding).
 *
 *  Instead of returning the value, returns a bunch of values allowing
 *  the caller to read, write, or delete the binding.  Value pointers
 *  are duk_tval pointers which can be mutated directly as long as
 *  refcounts are properly updated.  Note that any operation which may
 *  reallocate valstacks or compact objects may invalidate the returned
 *  duk_tval (but not object) pointers, so caller must be very careful.
 *
 *  If starting environment record 'env' is given, 'act' is ignored.
 *  However, if 'env' is NULL, the caller may identify, in 'act', an
 *  activation which hasn't had its declarative environment initialized
 *  yet.  The activation registers are then looked up, and its parent
 *  traversed normally.
 *
 *  The 'out' structure values are only valid if the function returns
 *  success (non-zero).
 */

/* lookup name from an open declarative record's registers */
static int get_identifier_open_decl_env_regs(duk_hthread *thr,
                                             duk_hstring *name,
                                             duk_hobject *env,
                                             duk_id_lookup_result *out) {
	duk_hthread *env_thr;
	duk_hobject *env_func;
	int env_regbase;
	duk_hobject *varmap;
	duk_tval *tv;
	int reg_rel;
	int idx;

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(name != NULL);
	DUK_ASSERT(env != NULL);
	DUK_ASSERT(out != NULL);

	DUK_ASSERT(DUK_HOBJECT_IS_DECENV(env));

	tv = duk_hobject_find_existing_entry_tval_ptr(env, DUK_HEAP_STRING_INT_CALLEE(thr));
	if (!tv) {
		/* env is closed, should be missing _callee, _thread, _regbase */
		DUK_ASSERT(duk_hobject_find_existing_entry_tval_ptr(env, DUK_HEAP_STRING_INT_CALLEE(thr)) == NULL);
		DUK_ASSERT(duk_hobject_find_existing_entry_tval_ptr(env, DUK_HEAP_STRING_INT_THREAD(thr)) == NULL);
		DUK_ASSERT(duk_hobject_find_existing_entry_tval_ptr(env, DUK_HEAP_STRING_INT_REGBASE(thr)) == NULL);
		return 0;
	}

	DUK_ASSERT(DUK_TVAL_IS_OBJECT(tv));
	DUK_ASSERT(DUK_TVAL_GET_OBJECT(tv) != NULL);
	DUK_ASSERT(DUK_HOBJECT_IS_COMPILEDFUNCTION(DUK_TVAL_GET_OBJECT(tv)));
	env_func = DUK_TVAL_GET_OBJECT(tv);
	DUK_ASSERT(env_func != NULL);

	tv = duk_hobject_find_existing_entry_tval_ptr(env_func, DUK_HTHREAD_STRING_INT_VARMAP(thr));
	if (!tv) {
		return 0;
	}
	DUK_ASSERT(DUK_TVAL_IS_OBJECT(tv));
	varmap = DUK_TVAL_GET_OBJECT(tv);
	DUK_ASSERT(varmap != NULL);

	tv = duk_hobject_find_existing_entry_tval_ptr(varmap, name);
	if (!tv) {
		return 0;
	}
	DUK_ASSERT(DUK_TVAL_IS_NUMBER(tv));
	reg_rel = DUK_TVAL_GET_NUMBER(tv);
	DUK_ASSERT(reg_rel >= 0 && reg_rel < ((duk_hcompiledfunction *) env_func)->nregs);

	tv = duk_hobject_find_existing_entry_tval_ptr(env, DUK_HEAP_STRING_INT_THREAD(thr));
	DUK_ASSERT(tv != NULL);
	DUK_ASSERT(DUK_TVAL_IS_OBJECT(tv));
	DUK_ASSERT(DUK_TVAL_GET_OBJECT(tv) != NULL);
	DUK_ASSERT(DUK_HOBJECT_IS_THREAD(DUK_TVAL_GET_OBJECT(tv)));
	env_thr = (duk_hthread *) DUK_TVAL_GET_OBJECT(tv);
	DUK_ASSERT(env_thr != NULL);

	/* Note: env_thr != thr is quite possible and normal, so careful
	 * with what thread is used for valstack lookup.
	 */

	tv = duk_hobject_find_existing_entry_tval_ptr(env, DUK_HEAP_STRING_INT_REGBASE(thr));
	DUK_ASSERT(tv != NULL);
	DUK_ASSERT(DUK_TVAL_IS_NUMBER(tv));
	env_regbase = DUK_TVAL_GET_NUMBER(tv);

	idx = env_regbase + reg_rel;
	tv = &env_thr->valstack[idx];
	DUK_ASSERT(tv >= env_thr->valstack && tv < env_thr->valstack_end);  /* FIXME: more accurate? */

	out->value = tv;
	out->this_binding = NULL;  /* implicit this value always undefined for
	                            * declarative environment records.
	                            */
	out->env = env;
	out->holder = NULL;

	return 1;
}

/* lookup name from current activation record's functions' registers */
static int get_identifier_activation_regs(duk_hthread *thr,
                                          duk_hstring *name,
                                          duk_activation *act,
                                          duk_id_lookup_result *out) {
	duk_tval *tv;
	duk_hobject *func;
	duk_hobject *varmap;
	int reg_rel;
	int idx;

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(name != NULL);
	DUK_ASSERT(act != NULL);
	DUK_ASSERT(out != NULL);

	func = act->func;
	DUK_ASSERT(func != NULL);
	DUK_ASSERT(DUK_HOBJECT_HAS_NEWENV(func));

	if (!DUK_HOBJECT_IS_COMPILEDFUNCTION(func)) {
		return 0;
	}

	tv = duk_hobject_find_existing_entry_tval_ptr(func, DUK_HTHREAD_STRING_INT_VARMAP(thr));
	if (!tv) {
		return 0;
	}
	DUK_ASSERT(DUK_TVAL_IS_OBJECT(tv));
	varmap = DUK_TVAL_GET_OBJECT(tv);
	DUK_ASSERT(varmap != NULL);

	tv = duk_hobject_find_existing_entry_tval_ptr(varmap, name);
	if (!tv) {
		return 0;
	}
	DUK_ASSERT(DUK_TVAL_IS_NUMBER(tv));
	reg_rel = DUK_TVAL_GET_NUMBER(tv);
	DUK_ASSERT(reg_rel >= 0 && reg_rel < ((duk_hcompiledfunction *) func)->nregs);

	idx = act->idx_bottom + reg_rel;
	DUK_ASSERT(idx >= act->idx_bottom);
	tv = &thr->valstack[idx];

	out->value = tv;
	out->this_binding = NULL;  /* implicit this value always undefined for
	                            * declarative environment records.
	                            */
	out->env = NULL;
	out->holder = NULL;

	return 1;
}

static int get_identifier_reference(duk_hthread *thr,
                                    duk_hobject *env,
                                    duk_hstring *name,
                                    duk_activation *act,
                                    int parents,
                                    duk_id_lookup_result *out) {
	duk_tval *tv;
	duk_u32 sanity;

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(env != NULL || act != NULL);
	DUK_ASSERT(name != NULL);
	DUK_ASSERT(out != NULL);

	DUK_ASSERT(!env || DUK_HOBJECT_IS_ENV(env));
	DUK_ASSERT(!env || !DUK_HOBJECT_HAS_ARRAY_PART(env));

	/*
	 *  Conceptually, we look for the identifier binding by starting from
	 *  'env' and following to chain of environment records (represented
	 *  by the prototype chain).
	 *
	 *  If 'env' is NULL, the current activation does not yet have an
	 *  allocated declarative environment record; this should be treated
	 *  exactly as if the environment record existed but had no bindings
	 *  other than register bindings.
	 *
	 *  Note: we assume that with the DUK_HOBJECT_FLAG_NEWENV cleared
	 *  the environment will always be initialized immediately; hence
	 *  a NULL 'env' should only happen with the flag set.  This is the
	 *  case for: (1) function calls, and (2) strict, direct eval calls.
	 */

	if (env == NULL && act != NULL) {
		duk_hobject *func;

		DUK_DDDPRINT("get_identifier_reference: env is NULL, activation is non-NULL -> "
		             "delayed env case, look up activation regs first");

		/*
		 *  Try registers
		 */

		if (get_identifier_activation_regs(thr, name, act, out)) {
			DUK_DDDPRINT("get_identifier_reference successful: "
			             "name=%!O -> value=%!T, this=%!T, env=%!O, holder=%!O "
			             "(found from register bindings when env=NULL)",
			             (duk_heaphdr *) name, out->value, out->this_binding,
			             out->env, out->holder);
			return 1;
		}

		DUK_DDDPRINT("not found in current activation regs");

		/*
		 *  Not found in registers, proceed to the parent record.
		 *  Here we need to determine what the parent would be,
		 *  if 'env' was not NULL (i.e. same logic as when initializing
		 *  the record).
		 *
		 *  Note that environment initialization is only deferred when
		 *  DUK_HOBJECT_HAS_NEWENV is set, and this only happens for:
		 *    - Function code
		 *    - Strict eval code
		 *
		 *  We only need to check _lexenv here; _varenv exists only if it
		 *  differs from _lexenv (and thus _lexenv will also be present).
		 */

		if (!parents) {
			DUK_DDDPRINT("get_identifier_reference failed, no parent traversal "
			             "(not found from register bindings when env=NULL)");
			goto fail_not_found;
		}

		func = act->func;
		DUK_ASSERT(func != NULL);
		DUK_ASSERT(DUK_HOBJECT_HAS_NEWENV(func));

		tv = duk_hobject_find_existing_entry_tval_ptr(func, DUK_HTHREAD_STRING_INT_LEXENV(thr));
		if (tv) {
			DUK_ASSERT(DUK_TVAL_IS_OBJECT(tv));
			env = DUK_TVAL_GET_OBJECT(tv);
		} else {
			DUK_ASSERT(duk_hobject_find_existing_entry_tval_ptr(func, DUK_HTHREAD_STRING_INT_VARENV(thr)) == NULL);
			env = thr->builtins[DUK_BIDX_GLOBAL_ENV];
		}

		DUK_DDDPRINT("continue lookup from env: %!iO", env);
	}

	/*
	 *  Prototype walking starting from 'env'.
	 *
	 *  ('act' is not needed anywhere here.)
	 */

	sanity = DUK_HOBJECT_PROTOTYPE_CHAIN_SANITY;
	while (env != NULL) {
		duk_tval *tv;
		int cl;

		DUK_DDDPRINT("get_identifier_reference, name=%!O, considering env=%p -> %!iO",
		             (duk_heaphdr *) name,
		             (void *) env,
		             (duk_heaphdr *) env);

		DUK_ASSERT(env != NULL);
		DUK_ASSERT(DUK_HOBJECT_IS_ENV(env));
		DUK_ASSERT(!DUK_HOBJECT_HAS_ARRAY_PART(env));

		cl = DUK_HOBJECT_GET_CLASS_NUMBER(env);
		DUK_ASSERT(cl == DUK_HOBJECT_CLASS_OBJENV || cl == DUK_HOBJECT_CLASS_DECENV);
		if (cl == DUK_HOBJECT_CLASS_DECENV) {
			/*
			 *  Declarative environment record.
			 *
			 *  Identifiers can never be stored in ancestors and are
			 *  always plain values, so we can use an internal helper
			 *  and access the value directly with an duk_tval ptr.
			 *
			 *  A closed environment is only indicated by it missing
			 *  the "book-keeping" properties required for accessing
			 *  register-bound variables.
			 */

			if (DUK_HOBJECT_HAS_ENVRECCLOSED(env)) {
				/* already closed */
				goto skip_regs;
			}

			if (get_identifier_open_decl_env_regs(thr, name, env, out)) {
				DUK_DDDPRINT("get_identifier_reference successful: "
				             "name=%!O -> value=%!T, this=%!T, env=%!O, holder=%!O "
				             "(declarative environment record, scope open, found in regs)",
				             (duk_heaphdr *) name, out->value, out->this_binding,
				             out->env, out->holder);
				return 1;
			}
		 skip_regs:

			tv = duk_hobject_find_existing_entry_tval_ptr(env, name);
			if (tv) {
				out->value = tv;
				out->this_binding = NULL;  /* implicit this value always undefined for
				                            * declarative environment records.
				                            */
				out->env = env;
				out->holder = env;

				DUK_DDDPRINT("get_identifier_reference successful: "
				             "name=%!O -> value=%!T, this=%!T, env=%!O, holder=%!O "
				             "(declarative environment record, found in properties)",
				             (duk_heaphdr *) name, out->value, out->this_binding,
				             out->env, out->holder);
				return 1;
			}
		} else {
			/*
			 *  Object environment record.
			 *
			 *  Binding (target) object is an external, uncontrolled object.
			 *  Identifier may be bound in an ancestor property, and may be
			 *  an accessor.
			 */

			/* FIXME: we could save space by using _target OR _this.  If _target, assume
			 * this binding is undefined.  If _this, assumes this binding is _this, and
			 * target is also _this.  One property would then be enough.
			 */

			duk_hobject *target;

			DUK_ASSERT(cl == DUK_HOBJECT_CLASS_OBJENV);

			tv = duk_hobject_find_existing_entry_tval_ptr(env, DUK_HEAP_STRING_INT_TARGET(thr));
			DUK_ASSERT(tv != NULL);
			DUK_ASSERT(DUK_TVAL_IS_OBJECT(tv));
			target = DUK_TVAL_GET_OBJECT(tv);
			DUK_ASSERT(target != NULL);

			/* Note: we must traverse the prototype chain, so use an actual
			 * hasprop call here.  The property may also be an accessor, so
			 * we can't get an duk_tval pointer here.
			 *
			 * out->holder is NOT set to the actual duk_hobject where the
			 * property is found, but rather the target object.
			 */

			if (duk_hobject_hasprop_raw(thr, target, name)) {
				out->value = NULL;  /* can't get value, may be accessor */

				tv = duk_hobject_find_existing_entry_tval_ptr(env, DUK_HEAP_STRING_INT_THIS(thr));
				out->this_binding = tv;  /* may be NULL */

				out->env = env;
				out->holder = target;

				DUK_DDDPRINT("get_identifier_reference successful: "
				             "name=%!O -> value=%!T, this=%!T, env=%!O, holder=%!O "
				             "(object environment record)",
				             (duk_heaphdr *) name, out->value, out->this_binding,
				             out->env, out->holder);
				return 1;
			}
		}

		if (!parents) {
			DUK_DDDPRINT("get_identifier_reference failed, no parent traversal "
			             "(not found from first traversed env)");
			goto fail_not_found;
		}

                if (sanity-- == 0) {
                        DUK_ERROR(thr, DUK_ERR_INTERNAL_ERROR, "prototype chain max depth reached (loop?)");
                }
		env = env->prototype;
	};

	/*
	 *  Not found (even in global object)
	 */

 fail_not_found:
	return 0;
}

/*
 *  HASVAR: check identifier binding from a given environment record
 *  without traversing its parents.
 *
 *  This primitive is not exposed to user code as such, but is used
 *  internally for e.g. declaration binding instantiation.
 *
 *  See E5 Sections:
 *    10.2.1.1.1 HasBinding(N)
 *    10.2.1.2.1 HasBinding(N)
 *
 *  Note: strictness has no bearing on this check.  Hence we don't take
 *  a 'strict' parameter.
 */

int duk_js_hasvar_envrec(duk_hthread *thr,
                         duk_hobject *env,
                         duk_hstring *name) {
	duk_id_lookup_result ref;
	int parents;

	DUK_DDDPRINT("hasvar: thr=%p, env=%p, name=%!O "
	             "(env -> %!dO)",
	             (void *) thr, (void *) env, (duk_heaphdr *) name,
	             (duk_heaphdr *) env);

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(env != NULL);
	DUK_ASSERT(name != NULL);

        DUK_ASSERT_REFCOUNT_NONZERO_HEAPHDR(env);
        DUK_ASSERT_REFCOUNT_NONZERO_HEAPHDR(name);

	DUK_ASSERT(DUK_HOBJECT_IS_ENV(env));
	DUK_ASSERT(!DUK_HOBJECT_HAS_ARRAY_PART(env));

	/* lookup results is ignored */
	parents = 0;
	return get_identifier_reference(thr, env, name, NULL, parents, &ref);
}

/*
 *  GETVAR
 *
 *  See E5 Sections:
 *    11.1.2 Identifier Reference
 *    10.3.1 Identifier Resolution
 *    11.13.1 Simple Assignment  [example of where the Reference is GetValue'd]
 *    8.7.1 GetValue (V)
 *    8.12.1 [[GetOwnProperty]] (P)
 *    8.12.2 [[GetProperty]] (P)
 *    8.12.3 [[Get]] (P)
 *
 *  If 'throw' is true, always leaves two values on top of stack: [val this].
 *
 *  If 'throw' is false, returns 0 if identifier cannot be resolved, and the
 *  stack will be unaffected in this case.  If identifier is resolved, returns
 *  1 and leaves [val this] on top of stack.
 *
 *  Note: the 'strict' flag of a reference returned by GetIdentifierReference
 *  is ignored by GetValue.  Hence we don't take a 'strict' parameter.
 *
 *  The 'throw' flag is needed for implementing 'typeof' for an unreferenced
 *  identifier.  An unreference identifier in other contexts generates a
 *  ReferenceError.
 */

static int getvar_helper(duk_hthread *thr,
                         duk_hobject *env,
                         duk_activation *act,
                         duk_hstring *name,
                         int throw) {
	duk_context *ctx = (duk_context *) thr;
	duk_id_lookup_result ref;
	duk_tval tv_tmp_obj;
	duk_tval tv_tmp_key;
	int parents;

	DUK_DDDPRINT("getvar: thr=%p, env=%p, act=%p, name=%!O "
	             "(env -> %!dO)",
	             (void *) thr, (void *) env, (void *) act,
	             (duk_heaphdr *) name, (duk_heaphdr *) env);

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(name != NULL);
	/* env and act may be NULL */

        DUK_ASSERT_REFCOUNT_NONZERO_HEAPHDR(env);
        DUK_ASSERT_REFCOUNT_NONZERO_HEAPHDR(name);

	parents = 1;     /* follow parent chain */
	if (get_identifier_reference(thr, env, name, act, parents, &ref)) {
		if (ref.value) {
			DUK_ASSERT(ref.this_binding == NULL);  /* always for register bindings */
			duk_push_tval(ctx, ref.value);
			duk_push_undefined(ctx);
		} else {
			DUK_ASSERT(ref.holder != NULL);

			/* Note: getprop may invoke any getter and invalidate any
			 * duk_tval pointers, so this must be done first.
			 */

			if (ref.this_binding) {
				duk_push_tval(ctx, ref.this_binding);
			} else {
				duk_push_undefined(ctx);
			}

			DUK_TVAL_SET_OBJECT(&tv_tmp_obj, ref.holder);
			DUK_TVAL_SET_STRING(&tv_tmp_key, name);
			(void) duk_hobject_getprop(thr, &tv_tmp_obj, &tv_tmp_key);  /* [this value] */

			/* ref.value, ref.this.binding invalidated here by getprop call */

			duk_insert(ctx, -2);  /* [this value] -> [value this] */
		}

		return 1;
	} else {
		if (throw) {
			DUK_ERROR(thr, DUK_ERR_REFERENCE_ERROR,
			          "identifier '%s' undefined",
			          (char *) DUK_HSTRING_GET_DATA(name));
		}

		return 0;
	}
}

int duk_js_getvar_envrec(duk_hthread *thr,
                         duk_hobject *env,
                         duk_hstring *name,
                         int throw) {
	return getvar_helper(thr, env, NULL, name, throw);
}

int duk_js_getvar_activation(duk_hthread *thr,
                             duk_activation *act,
                             duk_hstring *name,
                             int throw) {
	DUK_ASSERT(act != NULL);
	return getvar_helper(thr, act->lex_env, act, name, throw);
}

/*
 *  PUTVAR
 *
 *  See E5 Sections:
 *    11.1.2 Identifier Reference
 *    10.3.1 Identifier Resolution
 *    11.13.1 Simple Assignment  [example of where the Reference is PutValue'd]
 *    8.7.2 PutValue (V,W)  [see especially step 3.b, undefined -> automatic global in non-strict mode]
 *    8.12.4 [[CanPut]] (P)
 *    8.12.5 [[Put]] (P)
 *
 *  Note: may invalidate any valstack (or object) duk_tval pointers because
 *  putting a value may reallocate any object or any valstack.  Caller beware.
 */

static void putvar_helper(duk_hthread *thr,
                          duk_hobject *env,
                          duk_activation *act,
                          duk_hstring *name,
                          duk_tval *val,
                          int strict) {
	duk_id_lookup_result ref;
	duk_tval tv_tmp_obj;
	duk_tval tv_tmp_key;
	int parents;

	DUK_DDDPRINT("putvar: thr=%p, env=%p, act=%p, name=%!O, val=%p, strict=%d "
	             "(env -> %!dO, val -> %!T)",
	             (void *) thr, (void *) env, (void *) act,
	             (duk_heaphdr *) name, (void *) val, strict,
	             (duk_heaphdr *) env, val);

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(name != NULL);
	DUK_ASSERT(val != NULL);
	/* env and act may be NULL */

        DUK_ASSERT_REFCOUNT_NONZERO_HEAPHDR(env);
        DUK_ASSERT_REFCOUNT_NONZERO_HEAPHDR(name);
	DUK_ASSERT_REFCOUNT_NONZERO_TVAL(val);

	/*
	 *  In strict mode E5 protects 'eval' and 'arguments' from being
	 *  assigned to (or even declared anywhere).  Attempt to do so
	 *  should result in a compile time SyntaxError.  See the internal
	 *  design documentation for details.
	 *
	 *  Thus, we should never come here, run-time, for strict code,
	 *  and name 'eval' or 'arguments'.
	 */

	DUK_ASSERT(!strict ||
	           (name != DUK_HTHREAD_STRING_EVAL(thr) &&
	            name != DUK_HTHREAD_STRING_LC_ARGUMENTS(thr)));

	/*
	 *  Lookup variable and update in-place if found.
	 */

	parents = 1;     /* follow parent chain */

	if (get_identifier_reference(thr, env, name, act, parents, &ref)) {
		if (ref.value) {
			duk_tval tv_tmp;
			duk_tval *tv_val;

			DUK_ASSERT(ref.this_binding == NULL);  /* always for register bindings */

 			tv_val = ref.value;
			DUK_ASSERT(tv_val != NULL);
			DUK_TVAL_SET_TVAL(&tv_tmp, tv_val);
			DUK_TVAL_SET_TVAL(tv_val, val);
			DUK_TVAL_INCREF(thr, val);
			DUK_TVAL_DECREF(thr, &tv_tmp);  /* must be last */

			/* ref.value and ref.this_binding invalidated here */
		} else {
			DUK_ASSERT(ref.holder != NULL);

			DUK_TVAL_SET_OBJECT(&tv_tmp_obj, ref.holder);
			DUK_TVAL_SET_STRING(&tv_tmp_key, name);
			(void) duk_hobject_putprop(thr, &tv_tmp_obj, &tv_tmp_key, val, strict);

			/* ref.value and ref.this_binding invalidated here */
		}

		return;
	}
	
	/*
	 *  Not found: write to global object (non-strict) or ReferenceError
	 *  (strict); see E5 Section 8.7.2, step 3.
	 */

	if (strict) {
		DUK_DDDPRINT("identifier binding not found, strict => reference error");
		DUK_ERROR(thr, DUK_ERR_REFERENCE_ERROR, "identifier not defined");
	}

	DUK_DDDPRINT("identifier binding not found, not strict => set to global");

	DUK_TVAL_SET_OBJECT(&tv_tmp_obj, thr->builtins[DUK_BIDX_GLOBAL]);
	DUK_TVAL_SET_STRING(&tv_tmp_key, name);
	(void) duk_hobject_putprop(thr, &tv_tmp_obj, &tv_tmp_key, val, 0);  /* 0 = no throw */

	/* NB: 'val' may be invalidated here because put_value may realloc valstack,
	 * caller beware.
	 */
}

void duk_js_putvar_envrec(duk_hthread *thr,
                          duk_hobject *env,
                          duk_hstring *name,
                          duk_tval *val,
                          int strict) {
	putvar_helper(thr, env, NULL, name, val, strict);
}

void duk_js_putvar_activation(duk_hthread *thr,
                              duk_activation *act,
                              duk_hstring *name,
                              duk_tval *val,
                              int strict) {
	DUK_ASSERT(act != NULL);
	putvar_helper(thr, act->lex_env, act, name, val, strict);
}

/*
 *  DELVAR
 *
 *  See E5 Sections:
 *    11.4.1 The delete operator
 *    10.2.1.1.5 DeleteBinding (N)  [declarative environment record]
 *    10.2.1.2.5 DeleteBinding (N)  [object environment record]
 *
 *  Variable bindings established inside eval() are deletable (configurable),
 *  other bindings are not, including variables declared in global level.
 *  Registers are always non-deletable, and the deletion of other bindings
 *  is controlled by the configurable flag.
 *
 *  For strict mode code, the 'delete' operator should fail with a compile
 *  time SyntaxError if applied to identifiers.  Hence, no strict mode
 *  run-time deletion of identifiers should ever happen.  This function
 *  should never be called from strict mode code!
 */

static int delvar_helper(duk_hthread *thr,
                         duk_hobject *env,
                         duk_activation *act,
                         duk_hstring *name) {
	duk_id_lookup_result ref;
	int parents;

	DUK_DDDPRINT("delvar: thr=%p, env=%p, act=%p, name=%!O "
	             "(env -> %!dO)",
	             (void *) thr, (void *) env, (void *) act,
	             (duk_heaphdr *) name, (duk_heaphdr *) env);

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(name != NULL);
	/* env and act may be NULL */

        DUK_ASSERT_REFCOUNT_NONZERO_HEAPHDR(name);

	parents = 1;     /* follow parent chain */

	if (get_identifier_reference(thr, env, name, act, parents, &ref)) {
		if (ref.value) {
			/* value found, but in regs (not deletable) */
			return 0;
		}
		DUK_ASSERT(ref.holder != NULL);

		return duk_hobject_delprop_raw(thr, ref.holder, name, 0);
	}

	/*
	 *  Not found (even in global object).
	 *
	 *  In non-strict mode this is a silent SUCCESS (!), see E5 Section 11.4.1,
	 *  step 3.b.  In strict mode this case is a compile time SyntaxError so
	 *  we should not come here.
	 */

	DUK_DDDPRINT("identifier to be deleted not found: name=%!O "
	             "(treated as silent success)",
	             (duk_heaphdr *) name);
	return 1;
}

int duk_js_delvar_envrec(duk_hthread *thr,
                         duk_hobject *env,
                         duk_hstring *name) {
	return delvar_helper(thr, env, NULL, name);
}
	
int duk_js_delvar_activation(duk_hthread *thr,
                             duk_activation *act,
                             duk_hstring *name) {
	DUK_ASSERT(act != NULL);
	return delvar_helper(thr, act->lex_env, act, name);
}

/*
 *  DECLVAR
 *
 *  See E5 Sections:
 *    10.4.3 Entering Function Code
 *    10.5 Declaration Binding Instantion
 *    12.2 Variable Statement
 *    11.1.2 Identifier Reference
 *    10.3.1 Identifier Resolution
 *
 *  Variable declaration behavior is mainly discussed in Section 10.5,
 *  and is not discussed in the execution semantics (Sections 11-13).
 *
 *  Conceptually declarations happen when code (global, eval, function)
 *  is entered, before any user code is executed.  In practice, register-
 *  bound identifiers are 'declared' automatically (by virtue of being
 *  allocated to registers with the initial value 'undefined').  Other
 *  identifiers are declared in the function prologue with this primitive.
 *
 *  Since non-register bindings eventually back to an internal object's
 *  properties, the 'prop_flags' argument is used to specify binding
 *  type:
 *
 *    - Immutable binding: set DUK_PROPDESC_FLAG_WRITABLE to false
 *    - Non-deletable binding: set DUK_PROPDESC_FLAG_CONFIGURABLE to false
 *    - The flag DUK_PROPDESC_FLAG_ENUMERABLE should be set, although it
 *      doesn't really matter for internal objects
 *
 *  All bindings are non-deletable mutable bindings except:
 *
 *    - Declarations in eval code (mutable, deletable)
 *    - 'arguments' binding in strict function code (immutable)
 *    - Function name binding of a function expression (immutable)
 *
 *  Declarations may go to declarative environment records (always
 *  so for functions), but may also go to object environment records
 *  (e.g. global code).  The global object environment has special
 *  behavior when re-declaring a function (but not a variable); see
 *  E5.1 specification, Section 10.5, step 5.e.
 *
 *  Declarations always go to the 'top-most' environment record, i.e.
 *  we never check the record chain.  It's not an error even if a
 *  property (even an immutable or non-deletable one) of the same name
 *  already exists.
 *
 *  If a declared variable already exists, its value needs to be updated
 *  (if possible).  Returns 1 if a PUTVAR needs to be done by the caller;
 *  otherwise returns 0.
 */

static int declvar_helper(duk_hthread *thr,
                          duk_hobject *env,
                          duk_hstring *name,
                          duk_tval *tv_val,
                          int prop_flags,
                          int is_func_decl) {
	duk_context *ctx = (duk_context *) thr;
	duk_hobject *holder;
	int parents;
	duk_id_lookup_result ref;
	duk_tval *tv;

	DUK_DDDPRINT("declvar: thr=%p, env=%p, name=%!O, val=%!T, prop_flags=0x%08x, is_func_decl=%d "
	             "(env -> %!iO)",
	             (void *) thr, (void *) env, (duk_heaphdr *) name,
	             tv_val, prop_flags, is_func_decl, (duk_heaphdr *) env);

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(env != NULL);
	DUK_ASSERT(name != NULL);
	DUK_ASSERT(tv_val != NULL);

	/* Note: in strict mode the compiler should reject explicit
	 * declaration of 'eval' or 'arguments'.  However, internal
	 * bytecode may declare 'arguments' in the function prologue.
	 * We don't bother checking (or asserting) for these now.
	 */

	/* Note: tv_val is a stable tv_val pointer.  The caller makes
	 * a value copy into its stack frame, so 'tv_val' is not subject
	 * to side effects here.
	 */

	/*
	 *  Check whether already declared.
	 *
	 *  We need to check whether the binding exists in the environment
	 *  without walking its parents.  However, we still need to check
	 *  register-bound identifiers and the prototype chain of an object
	 *  environment target object.
	 */

	parents = 0;  /* just check 'env' */
	if (get_identifier_reference(thr, env, name, NULL, parents, &ref)) {
		int e_idx;
		int h_idx;
		int flags;

		/*
		 *  Variable already declared, ignore re-declaration.
		 *  The only exception is the updated behavior of E5.1 for
		 *  global function declarations, E5.1 Section 10.5, step 5.e.
		 *  This behavior does not apply to global variable declarations.
		 */

		if (!(is_func_decl && env == thr->builtins[DUK_BIDX_GLOBAL_ENV])) {
			DUK_DDDPRINT("re-declare a binding, ignoring");
			return 1;  /* 1 -> needs a PUTVAR */
		}

		/*
		 *  Special behavior in E5.1.
		 *
		 *  Note that even though parents == 0, the conflicting property
		 *  may be an inherited property (currently our global object's
		 *  prototype is Object.prototype).  Step 5.e first operates on
		 *  the existing property (which is potentially in an ancestor)
		 *  and then defines a new property in the global object (and
		 *  never modifies the ancestor).
		 *
		 *  Also note that this logic would become even more complicated
		 *  if the conflicting property might be a virtual one.  Object
		 *  prototype has no virtual properties, though.
		 *
		 *  FIXME: this is now very awkward, rework.
		 */

		DUK_DDDPRINT("re-declare a function binding in global object, "
		             "updated E5.1 processing");

		DUK_ASSERT(ref.holder != NULL);
		holder = ref.holder;

		/* holder will be set to the target object, not the actual object
		 * where the property was found (see get_identifier_reference()).
		 */
		DUK_ASSERT(DUK_HOBJECT_GET_CLASS_NUMBER(holder) == DUK_HOBJECT_CLASS_GLOBAL);
		DUK_ASSERT(!DUK_HOBJECT_HAS_SPECIAL_ARRAY(holder));  /* global object doesn't have array part */

		/* FIXME: use a helper for prototype traversal; no loop check here */
		/* must be found: was found earlier, and cannot be inherited */
		for (;;) {
			DUK_ASSERT(holder != NULL);
			duk_hobject_find_existing_entry(holder, name, &e_idx, &h_idx);
			if (e_idx >= 0) {
				break;
			}
			holder = holder->prototype;
		}
		DUK_ASSERT(holder != NULL);
		DUK_ASSERT(e_idx >= 0);

		/* ref.holder is global object, holder is the object with the
		 * conflicting property.
		 */

		flags = DUK_HOBJECT_E_GET_FLAGS(holder, e_idx);
		if (!(flags & DUK_PROPDESC_FLAG_CONFIGURABLE)) {
			if (flags & DUK_PROPDESC_FLAG_ACCESSOR) {
				DUK_DDDPRINT("existing property is a non-configurable "
				             "accessor -> reject");
				goto fail_existing_attributes;
			}
			if (!((flags & DUK_PROPDESC_FLAG_WRITABLE) &&
			      (flags & DUK_PROPDESC_FLAG_ENUMERABLE))) {
				DUK_DDDPRINT("existing property is a non-configurable "
				             "plain property which is not writable and "
				             "enumerable -> reject");
				goto fail_existing_attributes;
			}

			DUK_DDDPRINT("existing property is not configurable but "
			             "is plain, enumerable, and writable -> "
			             "allow redeclaration");
		}

		if (holder == ref.holder) {
			/* FIXME: if duk_hobject_define_property_internal() was updated
			 * to handle a pre-existing accessor property, this would be
			 * a simple call (like for the ancestor case).
			 */
			DUK_DDDPRINT("redefine, offending property in global object itself");

			if (flags & DUK_PROPDESC_FLAG_ACCESSOR) {
				duk_hobject *tmp;

				tmp = DUK_HOBJECT_E_GET_VALUE_GETTER(holder, e_idx);
				DUK_HOBJECT_E_SET_VALUE_GETTER(holder, e_idx, NULL);
				DUK_HOBJECT_DECREF(thr, tmp);
				tmp = tmp;  /* suppress warning */
				tmp = DUK_HOBJECT_E_GET_VALUE_SETTER(holder, e_idx);
				DUK_HOBJECT_E_SET_VALUE_SETTER(holder, e_idx, NULL);
				DUK_HOBJECT_DECREF(thr, tmp);
				tmp = tmp;  /* suppress warning */
			} else {
				duk_tval tv_tmp;

				tv = DUK_HOBJECT_E_GET_VALUE_TVAL_PTR(holder, e_idx);
				DUK_TVAL_SET_TVAL(&tv_tmp, tv);
				DUK_TVAL_SET_UNDEFINED_UNUSED(tv);
				DUK_TVAL_DECREF(thr, &tv_tmp);
			}

			/* Here tv_val would be potentially invalid if we didn't make
			 * a value copy at the caller.
			 */

			tv = DUK_HOBJECT_E_GET_VALUE_TVAL_PTR(holder, e_idx);
			DUK_TVAL_SET_TVAL(tv, tv_val);
			DUK_TVAL_INCREF(thr, tv);
			DUK_HOBJECT_E_SET_FLAGS(holder, e_idx, prop_flags);

			DUK_DDDPRINT("updated global binding, final result: "
			             "value -> %!T, prop_flags=0x%08x",
			             DUK_HOBJECT_E_GET_VALUE_TVAL_PTR(holder, e_idx),
			             prop_flags);
		} else {
			DUK_DDDPRINT("redefine, offending property in ancestor");

			DUK_ASSERT(ref.holder == thr->builtins[DUK_BIDX_GLOBAL]);
			duk_push_tval(ctx, tv_val);
			duk_hobject_define_property_internal(thr, ref.holder, name, prop_flags);
		}

		return 0;
	}

	/*
	 *  Not found (in registers or record objects).  Declare
	 *  to current variable environment.
	 */

	/*
	 *  Get holder object
	 */

	if (DUK_HOBJECT_IS_DECENV(env)) {
		holder = env;
	} else {
		DUK_ASSERT(DUK_HOBJECT_IS_OBJENV(env));

		tv = duk_hobject_find_existing_entry_tval_ptr(env, DUK_HEAP_STRING_INT_TARGET(thr));
		DUK_ASSERT(tv != NULL);
		DUK_ASSERT(DUK_TVAL_IS_OBJECT(tv));
		holder = DUK_TVAL_GET_OBJECT(tv);
		DUK_ASSERT(holder != NULL);
	}

	/*
	 *  Define new property
	 *
	 *  Note: this may fail if the holder is not extensible.
	 */

	/* FIXME: this is awkward as we use an internal method which doesn't handle
	 * extensibility etc correctly.  Basically we'd want to do a [[DefineOwnProperty]]
	 * or Object.defineProperty() here.
	 */

	if (!DUK_HOBJECT_HAS_EXTENSIBLE(holder)) {
		goto fail_not_extensible;
	}

	duk_push_hobject(ctx, holder);
	duk_push_hstring(ctx, name);
	duk_push_tval(ctx, tv_val);
	duk_def_prop(ctx, -3, prop_flags);  /* [holder name val] -> [holder] */
	duk_pop(ctx);

	return 0;

 fail_existing_attributes:
 fail_not_extensible:
	DUK_ERROR(thr, DUK_ERR_TYPE_ERROR, "declaration failed");
	return 0;
}

int duk_js_declvar_activation(duk_hthread *thr,
                              duk_activation *act,
                              duk_hstring *name,
                              duk_tval *tv_val,
                              int prop_flags,
                              int is_func_decl) {
	duk_hobject *env;
	duk_tval tv_val_copy;

	/*
	 *  Make a value copy of the input tv_val.  This ensures that
	 *  side effects cannot invalidate the pointer.
	 */

	DUK_TVAL_SET_TVAL(&tv_val_copy, tv_val);
	tv_val = &tv_val_copy;

	/*
	 *  Delayed env creation check
	 */

	if (!act->var_env) {
		DUK_ASSERT(act->lex_env == NULL);
		duk_js_init_activation_environment_records_delayed(thr, act);
	}
	env = act->var_env;
	DUK_ASSERT(env != NULL);
	DUK_ASSERT(DUK_HOBJECT_IS_ENV(env));

	return declvar_helper(thr, env, name, tv_val, prop_flags, is_func_decl);
}

