/*
 *  duk_heap allocation and freeing.
 */

#include "duk_internal.h"

/* constants for built-in string data depacking */
#define  BITPACK_LETTER_LIMIT  26
#define  BITPACK_UNDERSCORE    26
#define  BITPACK_FF            27
#define  BITPACK_SWITCH1       29
#define  BITPACK_SWITCH        30
#define  BITPACK_SEVENBIT      31

/*
 *  Free a heap object.
 *
 *  Free heap object and its internal (non-heap) pointers.  Assumes that
 *  caller has removed the object from heap allocated list or the string
 *  intern table, and any weak references (which strings may have) have
 *  been already dealt with.
 */

static void free_hobject_inner(duk_heap *heap, duk_hobject *h) {
	DUK_ASSERT(heap != NULL);
	DUK_ASSERT(h != NULL);

	DUK_FREE(heap, h->p);

	if (DUK_HOBJECT_IS_COMPILEDFUNCTION(h)) {
		duk_hcompiledfunction *f = (duk_hcompiledfunction *) h;
		DUK_UNREF(f);
		/* Currently nothing to free; 'data' is a heap object */
	} else if (DUK_HOBJECT_IS_NATIVEFUNCTION(h)) {
		duk_hnativefunction *f = (duk_hnativefunction *) h;
		DUK_UNREF(f);
		/* Currently nothing to free */
	} else if (DUK_HOBJECT_IS_THREAD(h)) {
		duk_hthread *t = (duk_hthread *) h;
		DUK_FREE(heap, t->valstack);
		DUK_FREE(heap, t->callstack);
		DUK_FREE(heap, t->catchstack);
		/* don't free h->resumer, because it exists in the heap */
	}
}

static void free_hbuffer_inner(duk_heap *heap, duk_hbuffer *h) {
	DUK_ASSERT(heap != NULL);
	DUK_ASSERT(h != NULL);

	if (DUK_HBUFFER_HAS_DYNAMIC(h)) {
		duk_hbuffer_dynamic *g = (duk_hbuffer_dynamic *) h;
		DUK_DDDPRINT("free dynamic buffer %p", g->curr_alloc);
		DUK_FREE(heap, g->curr_alloc);
	}
}

void duk_heap_free_heaphdr_raw(duk_heap *heap, duk_heaphdr *hdr) {
	DUK_ASSERT(heap);
	DUK_ASSERT(hdr);

	DUK_DDDPRINT("free heaphdr %p, htype %d", (void *) hdr, (int) DUK_HEAPHDR_GET_TYPE(hdr));

	switch (DUK_HEAPHDR_GET_TYPE(hdr)) {
	case DUK_HTYPE_STRING:
		/* no inner refs to free */
		break;
	case DUK_HTYPE_OBJECT:
		free_hobject_inner(heap, (duk_hobject *) hdr);
		break;
	case DUK_HTYPE_BUFFER:
		free_hbuffer_inner(heap, (duk_hbuffer *) hdr);
		break;
	default:
		DUK_NEVER_HERE();
	}

	DUK_FREE(heap, hdr);
}

/*
 *  Free the heap.
 *
 *  Frees heap-related non-heap-tracked allocations such as the
 *  string intern table; then frees the heap allocated objects;
 *  and finally frees the heap structure itself.  Reference counts
 *  and GC markers are ignored (and not updated) in this process,
 *  and finalizers won't be called.
 *
 *  The heap pointer and heap object pointers must not be used
 *  after this call.
 */

static void free_allocated(duk_heap *heap) {
	duk_heaphdr *curr;
	duk_heaphdr *next;

	curr = heap->heap_allocated;
	while (curr) {
		/* We don't log or warn about freeing zero refcount objects
		 * because they may happen with finalizer processing.
		 */

		DUK_DDDPRINT("FINALFREE (allocated): %!iO", curr);
		next = DUK_HEAPHDR_GET_NEXT(curr);
		duk_heap_free_heaphdr_raw(heap, curr);
		curr = next;
	}
}

#ifdef DUK_USE_REFERENCE_COUNTING
static void free_refzero_list(duk_heap *heap) {
	duk_heaphdr *curr;
	duk_heaphdr *next;

	curr = heap->refzero_list;
	while (curr) {
		DUK_DDDPRINT("FINALFREE (refzero_list): %!iO", curr);
		next = DUK_HEAPHDR_GET_NEXT(curr);
		duk_heap_free_heaphdr_raw(heap, curr);
		curr = next;
	}
}
#endif

