/*
 *  Reference counting implementation.
 */

#include "duk_internal.h"

#ifdef DUK_USE_REFERENCE_COUNTING

#ifndef DUK_USE_DOUBLE_LINKED_HEAP
#error internal error, reference counting requires a double linked heap
#endif

/*
 *  Misc
 */

static void queue_refzero(duk_heap *heap, duk_heaphdr *hdr) {
	/* tail insert: don't disturb head in case refzero is running */

	if (heap->refzero_list != NULL) {
		duk_heaphdr *hdr_prev;

		hdr_prev = heap->refzero_list_tail;
		DUK_ASSERT(hdr_prev != NULL);
		DUK_ASSERT(DUK_HEAPHDR_GET_NEXT(hdr_prev) == NULL);

		DUK_HEAPHDR_SET_NEXT(hdr, NULL);
		DUK_HEAPHDR_SET_PREV(hdr, hdr_prev);
		DUK_HEAPHDR_SET_NEXT(hdr_prev, hdr);
		heap->refzero_list_tail = hdr;
	} else {
		DUK_ASSERT(heap->refzero_list_tail == NULL);
		DUK_HEAPHDR_SET_NEXT(hdr, NULL);
		DUK_HEAPHDR_SET_PREV(hdr, NULL);
		heap->refzero_list = hdr;
		heap->refzero_list_tail = hdr;
	}
}

/*
 *  Heap object refcount finalization.
 *
 *  When an object is about to be freed, all other objects it refers to must
 *  be decref'd.  Refcount finalization does NOT free the object or its inner
 *  allocations (mark-and-sweep shares these helpers), it just manipulates
 *  the refcounts.
 *
 *  Note that any of the decref's may cause a refcount to drop to zero, BUT
 *  it will not be processed inline; instead, because refzero is already
 *  running, the objects will just be queued to refzero list and processed
 *  later.  This eliminates C recursion.
 */

static void refcount_finalize_hobject(duk_hthread *thr, duk_hobject *h) {
	int i;

	DUK_ASSERT(h);
	DUK_ASSERT(DUK_HEAPHDR_GET_TYPE((duk_heaphdr *) h) == DUK_HTYPE_OBJECT);

	/* XXX: better to get base and walk forwards? */

	for (i = 0; i < h->e_used; i++) {
		duk_hstring *key = DUK_HOBJECT_E_GET_KEY(h, i);
		if (!key) {
			continue;
		}
		duk_heap_heaphdr_decref(thr, (duk_heaphdr *) key);
		if (DUK_HOBJECT_E_SLOT_IS_ACCESSOR(h, i)) {
			duk_heap_heaphdr_decref(thr, (duk_heaphdr *) DUK_HOBJECT_E_GET_VALUE_GETTER(h, i));
			duk_heap_heaphdr_decref(thr, (duk_heaphdr *) DUK_HOBJECT_E_GET_VALUE_SETTER(h, i));
		} else {
			duk_heap_tval_decref(thr, DUK_HOBJECT_E_GET_VALUE_TVAL_PTR(h, i));
		}
	}

	for (i = 0; i < h->a_size; i++) {
		duk_heap_tval_decref(thr, DUK_HOBJECT_A_GET_VALUE_PTR(h, i));
	}

	/* hash part is a 'weak reference' and does not contribute */

	duk_heap_heaphdr_decref(thr, (duk_heaphdr *) h->prototype);

	if (DUK_HOBJECT_IS_COMPILEDFUNCTION(h)) {
		duk_hcompiledfunction *f = (duk_hcompiledfunction *) h;
		duk_tval *tv, *tv_end;
		duk_hobject **funcs, **funcs_end;

		DUK_ASSERT(f->data != NULL);  /* compiled functions must be created 'atomically' */

		tv = DUK_HCOMPILEDFUNCTION_GET_CONSTS_BASE(f);
		tv_end = DUK_HCOMPILEDFUNCTION_GET_CONSTS_END(f);
		while (tv < tv_end) {
			duk_heap_tval_decref(thr, tv);
			tv++;
		}

		funcs = DUK_HCOMPILEDFUNCTION_GET_FUNCS_BASE(f);
		funcs_end = DUK_HCOMPILEDFUNCTION_GET_FUNCS_END(f);
		while (funcs < funcs_end) {
			duk_heap_heaphdr_decref(thr, (duk_heaphdr *) *funcs);
			funcs++;
		}

		duk_heap_heaphdr_decref(thr, (duk_heaphdr *) f->data);
	} else if (DUK_HOBJECT_IS_NATIVEFUNCTION(h)) {
		duk_hnativefunction *f = (duk_hnativefunction *) h;
		DUK_UNREF(f);
		/* nothing to finalize */
	} else if (DUK_HOBJECT_IS_THREAD(h)) {
		duk_hthread *t = (duk_hthread *) h;
		duk_tval *tv;

		tv = t->valstack;
		while (tv < t->valstack_end) {
			duk_heap_tval_decref(thr, tv);
			tv++;
		}

		for (i = 0; i < t->callstack_top; i++) {
			duk_activation *act = &t->callstack[i];
			duk_heap_heaphdr_decref(thr, (duk_heaphdr *) act->func);
			duk_heap_heaphdr_decref(thr, (duk_heaphdr *) act->var_env);
			duk_heap_heaphdr_decref(thr, (duk_heaphdr *) act->lex_env);
		}

#if 0  /* nothing now */
		for (i = 0; i < t->catchstack_top; i++) {
			duk_catcher *cat = &t->catchstack[i];
		}
#endif

		for (i = 0; i < DUK_NUM_BUILTINS; i++) {
			duk_heap_heaphdr_decref(thr, (duk_heaphdr *) t->builtins[i]);
		}

		duk_heap_heaphdr_decref(thr, (duk_heaphdr *) t->resumer);
	}
}

