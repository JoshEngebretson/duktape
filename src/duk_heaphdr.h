/*
 *  Heap header definition and assorted macros, including ref counting.
 *  Access all fields through the accessor macros.
 */

#ifndef DUK_HEAPHDR_H_INCLUDED
#define DUK_HEAPHDR_H_INCLUDED

#include "duk_forwdecl.h"
#include "duk_tval.h"

/*
 *  Common heap header
 *
 *  All heap objects share the same flags and refcount fields.  Objects other
 *  than strings also need to have a single or double linked list pointers
 *  for insertion into the "heap allocated" list.  Strings are held in the
 *  heap-wide string table so they don't need link pointers.
 *
 *  Technically, 'h_refcount' must be wide enough to guarantee that it cannot
 *  wrap (otherwise objects might be freed incorrectly after wrapping).  This
 *  means essentially that the refcount field must be as wide as data pointers.
 *  On 64-bit platforms this means that the refcount needs to be 64 bits even
 *  if an 'int' is 32 bits.  This is a bit unfortunate, and compromising on
 *  this might be reasonable in the future.
 *
 *  Heap header size on 32-bit platforms: 8 bytes without reference counting,
 *  16 bytes with reference counting.
 */

struct duk_heaphdr {
	duk_u32 h_flags;
#if defined(DUK_USE_REFERENCE_COUNTING)
	size_t h_refcount;
#endif
	duk_heaphdr *h_next;
#if defined(DUK_USE_DOUBLE_LINKED_HEAP)
	/* refcounting requires direct heap frees, which in turn requires a dual linked heap */
	duk_heaphdr *h_prev;
#endif
};

struct duk_heaphdr_string {
	duk_u32 h_flags;
#if defined(DUK_USE_REFERENCE_COUNTING)
	size_t h_refcount;
#endif
};

#define  DUK_HEAPHDR_FLAGS_TYPE_MASK      0x0000000fU
#define  DUK_HEAPHDR_FLAGS_FLAG_MASK      (~DUK_HEAPHDR_FLAGS_TYPE_MASK)

                                              /* 4 bits for heap type */
#define  DUK_HEAPHDR_FLAGS_HEAP_START     4   /* 6 heap flags */
#define  DUK_HEAPHDR_FLAGS_USER_START     10  /* 22 user flags */

#define  DUK_HEAPHDR_HEAP_FLAG_NUMBER(n)  (DUK_HEAPHDR_FLAGS_HEAP_START + (n))
#define  DUK_HEAPHDR_USER_FLAG_NUMBER(n)  (DUK_HEAPHDR_FLAGS_USER_START + (n))
#define  DUK_HEAPHDR_HEAP_FLAG(n)         (1 << (DUK_HEAPHDR_FLAGS_HEAP_START + (n)))
#define  DUK_HEAPHDR_USER_FLAG(n)         (1 << (DUK_HEAPHDR_FLAGS_USER_START + (n)))

#define  DUK_HEAPHDR_FLAG_REACHABLE       DUK_HEAPHDR_HEAP_FLAG(0)  /* mark-and-sweep: reachable */
#define  DUK_HEAPHDR_FLAG_TEMPROOT        DUK_HEAPHDR_HEAP_FLAG(1)  /* mark-and-sweep: children not processed */
#define  DUK_HEAPHDR_FLAG_FINALIZABLE     DUK_HEAPHDR_HEAP_FLAG(2)  /* mark-and-sweep: finalizable (on current pass) */
#define  DUK_HEAPHDR_FLAG_FINALIZED       DUK_HEAPHDR_HEAP_FLAG(3)  /* mark-and-sweep: finalized (on previous pass) */

#define  DUK_HTYPE_MIN                    1
#define  DUK_HTYPE_STRING                 1
#define  DUK_HTYPE_OBJECT                 2
#define  DUK_HTYPE_BUFFER                 3
#define  DUK_HTYPE_MAX                    3

