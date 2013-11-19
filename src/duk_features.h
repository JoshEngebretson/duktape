/*
 *  Determine platform features, select feature selection defines
 *  (e.g. _XOPEN_SOURCE), include system headers, and define DUK_USE_XXX
 *  defines which are (only) checked in Duktape internal code for
 *  activated features.  Duktape feature selection is based on DUK_PROFILE,
 *  other user supplied defines, and automatic feature detection.
 *
 *  This file is included by duk_internal.h before anything else is
 *  included.  Feature selection defines (e.g. _XOPEN_SOURCE) are defined
 *  here before any system headers are included (which is a requirement for
 *  system headers to work correctly).  This file is responsible for including
 *  all system headers and contains all platform dependent cruft in general.
 *
 *  The public duktape.h has minimal feature detection required by the public
 *  API (for instance use of variadic macros is detected there).  Duktape.h
 *  exposes its detection results as DUK_API_xxx.  The public header and the
 *  implementation must agree on e.g. names and argument lists of exposed
 *  calls; these are checked by duk_features_sanity.h (duktape.h is not yet
 *  included when this file is included to avoid fouling up feature selection
 *  defines).
 *
 *  The general order of handling:
 *    - Compiler feature detection (require no includes)
 *    - Intermediate platform detection (-> easier platform defines)
 *    - Platform detection, system includes, byte order detection, etc
 *    - ANSI C wrappers (e.g. DUK_MEMCMP), wrappers for constants, etc
 *    - Duktape profile handling, DUK_USE_xxx constants are set
 *    - Duktape Date provider settings
 *    - Final sanity checks
 *
 *  DUK_F_XXX are internal feature detection macros which should not
 *  be used outside this header.
 *
 *  Useful resources:
 *
 *    http://sourceforge.net/p/predef/wiki/Home/
 *    http://sourceforge.net/p/predef/wiki/Architectures/
 *    http://stackoverflow.com/questions/5919996/how-to-detect-reliably-mac-os-x-ios-linux-windows-in-c-preprocessor
 *    http://en.wikipedia.org/wiki/C_data_types#Fixed-width_integer_types
 *
 *  FIXME: at the moment there is no direct way of configuring
 *  or overriding individual settings.
 */

#ifndef DUK_FEATURES_H_INCLUDED
#define DUK_FEATURES_H_INCLUDED

/* FIXME: platform detection and all includes and defines in one big
 * if-else ladder (now e.g. datetime providers is a separate ladder).
 */

/*
 *  Compiler features
 */

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
#define  DUK_F_C99
#else
#undef   DUK_F_C99
#endif

/*
 *  Provides the duk_rdtsc() inline function (if available)
 *
 *  See: http://www.mcs.anl.gov/~kazutomo/rdtsc.html
 */

/* XXX: more accurate detection of what gcc versions work; more inline
 * asm versions for other compilers.
 */
#if defined(__GNUC__) && defined(__i386__) && \
    !defined(__cplusplus) /* unsigned long long not standard */
static __inline__ unsigned long long duk_rdtsc(void) {
	unsigned long long int x;
	__asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
	return x;
}
#define  DUK_RDTSC_AVAILABLE 1
#elif defined(__GNUC__) && defined(__x86_64__) && \
    !defined(__cplusplus) /* unsigned long long not standard */
static __inline__ unsigned long long duk_rdtsc(void) {
	unsigned hi, lo;
	__asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
	return ((unsigned long long) lo) | (((unsigned long long) hi) << 32);
}
#define  DUK_RDTSC_AVAILABLE 1
#else
/* not available */
#undef  DUK_RDTSC_AVAILABLE
#endif

/*
 *  Intermediate platform, architecture, and compiler detection.  These are
 *  hopelessly intertwined - e.g. architecture defines depend on compiler etc.
 *
 *  Provide easier defines for platforms and compilers which are often tricky
 *  or verbose to detect.  The intent is not to provide intermediate defines for
 *  all features; only if existing feature defines are inconvenient.
 */

/* Intel x86 (32-bit) */
#if defined(i386) || defined(__i386) || defined(__i386__) || \
    defined(__i486__) || defined(__i586__) || defined(__i686__) || \
    defined(__IA32__) || defined(_M_IX86) || defined(__X86__) || \
    defined(_X86_) || defined(__THW_INTEL__) || defined(__I86__)
#define  DUK_F_X86
#endif

/* AMD64 (64-bit) */
#if defined(__amd64__) || defined(__amd64) || \
    defined(__x86_64__) || defined(__x86_64) || \
    defined(_M_X64) || defined(_M_AMD64)
#define  DUK_F_X64
#endif

/* FIXME: X32: pointers are 32-bit so packed format can be used */

