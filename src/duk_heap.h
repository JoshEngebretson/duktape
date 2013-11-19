/*
 *  Heap structure.
 *
 *  Heap contains allocated heap objects, interned strings, and built-in
 *  strings for one or more threads.
 */

#ifndef DUK_HEAP_H_INCLUDED
#define DUK_HEAP_H_INCLUDED

/* alloc function typedefs in duktape.h */

/*
 *  Heap flags
 */

#define  DUK_HEAP_FLAG_MARKANDSWEEP_RUNNING                     (1 << 0)  /* mark-and-sweep is currently running */
#define  DUK_HEAP_FLAG_MARKANDSWEEP_RECLIMIT_REACHED            (1 << 1)  /* mark-and-sweep marking reached a recursion limit and must use multi-pass marking */
#define  DUK_HEAP_FLAG_REFZERO_FREE_RUNNING                     (1 << 2)  /* refcount code is processing refzero list */

#define  DUK__HEAP_HAS_FLAGS(heap,bits)               ((heap)->flags & (bits))
#define  DUK__HEAP_SET_FLAGS(heap,bits)  do { \
		(heap)->flags |= (bits); \
	} while (0)
#define  DUK__HEAP_CLEAR_FLAGS(heap,bits)  do { \
		(heap)->flags &= ~(bits); \
	} while (0)

#define  DUK_HEAP_HAS_MARKANDSWEEP_RUNNING(heap)            DUK__HEAP_HAS_FLAGS((heap), DUK_HEAP_FLAG_MARKANDSWEEP_RUNNING)
#define  DUK_HEAP_HAS_MARKANDSWEEP_RECLIMIT_REACHED(heap)   DUK__HEAP_HAS_FLAGS((heap), DUK_HEAP_FLAG_MARKANDSWEEP_RECLIMIT_REACHED)
#define  DUK_HEAP_HAS_REFZERO_FREE_RUNNING(heap)            DUK__HEAP_HAS_FLAGS((heap), DUK_HEAP_FLAG_REFZERO_FREE_RUNNING)

#define  DUK_HEAP_SET_MARKANDSWEEP_RUNNING(heap)            DUK__HEAP_SET_FLAGS((heap), DUK_HEAP_FLAG_MARKANDSWEEP_RUNNING)
#define  DUK_HEAP_SET_MARKANDSWEEP_RECLIMIT_REACHED(heap)   DUK__HEAP_SET_FLAGS((heap), DUK_HEAP_FLAG_MARKANDSWEEP_RECLIMIT_REACHED)
#define  DUK_HEAP_SET_REFZERO_FREE_RUNNING(heap)            DUK__HEAP_SET_FLAGS((heap), DUK_HEAP_FLAG_REFZERO_FREE_RUNNING)

#define  DUK_HEAP_CLEAR_MARKANDSWEEP_RUNNING(heap)          DUK__HEAP_CLEAR_FLAGS((heap), DUK_HEAP_FLAG_MARKANDSWEEP_RUNNING)
#define  DUK_HEAP_CLEAR_MARKANDSWEEP_RECLIMIT_REACHED(heap) DUK__HEAP_CLEAR_FLAGS((heap), DUK_HEAP_FLAG_MARKANDSWEEP_RECLIMIT_REACHED)
#define  DUK_HEAP_CLEAR_REFZERO_FREE_RUNNING(heap)          DUK__HEAP_CLEAR_FLAGS((heap), DUK_HEAP_FLAG_REFZERO_FREE_RUNNING)

/*
 *  Longjmp types, also double as identifying continuation type for a rethrow (in 'finally')
 */

#define  DUK_LJ_TYPE_UNKNOWN      0    /* unused */
#define  DUK_LJ_TYPE_RETURN       1    /* value1 -> return value */
#define  DUK_LJ_TYPE_THROW        2    /* value1 -> error object */
#define  DUK_LJ_TYPE_BREAK        3    /* value1 -> label number */
#define  DUK_LJ_TYPE_CONTINUE     4    /* value1 -> label number */
#define  DUK_LJ_TYPE_YIELD        5    /* value1 -> yield value, iserror -> error / normal */
#define  DUK_LJ_TYPE_RESUME       6    /* value1 -> resume value, value2 -> resumee thread, iserror -> error/normal */
#define  DUK_LJ_TYPE_NORMAL       7    /* pseudo-type to indicate a normal continuation (for 'finally' rethrowing) */