#ifdef DUK_USE_MARK_AND_SWEEP
static void free_markandsweep_finalize_list(duk_heap *heap) {
	duk_heaphdr *curr;
	duk_heaphdr *next;

	curr = heap->finalize_list;
	while (curr) {
		DUK_DDDPRINT("FINALFREE (finalize_list): %!iO", curr);
		next = DUK_HEAPHDR_GET_NEXT(curr);
		duk_heap_free_heaphdr_raw(heap, curr);
		curr = next;
	}
}
#endif

static void free_stringtable(duk_heap *heap) {
	int i;

	/* strings are only tracked by stringtable */
	if (heap->st) {
		for (i = 0; i < heap->st_size; i++) {
			duk_hstring *e = heap->st[i];
			if (e == DUK_STRTAB_DELETED_MARKER(heap)) {
				continue;
			}

			/* strings have no inner allocations so free directly */
			DUK_DDDPRINT("FINALFREE (string): %!iO", e);
			DUK_FREE(heap, e);
#if 0  /* not strictly necessary */
			heap->st[i] = NULL;
#endif
		}
		DUK_FREE(heap, heap->st);
#if 0  /* not strictly necessary */
		heap->st = NULL;
#endif
	}
}

void duk_heap_free(duk_heap *heap) {
	DUK_DPRINT("free heap: %p", heap);

	/* Note: heap->heap_thread, heap->curr_thread, heap->heap_object are
	 * on the heap allocated list.
	 */

	DUK_DPRINT("freeing heap objects of heap: %p", heap);
	free_allocated(heap);

#ifdef DUK_USE_REFERENCE_COUNTING
	DUK_DPRINT("freeing refzero list of heap: %p", heap);
	free_refzero_list(heap);
#endif

#ifdef DUK_USE_MARK_AND_SWEEP
	DUK_DPRINT("freeing mark-and-sweep finalize list of heap: %p", heap);
	free_markandsweep_finalize_list(heap);
#endif

	DUK_DPRINT("freeing string table of heap: %p", heap);
	free_stringtable(heap);

	DUK_DPRINT("freeing heap structure: %p", heap);
	heap->free_func(heap->alloc_udata, heap);
}

/*
 *  Allocate a heap.
 *
 *  String table is initialized with built-in strings from genstrings.py.
 */

/* intern built-in strings from precooked data (genstrings.py) */
static int init_heap_strings(duk_heap *heap) {
	duk_bitdecoder_ctx bd_ctx;
	duk_bitdecoder_ctx *bd = &bd_ctx;  /* convenience */
	int i, j;

	DUK_MEMSET(&bd_ctx, 0, sizeof(bd_ctx));
	bd->data = (duk_u8 *) duk_strings_data;
	bd->length = DUK_STRDATA_DATA_LENGTH;

	for (i = 0; i < DUK_HEAP_NUM_STRINGS; i++) {
		duk_u8 tmp[DUK_STRDATA_MAX_STRLEN];
		duk_hstring *h;
		int len;
		int mode;
		int t;

		len = duk_bd_decode(bd, 5);
		mode = 32;		/* 0 = uppercase, 32 = lowercase (= 'a' - 'A') */
		for (j = 0; j < len; j++) {
			t = duk_bd_decode(bd, 5);
			if (t < BITPACK_LETTER_LIMIT) {
				t = t + 'A' + mode;
			} else if (t == BITPACK_UNDERSCORE) {
				t = (int) '_';
			} else if (t == BITPACK_FF) {
				/* Internal keys are prefixed with 0xFF in the stringtable
				 * (which makes them invalid UTF-8 on purpose).
				 */
				t = (int) 0xff;
			} else if (t == BITPACK_SWITCH1) {
				t = duk_bd_decode(bd, 5);
				DUK_ASSERT(t >= 0 && t <= 25);
				t = t + 'A' + (mode ^ 32);
			} else if (t == BITPACK_SWITCH) {
				mode = mode ^ 32;
				t = duk_bd_decode(bd, 5);
				DUK_ASSERT(t >= 0 && t <= 25);
				t = t + 'A' + mode;
			} else if (t == BITPACK_SEVENBIT) {
				t = duk_bd_decode(bd, 7);
			}
			tmp[j] = (duk_u8) t;
		}

		DUK_DDDPRINT("intern built-in string %d", i);
		h = duk_heap_string_intern(heap, tmp, len);
		if (!h) {
			goto error;
		}

		/* special flags */

		if (len > 0 && tmp[0] == 0xff) {
			DUK_HSTRING_SET_INTERNAL(h);
		}
		if (i == DUK_STRIDX_EVAL || i == DUK_STRIDX_LC_ARGUMENTS) {
			DUK_HSTRING_SET_EVAL_OR_ARGUMENTS(h);
		}
		if (i >= DUK_STRIDX_START_RESERVED && i < DUK_STRIDX_END_RESERVED) {
			DUK_HSTRING_SET_RESERVED_WORD(h);
			if (i >= DUK_STRIDX_START_STRICT_RESERVED) {
				DUK_HSTRING_SET_STRICT_RESERVED_WORD(h);
			}
		}

		DUK_DDDPRINT("interned: %!O", h);

		/* The incref macro takes a thread pointer but doesn't use it
		 * right now.
		 */
		DUK_HSTRING_INCREF(_never_referenced_, h);

		heap->strs[i] = h;
	}

	return 1;

 error:
	return 0;
}