/* MIPS */
#if defined(__mips__) || defined(mips) || defined(_MIPS_ISA) || \
    defined(_R3000) || defined(_R4000) || defined(_R5900) || \
    defined(_MIPS_ISA_MIPS1) || defined(_MIPS_ISA_MIPS2) || \
    defined(_MIPS_ISA_MIPS3) || defined(_MIPS_ISA_MIPS4) || \
    defined(__mips) || defined(__MIPS__)
#define  DUK_F_MIPS
#endif

/* Motorola 68K.  Not defined by VBCC, so user must define one of these
 * manually when using VBCC.
 */
#if defined(__m68k__) || defined(M68000) || defined(__MC68K__)
#define  DUK_F_M68K
#endif

/* BSD variant */
#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD) || \
    defined(__bsdi__) || defined(__DragonFly__)
#define  DUK_F_BSD
#endif

/* Atari ST TOS. __TOS__ defined by PureC (which doesn't work as a target now
 * because int is 16-bit, to be fixed).  No platform define in VBCC apparently,
 * so to use with VBCC, user must define '__TOS__' manually.
  */
#if defined(__TOS__)
#define  DUK_F_TOS
#endif

/* AmigaOS.  Neither AMIGA nor __amigaos__ is defined on VBCC, so user must
 * define 'AMIGA' manually.
 */
#if defined(AMIGA) || defined(__amigaos__)
#define  DUK_F_AMIGAOS
#endif

/*
 *  Platform detection and system includes
 *
 *  Feature selection (e.g. _XOPEN_SOURCE) must happen before any system
 *  headers are included.
 *
 *  Can trigger standard byte order detection (later in this file) or
 *  specify byte order explicitly on more exotic platforms.
 */

#if defined(__linux)
#ifndef  _POSIX_C_SOURCE
#define  _POSIX_C_SOURCE  200809L
#endif
#ifndef  _GNU_SOURCE
#define  _GNU_SOURCE      /* e.g. getdate_r */
#endif
#ifndef  _XOPEN_SOURCE
#define  _XOPEN_SOURCE    /* e.g. strptime */
#endif
#endif

#if defined(__APPLE__)
/* Apple OSX */
#define  DUK_F_STD_BYTEORDER_DETECT
#include <architecture/byte_order.h>
#include <limits.h>
#include <sys/param.h>
#elif defined(DUK_F_BSD)
/* BSD */
#define  DUK_F_STD_BYTEORDER_DETECT
#include <sys/endian.h>
#include <limits.h>
#include <sys/param.h>
#elif defined(DUK_F_TOS)
/* Atari ST TOS */
#define  DUK_USE_DOUBLE_BE
#include <limits.h>
#elif defined(DUK_F_AMIGAOS)
#if defined(DUK_F_M68K)
/* AmigaOS on M68k */
#define  DUK_USE_DOUBLE_BE
#include <limits.h>
#else
#error AmigaOS but not M68K, not supported now
#endif
#else
/* Linux and hopefully others */
#define  DUK_F_STD_BYTEORDER_DETECT
#include <endian.h>
#include <limits.h>
#include <sys/param.h>
#endif

/* Shared includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>  /* varargs */
#include <setjmp.h>
#include <stddef.h>  /* e.g. ptrdiff_t */

#ifdef DUK_F_TOS
/*FIXME*/
#else
#include <stdint.h>
#endif

#include <math.h>

/*
 *  Sanity check types and define bit types such as duk_u32
 */

/* FIXME: Is there a reason not to rely on C99 types only, and only fall
 * back to guessing if C99 types are not available?
 */

/* FIXME: How to do reasonable automatic detection on older compilers,
 * and how to allow user override?
 */

#ifdef INT_MAX
#if INT_MAX < 2147483647
#error INT_MAX too small, expected int to be 32 bits at least
#endif
#else
#error INT_MAX not defined
#endif

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L) && \
    !(defined(DUK_F_AMIGAOS) && defined(__VBCC__)) /* vbcc + AmigaOS has C99 but no inttypes.h */
/* C99 */
#include <inttypes.h>
typedef uint8_t duk_u8;
typedef int8_t duk_i8;
typedef uint16_t duk_u16;
typedef int16_t duk_i16;
typedef uint32_t duk_u32;
typedef int32_t duk_i32;
#else
/* FIXME: need actual detection here */
typedef unsigned char duk_u8;
typedef signed char duk_i8;
typedef unsigned short duk_u16;
typedef signed short duk_i16;
typedef unsigned int duk_u32;
typedef signed int duk_i32;
#endif

/*
 *  Check whether we should use 64-bit integers
 */

