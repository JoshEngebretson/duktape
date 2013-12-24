/*
 *  Debug dumping of duk_hobject.
 */

#include "duk_internal.h"

#ifdef DUK_USE_DEBUG

/* must match duk_hobject.h */
static const char *class_names[32] = {
	"unused",
	"Arguments",
	"Array",
	"Boolean",
	"Date",
	"Error",
	"Function",
	"JSON",
	"Math",
	"Number",
	"Object",
	"RegExp",
	"String",
	"global",
	"ObjEnv",
	"DecEnv",
	"Buffer",
	"Pointer",
	"unused",
	"unused",
	"unused",
	"unused",
	"unused",
	"unused",
	"unused",
	"unused",
	"unused",
	"unused",
	"unused",
	"unused",
	"unused",
	"unused",
};

/* for thread dumping */
static char get_activation_summary_char(duk_activation *act) {
	if (act->func) {
		if (DUK_HOBJECT_IS_COMPILEDFUNCTION(act->func)) {
			return 'c';
		} else if (DUK_HOBJECT_IS_NATIVEFUNCTION(act->func)) {
			return 'n';
		} else {
			/* should not happen */
			return '?';
		}
	} else {
		/* should not happen */
		return '?';
	}
}

/* for thread dumping */
static char get_tval_summary_char(duk_tval *tv) {
	switch (DUK_TVAL_GET_TAG(tv)) {
	case DUK_TAG_UNDEFINED:
		if (DUK_TVAL_IS_UNDEFINED_UNUSED(tv)) {
			return '.';
		}
		return 'u';
	case DUK_TAG_NULL:
		return 'n';
	case DUK_TAG_BOOLEAN:
		return 'b';
	case DUK_TAG_STRING:
		return 's';
	case DUK_TAG_OBJECT: {
		duk_hobject *h = DUK_TVAL_GET_OBJECT(tv);

		if (DUK_HOBJECT_IS_ARRAY(h)) {
			return 'A';
		} else if (DUK_HOBJECT_IS_COMPILEDFUNCTION(h)) {
			return 'C';
		} else if (DUK_HOBJECT_IS_NATIVEFUNCTION(h)) {
			return 'N';
		} else if (DUK_HOBJECT_IS_THREAD(h)) {
			return 'T';
		}
		return 'O';
	}
	case DUK_TAG_BUFFER: {
		return 'B';
	}
	case DUK_TAG_POINTER: {
		return 'P';
	}
	default:
		DUK_ASSERT(DUK_TVAL_IS_NUMBER(tv));
		return 'd';
	}
	return '?';
}

/* for thread dumping */
static char get_catcher_summary_char(duk_catcher *catcher) {
	switch (DUK_CAT_GET_TYPE(catcher)) {
	case DUK_CAT_TYPE_TCF:
		if (DUK_CAT_HAS_CATCH_ENABLED(catcher)) {
			if (DUK_CAT_HAS_FINALLY_ENABLED(catcher)) {
				return 'C';  /* catch and finally active */
			} else {
				return 'c';  /* only catch active */
			}
		} else {
			if (DUK_CAT_HAS_FINALLY_ENABLED(catcher)) {
				return 'f';  /* only finally active */
			} else {
				return 'w';  /* neither active (usually 'with') */
			}
		}
	case DUK_CAT_TYPE_LABEL:
		return 'l';
	case DUK_CAT_TYPE_UNKNOWN:
	default:
		return '?';
	}
	return '?';
}

