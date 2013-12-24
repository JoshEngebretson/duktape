/*
 *  Union to access IEEE double memory representation, indexes for double
 *  memory representation, and some macros for double manipulation.
 *
 *  Also used by packed duk_tval.  Use a union for bit manipulation to
 *  minimize aliasing issues in practice.  The C99 standard does not
 *  guarantee that this should work, but it's a very widely supported
 *  practice for low level manipulation.
 *
 *  IEEE double format summary:
 *
 *    seeeeeee eeeeffff ffffffff ffffffff ffffffff ffffffff ffffffff ffffffff
 *       A        B        C        D        E        F        G        H
 *
 *    s       sign bit
 *    eee...  exponent field
 *    fff...  fraction
 *
 *  See http://en.wikipedia.org/wiki/Double_precision_floating-point_format.
 *
 *  NaNs are represented as exponent 0x7ff and mantissa != 0.  The NaN is a
 *  signaling NaN when the highest bit of the mantissa is zero, and a quiet
 *  NaN when the highest bit is set.
 *
 *  At least three memory layouts are relevant here:
 *
 *    A B C D E F G H    Big endian (e.g. 68k)           DUK_USE_DOUBLE_BE
 *    H G F E D C B A    Little endian (e.g. x86)        DUK_USE_DOUBLE_LE
 *    D C B A H G F E    Middle/cross endian (e.g. ARM)  DUK_USE_DOUBLE_ME
 *
 *  ARM is a special case: ARM double values are in middle/cross endian
 *  format while ARM duk_uint64_t values are in standard little endian
 *  format (H G F E D C B A).  When a double is read as a duk_uint64_t
 *  from memory, the register will contain the (logical) value
 *  E F G H A B C D.  This requires some special handling below.
 *
 *  Indexes of various types (8-bit, 16-bit, 32-bit) in memory relative to
 *  the logical (big endian) order:
 *
 *  byte order      duk_uint8_t    duk_uint16_t     duk_uint32_t    
 *    BE             01234567         0123               01
 *    LE             76543210         3210               10
 *    ME (ARM)       32107654         1032               01
 *
 *  Some processors may alter NaN values in a floating point load+store.
 *  For instance, on X86 a FLD + FSTP may convert a signaling NaN to a
 *  quiet one.  This is catastrophic when NaN space is used in packed
 *  duk_tval values.  See: misc/clang_aliasing.c.
 */

#ifndef DUK_DBLUNION_H_INCLUDED
#define DUK_DBLUNION_H_INCLUDED

/*
 *  Union for accessing double parts, also serves as packed duk_tval
 */

union duk_double_union {
	double d;
#ifdef DUK_USE_64BIT_OPS
	duk_uint64_t ull[1];
#endif
	duk_uint32_t ui[2];
	duk_uint16_t us[4];
	duk_uint8_t uc[8];
#ifdef DUK_USE_PACKED_TVAL_POSSIBLE
	void *vp[2];  /* used by packed duk_tval, assumes sizeof(void *) == 4 */
#endif
};

typedef union duk_double_union duk_double_union;

/*
 *  Indexes of various types with respect to big endian (logical) layout
 */