/* dummy non-zero value to be used as an argument for longjmp(), see man longjmp */
#define  DUK_LONGJMP_DUMMY_VALUE  1

/*
 *  Mark-and-sweep flags
 *
 *  These are separate from heap level flags now but could be merged.
 *  The heap structure only contains a 'base mark-and-sweep flags'
 *  field and the GC caller can impose further flags.
 */

#define  DUK_MS_FLAG_EMERGENCY                (1 << 0)   /* emergency mode: try extra hard */
#define  DUK_MS_FLAG_NO_STRINGTABLE_RESIZE    (1 << 1)   /* don't resize stringtable (but may sweep it); needed during stringtable resize */
#define  DUK_MS_FLAG_NO_FINALIZERS            (1 << 2)   /* don't run finalizers (which may have arbitrary side effects) */
#define  DUK_MS_FLAG_NO_OBJECT_COMPACTION     (1 << 3)   /* don't compact objects; needed during object property allocation resize */

/*
 *  Other heap related defines
 */

/* Maximum duk_handle_call / duk_handle_safe_call depth.  Note that this
 * does not limit bytecode executor internal call depth at all (e.g.
 * for Ecmascript-to-Ecmascript calls, thread yields/resumes, etc).
 * There is a separate callstack depth limit for threads.
 */

#define  DUK_HEAP_DEFAULT_CALL_RECURSION_LIMIT             60  /* assuming 0.5 kB between calls, about 30kB of stack */ 

/* mark-and-sweep C recursion depth for marking phase; if reached,
 * mark object as a TEMPROOT and use multi-pass marking.
 */
#ifdef  DUK_USE_MARK_AND_SWEEP
#ifdef  DUK_USE_GC_TORTURE
#define  DUK_HEAP_DEFAULT_MARK_AND_SWEEP_RECURSION_LIMIT   3
#else
#define  DUK_HEAP_DEFAULT_MARK_AND_SWEEP_RECURSION_LIMIT   32
#endif
#endif

/* mark-and-sweep interval can be much lower with reference counting */
#ifdef  DUK_USE_MARK_AND_SWEEP
#ifdef  DUK_USE_REFERENCE_COUNTING
#define  DUK_HEAP_DEFAULT_MARK_AND_SWEEP_TRIGGER_LIMIT     10000
#else
#define  DUK_HEAP_DEFAULT_MARK_AND_SWEEP_TRIGGER_LIMIT     1000
#endif
#endif

/* stringcache is used for speeding up char-offset-to-byte-offset
 * translations for non-ASCII strings
 */
#define  DUK_HEAP_STRCACHE_SIZE                            4
#define  DUK_HEAP_STRINGCACHE_NOCACHE_LIMIT                16  /* strings up to the this length are not cached */

/* helper to insert a (non-string) heap object into heap allocated list */
#define  DUK_HEAP_INSERT_INTO_HEAP_ALLOCATED(heap,hdr)     duk_heap_insert_into_heap_allocated((heap),(hdr))

/*
 *  Stringtable
 */

/* initial stringtable size, must be prime and higher than DUK_UTIL_MIN_HASH_PRIME */
#define  DUK_STRTAB_INITIAL_SIZE            17

/* indicates a deleted string; any fixed non-NULL, non-hstring pointer works */
#define  DUK_STRTAB_DELETED_MARKER(heap)    ((duk_hstring *) heap)

/* resizing parameters */
#define  DUK_STRTAB_MIN_FREE_DIVISOR        4                /* load factor max 75% */
#define  DUK_STRTAB_MIN_USED_DIVISOR        4                /* load factor min 25% */
#define  DUK_STRTAB_GROW_ST_SIZE(n)         ((n) + (n))      /* used entries + approx 100% -> reset load to 50% */

#define  DUK_STRTAB_U32_MAX_STRLEN          10               /* 4'294'967'295 */
#define  DUK_STRTAB_HIGHEST_32BIT_PRIME     0xfffffffbU

/* probe sequence */
#define  DUK_STRTAB_HASH_INITIAL(hash,h_size)    ((hash) % (h_size))
#define  DUK_STRTAB_HASH_PROBE_STEP(hash)        DUK_UTIL_GET_HASH_PROBE_STEP((hash))

/*
 *  Built-in strings
 */

/* heap string indices are autogenerated in duk_strings.h */
#define  DUK_HEAP_GET_STRING(heap,idx)  ((heap)->strs[(idx)])