void duk_heap_refcount_finalize_heaphdr(duk_hthread *thr, duk_heaphdr *hdr) {
	DUK_ASSERT(hdr);

	switch (DUK_HEAPHDR_GET_TYPE(hdr)) {
	case DUK_HTYPE_OBJECT:
		refcount_finalize_hobject(thr, (duk_hobject *) hdr);
		break;
	case DUK_HTYPE_BUFFER:
		/* nothing to finalize */
		break;
	case DUK_HTYPE_STRING:
		/* cannot happen: strings are not put into refzero list (they don't even have the next/prev pointers) */
	default:
		DUK_NEVER_HERE();
	}
}

/*
 *  Refcount memory freeing loop.
 *
 *  Frees objects in the refzero_pending list until the list becomes
 *  empty.  When an object is freed, its references get decref'd and
 *  may cause further objects to be queued for freeing.
 *
 *  This could be expanded to allow incremental freeing: just bail out
 *  early and resume at a future alloc/decref/refzero.
 */

static void refzero_free_pending(duk_hthread *thr) {
	duk_heaphdr *h1, *h2;
	duk_heap *heap;
	int count = 0;

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(thr->heap != NULL);
	heap = thr->heap;
	DUK_ASSERT(heap != NULL);

	/*
	 *  Detect recursive invocation
	 */

	if (DUK_HEAP_HAS_REFZERO_FREE_RUNNING(heap)) {
		DUK_DDDPRINT("refzero free running, skip run");
		return;
	}

	/*
	 *  Churn refzero_list until empty
	 */

	DUK_HEAP_SET_REFZERO_FREE_RUNNING(heap);
	while (heap->refzero_list) {
		duk_hobject *obj;
		int rescued = 0;

		/*
		 *  Pick an object from the head (don't remove yet).
		 */

		h1 = heap->refzero_list;
		obj = (duk_hobject *) h1;
		DUK_DDPRINT("refzero processing %p: %!O", h1, h1);
		DUK_ASSERT(DUK_HEAPHDR_GET_PREV(h1) == NULL);
		DUK_ASSERT(DUK_HEAPHDR_GET_TYPE(h1) == DUK_HTYPE_OBJECT);  /* currently, always the case */

		/*
		 *  Finalizer check.
		 *
		 *  Note: running a finalizer may have arbitrary side effects, e.g.
		 *  queue more objects on refzero_list (tail), or even trigger a
		 *  mark-and-sweep.
		 *
		 *  Note: quick reject check should match vast majority of
		 *  objects and must be safe (not throw any errors, ever).
		 */

		/* FIXME: If object has FINALIZED, it was finalized by mark-and-sweep on
		 * its previous run.  Any point in running finalizer again here?  If
		 * finalization semantics is changed so that finalizer is only run once,
		 * checking for FINALIZED would happen here.
		 */

		if (duk_hobject_hasprop_raw(thr, obj, DUK_HTHREAD_STRING_INT_FINALIZER(thr))) {
			DUK_DDDPRINT("object has a finalizer, run it");

			DUK_ASSERT(h1->h_refcount == 0);
			h1->h_refcount++;  /* bump refcount to prevent refzero during finalizer processing */

			duk_hobject_run_finalizer(thr, obj);  /* must never longjmp */

			h1->h_refcount--;  /* remove artificial bump */
			DUK_ASSERT(h1->h_refcount >= 0);

			if (h1->h_refcount != 0) {
				DUK_DDDPRINT("-> object refcount after finalization non-zero, object will be rescued");
				rescued = 1;
			} else {
				DUK_DDDPRINT("-> object refcount still zero after finalization, object will be freed");
			}
		}

  		/* Refzero head is still the same.  This is the case even if finalizer
		 * inserted more refzero objects; they are inserted to the tail.
		 */
		DUK_ASSERT(h1 == heap->refzero_list);

		/*
		 *  Remove the object from the refzero list.  This cannot be done
		 *  before a possible finalizer has been executed; the finalizer
		 *  may trigger a mark-and-sweep, and mark-and-sweep must be able
		 *  to traverse a complete refzero_list.
		 */

		h2 = DUK_HEAPHDR_GET_NEXT(h1);
		if (h2) {
			DUK_HEAPHDR_SET_PREV(h2, NULL);  /* not strictly necessary */
			heap->refzero_list = h2;
		} else {
			heap->refzero_list = NULL;
			heap->refzero_list_tail = NULL;
		}

		/*
		 *  Rescue or free.
		 */

		if (rescued) {
			/* yes -> move back to heap allocated */
			DUK_DDPRINT("object rescued during refcount finalization: %p", (void *) h1);
			DUK_HEAPHDR_SET_PREV(h1, NULL);
			DUK_HEAPHDR_SET_NEXT(h1, heap->heap_allocated);
			heap->heap_allocated = h1;
		} else {
			/* no -> decref members, then free */
			refcount_finalize_hobject(thr, obj);
			duk_heap_free_heaphdr_raw(heap, h1);
		}

		count++;
	}
	DUK_HEAP_CLEAR_REFZERO_FREE_RUNNING(heap);

	DUK_DDDPRINT("refzero processed %d objects", count);

	/*
	 *  Once the whole refzero cascade has been freed, check for
	 *  a voluntary mark-and-sweep.
	 */

#ifdef DUK_USE_MARK_AND_SWEEP
	heap->mark_and_sweep_trigger_counter -= count;
	if (heap->mark_and_sweep_trigger_counter <= 0) {
		int rc;
		int emergency = 0;
		DUK_DPRINT("refcount triggering mark-and-sweep");
		rc = duk_heap_mark_and_sweep(heap, emergency);
		DUK_UNREF(rc);
		DUK_DPRINT("refcount triggered mark-and-sweep => rc %d", rc);
	}
#endif
}