void duk_debug_dump_hobject(duk_hobject *obj) {
	int i;
	const char *str_empty = "";
	const char *str_excl = "!";

	DUK_DPRINT("=== hobject %p ===", (void *) obj);
	if (!obj) {
		return;
	}

	DUK_DPRINT("  %sextensible", DUK_HOBJECT_HAS_EXTENSIBLE(obj) ? str_empty : str_excl);
	DUK_DPRINT("  %sconstructable", DUK_HOBJECT_HAS_CONSTRUCTABLE(obj) ? str_empty : str_excl);
	DUK_DPRINT("  %sbound", DUK_HOBJECT_HAS_BOUND(obj) ? str_empty : str_excl);
	DUK_DPRINT("  %scompiledfunction", DUK_HOBJECT_HAS_COMPILEDFUNCTION(obj) ? str_empty : str_excl);
	DUK_DPRINT("  %snativefunction", DUK_HOBJECT_HAS_NATIVEFUNCTION(obj) ? str_empty : str_excl);
	DUK_DPRINT("  %sthread", DUK_HOBJECT_HAS_THREAD(obj) ? str_empty : str_excl);
	DUK_DPRINT("  %sarray_part", DUK_HOBJECT_HAS_ARRAY_PART(obj) ? str_empty : str_excl);
	DUK_DPRINT("  %sstrict", DUK_HOBJECT_HAS_STRICT(obj) ? str_empty : str_excl);
	DUK_DPRINT("  %snewenv", DUK_HOBJECT_HAS_NEWENV(obj) ? str_empty : str_excl);
	DUK_DPRINT("  %snamebinding", DUK_HOBJECT_HAS_NAMEBINDING(obj) ? str_empty : str_excl);
	DUK_DPRINT("  %screateargs", DUK_HOBJECT_HAS_CREATEARGS(obj) ? str_empty : str_excl);
	DUK_DPRINT("  %senvrecclosed", DUK_HOBJECT_HAS_ENVRECCLOSED(obj) ? str_empty : str_excl);
	DUK_DPRINT("  %sspecial_array", DUK_HOBJECT_HAS_SPECIAL_ARRAY(obj) ? str_empty : str_excl);
	DUK_DPRINT("  %sspecial_stringobj", DUK_HOBJECT_HAS_SPECIAL_STRINGOBJ(obj) ? str_empty : str_excl);
	DUK_DPRINT("  %sspecial_arguments", DUK_HOBJECT_HAS_SPECIAL_ARGUMENTS(obj) ? str_empty : str_excl);

	DUK_DPRINT("  class: number %d -> %s",
	           (int) DUK_HOBJECT_GET_CLASS_NUMBER(obj),
	           class_names[(DUK_HOBJECT_GET_CLASS_NUMBER(obj)) & ((1 << DUK_HOBJECT_FLAG_CLASS_BITS) - 1)]);

	DUK_DPRINT("  prototype: %p -> %!O",
	           (void *) obj->prototype,
	           (duk_heaphdr *) obj->prototype);

	DUK_DPRINT("  props: p=%p, e_size=%d, e_used=%d, a_size=%d, h_size=%d",
	           (void *) obj->p,
	           (int) obj->e_size,
	           (int) obj->e_used,
	           (int) obj->a_size,
	           (int) obj->h_size);

	/*
	 *  Object (struct layout) specific dumping.  Inline code here
	 *  instead of helpers, to ensure debug line prefix is identical.
	 */

	if (DUK_HOBJECT_IS_COMPILEDFUNCTION(obj)) {
		duk_hcompiledfunction *h = (duk_hcompiledfunction *) obj;

		DUK_DPRINT("  hcompiledfunction");
		DUK_DPRINT("  data: %!O", h->data);
		DUK_DPRINT("  nregs: %d", (int) h->nregs);
		DUK_DPRINT("  nargs: %d", (int) h->nargs);

		if (h->data && DUK_HBUFFER_HAS_DYNAMIC(h->data) && DUK_HBUFFER_GET_DATA_PTR(h->data)) {
			DUK_DPRINT("  consts: %p (%d, %d bytes)",
			           (void *) DUK_HCOMPILEDFUNCTION_GET_CONSTS_BASE(h),
			           (int) DUK_HCOMPILEDFUNCTION_GET_CONSTS_COUNT(h),
			           (int) DUK_HCOMPILEDFUNCTION_GET_CONSTS_SIZE(h));
			DUK_DPRINT("  funcs: %p (%d, %d bytes)",
			           (void *) DUK_HCOMPILEDFUNCTION_GET_FUNCS_BASE(h),
			           (int) DUK_HCOMPILEDFUNCTION_GET_FUNCS_COUNT(h),
			           (int) DUK_HCOMPILEDFUNCTION_GET_FUNCS_SIZE(h));
			DUK_DPRINT("  bytecode: %p (%d, %d bytes)",
			           (void *) DUK_HCOMPILEDFUNCTION_GET_CODE_BASE(h),
			           (int) DUK_HCOMPILEDFUNCTION_GET_CODE_COUNT(h),
			           (int) DUK_HCOMPILEDFUNCTION_GET_CODE_SIZE(h));
		} else {
			DUK_DPRINT("  consts: ???");
			DUK_DPRINT("  funcs: ???");
			DUK_DPRINT("  bytecode: ???");
		}
	} else if (DUK_HOBJECT_IS_NATIVEFUNCTION(obj)) {
		duk_hnativefunction *h = (duk_hnativefunction *) obj;

		DUK_DPRINT("  hnativefunction");
		/* XXX: h->func, cannot print function pointers portably */
		DUK_DPRINT("  nargs: %d", (int) h->nargs);
	} else if (DUK_HOBJECT_IS_THREAD(obj)) {
		duk_hthread *thr = (duk_hthread *) obj;
		duk_tval *p;

		DUK_DPRINT("  hthread");
		DUK_DPRINT("  strict: %d", (int) thr->strict);
		DUK_DPRINT("  state: %d", (int) thr->state);

		DUK_DPRINT("  valstack_max: %d, callstack_max:%d, catchstack_max: %d",
		           thr->valstack_max, thr->callstack_max, thr->catchstack_max);

		DUK_DPRINT("  callstack: ptr %p, size %d, top %d, preventcount %d, used size %d entries (%d bytes), alloc size %d entries (%d bytes)",
		           (void *) thr->callstack,
		           thr->callstack_size,
		           thr->callstack_top,
		           thr->callstack_preventcount,
		           thr->callstack_top,
		           thr->callstack_top * sizeof(duk_activation),
		           thr->callstack_size,
		           thr->callstack_size * sizeof(duk_activation));

		DUK_DEBUG_SUMMARY_INIT();
		DUK_DEBUG_SUMMARY_CHAR('[');
		for (i = 0; i <= thr->callstack_size; i++) {
			if (i == thr->callstack_top) {
				DUK_DEBUG_SUMMARY_CHAR('|');
			}
			if (!thr->callstack) {
				DUK_DEBUG_SUMMARY_CHAR('@');
			} else if (i < thr->callstack_size) {
				if (i < thr->callstack_top) {
					/* tailcalling is nice to see immediately; other flags (e.g. strict)
					 * not that important.
					 */
					if (thr->callstack[i].flags & DUK_ACT_FLAG_TAILCALLED) {
						DUK_DEBUG_SUMMARY_CHAR('/');
					}
					DUK_DEBUG_SUMMARY_CHAR(get_activation_summary_char(&thr->callstack[i]));
				} else {
					DUK_DEBUG_SUMMARY_CHAR('.');
				}
			}
		}
		DUK_DEBUG_SUMMARY_CHAR(']');
		DUK_DEBUG_SUMMARY_FINISH();

		DUK_DPRINT("  valstack: ptr %p, end %p (%d), bottom %p (%d), top %p (%d), used size %d entries (%d bytes), alloc size %d entries (%d bytes)",
		           (void *) thr->valstack,
		           (void *) thr->valstack_end,
		           (int) (thr->valstack_end - thr->valstack),
		           (void *) thr->valstack_bottom,
		           (int) (thr->valstack_bottom - thr->valstack),
		           (void *) thr->valstack_top,
		           (int) (thr->valstack_top - thr->valstack),
		           (int) (thr->valstack_top - thr->valstack),
		           (int) (thr->valstack_top - thr->valstack) * sizeof(duk_tval),
		           (int) (thr->valstack_end - thr->valstack),
		           (int) (thr->valstack_end - thr->valstack) * sizeof(duk_tval));

		DUK_DEBUG_SUMMARY_INIT();
		DUK_DEBUG_SUMMARY_CHAR('[');
		p = thr->valstack;
		while (p <= thr->valstack_end) {
			i = (int) (p - thr->valstack);
			if (thr->callstack &&
			    thr->callstack_top > 0 &&
			    i == (thr->callstack + thr->callstack_top - 1)->idx_bottom) {
				DUK_DEBUG_SUMMARY_CHAR('>');
			}
			if (p == thr->valstack_top) {
				DUK_DEBUG_SUMMARY_CHAR('|');
			}
			if (p < thr->valstack_end) {
				if (p < thr->valstack_top) {
					DUK_DEBUG_SUMMARY_CHAR(get_tval_summary_char(p));
				} else {
					/* XXX: safe printer for these?  would be nice, because
					 * we could visualize whether the values are in proper
					 * state.
					 */
					DUK_DEBUG_SUMMARY_CHAR('.');
				}
			}
			p++;
		}
		DUK_DEBUG_SUMMARY_CHAR(']');
		DUK_DEBUG_SUMMARY_FINISH();

		DUK_DPRINT("  catchstack: ptr %p, size %d, top %d, used size %d entries (%d bytes), alloc size %d entries (%d bytes)",
		           (void *) thr->catchstack,
		           thr->catchstack_size,
		           thr->catchstack_top,
		           thr->catchstack_top,
		           thr->catchstack_top * sizeof(duk_catcher),
		           thr->catchstack_size,
		           thr->catchstack_size * sizeof(duk_catcher));

		DUK_DEBUG_SUMMARY_INIT();
		DUK_DEBUG_SUMMARY_CHAR('[');
		for (i = 0; i <= thr->catchstack_size; i++) {
			if (i == thr->catchstack_top) {
				DUK_DEBUG_SUMMARY_CHAR('|');
			}
			if (!thr->catchstack) {
				DUK_DEBUG_SUMMARY_CHAR('@');
			} else if (i < thr->catchstack_size) {
				if (i < thr->catchstack_top) {
					DUK_DEBUG_SUMMARY_CHAR(get_catcher_summary_char(&thr->catchstack[i]));
				} else {
					DUK_DEBUG_SUMMARY_CHAR('.');
				}
			}
		}
		DUK_DEBUG_SUMMARY_CHAR(']');
		DUK_DEBUG_SUMMARY_FINISH();

		DUK_DPRINT("  resumer: ptr %p",
		           (void *) thr->resumer);

#if 0  /* worth dumping? */
		for (i = 0; i < DUK_NUM_BUILTINS; i++) {
			DUK_DPRINT("  builtins[%d] -> %!@O", i, thr->builtins[i]);
		}
#endif
	}

	if (obj->p) {
		DUK_DPRINT("  props alloc size: %d",
		           (int) DUK_HOBJECT_P_COMPUTE_SIZE(obj->e_size, obj->a_size, obj->h_size));
	} else {
		DUK_DPRINT("  props alloc size: n/a");
	}

	DUK_DPRINT("  prop entries:");
	for (i = 0; i < obj->e_size; i++) {
		duk_hstring *k;
		duk_propvalue *v;

		k = DUK_HOBJECT_E_GET_KEY(obj, i);
		v = DUK_HOBJECT_E_GET_VALUE_PTR(obj, i);

		if (i >= obj->e_used) {
			DUK_DPRINT("    [%d]: UNUSED", i);
			continue;
		}

		if (!k) {
			DUK_DPRINT("    [%d]: NULL", i);
			continue;
		}

		if (DUK_HOBJECT_E_SLOT_IS_ACCESSOR(obj, i)) {
			DUK_DPRINT("    [%d]: [w=%d e=%d c=%d a=%d] %!O -> get:%p set:%p; get %!O; set %!O",
			           i,
			           DUK_HOBJECT_E_SLOT_IS_WRITABLE(obj, i),
			           DUK_HOBJECT_E_SLOT_IS_ENUMERABLE(obj, i),
			           DUK_HOBJECT_E_SLOT_IS_CONFIGURABLE(obj, i),
			           DUK_HOBJECT_E_SLOT_IS_ACCESSOR(obj, i),
			           k,
			           (void *) v->a.get,
			           (void *) v->a.set,
			           (duk_heaphdr *) v->a.get,
			           (duk_heaphdr *) v->a.set);
		} else {
			DUK_DPRINT("    [%d]: [w=%d e=%d c=%d a=%d] %!O -> %!T",
			           i,
			           DUK_HOBJECT_E_SLOT_IS_WRITABLE(obj, i),
			           DUK_HOBJECT_E_SLOT_IS_ENUMERABLE(obj, i),
			           DUK_HOBJECT_E_SLOT_IS_CONFIGURABLE(obj, i),
			           DUK_HOBJECT_E_SLOT_IS_ACCESSOR(obj, i),
			           k,
			           &v->v);
		}
	}

	DUK_DPRINT("  array entries:");
	for (i = 0; i < obj->a_size; i++) {
		DUK_DPRINT("    [%d]: [w=%d e=%d c=%d a=%d] %d -> %!T",
		           i,
		           1,  /* implicit attributes */
		           1,
		           1,
		           0,
		           i,
		           DUK_HOBJECT_A_GET_VALUE_PTR(obj, i));
	}

	DUK_DPRINT("  hash entries:");
	for (i = 0; i < obj->h_size; i++) {
		duk_uint32_t t = DUK_HOBJECT_H_GET_INDEX(obj, i);
		if (t == DUK_HOBJECT_HASHIDX_UNUSED) {
			DUK_DPRINT("    [%d]: unused", i);
		} else if (t == DUK_HOBJECT_HASHIDX_DELETED) {
			DUK_DPRINT("    [%d]: deleted", i);
		} else {
			DUK_DPRINT("    [%d]: %d",
			           i,
			           (int) t);
		}
	}
}