#define  DUK_HEAPHDR_GET_NEXT(h)       ((h)->h_next)
#define  DUK_HEAPHDR_SET_NEXT(h,val)   do { \
		(h)->h_next = (val); \
	} while (0)

#if defined(DUK_USE_DOUBLE_LINKED_HEAP)
#define  DUK_HEAPHDR_GET_PREV(h)       ((h)->h_prev)
#define  DUK_HEAPHDR_SET_PREV(h,val)   do { \
		(h)->h_prev = (val); \
	} while (0)
#endif

#if defined(DUK_USE_REFERENCE_COUNTING)
#define  DUK_HEAPHDR_GET_REFCOUNT(h)   ((h)->h_refcount)
#define  DUK_HEAPHDR_SET_REFCOUNT(h,val)  do { \
		(h)->h_refcount = (val); \
	} while (0)
#else
/* refcount macros not defined without refcounting, caller must #ifdef now */
#endif  /* DUK_USE_REFERENCE_COUNTING */

/*
 *  Note: type is treated as a field separate from flags, so some masking is
 *  involved in the macros below.
 */

#define  DUK_HEAPHDR_GET_FLAGS(h)      ((h)->h_flags & DUK_HEAPHDR_FLAGS_FLAG_MASK)
#define  DUK_HEAPHDR_SET_FLAGS(h,val)  do { \
		(h)->h_flags = ((h)->h_flags & ~(DUK_HEAPHDR_FLAGS_FLAG_MASK)) | (val); \
	} while (0)

#define  DUK_HEAPHDR_GET_TYPE(h)       ((h)->h_flags & DUK_HEAPHDR_FLAGS_TYPE_MASK)
#define  DUK_HEAPHDR_SET_TYPE(h,val)   do { \
		(h)->h_flags = ((h)->h_flags & ~(DUK_HEAPHDR_FLAGS_TYPE_MASK)) | (val); \
	} while (0)

#define  DUK_HEAPHDR_HTYPE_VALID(h)    ( \
	DUK_HEAPHDR_GET_TYPE((h)) >= DUK_HTYPE_MIN && \
	DUK_HEAPHDR_GET_TYPE((h)) <= DUK_HTYPE_MAX \
	)

#define  DUK_HEAPHDR_SET_TYPE_AND_FLAGS(h,tval,fval)  do { \
		(h)->h_flags = ((tval) & DUK_HEAPHDR_FLAGS_TYPE_MASK) | \
		               ((fval) & DUK_HEAPHDR_FLAGS_FLAG_MASK); \
	} while (0)

#define  DUK_HEAPHDR_SET_FLAG_BITS(h,bits)  do { \
		DUK_ASSERT(((bits) & ~(DUK_HEAPHDR_FLAGS_FLAG_MASK)) == 0); \
		(h)->h_flags |= (bits); \
	} while (0)

#define  DUK_HEAPHDR_CLEAR_FLAG_BITS(h,bits)  do { \
		DUK_ASSERT(((bits) & ~(DUK_HEAPHDR_FLAGS_FLAG_MASK)) == 0); \
		(h)->h_flags &= ~((bits)); \
	} while (0)

#define  DUK_HEAPHDR_CHECK_FLAG_BITS(h,bits)  (((h)->h_flags & (bits)) != 0)

#define  DUK_HEAPHDR_SET_REACHABLE(h)      DUK_HEAPHDR_SET_FLAG_BITS((h),DUK_HEAPHDR_FLAG_REACHABLE)
#define  DUK_HEAPHDR_CLEAR_REACHABLE(h)    DUK_HEAPHDR_CLEAR_FLAG_BITS((h),DUK_HEAPHDR_FLAG_REACHABLE)
#define  DUK_HEAPHDR_HAS_REACHABLE(h)      DUK_HEAPHDR_CHECK_FLAG_BITS((h),DUK_HEAPHDR_FLAG_REACHABLE)