#if defined(DUK_USE_DOUBLE_LE)
#ifdef DUK_USE_64BIT_OPS
#define DUK_DBL_IDX_ULL0   0
#endif
#define DUK_DBL_IDX_UI0    1
#define DUK_DBL_IDX_UI1    0
#define DUK_DBL_IDX_US0    3
#define DUK_DBL_IDX_US1    2
#define DUK_DBL_IDX_US2    1
#define DUK_DBL_IDX_US3    0
#define DUK_DBL_IDX_UC0    7
#define DUK_DBL_IDX_UC1    6
#define DUK_DBL_IDX_UC2    5
#define DUK_DBL_IDX_UC3    4
#define DUK_DBL_IDX_UC4    3
#define DUK_DBL_IDX_UC5    2
#define DUK_DBL_IDX_UC6    1
#define DUK_DBL_IDX_UC7    0
#define DUK_DBL_IDX_VP0    DUK_DBL_IDX_UI0  /* packed tval */
#define DUK_DBL_IDX_VP1    DUK_DBL_IDX_UI1  /* packed tval */
#elif defined(DUK_USE_DOUBLE_BE)
#ifdef DUK_USE_64BIT_OPS
#define DUK_DBL_IDX_ULL0   0
#endif
#define DUK_DBL_IDX_UI0    0
#define DUK_DBL_IDX_UI1    1
#define DUK_DBL_IDX_US0    0
#define DUK_DBL_IDX_US1    1
#define DUK_DBL_IDX_US2    2
#define DUK_DBL_IDX_US3    3
#define DUK_DBL_IDX_UC0    0
#define DUK_DBL_IDX_UC1    1
#define DUK_DBL_IDX_UC2    2
#define DUK_DBL_IDX_UC3    3
#define DUK_DBL_IDX_UC4    4
#define DUK_DBL_IDX_UC5    5
#define DUK_DBL_IDX_UC6    6
#define DUK_DBL_IDX_UC7    7
#define DUK_DBL_IDX_VP0    DUK_DBL_IDX_UI0  /* packed tval */
#define DUK_DBL_IDX_VP1    DUK_DBL_IDX_UI1  /* packed tval */
#elif defined(DUK_USE_DOUBLE_ME)
#ifdef DUK_USE_64BIT_OPS
#define DUK_DBL_IDX_ULL0   0  /* not directly applicable, byte order differs from a double */
#endif
#define DUK_DBL_IDX_UI0    0
#define DUK_DBL_IDX_UI1    1
#define DUK_DBL_IDX_US0    1
#define DUK_DBL_IDX_US1    0
#define DUK_DBL_IDX_US2    3
#define DUK_DBL_IDX_US3    2
#define DUK_DBL_IDX_UC0    3
#define DUK_DBL_IDX_UC1    2
#define DUK_DBL_IDX_UC2    1
#define DUK_DBL_IDX_UC3    0
#define DUK_DBL_IDX_UC4    7
#define DUK_DBL_IDX_UC5    6
#define DUK_DBL_IDX_UC6    5
#define DUK_DBL_IDX_UC7    4
#define DUK_DBL_IDX_VP0    DUK_DBL_IDX_UI0  /* packed tval */
#define DUK_DBL_IDX_VP1    DUK_DBL_IDX_UI1  /* packed tval */
#else
#error internal error
#endif

/*
 *  Helper macros for reading/writing memory representation parts, used
 *  by duk_numconv.c and duk_tval.h.
 */

#define DUK_DBLUNION_SET_DOUBLE(u,v)  do {  \
		(u)->d = (v); \
	} while (0)

#define DUK_DBLUNION_SET_HIGH32(u,v)  do {  \
		(u)->ui[DUK_DBL_IDX_UI0] = (duk_uint32_t) (v); \
	} while (0)

#ifdef DUK_USE_64BIT_OPS
#ifdef DUK_USE_DOUBLE_ME
#define DUK_DBLUNION_SET_HIGH32_ZERO_LOW32(u,v)  do { \
		(u)->ull[DUK_DBL_IDX_ULL0] = (duk_uint64_t) (v); \
	} while (0)
#else
#define DUK_DBLUNION_SET_HIGH32_ZERO_LOW32(u,v)  do { \
		(u)->ull[DUK_DBL_IDX_ULL0] = ((duk_uint64_t) (v)) << 32; \
	} while (0)
#endif
#else  /* DUK_USE_64BIT_OPS */
#define DUK_DBLUNION_SET_HIGH32_ZERO_LOW32(u,v)  do { \
		(u)->ui[DUK_DBL_IDX_UI0] = (duk_uint32_t) (v); \
		(u)->ui[DUK_DBL_IDX_UI1] = (duk_uint32_t) 0; \
	} while (0)
#endif  /* DUK_USE_64BIT_OPS */

#define DUK_DBLUNION_SET_LOW32(u,v)  do {  \
		(u)->ui[DUK_DBL_IDX_UI1] = (duk_uint32_t) (v); \
	} while (0)

#define DUK_DBLUNION_GET_DOUBLE(u)  ((u)->d)
#define DUK_DBLUNION_GET_HIGH32(u)  ((u)->ui[DUK_DBL_IDX_UI0])
#define DUK_DBLUNION_GET_LOW32(u)   ((u)->ui[DUK_DBL_IDX_UI1])