/* Quite incomplete now: require C99, avoid 64-bit types on VBCC because
 * they seem to misbehave.  Should use 64-bit operations at least on 64-bit
 * platforms even when C99 not available (perhaps integrate to bit type
 * detection?).
 */
#if defined(DUK_F_C99) && !defined(__VBCC__)
#define  DUK_USE_64BIT_OPS
#else
#undef  DUK_USE_64BIT_OPS
#endif

/*
 *  Support for unaligned accesses
 *
 *  Assume unaligned accesses are not supported unless specifically allowed
 *  in the target platform.
 */

/* FIXME: alignment is now only guaranteed to 4 bytes in any case, so doubles
 * are not guaranteed to be aligned.
 */

#if defined(__arm__) || defined(__thumb__) || defined(_ARM) || defined(_M_ARM)
#undef   DUK_USE_UNALIGNED_ACCESSES_POSSIBLE
#elif defined(DUK_F_MIPS)
#undef   DUK_USE_UNALIGNED_ACCESSES_POSSIBLE
#elif defined(DUK_F_X86) || defined(DUK_F_X64)
#define  DUK_USE_UNALIGNED_ACCESSES_POSSIBLE
#else
#undef   DUK_USE_UNALIGNED_ACCESSES_POSSIBLE
#endif

/*
 *  Byte order and double memory layout detection
 *
 *  This needs to be done before choosing a default profile, as it affects
 *  profile selection.
 */

/* FIXME: Not very good detection right now, expect to find __BYTE_ORDER
 * and __FLOAT_WORD_ORDER or resort to GCC/ARM specifics.  Improve the
 * detection code and perhaps allow some compiler define to override the
 * detection for unhandled cases.
 */

#if defined(DUK_F_STD_BYTEORDER_DETECT)
/* determine endianness variant: little-endian (LE), big-endian (BE), or "middle-endian" (ME) i.e. ARM */
#if (defined(__BYTE_ORDER) && defined(__LITTLE_ENDIAN) && (__BYTE_ORDER == __LITTLE_ENDIAN)) || \
    (defined(__LITTLE_ENDIAN__))
#if defined(__FLOAT_WORD_ORDER) && defined(__LITTLE_ENDIAN) && (__FLOAT_WORD_ORDER == __LITTLE_ENDIAN) || \
    (defined(__GNUC__) && !defined(__arm__))
#define DUK_USE_DOUBLE_LE
#elif (defined(__FLOAT_WORD_ORDER) && defined(__BIG_ENDIAN) && (__FLOAT_WORD_ORDER == __BIG_ENDIAN)) || \
      (defined(__GNUC__) && defined(__arm__))
#define DUK_USE_DOUBLE_ME
#else
#error unsupported: byte order is little endian but cannot determine IEEE double word order
#endif
#elif (defined(__BYTE_ORDER) && defined(__BIG_ENDIAN) && (__BYTE_ORDER == __BIG_ENDIAN)) || \
      (defined(__BIG_ENDIAN__))
#if (defined(__FLOAT_WORD_ORDER) && defined(__BIG_ENDIAN) && (__FLOAT_WORD_ORDER == __BIG_ENDIAN)) || \
    (defined(__GNUC__) && !defined(__arm__))
#define DUK_USE_DOUBLE_BE
#else
#error unsupported: byte order is big endian but cannot determine IEEE double word order
#endif
#else
#error unsupported: cannot determine byte order
#endif
#endif  /* DUK_F_STD_BYTEORDER_DETECT */

#if !defined(DUK_USE_DOUBLE_LE) && !defined(DUK_USE_DOUBLE_ME) && !defined(DUK_USE_DOUBLE_BE)
#error unsupported: cannot determine IEEE double byte order variant
#endif

/*
 *  Union to access IEEE double memory representation and indexes for double
 *  memory representation.
 *
 *  The double union is almost the same as a packed duk_tval, but only for
 *  accessing doubles e.g. for numconv and replacemenf functions, which are
 *  needed regardless of duk_tval representation.
 */