#define  DUK_HEAPHDR_SET_TEMPROOT(h)       DUK_HEAPHDR_SET_FLAG_BITS((h),DUK_HEAPHDR_FLAG_TEMPROOT)
#define  DUK_HEAPHDR_CLEAR_TEMPROOT(h)     DUK_HEAPHDR_CLEAR_FLAG_BITS((h),DUK_HEAPHDR_FLAG_TEMPROOT)
#define  DUK_HEAPHDR_HAS_TEMPROOT(h)       DUK_HEAPHDR_CHECK_FLAG_BITS((h),DUK_HEAPHDR_FLAG_TEMPROOT)

#define  DUK_HEAPHDR_SET_FINALIZABLE(h)    DUK_HEAPHDR_SET_FLAG_BITS((h),DUK_HEAPHDR_FLAG_FINALIZABLE)
#define  DUK_HEAPHDR_CLEAR_FINALIZABLE(h)  DUK_HEAPHDR_CLEAR_FLAG_BITS((h),DUK_HEAPHDR_FLAG_FINALIZABLE)
#define  DUK_HEAPHDR_HAS_FINALIZABLE(h)    DUK_HEAPHDR_CHECK_FLAG_BITS((h),DUK_HEAPHDR_FLAG_FINALIZABLE)

#define  DUK_HEAPHDR_SET_FINALIZED(h)      DUK_HEAPHDR_SET_FLAG_BITS((h),DUK_HEAPHDR_FLAG_FINALIZED)
#define  DUK_HEAPHDR_CLEAR_FINALIZED(h)    DUK_HEAPHDR_CLEAR_FLAG_BITS((h),DUK_HEAPHDR_FLAG_FINALIZED)
#define  DUK_HEAPHDR_HAS_FINALIZED(h)      DUK_HEAPHDR_CHECK_FLAG_BITS((h),DUK_HEAPHDR_FLAG_FINALIZED)

/* get or set a range of flags; m=first bit number, n=number of bits */
#define  DUK_HEAPHDR_GET_FLAG_RANGE(h,m,n)  (((h)->h_flags >> (m)) & ((1 << (n)) - 1))

#define  DUK_HEAPHDR_SET_FLAG_RANGE(h,m,n,v)  do { \
		(h)->h_flags = \
			((h)->h_flags & (~(((1 << (n)) - 1) << (m)))) \
			| ((v) << (m)); \
	} while (0)

/* init pointer fields to null */
#if defined(DUK_USE_DOUBLE_LINKED_HEAP)
#define  DUK_HEAPHDR_INIT_NULLS(h)       do { \
		(h)->h_next = NULL; \
	} while (0)
#else
#define  DUK_HEAPHDR_INIT_NULLS(h)       do { \
		(h)->h_next = NULL; \
		(h)->h_prev = NULL; \
	} while (0)
#endif

#define  DUK_HEAPHDR_STRING_INIT_NULLS(h)  /* currently nop */

/*
 *  Reference counting helper macros.  The macros take a thread argument
 *  and must thus always be executed in a specific thread context.  The
 *  thread argument is needed for features like finalization.  Currently
 *  it is not required for INCREF, but it is included just in case.
 *
 *  Note that 'raw' macros such as DUK_HEAPHDR_GET_REFCOUNT() are not
 *  defined without DUK_USE_REFERENCE_COUNTING, so caller must #ifdef
 *  around them.
 */

#if defined(DUK_USE_REFERENCE_COUNTING)