/*
 *  Double NaN manipulation macros related to NaN normalization needed when
 *  using the packed duk_tval representation.  NaN normalization is necessary
 *  to keep double values compatible with the duk_tval format.
 *
 *  When packed duk_tval is used, the NaN space is used to store pointers
 *  and other tagged values in addition to NaNs.  Actual NaNs are normalized
 *  to a specific format.  The macros below are used by the implementation
 *  to check and normalize NaN values when they might be created.  The macros
 *  are essentially NOPs when the non-packed duk_tval representation is used.
 *
 *  A FULL check is exact and checks all bits.  A NOTFULL check is used by
 *  the packed duk_tval and works correctly for all NaNs except those that
 *  begin with 0x7ff0.  Since the 'normalized NaN' values used with packed
 *  duk_tval begin with 0x7ff8, the partial check is reliable when packed
 *  duk_tval is used.
 *
 *  The ME variant below is specifically for ARM byte order, which has the
 *  feature that while doubles have a mixed byte order (32107654), unsigned
 *  long long values has a little endian byte order (76543210).  When writing
 *  a logical double value through a ULL pointer, the 32-bit words need to be
 *  swapped; hence the #ifdefs below for ULL writes with DUK_USE_DOUBLE_ME.
 *  This is not full ARM support but suffices for some environments.
 */

#ifdef DUK_USE_64BIT_OPS
#ifdef DUK_USE_DOUBLE_ME
#define DUK__DBLUNION_SET_NAN_FULL(u)  do { \
		(u)->ull[DUK_DBL_IDX_ULL0] = 0x000000007ff80000ULL; \
	} while (0)
#else
#define DUK__DBLUNION_SET_NAN_FULL(u)  do { \
		(u)->ull[DUK_DBL_IDX_ULL0] = 0x7ff8000000000000ULL; \
	} while (0)
#endif
#else  /* DUK_USE_64BIT_OPS */
#define DUK__DBLUNION_SET_NAN_FULL(u)  do { \
		(u)->ui[DUK_DBL_IDX_UI0] = (duk_uint32_t) 0x7ff80000UL; \
		(u)->ui[DUK_DBL_IDX_UI1] = (duk_uint32_t) 0x00000000UL; \
	} while (0)
#endif  /* DUK_USE_64BIT_OPS */

#define DUK__DBLUNION_SET_NAN_NOTFULL(u)  do { \
		(u)->us[DUK_DBL_IDX_US0] = 0x7ff8UL; \
	} while (0)

#ifdef DUK_USE_64BIT_OPS
#ifdef DUK_USE_DOUBLE_ME
#define DUK__DBLUNION_IS_NAN_FULL(u) \
	/* E == 0x7ff, F != 0 => NaN */ \
	((((u)->us[DUK_DBL_IDX_US0] & 0x7ff0UL) == 0x7ff0UL) && \
	 ((((u)->ull[DUK_DBL_IDX_ULL0]) & 0xffffffff000fffffULL) != 0))
#else
#define DUK__DBLUNION_IS_NAN_FULL(u) \
	/* E == 0x7ff, F != 0 => NaN */ \
	((((u)->us[DUK_DBL_IDX_US0] & 0x7ff0UL) == 0x7ff0UL) && \
	 ((((u)->ull[DUK_DBL_IDX_ULL0]) & 0x000fffffffffffffULL) != 0))
#endif
#else  /* DUK_USE_64BIT_OPS */
#define DUK__DBLUNION_IS_NAN_FULL(u) \
	/* E == 0x7ff, F != 0 => NaN */ \
	((((u)->ui[DUK_DBL_IDX_UI0] & 0x7ff00000UL) == 0x7ff00000UL) && \
	 (((u)->ui[DUK_DBL_IDX_UI0] & 0x000fffffUL) != 0 || \
          (u)->ui[DUK_DBL_IDX_UI1] != 0))
#endif  /* DUK_USE_64BIT_OPS */

#define DUK__DBLUNION_IS_NAN_NOTFULL(u) \
	/* E == 0x7ff, topmost four bits of F != 0 => assume NaN */ \
	((((u)->us[DUK_DBL_IDX_US0] & 0x7ff0UL) == 0x7ff0UL) && \
	 (((u)->us[DUK_DBL_IDX_US0] & 0x000fUL) != 0x0000UL))

