/*
 *  Mark-and-sweep garbage collection.
 */

#include "duk_internal.h"

#ifdef DUK_USE_MARK_AND_SWEEP

static void mark_heaphdr(duk_heap *heap, duk_heaphdr *h);
static void mark_tval(duk_heap *heap, duk_tval *tv);

/*
 *  Misc
 */

/* Select a thread for mark-and-sweep use.  This needs to change
 * later.
 */
static duk_hthread *get_temp_hthread(duk_heap *heap) {
	if (heap->curr_thread) {
		return heap->curr_thread;
	}
	return heap->heap_thread;  /* may be NULL, too */
}

/*
 *  Marking functions for heap types: mark children recursively
 */

static void mark_hstring(duk_heap *heap, duk_hstring *h) {
	DUK_DDDPRINT("mark_hstring: %p", (void *) h);

	DUK_ASSERT(h);

	/* nothing to process */
}

static void mark_hobject(duk_heap *heap, duk_hobject *h) {
	int i;

	DUK_DDDPRINT("mark_hobject: %p", (void *) h);

	DUK_ASSERT(h);

	/* XXX: use advancing pointers instead of index macros -> faster and smaller? */

	for (i = 0; i < h->e_used; i++) {
		duk_hstring *key = DUK_HOBJECT_E_GET_KEY(h, i);
		if (!key) {
			continue;
		}
		mark_heaphdr(heap, (duk_heaphdr *) key);
		if (DUK_HOBJECT_E_SLOT_IS_ACCESSOR(h, i)) {
			mark_heaphdr(heap, (duk_heaphdr *) DUK_HOBJECT_E_GET_VALUE_PTR(h, i)->a.get);
			mark_heaphdr(heap, (duk_heaphdr *) DUK_HOBJECT_E_GET_VALUE_PTR(h, i)->a.set);
		} else {
			mark_tval(heap, &DUK_HOBJECT_E_GET_VALUE_PTR(h, i)->v);
		}
	}

	for (i = 0; i < h->a_size; i++) {
		mark_tval(heap, DUK_HOBJECT_A_GET_VALUE_PTR(h, i));
	}

	/* hash part is a 'weak reference' and does not contribute */

	mark_heaphdr(heap, (duk_heaphdr *) h->prototype);

	if (DUK_HOBJECT_IS_COMPILEDFUNCTION(h)) {
		duk_hcompiledfunction *f = (duk_hcompiledfunction *) h;
		duk_tval *tv, *tv_end;
		duk_hobject **funcs, **funcs_end;

		/* 'data' is reachable through every compiled function which
		 * contains a reference.
		 */

		mark_heaphdr(heap, (duk_heaphdr *) f->data);

		tv = DUK_HCOMPILEDFUNCTION_GET_CONSTS_BASE(f);
		tv_end = DUK_HCOMPILEDFUNCTION_GET_CONSTS_END(f);
		while (tv < tv_end) {
			mark_tval(heap, tv);
			tv++;
		}

		funcs = DUK_HCOMPILEDFUNCTION_GET_FUNCS_BASE(f);
		funcs_end = DUK_HCOMPILEDFUNCTION_GET_FUNCS_END(f);
		while (funcs < funcs_end) {
			mark_heaphdr(heap, (duk_heaphdr *) *funcs);
			funcs++;
		}
	} else if (DUK_HOBJECT_IS_NATIVEFUNCTION(h)) {
		duk_hnativefunction *f = (duk_hnativefunction *) h;
		f = f;  /* suppress warning */
		/* nothing to mark */
	} else if (DUK_HOBJECT_IS_THREAD(h)) {
		duk_hthread *t = (duk_hthread *) h;
		duk_tval *tv;

		tv = t->valstack;
		while (tv < t->valstack_end) {
			mark_tval(heap, tv);
			tv++;
		}

		for (i = 0; i < t->callstack_top; i++) {
			duk_activation *act = &t->callstack[i];
			mark_heaphdr(heap, (duk_heaphdr *) act->func);
			mark_heaphdr(heap, (duk_heaphdr *) act->var_env);
			mark_heaphdr(heap, (duk_heaphdr *) act->lex_env);
		}

#if 0  /* nothing now */
		for (i = 0; i < t->catchstack_top; i++) {
			duk_catcher *cat = &t->catchstack[i];
		}
#endif

		mark_heaphdr(heap, (duk_heaphdr *) t->resumer);

		for (i = 0; i < DUK_NUM_BUILTINS; i++) {
			mark_heaphdr(heap, (duk_heaphdr *) t->builtins[i]);
		}
	}
}