#define  DUK_TVAL_INCREF(thr,tv)                duk_heap_tval_incref((tv))
#define  DUK_TVAL_DECREF(thr,tv)                duk_heap_tval_decref((thr),(tv))
#define  _DUK_HEAPHDR_INCREF(thr,h)             duk_heap_heaphdr_incref((h))
#define  _DUK_HEAPHDR_DECREF(thr,h)             duk_heap_heaphdr_decref((thr),(h))
#define  DUK_HEAPHDR_INCREF(thr,h)              _DUK_HEAPHDR_INCREF((thr),(duk_heaphdr *) (h))
#define  DUK_HEAPHDR_DECREF(thr,h)              _DUK_HEAPHDR_DECREF((thr),(duk_heaphdr *) (h))
#define  DUK_HSTRING_INCREF(thr,h)              _DUK_HEAPHDR_INCREF((thr),(duk_heaphdr *) (h))
#define  DUK_HSTRING_DECREF(thr,h)              _DUK_HEAPHDR_DECREF((thr),(duk_heaphdr *) (h))
#define  DUK_HOBJECT_INCREF(thr,h)              _DUK_HEAPHDR_INCREF((thr),(duk_heaphdr *) (h))
#define  DUK_HOBJECT_DECREF(thr,h)              _DUK_HEAPHDR_DECREF((thr),(duk_heaphdr *) (h))
#define  DUK_HBUFFER_INCREF(thr,h)              _DUK_HEAPHDR_INCREF((thr),(duk_heaphdr *) (h))
#define  DUK_HBUFFER_DECREF(thr,h)              _DUK_HEAPHDR_DECREF((thr),(duk_heaphdr *) (h))
#define  DUK_HCOMPILEDFUNCTION_INCREF(thr,h)    _DUK_HEAPHDR_INCREF((thr),(duk_heaphdr *) &(h)->obj)
#define  DUK_HCOMPILEDFUNCTION_DECREF(thr,h)    _DUK_HEAPHDR_DECREF((thr),(duk_heaphdr *) &(h)->obj)
#define  DUK_HNATIVEFUNCTION_INCREF(thr,h)      _DUK_HEAPHDR_INCREF((thr),(duk_heaphdr *) &(h)->obj)
#define  DUK_HNATIVEFUNCTION_DECREF(thr,h)      _DUK_HEAPHDR_DECREF((thr),(duk_heaphdr *) &(h)->obj)
#define  DUK_HTHREAD_INCREF(thr,h)              _DUK_HEAPHDR_INCREF((thr),(duk_heaphdr *) &(h)->obj)
#define  DUK_HTHREAD_DECREF(thr,h)              _DUK_HEAPHDR_DECREF((thr),(duk_heaphdr *) &(h)->obj)

#else  /* DUK_USE_REFERENCE_COUNTING */

#define  DUK_TVAL_INCREF(thr,v)                 /* nop */
#define  DUK_TVAL_DECREF(thr,v)                 /* nop */
#define  DUK_HEAPHDR_INCREF(thr,h)              /* nop */
#define  DUK_HEAPHDR_DECREF(thr,h)              /* nop */
#define  DUK_HSTRING_INCREF(thr,h)              /* nop */
#define  DUK_HSTRING_DECREF(thr,h)              /* nop */
#define  DUK_HOBJECT_INCREF(thr,h)              /* nop */
#define  DUK_HOBJECT_DECREF(thr,h)              /* nop */
#define  DUK_HBUFFER_INCREF(thr,h)              /* nop */
#define  DUK_HBUFFER_DECREF(thr,h)              /* nop */
#define  DUK_HCOMPILEDFUNCTION_INCREF(thr,h)    /* nop */
#define  DUK_HCOMPILEDFUNCTION_DECREF(thr,h)    /* nop */
#define  DUK_HNATIVEFUNCTION_INCREF(thr,h)      /* nop */
#define  DUK_HNATIVEFUNCTION_DECREF(thr,h)      /* nop */
#define  DUK_HTHREAD_INCREF(thr,h)              /* nop */
#define  DUK_HTHREAD_DECREF(thr,h)              /* nop */

#endif  /* DUK_USE_REFERENCE_COUNTING */

#endif  /* DUK_HEAPHDR_H_INCLUDED */