/*
 *  Raw memory calls: relative to heap, but no GC interaction
 */

#define  DUK_ALLOC_RAW(heap,size) \
	((heap)->alloc_func((heap)->alloc_udata, (size)))

#define  DUK_REALLOC_RAW(heap,ptr,newsize) \
	((heap)->realloc_func((heap)->alloc_udata, (ptr), (newsize)))

#define  DUK_FREE_RAW(heap,ptr) \
	((heap)->free_func((heap)->alloc_udata, (ptr)))

/*
 *  Memory calls: relative to heap, GC interaction, but no error throwing.
 *
 *  FIXME: currently a mark-and-sweep triggered by memory allocation will
 *  run using the heap->heap_thread.  This thread is also used for running
 *  mark-and-sweep finalization; this is not ideal because it breaks the
 *  isolation between multiple global environments.
 *
 *  Notes:
 *
 *    - DUK_FREE() is required to ignore NULL and any other possible return
 *      value of a zero-sized alloc/realloc (same as ANSI C free()).
 * 
 *    - There is no DUK_REALLOC_ZEROED (and checked variant) because we don't
 *      assume to know the old size.  Caller must zero the reallocated memory.
 *
 *    - DUK_REALLOC_INDIRECT() must be used when a mark-and-sweep triggered
 *      by an allocation failure might invalidate the original 'ptr', thus
 *      causing a realloc retry to use an invalid pointer.  Example: we're
 *      reallocating the value stack and a finalizer resizes the same value
 *      stack during mark-and-sweep.  The indirect variant knows the storage
 *      location of the pointer being reallocated and looks it up on every
 *      attempt; the storage location must of course be stable, which is
 *      always the case for heap objects now.
 *
 *      Note: the pointer in the storage location ('iptr') is read but is
 *      NOT updated; caller must do that.
 */

#define  DUK_ALLOC(heap,size)                            duk_heap_mem_alloc((heap), (size))
#define  DUK_ALLOC_ZEROED(heap,size)                     duk_heap_mem_alloc_zeroed((heap), (size))
#define  DUK_REALLOC(heap,ptr,newsize)                   duk_heap_mem_realloc((heap), (ptr), (newsize))
#define  DUK_REALLOC_INDIRECT(heap,iptr,newsize)         duk_heap_mem_realloc_indirect((heap), (iptr), (newsize))
#define  DUK_FREE(heap,ptr)                              duk_heap_mem_free((heap), (ptr))

/*
 *  Memory calls: relative to a thread, GC interaction, throw error on alloc failure
 */

/* XXX: add __func__; use DUK_FUNC_MACRO because __func__ is not always available */

#ifdef DUK_USE_VERBOSE_ERRORS
#define  DUK_ALLOC_CHECKED(thr,size)                     duk_heap_mem_alloc_checked((thr), (size), DUK_FILE_MACRO, DUK_LINE_MACRO)
#define  DUK_ALLOC_CHECKED_ZEROED(thr,size)              duk_heap_mem_alloc_checked_zeroed((thr), (size), DUK_FILE_MACRO, DUK_LINE_MACRO)
#define  DUK_REALLOC_CHECKED(thr,ptr,newsize)            duk_heap_mem_realloc_checked((thr), (ptr), (newsize), DUK_FILE_MACRO, DUK_LINE_MACRO)
#define  DUK_REALLOC_INDIRECT_CHECKED(thr,iptr,newsize)  duk_heap_mem_realloc_indirect_checked((thr), (iptr), (newsize), DUK_FILE_MACRO, DUK_LINE_MACRO)
#define  DUK_FREE_CHECKED(thr,ptr)                       duk_heap_mem_free((thr)->heap, (ptr))  /* must not fail */
#else
#define  DUK_ALLOC_CHECKED(thr,size)                     duk_heap_mem_alloc_checked((thr), (size))
#define  DUK_ALLOC_CHECKED_ZEROED(thr,size)              duk_heap_mem_alloc_checked_zeroed((thr), (size))
#define  DUK_REALLOC_CHECKED(thr,ptr,newsize)            duk_heap_mem_realloc_checked((thr), (ptr), (newsize))
#define  DUK_REALLOC_INDIRECT_CHECKED(thr,iptr,newsize)  duk_heap_mem_realloc_indirect_checked((thr), (iptr), (newsize))
#define  DUK_FREE_CHECKED(thr,ptr)                       duk_heap_mem_free((thr)->heap, (ptr))  /* must not fail */
#endif