/* indexes of various types with respect to big endian (logical) layout */
#if defined(DUK_USE_DOUBLE_LE)
#ifdef DUK_USE_64BIT_OPS
#define  DUK_DBL_IDX_ULL0   0
#endif
#define  DUK_DBL_IDX_UI0    1
#define  DUK_DBL_IDX_UI1    0
#define  DUK_DBL_IDX_US0    3
#define  DUK_DBL_IDX_US1    2
#define  DUK_DBL_IDX_US2    1
#define  DUK_DBL_IDX_US3    0
#define  DUK_DBL_IDX_UC0    7
#define  DUK_DBL_IDX_UC1    6
#define  DUK_DBL_IDX_UC2    5
#define  DUK_DBL_IDX_UC3    4
#define  DUK_DBL_IDX_UC4    3
#define  DUK_DBL_IDX_UC5    2
#define  DUK_DBL_IDX_UC6    1
#define  DUK_DBL_IDX_UC7    0
#define  DUK_DBL_IDX_VP0    DUK_DBL_IDX_UI0  /* packed tval */
#define  DUK_DBL_IDX_VP1    DUK_DBL_IDX_UI1  /* packed tval */
#elif defined(DUK_USE_DOUBLE_BE)
#ifdef DUK_USE_64BIT_OPS
#define  DUK_DBL_IDX_ULL0   0
#endif
#define  DUK_DBL_IDX_UI0    0
#define  DUK_DBL_IDX_UI1    1
#define  DUK_DBL_IDX_US0    0
#define  DUK_DBL_IDX_US1    1
#define  DUK_DBL_IDX_US2    2
#define  DUK_DBL_IDX_US3    3
#define  DUK_DBL_IDX_UC0    0
#define  DUK_DBL_IDX_UC1    1
#define  DUK_DBL_IDX_UC2    2
#define  DUK_DBL_IDX_UC3    3
#define  DUK_DBL_IDX_UC4    4
#define  DUK_DBL_IDX_UC5    5
#define  DUK_DBL_IDX_UC6    6
#define  DUK_DBL_IDX_UC7    7
#define  DUK_DBL_IDX_VP0    DUK_DBL_IDX_UI0  /* packed tval */
#define  DUK_DBL_IDX_VP1    DUK_DBL_IDX_UI1  /* packed tval */
#elif defined(DUK_USE_DOUBLE_ME)
#ifdef DUK_USE_64BIT_OPS
#define  DUK_DBL_IDX_ULL0   0  /* not directly applicable, byte order differs from a double */
#endif
#define  DUK_DBL_IDX_UI0    0
#define  DUK_DBL_IDX_UI1    1
#define  DUK_DBL_IDX_US0    1
#define  DUK_DBL_IDX_US1    0
#define  DUK_DBL_IDX_US2    3
#define  DUK_DBL_IDX_US3    2
#define  DUK_DBL_IDX_UC0    3
#define  DUK_DBL_IDX_UC1    2
#define  DUK_DBL_IDX_UC2    1
#define  DUK_DBL_IDX_UC3    0
#define  DUK_DBL_IDX_UC4    7
#define  DUK_DBL_IDX_UC5    6
#define  DUK_DBL_IDX_UC6    5
#define  DUK_DBL_IDX_UC7    4
#define  DUK_DBL_IDX_VP0    DUK_DBL_IDX_UI0  /* packed tval */
#define  DUK_DBL_IDX_VP1    DUK_DBL_IDX_UI1  /* packed tval */
#else
#error internal error
#endif

union duk_double_union {
	double d;
	/* FIXME: type size assumptions, fix */
#ifdef DUK_USE_64BIT_OPS
	unsigned long long ull[1];
#endif
	unsigned int ui[2];
	unsigned short us[4];
	unsigned char uc[8];
};
typedef union duk_double_union duk_double_union;

/* macros for duk_numconv.c */
#define  DUK_DBLUNION_SET_DOUBLE(u,v)  do {  \
		(u)->d = (v); \
	} while (0)
#define  DUK_DBLUNION_SET_HIGH32(u,v)  do {  \
		(u)->ui[DUK_DBL_IDX_UI0] = (unsigned int) (v); \
	} while (0)
#define  DUK_DBLUNION_SET_LOW32(u,v)  do {  \
		(u)->ui[DUK_DBL_IDX_UI1] = (unsigned int) (v); \
	} while (0)
#define  DUK_DBLUNION_GET_DOUBLE(u)  ((u)->d)
#define  DUK_DBLUNION_GET_HIGH32(u)  ((u)->ui[DUK_DBL_IDX_UI0])
#define  DUK_DBLUNION_GET_LOW32(u)   ((u)->ui[DUK_DBL_IDX_UI1])

/*
 *  Check whether or not a packed duk_tval representation is possible
 */

/* best effort viability checks, not particularly accurate */
#undef  DUK_USE_PACKED_TVAL_POSSIBLE
#if (defined(__WORDSIZE) && (__WORDSIZE == 32)) && \
    (defined(UINT_MAX) && (UINT_MAX == 4294967295))
#define DUK_USE_PACKED_TVAL_POSSIBLE
#elif defined(DUK_F_AMIGAOS) /* FIXME: M68K */
#define DUK_USE_PACKED_TVAL_POSSIBLE
#endif

/*
 *  Detection of double constants and math related functions.  Availability
 *  of constants and math functions is a significant porting concern.
 *
 *  INFINITY/HUGE_VAL is problematic on GCC-3.3: it causes an overflow warning
 *  and there is no pragma in GCC-3.3 to disable it.  Using __builtin_inf()
 *  avoids this problem for some reason.
 */