#ifdef DUK_USE_64BIT_OPS
#ifdef DUK_USE_DOUBLE_ME
#define DUK__DBLUNION_IS_NORMALIZED_NAN_FULL(u) \
	((u)->ull[DUK_DBL_IDX_ULL0] == 0x000000007ff80000ULL)
#else
#define DUK__DBLUNION_IS_NORMALIZED_NAN_FULL(u) \
	((u)->ull[DUK_DBL_IDX_ULL0] == 0x7ff8000000000000ULL)
#endif
#else  /* DUK_USE_64BIT_OPS */
#define DUK__DBLUNION_IS_NORMALIZED_NAN_FULL(u) \
	(((u)->ui[DUK_DBL_IDX_UI0] == 0x7ff80000UL) && \
	 ((u)->ui[DUK_DBL_IDX_UI1] == 0x00000000UL))
#endif  /* DUK_USE_64BIT_OPS */

#define DUK__DBLUNION_IS_NORMALIZED_NAN_NOTFULL(u) \
	/* E == 0x7ff, F == 8 => normalized NaN */ \
	((u)->us[DUK_DBL_IDX_US0] == 0x7ff8UL)

#define DUK__DBLUNION_NORMALIZE_NAN_CHECK_FULL(u)  do { \
		if (DUK__DBLUNION_IS_NAN_FULL((u))) { \
			DUK__DBLUNION_SET_NAN_FULL((u)); \
		} \
	} while (0)

#define DUK__DBLUNION_NORMALIZE_NAN_CHECK_NOTFULL(u)  do { \
		if (DUK__DBLUNION_IS_NAN_NOTFULL((u))) { \
			DUK__DBLUNION_SET_NAN_NOTFULL((u)); \
		} \
	} while (0)

/* Concrete macros for NaN handling used by the implementation internals.
 * Chosen so that they match the duk_tval representation: with a packed
 * duk_tval, ensure NaNs are properly normalized; with a non-packed duk_tval
 * these are essentially NOPs.
 */

#if defined(DUK_USE_PACKED_TVAL)
#if defined(DUK_USE_FULL_TVAL)
#define DUK_DBLUNION_NORMALIZE_NAN_CHECK(u)  DUK__DBLUNION_NORMALIZE_NAN_CHECK_FULL((u))
#define DUK_DBLUNION_IS_NAN(u)               DUK__DBLUNION_IS_NAN_FULL((u))
#define DUK_DBLUNION_IS_NORMALIZED_NAN(u)    DUK__DBLUNION_IS_NORMALIZED_NAN_FULL((u))
#define DUK_DBLUNION_SET_NAN(d)              DUK__DBLUNION_SET_NAN_FULL((d))
#else
#define DUK_DBLUNION_NORMALIZE_NAN_CHECK(u)  DUK__DBLUNION_NORMALIZE_NAN_CHECK_NOTFULL((u))
#define DUK_DBLUNION_IS_NAN(u)               DUK__DBLUNION_IS_NAN_NOTFULL((u))
#define DUK_DBLUNION_IS_NORMALIZED_NAN(u)    DUK__DBLUNION_IS_NORMALIZED_NAN_NOTFULL((u))
#define DUK_DBLUNION_SET_NAN(d)              DUK__DBLUNION_SET_NAN_NOTFULL((d))
#endif
#define DUK_DBLUNION_IS_NORMALIZED(u) \
	(!DUK_DBLUNION_IS_NAN((u)) ||  /* either not a NaN */ \
	 DUK_DBLUNION_IS_NORMALIZED_NAN((u)))  /* or is a normalized NaN */
#else  /* DUK_USE_PACKED_TVAL */
#define DUK_DBLUNION_NORMALIZE_NAN_CHECK(u)  /* nop: no need to normalize */
#define DUK_DBLUNION_IS_NAN(u)               (DUK_ISNAN((u)->d))
#define DUK_DBLUNION_IS_NORMALIZED_NAN(u)    (DUK_ISNAN((u)->d))
#define DUK_DBLUNION_IS_NORMALIZED(u)        1  /* all doubles are considered normalized */
#define DUK_DBLUNION_SET_NAN(u)  do { \
		/* in non-packed representation we don't care about which NaN is used */ \
		(u)->d = DUK_DOUBLE_NAN; \
	} while (0)
#endif  /* DUK_USE_PACKED_TVAL */

#endif  /* DUK_DBLUNION_H_INCLUDED */