/*
 *  Incref and decref functions.
 *
 *  Decref may trigger immediate refzero handling, which may free and finalize
 *  an arbitrary number of objects.
 *  
 */

void duk_heap_tval_incref(duk_tval *tv) {
#if 0
	DUK_DDDPRINT("tval incref %p (%d->%d): %!T",
	             (void *) tv,
	             (tv != NULL && DUK_TVAL_IS_HEAP_ALLOCATED(tv) ? DUK_TVAL_GET_HEAPHDR(tv)->h_refcount : 0),
	             (tv != NULL && DUK_TVAL_IS_HEAP_ALLOCATED(tv) ? DUK_TVAL_GET_HEAPHDR(tv)->h_refcount + 1 : 0),
	             tv);
#endif

	if (!tv) {
		return;
	}

	if (DUK_TVAL_IS_HEAP_ALLOCATED(tv)) {
		duk_heaphdr *h = DUK_TVAL_GET_HEAPHDR(tv);
		if (h) {
			DUK_ASSERT(DUK_HEAPHDR_HTYPE_VALID(h));
			DUK_ASSERT(h->h_refcount >= 0);
			h->h_refcount++;
		}
	}
}

void duk_heap_tval_decref(duk_hthread *thr, duk_tval *tv) {
#if 0
	DUK_DDDPRINT("tval decref %p (%d->%d): %!T",
	             (void *) tv,
	             (tv != NULL && DUK_TVAL_IS_HEAP_ALLOCATED(tv) ? DUK_TVAL_GET_HEAPHDR(tv)->h_refcount : 0),
	             (tv != NULL && DUK_TVAL_IS_HEAP_ALLOCATED(tv) ? DUK_TVAL_GET_HEAPHDR(tv)->h_refcount - 1 : 0),
	             tv);
#endif

	if (!tv) {
		return;
	}

	if (DUK_TVAL_IS_HEAP_ALLOCATED(tv)) {
		duk_heap_heaphdr_decref(thr, DUK_TVAL_GET_HEAPHDR(tv));
	}
}