#define  DUK_DOUBLE_2TO32     4294967296.0
#define  DUK_DOUBLE_2TO31     2147483648.0

#undef  DUK_USE_COMPUTED_INFINITY
#if defined(__GNUC__) && defined(__GNUC_MINOR__) && \
    (((__GNUC__ == 4) && (__GNUC_MINOR__ < 6)) || (__GNUC__ < 4))
/* GCC older than 4.6: avoid overflow warnings related to using INFINITY */
#define  DUK_DOUBLE_INFINITY  (__builtin_inf())
#elif defined(INFINITY)
#define  DUK_DOUBLE_INFINITY  ((double) INFINITY)
#elif !defined(__VBCC__)
#define  DUK_DOUBLE_INFINITY  (1.0 / 0.0)
#else
/* In VBCC (1.0 / 0.0) results in a warning and 0.0 instead of infinity.
 * Use a computed infinity(initialized when a heap is created at the
 * latest).
 */
extern double duk_computed_infinity;
#define  DUK_USE_COMPUTED_INFINITY
#define  DUK_DOUBLE_INFINITY  duk_computed_infinity
#endif

#undef  DUK_USE_COMPUTED_NAN
#if defined(NAN)
#define  DUK_DOUBLE_NAN       NAN
#elif !defined(__VBCC__)
#define  DUK_DOUBLE_NAN       (0.0 / 0.0)
#else
/* In VBCC (0.0 / 0.0) results in a warning and 0.0 instead of NaN.
 * Use a computed NaN (initialized when a heap is created at the
 * latest).
 */
extern double duk_computed_nan;
#define  DUK_USE_COMPUTED_NAN
#define  DUK_DOUBLE_NAN       duk_computed_nan
#endif

/* Many platforms are missing fpclassify() and friends, so use replacements
 * if necessary.  The replacement constants (FP_NAN etc) can be anything but
 * match Linux constants now.
 */
#undef  DUK_USE_REPL_FPCLASSIFY
#undef  DUK_USE_REPL_SIGNBIT
#undef  DUK_USE_REPL_ISFINITE
#undef  DUK_USE_REPL_ISNAN
#if !(defined(FP_NAN) && defined(FP_INFINITE) && defined(FP_ZERO) && \
      defined(FP_SUBNORMAL) && defined(FP_NORMAL)) || \
    (defined(DUK_F_AMIGAOS) && defined(__VBCC__))
#define  DUK_USE_REPL_FPCLASSIFY
#define  DUK_USE_REPL_SIGNBIT
#define  DUK_USE_REPL_ISFINITE
#define  DUK_USE_REPL_ISNAN
#define  DUK_FPCLASSIFY       duk_repl_fpclassify
#define  DUK_SIGNBIT          duk_repl_signbit
#define  DUK_ISFINITE         duk_repl_isfinite
#define  DUK_ISNAN            duk_repl_isnan
#define  DUK_FP_NAN           0
#define  DUK_FP_INFINITE      1
#define  DUK_FP_ZERO          2
#define  DUK_FP_SUBNORMAL     3
#define  DUK_FP_NORMAL        4
#else
#define  DUK_FPCLASSIFY       fpclassify
#define  DUK_SIGNBIT          signbit
#define  DUK_ISFINITE         isfinite
#define  DUK_ISNAN            isnan
#define  DUK_FP_NAN           FP_NAN
#define  DUK_FP_INFINITE      FP_INFINITE
#define  DUK_FP_ZERO          FP_ZERO
#define  DUK_FP_SUBNORMAL     FP_SUBNORMAL
#define  DUK_FP_NORMAL        FP_NORMAL
#endif

/* Some math functions are C99 only.  This is also an issue with some
 * embedded environments using uclibc where uclibc has been configured
 * not to provide some functions.  For now, use replacements whenever
 * using uclibc.
 */
#if defined(DUK_F_C99) && \
    !defined(__UCLIBC__) /* uclibc may be missing these */ && \
    !(defined(DUK_F_AMIGAOS) && defined(__VBCC__)) /* vbcc + AmigaOS may be missing these */
#define  DUK_USE_MATH_FMIN
#define  DUK_USE_MATH_FMAX
#define  DUK_USE_MATH_ROUND
#else
#undef  DUK_USE_MATH_FMIN
#undef  DUK_USE_MATH_FMAX
#undef  DUK_USE_MATH_ROUND
#endif