static int init_heap_thread(duk_heap *heap) {
	duk_hthread *thr;
	
	DUK_DDPRINT("heap init: alloc heap thread");
	thr = duk_hthread_alloc(heap,
	                        DUK_HOBJECT_FLAG_EXTENSIBLE |
	                        DUK_HOBJECT_FLAG_THREAD |
	                        DUK_HOBJECT_CLASS_AS_FLAGS(DUK_HOBJECT_CLASS_THREAD));
	if (!thr) {
		DUK_DPRINT("failed to alloc heap_thread");
		return 0;
	}
	thr->state = DUK_HTHREAD_STATE_INACTIVE;
	thr->strs = heap->strs;

	heap->heap_thread = thr;
	DUK_HTHREAD_INCREF(thr, thr);  /* Note: first argument not really used */

	/* 'thr' is now reachable */

	if (!duk_hthread_init_stacks(heap, thr)) {
		return 0;
	}

	/* FIXME: this may now fail, and is not handled correctly */
	duk_hthread_create_builtin_objects(thr);

	/* default prototype (Note: 'thr' must be reachable) */
	DUK_HOBJECT_SET_PROTOTYPE(thr, (duk_hobject *) thr, thr->builtins[DUK_BIDX_THREAD_PROTOTYPE]);

	return 1;
}