/*
 *  Memory constants
 */

#define  DUK_HEAP_ALLOC_FAIL_MARKANDSWEEP_LIMIT           5   /* Retry allocation after mark-and-sweep for this
                                                               * many times.  A single mark-and-sweep round is
                                                               * not guaranteed to free all unreferenced memory
                                                               * because of finalization (in fact, ANY number of
                                                               * rounds is strictly not enough).
                                                               */

#define  DUK_HEAP_ALLOC_FAIL_MARKANDSWEEP_EMERGENCY_LIMIT  3  /* Starting from this round, use emergency mode
                                                               * for mark-and-sweep.
                                                               */

/*
 *  String cache should ideally be at duk_hthread level, but that would
 *  cause string finalization to slow down relative to the number of
 *  threads; string finalization must check the string cache for "weak"
 *  references to the string being finalized to avoid dead pointers.
 *
 *  Thus, string caches are now at the heap level now.
 */

struct duk_strcache {
	duk_hstring *h;
	duk_u32 bidx;
	duk_u32 cidx;
};

/*
 *  Longjmp state, contains the information needed to perform a longjmp.
 *  Longjmp related values are written to value1, value2, and iserror.
 */

struct duk_ljstate {
	duk_jmpbuf *jmpbuf_ptr;   /* current setjmp() catchpoint */
	duk_hobject *errhandler;  /* function to invoke for errors before unwinding; may be NULL, -borrowed reference- (must be in valstack) */
	int type;                 /* longjmp type */
	duk_tval value1;          /* 1st related value (type specific) */
	duk_tval value2;          /* 2nd related value (type specific) */
	int iserror;              /* isError flag for yield */
};

/*
 *  Main heap structure
 */

struct duk_heap {
	int flags;

	/* allocator functions */
        duk_alloc_function alloc_func;
        duk_realloc_function realloc_func;
        duk_free_function free_func;
        void *alloc_udata;

	/* allocated heap objects */
	duk_heaphdr *heap_allocated;

	/* work list for objects whose refcounts are zero but which have not been
	 * "finalized"; avoids recursive C calls when refcounts go to zero in a
	 * chain of objects.
	 */
#ifdef DUK_USE_REFERENCE_COUNTING
	duk_heaphdr *refzero_list;
	duk_heaphdr *refzero_list_tail;
#endif

#ifdef DUK_USE_MARK_AND_SWEEP
	/* mark-and-sweep control */
	int mark_and_sweep_trigger_counter;
	int mark_and_sweep_trigger_limit;
	int mark_and_sweep_recursion_depth;
	int mark_and_sweep_recursion_limit;

	/* mark-and-sweep flags automatically active (used for critical sections) */
	int mark_and_sweep_base_flags;

	/* work list for objects to be finalized (by mark-and-sweep) */
	duk_heaphdr *finalize_list;
#endif

	/* fatal error handling, called e.g. when a longjmp() is needed but
	 * lj.jmpbuf_ptr is NULL.  fatal_func must never return.
	 */
	duk_fatal_function fatal_func;

	/* longjmp state */
	duk_ljstate lj;

	/* marker for detecting internal "double faults", see duk_error_throw.c */
	int handling_error;

	/* heap thread, used internally and for finalization */
	duk_hthread *heap_thread;

	/* current thread */
	duk_hthread *curr_thread;	/* currently running thread */

	/* heap level "stash" object (e.g., various reachability roots) */
	duk_hobject *heap_object;

	/* duk_handle_call / duk_handle_safe_call recursion depth limiting */
	int call_recursion_depth;
	int call_recursion_limit;

	/* mix-in value for computing string hashes; should be reasonably unpredictable */
        duk_u32 hash_seed;

	/* rnd_state for duk_util_tinyrandom.c */
	duk_u32 rnd_state;

	/* string intern table (weak refs) */
	duk_hstring **st;
	duk_u32 st_size;     /* alloc size in elements */
	duk_u32 st_used;     /* used elements (includes DELETED) */

	/* string access cache (codepoint offset -> byte offset) for fast string
	 * character looping; 'weak' reference which needs special handling in GC.
	 */
	duk_strcache strcache[DUK_HEAP_STRCACHE_SIZE];

	/* built-in strings */
	duk_hstring *strs[DUK_HEAP_NUM_STRINGS];
};