void duk_debug_dump_callstack(duk_hthread *thr) {
	int i;

	DUK_DPRINT("=== hthread %p callstack: %d entries ===",
	           (void *) thr,
	           (thr == NULL ? 0 : thr->callstack_top));
	if (!thr) {
		return;
	}

	for (i = 0; i < thr->callstack_top; i++) {
		duk_activation *act = &thr->callstack[i];
		duk_tval *this_binding = NULL;

		this_binding = thr->valstack + act->idx_bottom - 1;
		if (this_binding < thr->valstack || this_binding >= thr->valstack_top) {
			this_binding = NULL;
		}

		DUK_DPRINT("  [%d] -> flags=0x%08x, func=%!O, var_env=%!iO, lex_env=%!iO, pc=%d, idx_bottom=%d, idx_retval=%d, this_binding=%!T",
		           i,
		           act->flags,
		           (duk_heaphdr *) act->func,
		           (duk_heaphdr *) act->var_env,
		           (duk_heaphdr *) act->lex_env,
		           act->pc,
		           act->idx_bottom,
		           act->idx_retval,
		           this_binding);
	}
}

void duk_debug_dump_activation(duk_hthread *thr, duk_activation *act) {
	if (!act) {
		DUK_DPRINT("duk_activation: NULL");
	} else {
		duk_tval *this_binding = NULL;

		this_binding = thr->valstack + act->idx_bottom - 1;
		if (this_binding < thr->valstack || this_binding >= thr->valstack_top) {
			this_binding = NULL;
		}

		DUK_DPRINT("duk_activation: %p -> flags=0x%08x, func=%!O, var_env=%!O, lex_env=%!O, pc=%d, idx_bottom=%d, idx_retval=%d, this_binding=%!T",
		           (void *) act,
		           act->flags,
		           (duk_heaphdr *) act->func,
		           (duk_heaphdr *) act->var_env,
		           (duk_heaphdr *) act->lex_env,
		           act->pc,
		           act->idx_bottom,
		           act->idx_retval,
		           this_binding);
	}
}

#endif  /* DUK_USE_DEBUG */

