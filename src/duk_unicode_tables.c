/*
 *  Unicode support tables automatically generated during build.
 */

#include "duk_internal.h"

/*
 *  Unicode tables containing ranges of Unicode characters in a
 *  packed format.  These tables are used to match non-ASCII
 *  characters of complex productions by resorting to a linear
 *  range-by-range comparison.  This is very slow, but is expected
 *  to be very rare in practical Ecmascript source code, and thus
 *  compactness is most important.
 *
 *  The tables are matched using uni_range_match() and the format
 *  is described in src/extract_chars.py.
 */

#ifdef DUK_USE_SOURCE_NONBMP
/* IdentifierStart production with ASCII excluded */
/* duk_unicode_identifier_start_noascii[] */
#include "duk_unicode_ids_noa.c"
#else
/* IdentifierStart production with ASCII and non-BMP excluded */
/* duk_unicode_identifier_start_noascii_bmponly[] */
#include "duk_unicode_ids_noa_bmpo.c"
#endif

#ifdef DUK_USE_SOURCE_NONBMP
/* IdentifierPart production with IdentifierStart and ASCII excluded */
/* duk_unicode_identifier_part_minus_identifier_start_noascii[] */
#include "duk_unicode_idp_m_ids_noa.c"
#else
/* IdentifierPart production with IdentifierStart, ASCII, and non-BMP excluded */
/* duk_unicode_identifier_part_minus_identifier_start_noascii_bmponly[] */
#include "duk_unicode_idp_m_ids_noa_bmpo.c"
#endif

/*
 *  Case conversion tables generated using src/extract_caseconv.py.
 */

/* duk_unicode_caseconv_uc[] */
/* duk_unicode_caseconv_lc[] */

#include "duk_unicode_caseconv.c"