/*
 *  ANSI C string/memory function wrapper defines to allow easier workarounds.
 *
 *  For instance, some platforms don't support zero-size memcpy correctly,
 *  some arcane uclibc versions have a buggy memcpy (but working memmove)
 *  and so on.  Such broken platforms can be dealt with here.
 */

/* Old uclibcs have a broken memcpy so use memmove instead (this is overly
 * wide now on purpose):
 * http://lists.uclibc.org/pipermail/uclibc-cvs/2008-October/025511.html
 */
#if defined(__UCLIBC__)
#define  DUK_MEMCPY       memmove
#else
#define  DUK_MEMCPY       memcpy
#endif

#define  DUK_MEMMOVE      memmove
#define  DUK_MEMCMP       memcmp
#define  DUK_MEMSET       memset
#define  DUK_STRCMP       strcmp
#define  DUK_STRNCMP      strncmp
#define  DUK_SPRINTF      sprintf
#define  DUK_SNPRINTF     snprintf
#define  DUK_VSPRINTF     vsprintf
#define  DUK_VSNPRINTF    vsnprintf

/*
 *  Macro hackery to convert e.g. __LINE__ to a string without formatting,
 *  see: http://stackoverflow.com/questions/240353/convert-a-preprocessor-token-to-a-string
 */

#define  DUK_F_STRINGIFY_HELPER(x)  #x
#define  DUK_MACRO_STRINGIFY(x)  DUK_F_STRINGIFY_HELPER(x)

/*
 *  Macro for suppressing warnings for potentially unreferenced variables.
 *  The variables can be actually unreferenced or unreferenced in some
 *  specific cases only; for instance, if a variable is only debug printed,
 *  it is unreferenced when debug printing is disabled.
 *
 *  (Introduced here because it's potentially compiler specific.)
 */

#define  DUK_UNREF(x)  do { \
		(void) (x); \
	} while (0)

/*
 *  __FILE__, __LINE__, __func__ are wrapped.  Especially __func__ is a
 *  problem because it is not available even in some compilers which try
 *  to be C99 compatible (e.g. VBCC with -c99 option).
 */

#define  DUK_FILE_MACRO  __FILE__

#define  DUK_LINE_MACRO  __LINE__

#if !defined(__VBCC__)
#define  DUK_FUNC_MACRO  __func__
#else
#define  DUK_FUNC_MACRO  "unknown"
#endif

/* 
 *  Profile processing
 *
 *  DUK_PROFILE values:
 *    0      custom
 *    100    FULL
 *    101    FULL_DEBUG
 *    200    MINIMAL
 *    201    MINIMAL_DEBUG
 *    300    TINY
 *    301    TINY_DEBUG
 *    400    PORTABLE        [tagged types]
 *    401    PORTABLE_DEBUG  [tagged types]
 *    500    TORTURE         [tagged types + torture]
 *    501    TORTURE_DEBUG   [tagged types + torture]
 */

#if !defined(DUK_PROFILE)
#if defined(DUK_USE_PACKED_TVAL_POSSIBLE)
#define  DUK_PROFILE  100
#else
#define  DUK_PROFILE  400
#endif
#endif

#if (DUK_PROFILE > 0)

/* start with the settings for the FULL profile */

#define  DUK_USE_SELF_TEST_TVAL
#define  DUK_USE_PACKED_TVAL
#undef   DUK_USE_FULL_TVAL
#define  DUK_USE_REFERENCE_COUNTING
#define  DUK_USE_DOUBLE_LINKED_HEAP
#define  DUK_USE_MARK_AND_SWEEP
#define  DUK_USE_AUGMENT_ERRORS
#define  DUK_USE_TRACEBACKS
#undef   DUK_USE_GC_TORTURE
#undef   DUK_USE_DEBUG
#undef   DUK_USE_DDEBUG
#undef   DUK_USE_DDDEBUG
#undef   DUK_USE_DPRINT_RDTSC                       /* feature determination below */
#define  DUK_USE_VERBOSE_ERRORS
#undef   DUK_USE_ASSERTIONS
#undef   DUK_USE_VARIADIC_MACROS                    /* feature determination below */
#define  DUK_USE_PROVIDE_DEFAULT_ALLOC_FUNCTIONS
#undef   DUK_USE_EXPLICIT_NULL_INIT
#define  DUK_USE_REGEXP_SUPPORT
#define  DUK_USE_STRICT_UTF8_SOURCE
#define  DUK_USE_OCTAL_SUPPORT
#define  DUK_USE_SOURCE_NONBMP
#define  DUK_USE_DPRINT_COLORS
#define  DUK_USE_BROWSER_LIKE
#define  DUK_USE_SECTION_B