/*
 *  Prototypes
 */

duk_heap *duk_heap_alloc(duk_alloc_function alloc_func,
                         duk_realloc_function realloc_func,
                         duk_free_function free_func,
                         void *alloc_udata,
                         duk_fatal_function fatal_func);
void duk_heap_free(duk_heap *heap);
void duk_heap_free_heaphdr_raw(duk_heap *heap, duk_heaphdr *hdr);

void duk_heap_insert_into_heap_allocated(duk_heap *heap, duk_heaphdr *hdr);
#if defined(DUK_USE_DOUBLE_LINKED_HEAP) && defined(DUK_USE_REFERENCE_COUNTING)
void duk_heap_remove_any_from_heap_allocated(duk_heap *heap, duk_heaphdr *hdr);
#endif

duk_hstring *duk_heap_string_lookup(duk_heap *heap, duk_u8 *str, duk_u32 blen);
duk_hstring *duk_heap_string_intern(duk_heap *heap, duk_u8 *str, duk_u32 blen);
duk_hstring *duk_heap_string_intern_checked(duk_hthread *thr, duk_u8 *str, duk_u32 len);
duk_hstring *duk_heap_string_lookup_u32(duk_heap *heap, duk_u32 val);
duk_hstring *duk_heap_string_intern_u32(duk_heap *heap, duk_u32 val);
duk_hstring *duk_heap_string_intern_u32_checked(duk_hthread *thr, duk_u32 val);
void duk_heap_string_remove(duk_heap *heap, duk_hstring *h);
void duk_heap_force_stringtable_resize(duk_heap *heap);

void duk_heap_strcache_string_remove(duk_heap *heap, duk_hstring *h);
duk_u32 duk_heap_strcache_offset_char2byte(duk_hthread *thr, duk_hstring *h, duk_u32 char_offset);

#ifdef DUK_USE_PROVIDE_DEFAULT_ALLOC_FUNCTIONS
void *duk_default_alloc_function(void *udata, size_t size);
void *duk_default_realloc_function(void *udata, void *ptr, size_t newsize);
void duk_default_free_function(void *udata, void *ptr);
#endif

void *duk_heap_mem_alloc(duk_heap *heap, size_t size);
void *duk_heap_mem_alloc_zeroed(duk_heap *heap, size_t size);
void *duk_heap_mem_realloc(duk_heap *heap, void *ptr, size_t newsize);
void *duk_heap_mem_realloc_indirect(duk_heap *heap, void **iptr, size_t newsize);
void duk_heap_mem_free(duk_heap *heap, void *ptr);

#ifdef DUK_USE_VERBOSE_ERRORS
void *duk_heap_mem_alloc_checked(duk_hthread *thr, size_t size, const char *filename, int line);
void *duk_heap_mem_alloc_checked_zeroed(duk_hthread *thr, size_t size, const char *filename, int line);
void *duk_heap_mem_realloc_checked(duk_hthread *thr, void *ptr, size_t newsize, const char *filename, int line);
void *duk_heap_mem_realloc_indirect_checked(duk_hthread *thr, void **iptr, size_t newsize, const char *filename, int line);
#else
void *duk_heap_mem_alloc_checked(duk_hthread *thr, size_t size);
void *duk_heap_mem_alloc_checked_zeroed(duk_hthread *thr, size_t size);
void *duk_heap_mem_realloc_checked(duk_hthread *thr, void *ptr, size_t newsize);
void *duk_heap_mem_realloc_indirect_checked(duk_hthread *thr, void **iptr, size_t newsize);
#endif

#ifdef DUK_USE_REFERENCE_COUNTING
void duk_heap_tval_incref(duk_tval *tv);
void duk_heap_tval_decref(duk_hthread *thr, duk_tval *tv);
void duk_heap_heaphdr_incref(duk_heaphdr *h);
void duk_heap_heaphdr_decref(duk_hthread *thr, duk_heaphdr *h);
void duk_heap_refcount_finalize_heaphdr(duk_hthread *thr, duk_heaphdr *hdr);
#else
/* no refcounting */
#endif

#ifdef DUK_USE_MARK_AND_SWEEP
int duk_heap_mark_and_sweep(duk_heap *heap, int flags);
#endif

duk_u32 duk_heap_hashstring(duk_heap *heap, duk_u8 *str, duk_u32 len);

#endif  /* DUK_HEAP_H_INCLUDED */