/* recursion tracking happens here only */
static void mark_heaphdr(duk_heap *heap, duk_heaphdr *h) {
	DUK_DDDPRINT("mark_heaphdr %p, type %d",
	             (void *) h,
	             h ? DUK_HEAPHDR_GET_TYPE(h) : -1);
	if (!h) {
		return;
	}

	if (DUK_HEAPHDR_HAS_REACHABLE(h)) {
		DUK_DDDPRINT("already marked reachable, skip");
		return;
	}
	DUK_HEAPHDR_SET_REACHABLE(h);

	if (heap->mark_and_sweep_recursion_depth >= heap->mark_and_sweep_recursion_limit) {
		/* log this with a normal debug level because this should be relatively rare */
		DUK_DPRINT("mark-and-sweep recursion limit reached, marking as temproot: %p", (void *) h);
		DUK_HEAP_SET_MARKANDSWEEP_RECLIMIT_REACHED(heap);
		DUK_HEAPHDR_SET_TEMPROOT(h);
		return;
	}

	heap->mark_and_sweep_recursion_depth++;

	switch (DUK_HEAPHDR_GET_TYPE(h)) {
	case DUK_HTYPE_STRING:
		mark_hstring(heap, (duk_hstring *) h);
		break;
	case DUK_HTYPE_OBJECT:
		mark_hobject(heap, (duk_hobject *) h);
		break;
	case DUK_HTYPE_BUFFER:
		/* nothing to mark */
		break;
	default:
		DUK_DPRINT("attempt to mark heaphdr %p with invalid htype %d", (void *) h, (int) DUK_HEAPHDR_GET_TYPE(h));
		DUK_NEVER_HERE();
	}

	heap->mark_and_sweep_recursion_depth--;
}

static void mark_tval(duk_heap *heap, duk_tval *tv) {
	DUK_DDDPRINT("mark_tval %p", (void *) tv);
	if (!tv) {
		return;
	}
	if (DUK_TVAL_IS_HEAP_ALLOCATED(tv)) {
		mark_heaphdr(heap, DUK_TVAL_GET_HEAPHDR(tv)); 
	}
}

/*
 *  Mark the heap.
 */

static void mark_roots_heap(duk_heap *heap) {
	int i;

	DUK_DDPRINT("mark_roots_heap: %p", (void *) heap);

	mark_heaphdr(heap, (duk_heaphdr *) heap->heap_thread);
	mark_heaphdr(heap, (duk_heaphdr *) heap->heap_object);

	for (i = 0; i < DUK_HEAP_NUM_STRINGS; i++) {
		duk_hstring *h = heap->strs[i];
		mark_heaphdr(heap, (duk_heaphdr *) h);
	}

	/* heap->lj.errhandler: not marked, because a borrowed reference
	 * (actual value is required to be in an active part of some
	 * valstack)
	 */

	mark_tval(heap, &heap->lj.value1);
	mark_tval(heap, &heap->lj.value2);
}

/*
 *  Mark refzero_list objects.
 *
 *  Objects on the refzero_list have no inbound references.  They might have
 *  outbound references to objects that we might free, which would invalidate
 *  any references held by the refzero objects.  A refzero object might also
 *  be rescued by refcount finalization.  Refzero objects are treated as
 *  reachability roots to ensure they (or anything they point to) are not
 *  freed in mark-and-sweep.
 */

#ifdef DUK_USE_REFERENCE_COUNTING
static void mark_refzero_list(duk_heap *heap) {
	duk_heaphdr *hdr;

	DUK_DDPRINT("mark_refzero_list: %p", (void *) heap);

	hdr = heap->refzero_list;
	while (hdr) {
		mark_heaphdr(heap, hdr);
		hdr = DUK_HEAPHDR_GET_NEXT(hdr);
	}
}
#endif

/*
 *  Mark unreachable, finalizable objects.
 *
 *  Such objects will be moved aside and their finalizers run later.  They have
 *  to be treated as reachability roots for their properties etc to remain
 *  allocated.  This marking is only done for unreachable values which would
 *  be swept later (refzero_list is thus excluded).
 *
 *  Objects are first marked FINALIZABLE and only then marked as reachability
 *  roots; otherwise circular references might be handled inconsistently.
 */

static void mark_finalizable(duk_heap *heap) {
	duk_hthread *thr;
	duk_heaphdr *hdr;
	int count_finalizable = 0;

	DUK_DDPRINT("mark_finalizable: %p", (void *) heap);

	/* FIXME: placeholder */
	thr = get_temp_hthread(heap);
	DUK_ASSERT(thr != NULL);

	hdr = heap->heap_allocated;
	while (hdr) {
		if (!DUK_HEAPHDR_HAS_REACHABLE(hdr) &&
		    DUK_HEAPHDR_GET_TYPE(hdr) == DUK_HTYPE_OBJECT &&
		    !DUK_HEAPHDR_HAS_FINALIZED(hdr) &&
		    duk_hobject_hasprop_raw(thr, (duk_hobject *) hdr, DUK_HTHREAD_STRING_INT_FINALIZER(thr))) {

			/* heaphdr:
			 *  - is not reachable
			 *  - is an object
			 *  - is not a finalized object
			 *  - has a finalizer
			 */

			DUK_DDPRINT("unreachable heap object will be finalized -> mark as finalizable and treat as a reachability root: %p", hdr);
			DUK_HEAPHDR_SET_FINALIZABLE(hdr);
			count_finalizable ++;
		}

		hdr = DUK_HEAPHDR_GET_NEXT(hdr);
	}

	if (count_finalizable == 0) {
		return;
	}

	DUK_DDPRINT("marked %d heap objects as finalizable, now mark them reachable", count_finalizable);

	hdr = heap->heap_allocated;
	while (hdr) {
		if (DUK_HEAPHDR_HAS_FINALIZABLE(hdr)) {
			mark_heaphdr(heap, hdr);
		}

		hdr = DUK_HEAPHDR_GET_NEXT(hdr);
	}

	/* Caller will finish the marking process if we hit a recursion limit. */
}