/* unaligned accesses */
#ifdef DUK_USE_UNALIGNED_ACCESSES_POSSIBLE
#define  DUK_USE_HASHBYTES_UNALIGNED_U32_ACCESS
#define  DUK_USE_HOBJECT_UNALIGNED_LAYOUT
#else
#undef   DUK_USE_HASHBYTES_UNALIGNED_U32_ACCESS
#undef   DUK_USE_HOBJECT_UNALIGNED_LAYOUT
#endif

/* profile specific modifications */

#if (DUK_PROFILE == 100)
/* FULL */
#elif (DUK_PROFILE == 101)
/* FULL_DEBUG */
#define  DUK_USE_DEBUG
#undef   DUK_USE_DDEBUG
#undef   DUK_USE_DDDEBUG
#define  DUK_USE_ASSERTIONS
#elif (DUK_PROFILE == 200)
/* MINIMAL */
#undef   DUK_USE_TRACEBACKS
#elif (DUK_PROFILE == 201)
/* MINIMAL_DEBUG */
#undef   DUK_USE_TRACEBACKS
#define  DUK_USE_DEBUG
#undef   DUK_USE_DDEBUG
#undef   DUK_USE_DDDEBUG
#define  DUK_USE_ASSERTIONS
#elif (DUK_PROFILE == 300)
/* TINY */
#undef   DUK_USE_SELF_TEST_TVAL
#undef   DUK_USE_REFERENCE_COUNTING
#undef   DUK_USE_DOUBLE_LINKED_HEAP
#define  DUK_USE_MARK_AND_SWEEP
#undef   DUK_USE_AUGMENT_ERRORS
#undef   DUK_USE_TRACEBACKS
#undef   DUK_USE_VERBOSE_ERRORS
#elif (DUK_PROFILE == 301)
/* TINY_DEBUG */
#undef   DUK_USE_SELF_TEST_TVAL
#undef   DUK_USE_REFERENCE_COUNTING
#undef   DUK_USE_DOUBLE_LINKED_HEAP
#define  DUK_USE_MARK_AND_SWEEP
#undef   DUK_USE_AUGMENT_ERRORS
#undef   DUK_USE_TRACEBACKS
#undef   DUK_USE_VERBOSE_ERRORS
#define  DUK_USE_DEBUG
#undef   DUK_USE_DDEBUG
#undef   DUK_USE_DDDEBUG
#define  DUK_USE_ASSERTIONS
#elif (DUK_PROFILE == 400)
#undef   DUK_USE_PACKED_TVAL
#undef   DUK_USE_FULL_TVAL
#define  DUK_USE_EXPLICIT_NULL_INIT
#elif (DUK_PROFILE == 401)
#undef   DUK_USE_PACKED_TVAL
#undef   DUK_USE_FULL_TVAL
#define  DUK_USE_EXPLICIT_NULL_INIT
#undef   DUK_USE_GC_TORTURE
#define  DUK_USE_DEBUG
#undef   DUK_USE_DDEBUG
#undef   DUK_USE_DDDEBUG
#define  DUK_USE_ASSERTIONS
#elif (DUK_PROFILE == 500)
#undef   DUK_USE_PACKED_TVAL
#undef   DUK_USE_FULL_TVAL
#define  DUK_USE_GC_TORTURE
#elif (DUK_PROFILE == 501)
#undef   DUK_USE_PACKED_TVAL
#undef   DUK_USE_FULL_TVAL
#define  DUK_USE_GC_TORTURE
#define  DUK_USE_DEBUG
#undef   DUK_USE_DDEBUG
#undef   DUK_USE_DDDEBUG
#undef   DUK_USE_ASSERTIONS
#else
#error unknown DUK_PROFILE
#endif

/* FIXME: how to handle constants like these? */
#if defined(DUK_USE_TRACEBACKS) && !defined(DUK_OPT_TRACEBACK_DEPTH)
#define  DUK_OPT_TRACEBACK_DEPTH  10
#endif

/*
 *  Dynamically detected features
 */

#if defined(DUK_RDTSC_AVAILABLE) && defined(DUK_OPT_DPRINT_RDTSC)
#define  DUK_USE_DPRINT_RDTSC
#else
#undef  DUK_USE_DPRINT_RDTSC
#endif

#ifdef DUK_F_C99
#define  DUK_USE_VARIADIC_MACROS
#else
#undef  DUK_USE_VARIADIC_MACROS
#endif

/* Variable size array at the end of a structure is nonportable.  There are
 * three alternatives:
 *  1) C99 (flexible array member): char buf[]
 *  2) Compiler specific (e.g. GCC): char buf[0]
 *  3) Portable but wastes memory / complicates allocation: char buf[1]
 */