duk_heap *duk_heap_alloc(duk_alloc_function alloc_func,
                         duk_realloc_function realloc_func,
                         duk_free_function free_func,
                         void *alloc_udata,
                         duk_fatal_function fatal_func) {
	duk_heap *res = NULL;

	DUK_DPRINT("allocate heap");

#ifdef DUK_USE_COMPUTED_NAN
	do {
		/* Workaround for some exotic platforms where NAN is missing
		 * and the expression (0.0 / 0.0) does NOT result in a NaN.
		 * Such platforms use the global 'duk_computed_nan' which must
		 * be initialized at runtime.  Use 'volatile' to ensure that
		 * the compiler will actually do the computation and not try
		 * to do constant folding which might result in the original
		 * problem.
		 */
		volatile double dbl1 = 0.0;
		volatile double dbl2 = 0.0;
		duk_computed_nan = dbl1 / dbl2;
	} while(0);
#endif

#ifdef DUK_USE_COMPUTED_INFINITY
	do {
		/* Similar workaround for INFINITY. */
		volatile double dbl1 = 1.0;
		volatile double dbl2 = 0.0;
		duk_computed_infinity = dbl1 / dbl2;
	} while(0);
#endif

	/* use a raw call, all macros expect the heap to be initialized */
	res = (duk_heap *) alloc_func(alloc_udata, sizeof(duk_heap));
	if (!res) {
		goto error;
	}

	/* zero everything */
	DUK_MEMSET(res, 0, sizeof(*res));

	/* explicit NULL inits */
#ifdef DUK_USE_EXPLICIT_NULL_INIT
	res->alloc_udata = NULL;
	res->heap_allocated = NULL;
#ifdef DUK_USE_REFERENCE_COUNTING
	res->refzero_list = NULL;
	res->refzero_list_tail = NULL;
#endif
#ifdef DUK_USE_MARK_AND_SWEEP
	res->finalize_list = NULL;
#endif
	res->heap_thread = NULL;
	res->curr_thread = NULL;
	res->heap_object = NULL;
	res->st = NULL;
	{
		int i;
	        for (i = 0; i < DUK_HEAP_NUM_STRINGS; i++) {
        	        res->strs[i] = NULL;
	        }
	}
#endif

	/* initialize the structure, roughly in order */
	res->alloc_func = alloc_func;
	res->realloc_func = realloc_func;
	res->free_func = free_func;
	res->alloc_udata = alloc_udata;
	res->fatal_func = fatal_func;

#ifdef DUK_USE_MARK_AND_SWEEP
	res->mark_and_sweep_recursion_limit = DUK_HEAP_DEFAULT_MARK_AND_SWEEP_RECURSION_LIMIT;
	res->mark_and_sweep_trigger_limit = DUK_HEAP_DEFAULT_MARK_AND_SWEEP_TRIGGER_LIMIT;
	/* res->mark_and_sweep_trigger_counter == 0 -> now causes immediate GC; which is OK */
#endif

	res->call_recursion_depth = 0;
	res->call_recursion_limit = DUK_HEAP_DEFAULT_CALL_RECURSION_LIMIT;

	/* FIXME: use the pointer as a seed for now: mix in time at least */

	/* cast through C99 intptr_t to avoid GCC warning:
	 *
	 *   warning: cast from pointer to integer of different size [-Wpointer-to-int-cast]
	 */
	res->hash_seed = (duk_u32) (intptr_t) res;
	res->rnd_state = (duk_u32) (intptr_t) res;

#ifdef DUK_USE_EXPLICIT_NULL_INIT
	res->lj.jmpbuf_ptr = NULL;
#endif
	DUK_ASSERT(res->lj.type == DUK_LJ_TYPE_UNKNOWN);  /* zero */

	DUK_TVAL_SET_UNDEFINED_UNUSED(&res->lj.value1);
	DUK_TVAL_SET_UNDEFINED_UNUSED(&res->lj.value2);

#if (DUK_STRTAB_INITIAL_SIZE < DUK_UTIL_MIN_HASH_PRIME)
#error initial heap stringtable size is defined incorrectly
#endif

	res->st = (duk_hstring **) alloc_func(alloc_udata, sizeof(duk_hstring *) * DUK_STRTAB_INITIAL_SIZE);
	if (!res->st) {
		goto error;
	}
	res->st_size = DUK_STRTAB_INITIAL_SIZE;
#ifdef DUK_USE_EXPLICIT_NULL_INIT
	{
		int i;
	        for (i = 0; i < res->st_size; i++) {
        	        res->st[i] = NULL;
	        }
	}
#else
	DUK_MEMSET(res->st, 0, sizeof(duk_hstring *) * DUK_STRTAB_INITIAL_SIZE);
#endif

	/* strcache init */
#ifdef DUK_USE_EXPLICIT_NULL_INIT
	{
		int i;
		for (i = 0; i < DUK_HEAP_STRCACHE_SIZE; i++) {
			res->strcache[i].h = NULL;
		}
	}
#endif

	/* FIXME: error handling is incomplete.  It would be cleanest if
	 * there was a setjmp catchpoint, so that all init code could
	 * freely throw errors.  If that were the case, the return code
	 * passing here could be removed.
	 */

	/* built-in strings */
	DUK_DDPRINT("HEAP: INIT STRINGS");
	if (!init_heap_strings(res)) {
		goto error;
	}

	/* heap thread */
	DUK_DDPRINT("HEAP: INIT HEAP THREAD");
	if (!init_heap_thread(res)) {
		goto error;
	}

	/* heap object */
	DUK_DDPRINT("HEAP: INIT HEAP OBJECT");
	DUK_ASSERT(res->heap_thread != NULL);
	res->heap_object = duk_hobject_alloc(res, DUK_HOBJECT_FLAG_EXTENSIBLE |
	                                          DUK_HOBJECT_CLASS_AS_FLAGS(DUK_HOBJECT_CLASS_OBJECT));
	if (!res->heap_object) {
		goto error;
	}
	DUK_HOBJECT_INCREF(res->heap_thread, res->heap_object);

	DUK_DPRINT("allocated heap: %p", res);
	return res;

 error:
	DUK_DPRINT("heap allocation failed");

	if (res) {
		/* assumes that allocated pointers and alloc funcs are valid
		 * if res exists
		 */
		DUK_ASSERT(res->alloc_func != NULL);
		DUK_ASSERT(res->realloc_func != NULL);
		DUK_ASSERT(res->free_func != NULL);
		duk_heap_free(res);
	}
	return NULL;
}