/*
 *  Fallback marking handler if recursion limit is reached.
 *
 *  Iterates 'temproots' until recursion limit is no longer hit.  Note
 *  that temproots may reside either in heap allocated list or the
 *  refzero work list.  This is a slow scan, but guarantees that we
 *  finish with a bounded C stack.
 *
 *  Note that nodes may have been marked as temproots before this
 *  scan begun, OR they may have been marked during the scan (as
 *  we process nodes recursively also during the scan).  This is
 *  intended behavior.
 */

#ifdef DUK_USE_DEBUG
static void handle_temproot(duk_heap *heap, duk_heaphdr *hdr, int *count) {
#else
static void handle_temproot(duk_heap *heap, duk_heaphdr *hdr) {
#endif
	if (!DUK_HEAPHDR_HAS_TEMPROOT(hdr)) {
		DUK_DDDPRINT("not a temp root: %p", (void *) hdr);
		return;
	}

	DUK_DDDPRINT("found a temp root: %p", (void *) hdr);
	DUK_HEAPHDR_CLEAR_TEMPROOT(hdr);
	DUK_HEAPHDR_CLEAR_REACHABLE(hdr);  /* done so that mark_heaphdr() works correctly */
	mark_heaphdr(heap, hdr);

#ifdef DUK_USE_DEBUG
	(*count)++;
#endif
}

static void mark_temproots_by_heap_scan(duk_heap *heap) {
	duk_heaphdr *hdr;
#ifdef DUK_USE_DEBUG
	int count;
#endif

	DUK_DDPRINT("mark_temproots_by_heap_scan: %p", (void *) heap);

	while (DUK_HEAP_HAS_MARKANDSWEEP_RECLIMIT_REACHED(heap)) {
		DUK_DDPRINT("recursion limit reached, doing heap scan to continue from temproots");

#ifdef DUK_USE_DEBUG
		count = 0;
#endif
		DUK_HEAP_CLEAR_MARKANDSWEEP_RECLIMIT_REACHED(heap);

		hdr = heap->heap_allocated;
		while (hdr) {
#ifdef DUK_USE_DEBUG
			handle_temproot(heap, hdr, &count);
#else
			handle_temproot(heap, hdr);
#endif
			hdr = DUK_HEAPHDR_GET_NEXT(hdr);
		}

		/* must also check refzero_list */
#ifdef DUK_USE_REFERENCE_COUNTING
		hdr = heap->refzero_list;
		while (hdr) {
#ifdef DUK_USE_DEBUG
			handle_temproot(heap, hdr, &count);
#else
			handle_temproot(heap, hdr);
#endif
			hdr = DUK_HEAPHDR_GET_NEXT(hdr);
		}
#endif  /* DUK_USE_REFERENCE_COUNTING */

#ifdef DUK_USE_DEBUG
		DUK_DDPRINT("temproot mark heap scan processed %d temp roots", count);
#endif
	}
}

/*
 *  Finalize refcounts for heap elements just about to be freed.
 *  This must be done for all objects before freeing to avoid any
 *  stale pointer dereferences.
 *
 *  Note that this must deduce the set of objects to be freed
 *  identically to sweep_heap().
 */

#ifdef DUK_USE_REFERENCE_COUNTING
static void finalize_refcounts(duk_heap *heap) {
	duk_hthread *thr;
	duk_heaphdr *hdr;

	/* FIXME: placeholder */
	thr = get_temp_hthread(heap);
	DUK_ASSERT(thr != NULL);

	DUK_DDPRINT("finalize_refcounts: heap=%p, hthread=%p",
	            (void *) heap, (void *) thr);

	hdr = heap->heap_allocated;
	while (hdr) {
		if (!DUK_HEAPHDR_HAS_REACHABLE(hdr)) {
			/*
			 *  Unreachable object about to be swept.  Finalize target refcounts
			 *  (objects which the unreachable object points to) without doing
			 *  refzero processing.  Recursive decrefs are also prevented when
			 *  refzero processing is disabled.
			 *
			 *  Value cannot be a finalizable object, as they have been made
			 *  temporarily reachable for this round.
			 */

			DUK_DDDPRINT("unreachable object, refcount finalize before sweeping: %p", (void *) hdr);
			duk_heap_refcount_finalize_heaphdr(thr, hdr);
		}

		hdr = DUK_HEAPHDR_GET_NEXT(hdr);
	}
}
#endif  /* DUK_USE_REFERENCE_COUNTING */

/*
 *  Clear (reachable) flags of refzero work list.
 */

#ifdef DUK_USE_REFERENCE_COUNTING
static void clear_refzero_list_flags(duk_heap *heap) {
	duk_heaphdr *hdr;

	DUK_DDPRINT("clear_refzero_list_flags: %p", (void *) heap);

	hdr = heap->refzero_list;
	while (hdr) {
		DUK_HEAPHDR_CLEAR_REACHABLE(hdr);
		DUK_ASSERT(!DUK_HEAPHDR_HAS_FINALIZABLE(hdr));
		DUK_ASSERT(!DUK_HEAPHDR_HAS_FINALIZED(hdr));
		DUK_ASSERT(!DUK_HEAPHDR_HAS_TEMPROOT(hdr));
		hdr = DUK_HEAPHDR_GET_NEXT(hdr);
	}
}
#endif  /* DUK_USE_REFERENCE_COUNTING */

/*
 *  Sweep stringtable
 */

static void sweep_stringtable(duk_heap *heap) {
	duk_hstring *h;
	int i;
#ifdef DUK_USE_DEBUG
	int count_free = 0;
	int count_keep = 0;
#endif

	DUK_DDPRINT("sweep_stringtable: %p", (void *) heap);

	for (i = 0; i < heap->st_size; i++) {
		h = heap->st[i];
		if (h == NULL || h == DUK_STRTAB_DELETED_MARKER(heap)) {
			continue;
		} else if (DUK_HEAPHDR_HAS_REACHABLE((duk_heaphdr *) h)) {
#ifdef DUK_USE_DEBUG
			count_keep++;
#endif
			continue;
		}

#ifdef DUK_USE_DEBUG
		count_free++;
#endif

#if defined(DUK_USE_DEBUG) && defined(DUK_USE_REFERENCE_COUNTING)
		/* Non-zero refcounts should not happen for unreachable strings,
		 * because we refcount finalize all unreachable objects which
		 * should have decreased unreachable string refcounts to zero
		 * (even for cycles).
		 */
		DUK_ASSERT(DUK_HEAPHDR_GET_REFCOUNT((duk_heaphdr *) h) == 0);
#endif

		DUK_DDDPRINT("sweep string, not reachable: %p", (void *) h);

		/* deal with weak references first */
		duk_heap_strcache_string_remove(heap, (duk_hstring *) h);

		/* remove the string (mark DELETED), could also call
		 * duk_heap_string_remove() but that would be slow and
		 * pointless because we already know the slot.
		 */
		heap->st[i] = DUK_STRTAB_DELETED_MARKER(heap);

		/* then free */
#if 1
		DUK_FREE(heap, (duk_heaphdr *) h);  /* no inner refs/allocs, just free directly */
#else
		duk_heap_free_heaphdr_raw(heap, (duk_heaphdr *) h);  /* this would be OK but unnecessary */
#endif
	}

#ifdef DUK_USE_DEBUG
	DUK_DPRINT("mark-and-sweep sweep stringtable: %d freed, %d kept", count_free, count_keep);
#endif
}

/*
 *  Sweep heap
 */

static void sweep_heap(duk_heap *heap, int flags) {
	duk_heaphdr *prev;  /* last element that was left in the heap */
	duk_heaphdr *curr;
	duk_heaphdr *next;
#ifdef DUK_USE_DEBUG
	int count_free = 0;
	int count_keep = 0;
	int count_finalize = 0;
	int count_rescue = 0;
#endif

	DUK_DDPRINT("sweep_heap: %p", (void *) heap);

	prev = NULL;
	curr = heap->heap_allocated;
	heap->heap_allocated = NULL;
	while (curr) {
		/* strings are never placed on the heap allocated list */
		DUK_ASSERT(DUK_HEAPHDR_GET_TYPE(curr) != DUK_HTYPE_STRING);

		next = DUK_HEAPHDR_GET_NEXT(curr);

		if (DUK_HEAPHDR_HAS_REACHABLE(curr)) {
			/*
			 *  Reachable object, keep
			 */

			DUK_DDDPRINT("sweep, reachable: %p", (void *) curr);

			if (DUK_HEAPHDR_HAS_FINALIZABLE(curr)) {
				/*
				 *  If object has been marked finalizable, move it to the
				 *  "to be finalized" work list.  It will be collected on
				 *  the next mark-and-sweep if it is still unreachable
				 *  after running the finalizer.
				 */

				DUK_ASSERT(!DUK_HEAPHDR_HAS_FINALIZED(curr));
				DUK_ASSERT(DUK_HEAPHDR_GET_TYPE(curr) == DUK_HTYPE_OBJECT);
				DUK_DDDPRINT("object has finalizer, move to finalization work list: %p", (void *) curr);

#ifdef DUK_USE_DOUBLE_LINKED_HEAP
				if (heap->finalize_list) {
					DUK_HEAPHDR_SET_PREV(heap->finalize_list, curr);
				}
				DUK_HEAPHDR_SET_PREV(curr, NULL);
#endif
				DUK_HEAPHDR_SET_NEXT(curr, heap->finalize_list);
				heap->finalize_list = curr;
#ifdef DUK_USE_DEBUG
				count_finalize++;
#endif
			} else {
				/*
				 *  Object will be kept; queue object back to heap_allocated (to tail)
				 */

				if (DUK_HEAPHDR_HAS_FINALIZED(curr)) {
					/*
					 *  Object's finalizer was executed on last round, and
					 *  object has been happily rescued.
					 */

					DUK_ASSERT(!DUK_HEAPHDR_HAS_FINALIZABLE(curr));
					DUK_ASSERT(DUK_HEAPHDR_GET_TYPE(curr) == DUK_HTYPE_OBJECT);
					DUK_DDPRINT("object rescued during mark-and-sweep finalization: %p", (void *) curr);
#ifdef DUK_USE_DEBUG
					count_rescue++;
#endif
				} else {
					/*
					 *  Plain, boring reachable object.
					 */
#ifdef DUK_USE_DEBUG
					count_keep++;
#endif
				}

				if (!heap->heap_allocated) {
					heap->heap_allocated = curr;
				}
				if (prev) {
					DUK_HEAPHDR_SET_NEXT(prev, curr);
				}
#ifdef DUK_USE_DOUBLE_LINKED_HEAP
				DUK_HEAPHDR_SET_PREV(curr, prev);
#endif
				prev = curr;
			}

			DUK_HEAPHDR_CLEAR_REACHABLE(curr);
			DUK_HEAPHDR_CLEAR_FINALIZED(curr);
			DUK_HEAPHDR_CLEAR_FINALIZABLE(curr);

			DUK_ASSERT(!DUK_HEAPHDR_HAS_REACHABLE(curr));
			DUK_ASSERT(!DUK_HEAPHDR_HAS_FINALIZED(curr));
			DUK_ASSERT(!DUK_HEAPHDR_HAS_FINALIZABLE(curr));

			curr = next;
		} else {
			/*
			 *  Unreachable object, free
			 */

			DUK_DDDPRINT("sweep, not reachable: %p", (void *) curr);

#if defined(DUK_USE_DEBUG) && defined(DUK_USE_REFERENCE_COUNTING)
			/* Non-zero refcounts should not happen because we refcount
			 * finalize all unreachable objects which should cancel out
			 * refcounts (even for cycles).
			 */
			DUK_ASSERT(DUK_HEAPHDR_GET_REFCOUNT(curr) == 0);
#endif
			DUK_ASSERT(!DUK_HEAPHDR_HAS_FINALIZABLE(curr));

			if (DUK_HEAPHDR_HAS_FINALIZED(curr)) {
				DUK_DDDPRINT("finalized object not rescued: %p", (void *) curr);
			}

			/* Note: object cannot be a finalizable unreachable object, as
			 * they have been marked temporarily reachable for this round,
			 * and are handled above.
			 */

#ifdef DUK_USE_DEBUG
			count_free++;
#endif

			/* weak refs should be handled here, but no weak refs for
			 * any non-string objects exist right now.
			 */

			/* free object and all auxiliary (non-heap) allocs */
			duk_heap_free_heaphdr_raw(heap, curr);

			curr = next;
		}
	}
	if (prev) {
		DUK_HEAPHDR_SET_NEXT(prev, NULL);
	}

#ifdef DUK_USE_DEBUG
	DUK_DPRINT("mark-and-sweep sweep objects (non-string): %d freed, %d kept, %d rescued, %d queued for finalization",
	            count_free, count_keep, count_rescue, count_finalize);
#endif
}

/*
 *  Run (object) finalizers in the "to be finalized" work list.
 */

static void run_object_finalizers(duk_heap *heap) {
	duk_heaphdr *curr;
	duk_heaphdr *next;
#ifdef DUK_USE_DEBUG
	int count = 0;
#endif
	duk_hthread *thr;

	DUK_DDPRINT("run_object_finalizers: %p", (void *) heap);

	/* FIXME: placeholder */
	thr = get_temp_hthread(heap);
	DUK_ASSERT(thr != NULL);

	curr = heap->finalize_list;
	while (curr) {
		DUK_DDDPRINT("mark-and-sweep finalize: %p", (void *) curr);

		DUK_ASSERT(DUK_HEAPHDR_GET_TYPE(curr) == DUK_HTYPE_OBJECT);  /* only objects have finalizers */
		DUK_ASSERT(!DUK_HEAPHDR_HAS_REACHABLE(curr));                /* flags have been already cleared */
		DUK_ASSERT(!DUK_HEAPHDR_HAS_TEMPROOT(curr));
		DUK_ASSERT(!DUK_HEAPHDR_HAS_FINALIZABLE(curr));
		DUK_ASSERT(!DUK_HEAPHDR_HAS_FINALIZED(curr));

		/* run the finalizer */
		duk_hobject_run_finalizer(thr, (duk_hobject *) curr);  /* must never longjmp */

		/* mark FINALIZED, for next mark-and-sweep (will collect unless has become reachable;
		 * prevent running finalizer again if reachable)
		 */
		DUK_HEAPHDR_SET_FINALIZED(curr);

		/* queue back to heap_allocated */
		next = DUK_HEAPHDR_GET_NEXT(curr);
		DUK_HEAP_INSERT_INTO_HEAP_ALLOCATED(heap, curr);

		curr = next;
#ifdef DUK_USE_DEBUG
		count++;
#endif
	}

	/* finalize_list will always be processed completely */
	heap->finalize_list = NULL;

#ifdef DUK_USE_DEBUG
	DUK_DPRINT("mark-and-sweep finalize objects: %d finalizers called", count);
#endif
}

/*
 *  Object compaction.
 *
 *  Compaction is assumed to never throw an error.
 */

static int _protected_compact_object(duk_context *ctx) {
	/* FIXME: for threads, compact value stack, call stack, catch stack? */

	duk_hobject *obj = duk_get_hobject(ctx, -1);
	DUK_ASSERT(obj != NULL);
	duk_hobject_compact_props((duk_hthread *) ctx, obj);
	return 0;
}

#ifdef DUK_USE_DEBUG
static void compact_object_list(duk_heap *heap, duk_hthread *thr, duk_heaphdr *start, int *p_count_check, int *p_count_compact, int *p_count_bytes_saved) {
#else
static void compact_object_list(duk_heap *heap, duk_hthread *thr, duk_heaphdr *start) {
#endif
	duk_heaphdr *curr;
#ifdef DUK_USE_DEBUG
	size_t old_size, new_size;
#endif
	duk_hobject *obj;

	curr = start;
	while (curr) {
		DUK_DDDPRINT("mark-and-sweep compact: %p", (void *) curr);

		if (DUK_HEAPHDR_GET_TYPE(curr) != DUK_HTYPE_OBJECT) {
			goto next;	
		}
		obj = (duk_hobject *) curr;

#ifdef DUK_USE_DEBUG
		old_size = DUK_HOBJECT_P_COMPUTE_SIZE(obj->e_size, obj->a_size, obj->h_size);
#endif

		DUK_DDPRINT("compact object: %p", (void *) obj);
		duk_push_hobject((duk_context *) thr, obj);
		duk_safe_call((duk_context *) thr, _protected_compact_object, 1, 0, DUK_INVALID_INDEX);  /* XXX: replace errhandler with NULL? */

#ifdef DUK_USE_DEBUG
		new_size = DUK_HOBJECT_P_COMPUTE_SIZE(obj->e_size, obj->a_size, obj->h_size);
#endif

#ifdef DUK_USE_DEBUG
		(*p_count_compact)++;
		(*p_count_bytes_saved) += old_size - new_size;
#endif

	 next:
		curr = DUK_HEAPHDR_GET_NEXT(curr);
#ifdef DUK_USE_DEBUG
		(*p_count_check)++;
#endif
	}
}

static void compact_objects(duk_heap *heap) {
	/* FIXME: which lists should participate?  to be finalized? */
#ifdef DUK_USE_DEBUG
	int count_check = 0;
	int count_compact = 0;
	int count_bytes_saved = 0;
#endif
	duk_hthread *thr;

	DUK_DDPRINT("compact_objects: %p", (void *) heap);

	/* FIXME: placeholder */
	thr = get_temp_hthread(heap);
	DUK_ASSERT(thr != NULL);

#ifdef DUK_USE_DEBUG
	compact_object_list(heap, thr, heap->heap_allocated, &count_check, &count_compact, &count_bytes_saved);
	compact_object_list(heap, thr, heap->finalize_list, &count_check, &count_compact, &count_bytes_saved);
#ifdef DUK_USE_REFERENCE_COUNTING
	compact_object_list(heap, thr, heap->refzero_list, &count_check, &count_compact, &count_bytes_saved);
#endif
#else
	compact_object_list(heap, thr, heap->heap_allocated);
	compact_object_list(heap, thr, heap->finalize_list);
#ifdef DUK_USE_REFERENCE_COUNTING
	compact_object_list(heap, thr, heap->refzero_list);
#endif
#endif

#ifdef DUK_USE_DEBUG
	DUK_DPRINT("mark-and-sweep compact objects: %d checked, %d compaction attempts, %d bytes saved by compaction", count_check, count_compact, count_bytes_saved);
#endif
}

/*
 *  Resize stringtable.
 */

static void resize_stringtable(duk_heap *heap) {
	DUK_DDPRINT("resize_stringtable: %p", (void *) heap);
	duk_heap_force_stringtable_resize(heap);
}

/*
 *  Assertion helpers.
 */

#ifdef DUK_USE_ASSERTIONS
static void assert_heaphdr_flags(duk_heap *heap) {
	duk_heaphdr *hdr;

	hdr = heap->heap_allocated;
	while (hdr) {
		DUK_ASSERT(!DUK_HEAPHDR_HAS_REACHABLE(hdr));
		DUK_ASSERT(!DUK_HEAPHDR_HAS_TEMPROOT(hdr));
		DUK_ASSERT(!DUK_HEAPHDR_HAS_FINALIZABLE(hdr));
		/* may have FINALIZED */
		hdr = DUK_HEAPHDR_GET_NEXT(hdr);
	}

#ifdef DUK_USE_REFERENCE_COUNTING
	hdr = heap->refzero_list;
	while (hdr) {
		DUK_ASSERT(!DUK_HEAPHDR_HAS_REACHABLE(hdr));
		DUK_ASSERT(!DUK_HEAPHDR_HAS_TEMPROOT(hdr));
		DUK_ASSERT(!DUK_HEAPHDR_HAS_FINALIZABLE(hdr));
		DUK_ASSERT(!DUK_HEAPHDR_HAS_FINALIZED(hdr));
		hdr = DUK_HEAPHDR_GET_NEXT(hdr);
	}
#endif  /* DUK_USE_REFERENCE_COUNTING */
}

#ifdef DUK_USE_REFERENCE_COUNTING
static void assert_valid_refcounts(duk_heap *heap) {
	duk_heaphdr *hdr = heap->heap_allocated;
	while (hdr) {
		if (DUK_HEAPHDR_GET_REFCOUNT(hdr) == 0 &&
		    DUK_HEAPHDR_HAS_FINALIZED(hdr)) {
			/* An object may be in heap_allocated list with a zero
			 * refcount if it has just been finalized and is waiting
			 * to be collected by the next cycle.
			 */
		} else if (DUK_HEAPHDR_GET_REFCOUNT(hdr) == 0) {
			/* An object may be in heap_allocated list with a zero
			 * refcount also if it is a temporary object created by
			 * a finalizer; because finalization now runs inside
			 * mark-and-sweep, such objects will not be queued to
			 * refzero_list and will thus appear here with refcount
			 * zero.
			 */
		} else if (DUK_HEAPHDR_GET_REFCOUNT(hdr) < 0) {
			DUK_DPRINT("invalid refcount: %d, %p -> %!O",
			           (hdr != NULL ? DUK_HEAPHDR_GET_REFCOUNT(hdr) : 0), (void *) hdr, hdr);
			DUK_ASSERT(DUK_HEAPHDR_GET_REFCOUNT(hdr) > 0);
		}
		hdr = DUK_HEAPHDR_GET_NEXT(hdr);
	}
}
#endif  /* DUK_USE_REFERENCE_COUNTING */
#endif  /* DUK_USE_ASSERTIONS */

/*
 *  Main mark-and-sweep function.
 *
 *  'flags' represents the features requested by the caller.  The current
 *  heap->mark_and_sweep_base_flags is ORed automatically into the flags;
 *  the base flags mask typically prevents certain mark-and-sweep operations
 *  to avoid trouble.
 */

int duk_heap_mark_and_sweep(duk_heap *heap, int flags) {
	/* FIXME: thread selection for mark-and-sweep is currently a hack.
	 * If we don't have a thread, the entire mark-and-sweep is now
	 * skipped (although we could just skip finalizations).
	 */
	if (get_temp_hthread(heap) == NULL) {
		DUK_DPRINT("temporary hack: gc skipped because we don't have a temp thread");

		/* reset voluntary gc trigger count */
		heap->mark_and_sweep_trigger_counter = heap->mark_and_sweep_trigger_limit;
		return DUK_ERR_OK;
	}

	DUK_DPRINT("garbage collect (mark-and-sweep) starting, requested flags: 0x%08x, effective flags: 0x%08x",
	           flags, flags | heap->mark_and_sweep_base_flags);

	flags |= heap->mark_and_sweep_base_flags;

	/*
	 *  Assertions before
	 */

#ifdef DUK_USE_ASSERTIONS
	DUK_ASSERT(!DUK_HEAP_HAS_MARKANDSWEEP_RUNNING(heap));
	DUK_ASSERT(!DUK_HEAP_HAS_MARKANDSWEEP_RECLIMIT_REACHED(heap));
	DUK_ASSERT(heap->mark_and_sweep_recursion_depth == 0);
	DUK_ASSERT(heap->mark_and_sweep_recursion_limit >= 1);
	assert_heaphdr_flags(heap);
#ifdef DUK_USE_REFERENCE_COUNTING
	/* Note: DUK_HEAP_HAS_REFZERO_FREE_RUNNING(heap) may be true; a refcount
	 * finalizer may trigger a mark-and-sweep.
	 */
	assert_valid_refcounts(heap);
#endif  /* DUK_USE_REFERENCE_COUNTING */
#endif  /* DUK_USE_ASSERTIONS */

	/*
	 *  Begin
	 */

	DUK_HEAP_SET_MARKANDSWEEP_RUNNING(heap);

	/*
	 *  Mark roots, hoping that recursion limit is not normally hit.
	 *  If recursion limit is hit, run additional reachability rounds
	 *  starting from "temproots" until marking is complete.
	 *
	 *  Marking happens in two phases: first we mark actual reachability
	 *  roots (and run "temproots" to complete the process).  Then we
	 *  check which objects are unreachable and are finalizable; such
	 *  objects are marked as FINALIZABLE and marked as reachability
	 *  (and "temproots" is run again to complete the process).
	 */

	mark_roots_heap(heap);               /* main reachability roots */
#ifdef DUK_USE_REFERENCE_COUNTING
	mark_refzero_list(heap);             /* refzero_list treated as reachability roots */
#endif
	mark_temproots_by_heap_scan(heap);   /* temproots */

	mark_finalizable(heap);              /* mark finalizable as reachability roots */
	mark_temproots_by_heap_scan(heap);   /* temproots */

	/*
	 *  Sweep garbage and remove marking flags, and move objects with
	 *  finalizers to the finalizer work list.
	 *
	 *  Objects to be swept need to get their refcounts finalized before
	 *  they are swept.  In other words, their target object refcounts
	 *  need to be decreased.  This has to be done before freeing any
	 *  objects to avoid decref'ing dangling pointers (which may happen
	 *  even without bugs, e.g. with reference loops)
	 *
	 *  Because strings don't point to other heap objects, similar
	 *  finalization is not necessary for strings.
	 */

	/* FIXME: more emergency behavior, e.g. find smaller hash sizes etc */

#ifdef DUK_USE_REFERENCE_COUNTING
	finalize_refcounts(heap);
#endif
	sweep_heap(heap, flags);
	sweep_stringtable(heap);
#ifdef DUK_USE_REFERENCE_COUNTING
	clear_refzero_list_flags(heap);
#endif

	/*
	 *  Object compaction (emergency only).
	 *
	 *  Object compaction is a separate step after sweeping, as there is
	 *  more free memory for it to work with.  Also, currently compaction
	 *  may insert new objects into the heap allocated list and the string
	 *  table which we don't want to do during a sweep (the reachability
	 *  flags of such objects would be incorrect).  The objects inserted
	 *  are currently:
	 *
	 *    - a temporary duk_hbuffer for a new properties allocation
	 *    - if array part is abandoned, string keys are interned
	 *
	 *  The object insertions go to the front of the list, so they do not
	 *  cause an infinite loop (they are not compacted).
	 */

	if ((flags & DUK_MS_FLAG_EMERGENCY) &&
	    !(flags & DUK_MS_FLAG_NO_OBJECT_COMPACTION)) {
		compact_objects(heap);
	}

	/*
	 *  String table resize check.
	 *
	 *  Note: this may silently (and safely) fail if GC is caused by an
	 *  allocation call in stringtable resize_hash().  Resize_hash()
	 *  will prevent a recursive call to itself by setting the
	 *  DUK_MS_FLAG_NO_STRINGTABLE_RESIZE in heap->mark_and_sweep_base_flags.
	 */

	/* FIXME: stringtable emergency compaction? */

	if (!(flags & DUK_MS_FLAG_NO_STRINGTABLE_RESIZE)) {
		resize_stringtable(heap);
	} else {
		DUK_DPRINT("stringtable resize skipped because DUK_MS_FLAG_NO_STRINGTABLE_RESIZE is set");
	}

	/*
	 *  Finalize objects in the finalization work list.  Finalized
	 *  objects are queued back to heap_allocated with FINALIZED set.
	 *
	 *  Since finalizers may cause arbitrary side effects, they are
	 *  prevented during string table and object property allocation
	 *  resizing using the DUK_MS_FLAG_NO_FINALIZERS flag in
	 *  heap->mark_and_sweep_base_flags.
	 *
	 *  Finalization currently happens inside "MARKANDSWEEP_RUNNING"
	 *  protection (no mark-and-sweep may be triggered by the
	 *  finalizers).  As a side effect:
	 *
	 *    1) an out-of-memory error inside a finalizer will not
	 *       cause a mark-and-sweep and may cause the finalizer
	 *       to fail unnecessarily
	 *
	 *    2) any temporary objects whose refcount decreases to zero
	 *       during finalization will not be put into refzero_list;
	 *       they can only be collected by another mark-and-sweep
	 *
	 *  This is not optimal, but since the sweep for this phase has
	 *  already happened, this is probably good enough for now.
	 */

	if (!(flags & DUK_MS_FLAG_NO_FINALIZERS)) {
		run_object_finalizers(heap);
	} else {
		DUK_DPRINT("finalizer run skipped because DUK_MS_FLAG_NO_FINALIZERS is set");
	}

	/*
	 *  Finish
	 */

	DUK_HEAP_CLEAR_MARKANDSWEEP_RUNNING(heap);

	/*
	 *  Assertions after
	 */

#ifdef DUK_USE_ASSERTIONS
	DUK_ASSERT(!DUK_HEAP_HAS_MARKANDSWEEP_RUNNING(heap));
	DUK_ASSERT(!DUK_HEAP_HAS_MARKANDSWEEP_RECLIMIT_REACHED(heap));
	DUK_ASSERT(heap->mark_and_sweep_recursion_depth == 0);
	DUK_ASSERT(heap->mark_and_sweep_recursion_limit > 1);
	assert_heaphdr_flags(heap);
#ifdef DUK_USE_REFERENCE_COUNTING
	/* Note: DUK_HEAP_HAS_REFZERO_FREE_RUNNING(heap) may be true; a refcount
	 * finalizer may trigger a mark-and-sweep.
	 */
	assert_valid_refcounts(heap);
#endif  /* DUK_USE_REFERENCE_COUNTING */
#endif  /* DUK_USE_ASSERTIONS */

	/*
	 *  Reset trigger counter
	 */

	/* very simplistic now, should be relative to heap size */
	heap->mark_and_sweep_trigger_counter = heap->mark_and_sweep_trigger_limit;

	DUK_DPRINT("garbage collect (mark-and-sweep) finished (trigger reset to %d)",
	           heap->mark_and_sweep_trigger_counter);
	return DUK_ERR_OK;
}

#else

/* no mark-and-sweep gc */

#endif  /* DUK_USE_MARK_AND_SWEEP */