void duk_heap_heaphdr_incref(duk_heaphdr *h) {
#if 0
	DUK_DDDPRINT("heaphdr incref %p (%d->%d): %!O",
	             (void *) h,
	             (h != NULL ? h->h_refcount : 0),
	             (h != NULL ? h->h_refcount + 1 : 0),
	             h);
#endif

	if (!h) {
		return;
	}
	DUK_ASSERT(DUK_HEAPHDR_HTYPE_VALID(h));
	DUK_ASSERT(h->h_refcount >= 0);

	h->h_refcount++;
}

void duk_heap_heaphdr_decref(duk_hthread *thr, duk_heaphdr *h) {
	duk_heap *heap;

#if 0
	DUK_DDDPRINT("heaphdr decref %p (%d->%d): %!O",
	             (void *) h,
	             (h != NULL ? h->h_refcount : 0),
	             (h != NULL ? h->h_refcount - 1 : 0),
	             h);
#endif

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(thr->heap != NULL);

	if (!h) {
		return;
	}
	DUK_ASSERT(DUK_HEAPHDR_HTYPE_VALID(h));
	DUK_ASSERT(h->h_refcount >= 1);

	if (--h->h_refcount != 0) {
		return;
	}

	heap = thr->heap;
	DUK_DDDPRINT("refzero %p: %!O", (void *) h, h);

#ifdef DUK_USE_MARK_AND_SWEEP
	/*
	 *  If mark-and-sweep is running, don't process 'refzero' situations at all.
	 *  They may happen because mark-and-sweep needs to finalize refcounts for
	 *  each object it sweeps.  Otherwise the target objects of swept objects
	 *  would have incorrect refcounts.
	 *
	 *  Note: mark-and-sweep could use a separate decref handler to avoid coming
	 *  here at all.  However, mark-and-sweep may also call finalizers, which
	 *  can do arbitrary operations and would use this decref variant anyway.
	 */
	if (DUK_HEAP_HAS_MARKANDSWEEP_RUNNING(heap)) {
		DUK_DDDPRINT("refzero handling suppressed when mark-and-sweep running, object: %p", (void *) h);
		return;
	}
#endif

	switch (DUK_HEAPHDR_GET_TYPE(h)) {
	case DUK_HTYPE_STRING:
		/*
		 *  Strings have no internal references but do have "weak"
		 *  references in the string cache.  Also note that strings
		 *  are not on the heap_allocated list like other heap
		 *  elements.
		 */

		duk_heap_strcache_string_remove(heap, (duk_hstring *) h);
		duk_heap_string_remove(heap, (duk_hstring *) h);
		duk_heap_free_heaphdr_raw(heap, h);
		break;

	case DUK_HTYPE_OBJECT:
		/*
		 *  Objects have internal references.  Must finalize through
		 *  the "refzero" work list.
		 */

		duk_heap_remove_any_from_heap_allocated(heap, h);
		queue_refzero(heap, h);
		refzero_free_pending(thr);
		break;

	case DUK_HTYPE_BUFFER:
		/*
		 *  Buffers have no internal references.  However, a dynamic
		 *  buffer has a separate allocation for the buffer.  This is
		 *  freed by duk_heap_free_heaphdr_raw().
		 */

		duk_heap_remove_any_from_heap_allocated(heap, h);
		duk_heap_free_heaphdr_raw(heap, h);
		break;

	default:
		DUK_DPRINT("invalid heap type in decref: %d", (int) DUK_HEAPHDR_GET_TYPE(h));
		DUK_NEVER_HERE();
	}
}

#else

/* no refcounting */

#endif  /* DUK_USE_REFERENCE_COUNTING */