/* FIXME: Currently unused, only hbuffer.h needed this at some point. */
#undef  DUK_USE_FLEX_C99
#undef  DUK_USE_FLEX_ZEROSIZE
#undef  DUK_USE_FLEX_ONESIZE
#if defined(DUK_F_C99)
#define  DUK_USE_FLEX_C99
#elif defined(__GNUC__)
#define  DUK_USE_FLEX_ZEROSIZE
#else
#define  DUK_USE_FLEX_ONESIZE
#endif

/* FIXME: GCC pragma inside a function fails in some earlier GCC versions (e.g. gcc 4.5).
 * This is very approximate but allows clean builds for development right now.
 */
/* http://gcc.gnu.org/onlinedocs/cpp/Common-Predefined-Macros.html */
#if defined(__GNUC__) && defined(__GNUC_MINOR__) && (__GNUC__ == 4) && (__GNUC_MINOR__ >= 6)
#define  DUK_USE_GCC_PRAGMAS
#else
#undef  DUK_USE_GCC_PRAGMAS
#endif

/*
 *  Date built-in platform primitive selection
 *
 *  This is a direct platform dependency which is difficult to eliminate.
 *  Select provider through defines, and then include necessary system
 *  headers so that duk_builtin_date.c compiles.
 *
 *  FIXME: add a way to provide custom functions to provide the critical
 *  primitives; this would be convenient when porting to unknown platforms
 *  (rather than muck with Duktape internals).
 */

/* NOW = getting current time (required)
 * TZO = getting local time offset (required)
 * PRS = parse datetime (optional)
 * FMT = format datetime (optional)
 */

#if defined(_WIN64)
/* Windows 64-bit */
#error WIN64 not supported
#elif defined(_WIN32) || defined(WIN32)
/* Windows 32-bit */
#error WIN32 not supported
#elif defined(__APPLE__)
/* Mac OSX, iPhone, Darwin */
#define  DUK_USE_DATE_NOW_GETTIMEOFDAY
#define  DUK_USE_DATE_TZO_GMTIME_R
#define  DUK_USE_DATE_PRS_STRPTIME
#define  DUK_USE_DATE_FMT_STRFTIME
#elif defined(__linux)
/* Linux (__unix also defined) */
#define  DUK_USE_DATE_NOW_GETTIMEOFDAY
#define  DUK_USE_DATE_TZO_GMTIME_R
#define  DUK_USE_DATE_PRS_STRPTIME
#define  DUK_USE_DATE_FMT_STRFTIME
#elif defined(__unix)
/* Other Unix */
#define  DUK_USE_DATE_NOW_GETTIMEOFDAY
#define  DUK_USE_DATE_TZO_GMTIME_R
#define  DUK_USE_DATE_PRS_STRPTIME
#define  DUK_USE_DATE_FMT_STRFTIME
#elif defined(__posix)
/* POSIX */
#define  DUK_USE_DATE_NOW_GETTIMEOFDAY
#define  DUK_USE_DATE_TZO_GMTIME_R
#define  DUK_USE_DATE_PRS_STRPTIME
#define  DUK_USE_DATE_FMT_STRFTIME
#elif defined(DUK_F_TOS)
/* Atari ST TOS */
#define  DUK_USE_DATE_NOW_TIME
#define  DUK_USE_DATE_TZO_GMTIME
/* no parsing (not an error) */
#define  DUK_USE_DATE_FMT_STRFTIME
#elif defined(DUK_F_AMIGAOS)
/* AmigaOS */
#define  DUK_USE_DATE_NOW_TIME
#define  DUK_USE_DATE_TZO_GMTIME
/* no parsing (not an error) */
#define  DUK_USE_DATE_FMT_STRFTIME
#else
#error platform not supported
#endif

#if defined(DUK_USE_DATE_NOW_GETTIMEOFDAY)
#include <sys/time.h>
#endif

#if defined(DUK_USE_DATE_TZO_GMTIME) || \
    defined(DUK_USE_DATE_TZO_GMTIME_R) || \
    defined(DUK_USE_DATE_PRS_STRPTIME) || \
    defined(DUK_USE_DATE_FMT_STRFTIME)
/* just a sanity check */
#if defined(__linux) && !defined(_XOPEN_SOURCE)
#error expected _XOPEN_SOURCE to be defined here
#endif
#include <time.h>
#endif

#else  /* DUK_PROFILE > 0 */

/*
 *  All DUK_USE_ defines must be defined manually, no compiler
 *  or platform feature detection.
 */

#endif  /* DUK_PROFILE > 0 */

/* FIXME: An alternative approach to customization would be to include
 * some user define file at this point.  The user file could then modify
 * the base settings.  Something like:
 * #ifdef DUK_CUSTOM_HEADER
 * #include "duk_custom.h"
 * #endif
 */

#endif  /* DUK_FEATURES_H_INCLUDED */

