/*
 *  Number-to-string and string-to-number conversions.
 *
 *  Slow path number-to-string and string-to-number conversion is based on
 *  a Dragon4 variant, with fast paths for small integers.  Big integer
 *  arithmetic is needed for guaranteeing that the conversion is correct
 *  and uses a minimum number of digits.  The big number arithmetic has a
 *  fixed maximum size and does not require dynamic allocations.
 *
 *  See: doc/number-conversion.txt.
 */

#include "duk_internal.h"

#define IEEE_DOUBLE_EXP_BIAS  1023
#define IEEE_DOUBLE_EXP_MIN   (-1022)   /* biased exp == 0 -> denormal, exp -1022 */

#define DIGITCHAR(x)  duk_lc_digits[(x)]

/*
 *  Tables generated with src/gennumdigits.py.
 *
 *  str2num_digits_for_radix indicates, for each radix, how many input
 *  digits should be considered significant for string-to-number conversion.
 *  The input is also padded to this many digits to give the Dragon4
 *  conversion enough (apparent) precision to work with.
 *
 *  str2num_exp_limits indicates, for each radix, the radix-specific
 *  minimum/maximum exponent values (for a Dragon4 integer mantissa)
 *  below and above which the number is guaranteed to underflow to zero
 *  or overflow to Infinity.  This allows parsing to keep bigint values
 *  bounded.
 */

static const unsigned char str2num_digits_for_radix[] = {
	69, 44, 35, 30, 27, 25, 23, 22, 20, 20,    /* 2 to 11 */
	20, 19, 19, 18, 18, 17, 17, 17, 16, 16,    /* 12 to 21 */
	16, 16, 16, 15, 15, 15, 15, 15, 15, 14,    /* 22 to 31 */
	14, 14, 14, 14, 14                         /* 31 to 36 */
};

typedef struct {
	duk_int16_t upper;
	duk_int16_t lower;
} duk_exp_limits;

static const duk_exp_limits str2num_exp_limits[] = {
	{ 957, -1147 }, { 605, -725 },  { 479, -575 },  { 414, -496 },
	{ 372, -446 },  { 342, -411 },  { 321, -384 },  { 304, -364 },
	{ 291, -346 },  { 279, -334 },  { 268, -323 },  { 260, -312 },
	{ 252, -304 },  { 247, -296 },  { 240, -289 },  { 236, -283 },
	{ 231, -278 },  { 227, -273 },  { 223, -267 },  { 220, -263 },
	{ 216, -260 },  { 213, -256 },  { 210, -253 },  { 208, -249 },
	{ 205, -246 },  { 203, -244 },  { 201, -241 },  { 198, -239 },
	{ 196, -237 },  { 195, -234 },  { 193, -232 },  { 191, -230 },
	{ 190, -228 },  { 188, -226 },  { 187, -225 },
};

/*
 *  Limited functionality bigint implementation.
 *
 *  Restricted to non-negative numbers with less than 32 * BI_MAX_PARTS bits,
 *  with the caller responsible for ensuring this is never exceeded.  No memory
 *  allocation (except stack) is needed for bigint computation.  Operations
 *  have been tailored for number conversion needs.
 *
 *  Argument order is "assignment order", i.e. target first, then arguments:
 *  x <- y * z  -->  bi_mul(x, y, z);
 */

/* This upper value has been experimentally determined; debug build will check
 * bigint size with assertions.
 */
#define BI_MAX_PARTS  37  /* 37x32 = 1184 bits */

#ifdef DUK_USE_DDDEBUG
#define BI_PRINT(name,x)  bi_print((name),(x))
#else
#define BI_PRINT(name,x)
#endif

/* Current size is about 152 bytes. */
typedef struct {
	int n;
	duk_uint32_t v[BI_MAX_PARTS];  /* low to high */
} duk_bigint;

#ifdef DUK_USE_DDDEBUG
static void bi_print(const char *name, duk_bigint *x) {
	/* Overestimate required size; debug code so not critical to be tight. */
	char buf[BI_MAX_PARTS * 9 + 64];
	char *p = buf;
	int i;

	/* No NUL term checks in this debug code. */
	p += DUK_SPRINTF(p, "%p n=%d", (void *) x, x->n);
	if (x->n == 0) {
		p += DUK_SPRINTF(p, " 0");
	}
	for (i = x->n - 1; i >= 0; i--) {
		p += DUK_SPRINTF(p, " %08x", (unsigned int) x->v[i]);
	}

	DUK_DDDPRINT("%s: %s", name, buf);
}
#endif

#ifdef DUK_USE_ASSERTIONS
static int bi_is_valid(duk_bigint *x) {
	int is_normalized = (x->n == 0) || (x->v[x->n - 1] != 0);
	int is_valid_size = (x->n >= 0) && (x->n <= BI_MAX_PARTS);
	return is_normalized && is_valid_size;
}
#endif

static void bi_normalize(duk_bigint *x) {
	int i;

	for (i = x->n - 1; i >= 0; i--) {
		if (x->v[i] != 0) {
			break;
		}
	}

	/* Note: if 'x' is zero, x->n becomes 0 here */
	x->n = i + 1;
	DUK_ASSERT(bi_is_valid(x));
}

/* x <- y */
static void bi_copy(duk_bigint *x, duk_bigint *y) {
	int n;

	n = y->n;
	x->n = n;
	if (n == 0) {
		return;
	}
	DUK_MEMCPY((void *) x->v, (void *) y->v, (size_t) (sizeof(duk_uint32_t) * n));
}

static void bi_set_small(duk_bigint *x, duk_uint32_t v) {
	if (v == 0) {
		x->n = 0;
	} else {
		x->n = 1;
		x->v[0] = v;
	}
	DUK_ASSERT(bi_is_valid(x));
}

/* Return value: <0  <=>  x < y
 *                0  <=>  x == y
 *               >0  <=>  x > y
 */
static int bi_compare(duk_bigint *x, duk_bigint *y) {
	int i;
	int nx, ny;
	duk_uint32_t tx, ty;

	DUK_ASSERT(bi_is_valid(x));
	DUK_ASSERT(bi_is_valid(y));

	nx = x->n;
	ny = y->n;
	if (nx > ny) {
		goto ret_gt;
	}
	if (nx < ny) {
		goto ret_lt;
	}
	for (i = nx - 1; i >= 0; i--) {
		tx = x->v[i];
		ty = y->v[i];

		if (tx > ty) {
			goto ret_gt;
		}
		if (tx < ty) {
			goto ret_lt;
		}
	}

	return 0;

 ret_gt:
	return 1;

 ret_lt:
	return -1;
}

/* x <- y + z */
#ifdef DUK_USE_64BIT_OPS
static void bi_add(duk_bigint *x, duk_bigint *y, duk_bigint *z) {
	duk_uint64_t tmp;
	int i, ny, nz;

	DUK_ASSERT(bi_is_valid(y));
	DUK_ASSERT(bi_is_valid(z));

	if (z->n > y->n) {
		duk_bigint *t;
		t = y; y = z; z = t;
	}
	DUK_ASSERT(y->n >= z->n);

	ny = y->n; nz = z->n;
	tmp = 0;
	for (i = 0; i < ny; i++) {
		DUK_ASSERT(i < BI_MAX_PARTS);
		tmp += y->v[i];
		if (i < nz) {
			tmp += z->v[i];
		}
		x->v[i] = (duk_uint32_t) (tmp & 0xffffffffUL);
		tmp = tmp >> 32;
	}
	if (tmp != 0) {
		DUK_ASSERT(i < BI_MAX_PARTS);
		x->v[i++] = (duk_uint32_t) tmp;
	}
	x->n = i;
	DUK_ASSERT(x->n <= BI_MAX_PARTS);

	/* no need to normalize */
	DUK_ASSERT(bi_is_valid(x));
}
#else  /* DUK_USE_64BIT_OPS */
static void bi_add(duk_bigint *x, duk_bigint *y, duk_bigint *z) {
	duk_uint32_t carry, tmp1, tmp2;
	int i, ny, nz;

	DUK_ASSERT(bi_is_valid(y));
	DUK_ASSERT(bi_is_valid(z));

	if (z->n > y->n) {
		duk_bigint *t;
		t = y; y = z; z = t;
	}
	DUK_ASSERT(y->n >= z->n);

	ny = y->n; nz = z->n;
	carry = 0;
	for (i = 0; i < ny; i++) {
		/* Carry is detected based on wrapping which relies on exact 32-bit
		 * types.
		 */
		DUK_ASSERT(i < BI_MAX_PARTS);
		tmp1 = y->v[i];
		tmp2 = tmp1;
		if (i < nz) {
			tmp2 += z->v[i];
		}

		/* Careful with carry condition:
		 *  - If carry not added: 0x12345678 + 0 + 0xffffffff = 0x12345677 (< 0x12345678)
		 *  - If carry added:     0x12345678 + 1 + 0xffffffff = 0x12345678 (== 0x12345678)
		 */
		if (carry) {
			tmp2++;
			carry = (tmp2 <= tmp1 ? 1 : 0);
		} else {
			carry = (tmp2 < tmp1 ? 1 : 0);
		}

		x->v[i] = tmp2;
	}
	if (carry) {
		DUK_ASSERT(i < BI_MAX_PARTS);
		DUK_ASSERT(carry == 1);
		x->v[i++] = carry;
	}
	x->n = i;
	DUK_ASSERT(x->n <= BI_MAX_PARTS);

	/* no need to normalize */
	DUK_ASSERT(bi_is_valid(x));
}
#endif  /* DUK_USE_64BIT_OPS */

/* x <- y + z */
static void bi_add_small(duk_bigint *x, duk_bigint *y, duk_uint32_t z) {
	duk_bigint tmp;

	DUK_ASSERT(bi_is_valid(y));

	/* FIXME: optimize, though only one caller now */
	bi_set_small(&tmp, z);
	bi_add(x, y, &tmp);

	DUK_ASSERT(bi_is_valid(x));
}

#if 0  /* unused */
/* x <- x + y, use t as temp */
static void bi_add_copy(duk_bigint *x, duk_bigint *y, duk_bigint *t) {
	bi_add(t, x, y);
	bi_copy(x, t);
}
#endif

/* x <- y - z, require x >= y => z >= 0, i.e. y >= z */
#ifdef DUK_USE_64BIT_OPS
static void bi_sub(duk_bigint *x, duk_bigint *y, duk_bigint *z) {
	int ny, nz;
	int i;
	duk_uint32_t ty, tz;
	int64_t tmp;

	DUK_ASSERT(bi_is_valid(y));
	DUK_ASSERT(bi_is_valid(z));
	DUK_ASSERT(bi_compare(y, z) >= 0);
	DUK_ASSERT(y->n >= z->n);

	ny = y->n; nz = z->n;
	tmp = 0;
	for (i = 0; i < ny; i++) {
		ty = y->v[i];
		if (i < nz) {
			tz = z->v[i];
		} else {
			tz = 0;
		}
		tmp = (int64_t) ty - (int64_t) tz + tmp;
		x->v[i] = (duk_uint32_t) (tmp & 0xffffffffUL);
		tmp = tmp >> 32;  /* 0 or -1 */
	}
	DUK_ASSERT(tmp == 0);

	x->n = i;
	bi_normalize(x);  /* need to normalize, may even cancel to 0 */
	DUK_ASSERT(bi_is_valid(x));
}
#else
static void bi_sub(duk_bigint *x, duk_bigint *y, duk_bigint *z) {
	int ny, nz;
	int i;
	duk_uint32_t tmp1, tmp2, borrow;

	DUK_ASSERT(bi_is_valid(y));
	DUK_ASSERT(bi_is_valid(z));
	DUK_ASSERT(bi_compare(y, z) >= 0);
	DUK_ASSERT(y->n >= z->n);

	ny = y->n; nz = z->n;
	borrow = 0;
	for (i = 0; i < ny; i++) {
		/* Borrow is detected based on wrapping which relies on exact 32-bit
		 * types.
		 */
		tmp1 = y->v[i];
		tmp2 = tmp1;
		if (i < nz) {
			tmp2 -= z->v[i];
		}

		/* Careful with borrow condition:
		 *  - If borrow not subtracted: 0x12345678 - 0 - 0xffffffff = 0x12345679 (> 0x12345678)
		 *  - If borrow subtracted:     0x12345678 - 1 - 0xffffffff = 0x12345678 (== 0x12345678)
		 */
		if (borrow) {
			tmp2--;
			borrow = (tmp2 >= tmp1 ? 1 : 0);
		} else {
			borrow = (tmp2 > tmp1 ? 1 : 0);
		}

		x->v[i] = tmp2;
	}
	DUK_ASSERT(borrow == 0);

	x->n = i;
	bi_normalize(x);  /* need to normalize, may even cancel to 0 */
	DUK_ASSERT(bi_is_valid(x));
}
#endif

#if 0  /* unused */
/* x <- y - z */
static void bi_sub_small(duk_bigint *x, duk_bigint *y, duk_uint32_t z) {
	duk_bigint tmp;

	DUK_ASSERT(bi_is_valid(y));

	/* FIXME: optimize */
	bi_set_small(&tmp, z);
	bi_sub(x, y, &tmp);

	DUK_ASSERT(bi_is_valid(x));
}
#endif

/* x <- x - y, use t as temp */
static void bi_sub_copy(duk_bigint *x, duk_bigint *y, duk_bigint *t) {
	bi_sub(t, x, y);
	bi_copy(x, t);
}

/* x <- y * z */
static void bi_mul(duk_bigint *x, duk_bigint *y, duk_bigint *z) {
	int i, j, nx, nz;

	DUK_ASSERT(bi_is_valid(y));
	DUK_ASSERT(bi_is_valid(z));

	nx = y->n + z->n;  /* max possible */
	DUK_ASSERT(nx <= BI_MAX_PARTS);

	if (nx == 0) {
		/* Both inputs are zero; cases where only one is zero can go
		 * through main algorithm.
		 */
		x->n = 0;
		return;
	}

	DUK_MEMSET((void *) x->v, 0, (size_t) (sizeof(duk_uint32_t) * nx));
	x->n = nx;

	nz = z->n;
	for (i = 0; i < y->n; i++) {
#ifdef DUK_USE_64BIT_OPS
		duk_uint64_t tmp = 0;
		for (j = 0; j < nz; j++) {
			tmp += (duk_uint64_t) y->v[i] * (duk_uint64_t) z->v[j] + x->v[i+j];
			x->v[i+j] = (duk_uint32_t) (tmp & 0xffffffffUL);
			tmp = tmp >> 32;
		}
		if (tmp > 0) {
			DUK_ASSERT(i + j < nx);
			DUK_ASSERT(i + j < BI_MAX_PARTS);
			DUK_ASSERT(x->v[i+j] == 0);
			x->v[i+j] = (duk_uint32_t) tmp;
		}
#else
		/*
		 *  Multiply + add + carry for 32-bit components using only 16x16->32
		 *  multiplies and carry detection based on unsigned overflow.
		 *
		 *    1st mult, 32-bit: (A*2^16 + B)
		 *    2nd mult, 32-bit: (C*2^16 + D)
		 *    3rd add, 32-bit: E
		 *    4th add, 32-bit: F
		 *
		 *      (AC*2^16 + B) * (C*2^16 + D) + E + F
		 *    = AC*2^32 + AD*2^16 + BC*2^16 + BD + E + F
		 *    = AC*2^32 + (AD + BC)*2^16 + (BD + E + F)
		 *    = AC*2^32 + AD*2^16 + BC*2^16 + (BD + E + F)
		 */
		duk_uint32_t a, b, c, d, e, f;
		duk_uint32_t r, s, t;

		a = y->v[i]; b = a & 0xffffUL; a = a >> 16;

		f = 0;
		for (j = 0; j < nz; j++) {
			c = z->v[j]; d = c & 0xffffUL; c = c >> 16;
			e = x->v[i+j];

			/* build result as: (r << 32) + s: start with (BD + E + F) */
			r = 0;
			s = b * d;

			/* add E */
			t = s + e;
			if (t < s) { r++; }  /* carry */
			s = t;

			/* add F */
			t = s + f;
			if (t < s) { r++; }  /* carry */
			s = t;

			/* add BC*2^16 */
			t = b * c;
			r += (t >> 16);
			t = s + ((t & 0xffffUL) << 16);
			if (t < s) { r++; }  /* carry */
			s = t;

			/* add AD*2^16 */
			t = a * d;
			r += (t >> 16);
			t = s + ((t & 0xffffUL) << 16);
			if (t < s) { r++; }  /* carry */
			s = t;

			/* add AC*2^32 */
			t = a * c;
			r += t;

			DUK_DDDPRINT("ab=%08x cd=%08x ef=%08x -> rs=%08x %08x", y->v[i], z->v[j], x->v[i+j], r, s);

			x->v[i+j] = s;
			f = r;
		}
		if (f > 0) {
			DUK_ASSERT(i + j < nx);
			DUK_ASSERT(i + j < BI_MAX_PARTS);
			DUK_ASSERT(x->v[i+j] == 0);
			x->v[i+j] = (duk_uint32_t) f;
		}
#endif  /* DUK_USE_64BIT_OPS */
	}

	bi_normalize(x);
	DUK_ASSERT(bi_is_valid(x));
}

/* x <- y * z */
static void bi_mul_small(duk_bigint *x, duk_bigint *y, duk_uint32_t z) {
	duk_bigint tmp;

	DUK_ASSERT(bi_is_valid(y));

	/* FIXME: optimize */
	bi_set_small(&tmp, z);
	bi_mul(x, y, &tmp);

	DUK_ASSERT(bi_is_valid(x));
}

/* x <- x * y, use t as temp */
static void bi_mul_copy(duk_bigint *x, duk_bigint *y, duk_bigint *t) {
	bi_mul(t, x, y);
	bi_copy(x, t);
}

/* x <- x * y, use t as temp */
static void bi_mul_small_copy(duk_bigint *x, duk_uint32_t y, duk_bigint *t) {
	bi_mul_small(t, x, y);
	bi_copy(x, t);
}

static int bi_is_even(duk_bigint *x) {
	DUK_ASSERT(bi_is_valid(x));
	return (x->n == 0) || ((x->v[0] & 0x01) == 0);
}

static int bi_is_zero(duk_bigint *x) {
	DUK_ASSERT(bi_is_valid(x));
	return (x->n == 0);  /* this is the case for normalized numbers */
}

/* Bigint is 2^52.  Used to detect normalized IEEE double mantissa values
 * which are at the lowest edge (next floating point value downwards has
 * a different exponent).  The lowest mantissa has the form:
 *
 *     1000........000    (52 zeroes; only "hidden bit" is set)
 */
static int bi_is_2to52(duk_bigint *x) {
	DUK_ASSERT(bi_is_valid(x));
	return (x->n == 2) && (x->v[0] == 0) && (x->v[1] == (1 << (52-32)));
}

/* x <- (1<<y) */
static void bi_twoexp(duk_bigint *x, int y) {
	int n, r;

	n = (y / 32) + 1;
	DUK_ASSERT(n > 0);
	r = y % 32;
	DUK_MEMSET((void *) x->v, 0, sizeof(duk_uint32_t) * n);
	x->n = n;
	x->v[n - 1] = (((duk_uint32_t) 1) << r);
}

/* x <- b^y; use t1 and t2 as temps */
static void bi_exp_small(duk_bigint *x, int b, int y, duk_bigint *t1, duk_bigint *t2) {
	/* Fast path the binary case */

	DUK_ASSERT(x != t1 && x != t2 && t1 != t2);  /* distinct bignums, easy mistake to make */
	DUK_ASSERT(b >= 0);
	DUK_ASSERT(y >= 0);

	if (b == 2) {
		bi_twoexp(x, y);
		return;
	}

	/* http://en.wikipedia.org/wiki/Exponentiation_by_squaring */

	DUK_DDDPRINT("exp_small: b=%d, y=%d", b, y);

	bi_set_small(x, 1);
	bi_set_small(t1, b);
	for (;;) {
		/* Loop structure ensures that we don't compute t1^2 unnecessarily
		 * on the final round, as that might create a bignum exceeding the
		 * current BI_MAX_PARTS limit.
		 */
		if (y & 0x01) {
			bi_mul_copy(x, t1, t2);
		}
		y = y >> 1;
		if (y == 0) {
			break;
		}
		bi_mul_copy(t1, t1, t2);
	}

	BI_PRINT("exp_small result", x);
}

/*
 *  A Dragon4 number-to-string variant, based on:
 *
 *    Guy L. Steele Jr., Jon L. White: "How to Print Floating-Point Numbers
 *    Accurately"
 *
 *    Robert G. Burger, R. Kent Dybvig: "Printing Floating-Point Numbers
 *    Quickly and Accurately"
 *
 *  The current algorithm is based on Figure 1 of the Burger-Dybvig paper,
 *  i.e. the base implementation without logarithm estimation speedups
 *  (these would increase code footprint considerably).  Fixed-format output
 *  does not follow the suggestions in the paper; instead, we generate an
 *  extra digit and round-with-carry.
 *
 *  The same algorithm is used for number parsing (with b=10 and B=2)
 *  by generating one extra digit and doing rounding manually.
 *
 *  See doc/number-conversion.txt for limitations.
 */

/* Maximum number of digits generated. */
#define MAX_OUTPUT_DIGITS          1040  /* (Number.MAX_VALUE).toString(2).length == 1024, + spare */

/* Maximum number of characters in formatted value. */
#define MAX_FORMATTED_LENGTH       1040  /* (-Number.MAX_VALUE).toString(2).length == 1025, + spare */

/* Number and (minimum) size of bigints in the nc_ctx structure. */
#define NUMCONV_CTX_NUM_BIGINTS    7
#define NUMCONV_CTX_BIGINTS_SIZE   (sizeof(duk_bigint) * NUMCONV_CTX_NUM_BIGINTS)

typedef struct {
	/* Currently about 7*152 = 1064 bytes.  The space for these
	 * duk_bigints is used also as a temporary buffer for generating
	 * the final string.  This is a bit awkard; a union would be
	 * more correct.
	 */
	duk_bigint f, r, s, mp, mm, t1, t2;

	int is_s2n;        /* if 1, doing a string-to-number; else doing a number-to-string */
	int is_fixed;      /* if 1, doing a fixed format output (not free format) */
	int req_digits;    /* request number of output digits; 0 = free-format */
	int abs_pos;       /* digit position is absolute, not relative */
	int e;             /* exponent for 'f' */
	int b;             /* input radix */
	int B;             /* output radix */
	int k;             /* see algorithm */
	int low_ok;        /* see algorithm */
	int high_ok;       /* see algorithm */
	int unequal_gaps;  /* m+ != m- (very rarely) */

	/* Buffer used for generated digits, values are in the range [0,B-1]. */
	char digits[MAX_OUTPUT_DIGITS];
	int count;  /* digit count */
} duk_numconv_stringify_ctx;

/* Note: computes with 'idx' in assertions, so caller beware.
 * 'idx' is preincremented, i.e. '1' on first call, because it
 * is more convenient for the caller.
 */
#define DRAGON4_OUTPUT(nc_ctx,preinc_idx,x)  do { \
		DUK_ASSERT((preinc_idx) - 1 >= 0); \
		DUK_ASSERT((preinc_idx) - 1 < MAX_OUTPUT_DIGITS); \
		((nc_ctx)->digits[(preinc_idx) - 1]) = (x); \
	} while(0)

size_t dragon4_format_uint32(char *buf, unsigned int x, int radix) {
	char *p;
	size_t len;
	int dig;
	int t;

	DUK_ASSERT(radix >= 2 && radix <= 36);

	/* A 32-bit unsigned integer formats to at most 32 digits (the
	 * worst case happens with radix == 2).  Output the digits backwards,
	 * and use a memmove() to get them in the right place.
	 */

	p = buf + 32;
	for (;;) {
		t = x / radix;
		dig = x - t * radix;
		x = t;

		DUK_ASSERT(dig >= 0 && dig < 36);
		*(--p) = DIGITCHAR(dig);

		if (x == 0) {
			break;
		}
	}
	len = (buf + 32) - p;

	DUK_MEMMOVE((void *) buf, (void *) p, len);

	return len;
}

static void dragon4_prepare(duk_numconv_stringify_ctx *nc_ctx) {
	int lowest_mantissa;

#if 1
	/* Assume IEEE round-to-even, so that shorter encoding can be used
	 * when round-to-even would produce correct result.  By removing
	 * this check (and having low_ok == high_ok == 0) the results would
	 * still be accurate but in some cases longer than necessary.
	 */
	if (bi_is_even(&nc_ctx->f)) {
		DUK_DDDPRINT("f is even");
		nc_ctx->low_ok = 1;
		nc_ctx->high_ok = 1;
	} else {
		DUK_DDDPRINT("f is odd");
		nc_ctx->low_ok = 0;
		nc_ctx->high_ok = 0;
	}
#else
	/* Note: not honoring round-to-even should work but now generates incorrect
	 * results.  For instance, 1e23 serializes to "a000...", i.e. the first digit
	 * equals the radix (10).  Scaling stops one step too early in this case.
	 * Don't know why this is the case, but since this code path is unused, it
	 * doesn't matter.
	 */
	nc_ctx->low_ok = 0;
	nc_ctx->high_ok = 0;
#endif

	/* For string-to-number, pretend we never have the lowest mantissa as there
	 * is no natural "precision" for inputs.  Having lowest_mantissa == 0, we'll
	 * fall into the base cases for both e >= 0 and e < 0.
	 */
	if (nc_ctx->is_s2n) {
		lowest_mantissa = 0;
	} else {
		lowest_mantissa = bi_is_2to52(&nc_ctx->f);
	}

	nc_ctx->unequal_gaps = 0;
	if (nc_ctx->e >= 0) {
		/* exponent non-negative (and thus not minimum exponent) */

		if (lowest_mantissa) {
			/* (>= e 0) AND (= f (expt b (- p 1)))
			 *
			 * be <- (expt b e) == b^e
			 * be1 <- (* be b) == (expt b (+ e 1)) == b^(e+1)
			 * r <- (* f be1 2) == 2 * f * b^(e+1)    [if b==2 -> f * b^(e+2)]
			 * s <- (* b 2)                           [if b==2 -> 4]
			 * m+ <- be1 == b^(e+1)
			 * m- <- be == b^e
			 * k <- 0
			 * B <- B
			 * low_ok <- round
			 * high_ok <- round
			 */

			DUK_DDDPRINT("non-negative exponent (not smallest exponent); "
			             "lowest mantissa value for this exponent -> "
			             "unequal gaps");

			bi_exp_small(&nc_ctx->mm, nc_ctx->b, nc_ctx->e, &nc_ctx->t1, &nc_ctx->t2);  /* mm <- b^e */
			bi_mul_small(&nc_ctx->mp, &nc_ctx->mm, nc_ctx->b);  /* mp <- b^(e+1) */
			bi_mul_small(&nc_ctx->t1, &nc_ctx->f, 2);
			bi_mul(&nc_ctx->r, &nc_ctx->t1, &nc_ctx->mp);       /* r <- (2 * f) * b^(e+1) */
			bi_set_small(&nc_ctx->s, nc_ctx->b * 2);            /* s <- 2 * b */
			nc_ctx->unequal_gaps = 1;
		} else {
			/* (>= e 0) AND (not (= f (expt b (- p 1))))
			 *
			 * be <- (expt b e) == b^e
			 * r <- (* f be 2) == 2 * f * b^e    [if b==2 -> f * b^(e+1)]
			 * s <- 2
			 * m+ <- be == b^e
			 * m- <- be == b^e
			 * k <- 0
			 * B <- B
			 * low_ok <- round
			 * high_ok <- round
			 */

			DUK_DDDPRINT("non-negative exponent (not smallest exponent); "
			             "not lowest mantissa for this exponent -> "
			             "equal gaps");

			bi_exp_small(&nc_ctx->mm, nc_ctx->b, nc_ctx->e, &nc_ctx->t1, &nc_ctx->t2);  /* mm <- b^e */
			bi_copy(&nc_ctx->mp, &nc_ctx->mm);                /* mp <- b^e */
			bi_mul_small(&nc_ctx->t1, &nc_ctx->f, 2);
			bi_mul(&nc_ctx->r, &nc_ctx->t1, &nc_ctx->mp);     /* r <- (2 * f) * b^e */
			bi_set_small(&nc_ctx->s, 2);                      /* s <- 2 */
		}
	} else {
		/* When doing string-to-number, lowest_mantissa is always 0 so
		 * the exponent check, while incorrect, won't matter.
		 */
		if (nc_ctx->e > IEEE_DOUBLE_EXP_MIN /*not minimum exponent*/ &&
		    lowest_mantissa /* lowest mantissa for this exponent*/) {
			/* r <- (* f b 2)                                [if b==2 -> (* f 4)]
			 * s <- (* (expt b (- 1 e)) 2) == b^(1-e) * 2    [if b==2 -> b^(2-e)]
			 * m+ <- b == 2
			 * m- <- 1
			 * k <- 0
			 * B <- B
			 * low_ok <- round
			 * high_ok <- round
			 */

			DUK_DDDPRINT("negative exponent; not minimum exponent and "
			             "lowest mantissa for this exponent -> "
			             "unequal gaps");

			bi_mul_small(&nc_ctx->r, &nc_ctx->f, nc_ctx->b * 2);  /* r <- (2 * b) * f */
			bi_exp_small(&nc_ctx->t1, nc_ctx->b, 1 - nc_ctx->e, &nc_ctx->s, &nc_ctx->t2);  /* NB: use 's' as temp on purpose */
			bi_mul_small(&nc_ctx->s, &nc_ctx->t1, 2);             /* s <- b^(1-e) * 2 */
			bi_set_small(&nc_ctx->mp, 2);
			bi_set_small(&nc_ctx->mm, 1);
			nc_ctx->unequal_gaps = 1;
		} else {
			/* r <- (* f 2)
			 * s <- (* (expt b (- e)) 2) == b^(-e) * 2    [if b==2 -> b^(1-e)]
			 * m+ <- 1
			 * m- <- 1
			 * k <- 0
			 * B <- B
			 * low_ok <- round
			 * high_ok <- round
			 */

			DUK_DDDPRINT("negative exponent; minimum exponent or not "
			             "lowest mantissa for this exponent -> "
			             "equal gaps");

			bi_mul_small(&nc_ctx->r, &nc_ctx->f, 2);            /* r <- 2 * f */
			bi_exp_small(&nc_ctx->t1, nc_ctx->b, -nc_ctx->e, &nc_ctx->s, &nc_ctx->t2);  /* NB: use 's' as temp on purpose */
			bi_mul_small(&nc_ctx->s, &nc_ctx->t1, 2);           /* s <- b^(-e) * 2 */
			bi_set_small(&nc_ctx->mp, 1);
			bi_set_small(&nc_ctx->mm, 1);
		}
	}
}

static void dragon4_scale(duk_numconv_stringify_ctx *nc_ctx) {
	int k = 0;

	/* This is essentially the 'scale' algorithm, with recursion removed.
	 * Note that 'k' is either correct immediately, or will move in one
	 * direction in the loop.  There's no need to do the low/high checks
	 * on every round (like the Scheme algorithm does).
	 *
	 * The scheme algorithm finds 'k' and updates 's' simultaneously,
	 * while the logical algorithm finds 'k' with 's' having its initial
	 * value, after which 's' is updated separately (see the Burger-Dybvig
	 * paper, Section 3.1, steps 2 and 3).
	 *
	 * The case where m+ == m- (almost always) is optimized for, because
	 * it reduces the bigint operations considerably and almost always
	 * applies.  The scale loop only needs to work with m+, so this works.
	 */

	/* XXX: this algorithm could be optimized quite a lot by using e.g.
	 * a logarithm based estimator for 'k' and performing B^n multiplication
	 * using a lookup table or using some bit-representation based exp
	 * algorithm.  Currently we just loop, with significant performance
	 * impact for very large and very small numbers.
	 */

	DUK_DDDPRINT("scale: B=%d, low_ok=%d, high_ok=%d", nc_ctx->B, nc_ctx->low_ok, nc_ctx->high_ok);
	BI_PRINT("r(init)", &nc_ctx->r);
	BI_PRINT("s(init)", &nc_ctx->s);
	BI_PRINT("mp(init)", &nc_ctx->mp);
	BI_PRINT("mm(init)", &nc_ctx->mm);

	for (;;) {
		DUK_DDDPRINT("scale loop (inc k), k=%d", k);
		BI_PRINT("r", &nc_ctx->r);
		BI_PRINT("s", &nc_ctx->s);
		BI_PRINT("m+", &nc_ctx->mp);
		BI_PRINT("m-", &nc_ctx->mm);

		bi_add(&nc_ctx->t1, &nc_ctx->r, &nc_ctx->mp);  /* t1 = (+ r m+) */
		if (bi_compare(&nc_ctx->t1, &nc_ctx->s) >= (nc_ctx->high_ok ? 0 : 1)) {
			DUK_DDDPRINT("k is too low");
			/* r <- r
			 * s <- (* s B)
			 * m+ <- m+
			 * m- <- m-
			 * k <- (+ k 1)
			 */

			bi_mul_small_copy(&nc_ctx->s, nc_ctx->B, &nc_ctx->t1);
			k++;
		} else {
			break;
		}
	}

	/* k > 0 -> k was too low, and cannot be too high */
	if (k > 0) {
		goto skip_dec_k;
	}

	for (;;) {
		DUK_DDDPRINT("scale loop (dec k), k=%d", k);
		BI_PRINT("r", &nc_ctx->r);
		BI_PRINT("s", &nc_ctx->s);
		BI_PRINT("m+", &nc_ctx->mp);
		BI_PRINT("m-", &nc_ctx->mm);

		bi_add(&nc_ctx->t1, &nc_ctx->r, &nc_ctx->mp);  /* t1 = (+ r m+) */
		bi_mul_small(&nc_ctx->t2, &nc_ctx->t1, nc_ctx->B);   /* t2 = (* (+ r m+) B) */
		if (bi_compare(&nc_ctx->t2, &nc_ctx->s) <= (nc_ctx->high_ok ? -1 : 0)) {
			DUK_DDDPRINT("k is too high");
			/* r <- (* r B)
			 * s <- s
			 * m+ <- (* m+ B)
			 * m- <- (* m- B)
			 * k <- (- k 1)
			 */
			bi_mul_small_copy(&nc_ctx->r, nc_ctx->B, &nc_ctx->t1);
			bi_mul_small_copy(&nc_ctx->mp, nc_ctx->B, &nc_ctx->t1);
			if (nc_ctx->unequal_gaps) {
				DUK_DDDPRINT("m+ != m- -> need to update m- too");
				bi_mul_small_copy(&nc_ctx->mm, nc_ctx->B, &nc_ctx->t1);
			}
			k--;
		} else {
			break;
		}
	}

 skip_dec_k:

	if (!nc_ctx->unequal_gaps) {
		DUK_DDDPRINT("equal gaps, copy m- from m+");
		bi_copy(&nc_ctx->mm, &nc_ctx->mp);  /* mm <- mp */
	}
	nc_ctx->k = k;

	DUK_DDDPRINT("final k: %d", k);
	BI_PRINT("r(final)", &nc_ctx->r);
	BI_PRINT("s(final)", &nc_ctx->s);
	BI_PRINT("mp(final)", &nc_ctx->mp);
	BI_PRINT("mm(final)", &nc_ctx->mm);
}

static void dragon4_generate(duk_numconv_stringify_ctx *nc_ctx) {
	int tc1, tc2;
	int d;
	int count = 0;

	/*
	 *  Digit generation loop.
	 *
	 *  Different termination conditions:
	 *
	 *    1. Free format output.  Terminate when shortest accurate
	 *       representation found.
	 *
	 *    2. Fixed format output, with specific number of digits.
	 *       Ignore termination conditions, terminate when digits
	 *       generated.  Caller requests an extra digit and rounds.
	 *
	 *    3. Fixed format output, with a specific absolute cut-off
	 *       position (e.g. 10 digits after decimal point).  Note
	 *       that we always generate at least one digit, even if
	 *       the digit is below the cut-off point already.
	 */

	for (;;) {
		DUK_DDDPRINT("generate loop, count=%d, k=%d, B=%d, low_ok=%d, high_ok=%d",
		             count, nc_ctx->k, nc_ctx->B, nc_ctx->low_ok, nc_ctx->high_ok);
		BI_PRINT("r", &nc_ctx->r);
		BI_PRINT("s", &nc_ctx->s);
		BI_PRINT("m+", &nc_ctx->mp);
		BI_PRINT("m-", &nc_ctx->mm);

		/* (quotient-remainder (* r B) s) using a dummy subtraction loop */
		bi_mul_small(&nc_ctx->t1, &nc_ctx->r, nc_ctx->B);       /* t1 <- (* r B) */
		d = 0;
		for (;;) {
			if (bi_compare(&nc_ctx->t1, &nc_ctx->s) < 0) {
				break;
			}
			bi_sub_copy(&nc_ctx->t1, &nc_ctx->s, &nc_ctx->t2);  /* t1 <- t1 - s */
			d++;
		}
		bi_copy(&nc_ctx->r, &nc_ctx->t1);  /* r <- (remainder (* r B) s) */
		                                   /* d <- (quotient (* r B) s)   (in range 0...B-1) */
		DUK_DDDPRINT("-> d(quot)=%d", d);
		BI_PRINT("r(rem)", &nc_ctx->r);

		bi_mul_small_copy(&nc_ctx->mp, nc_ctx->B, &nc_ctx->t2); /* m+ <- (* m+ B) */
		bi_mul_small_copy(&nc_ctx->mm, nc_ctx->B, &nc_ctx->t2); /* m- <- (* m- B) */
		BI_PRINT("mp(upd)", &nc_ctx->mp);
		BI_PRINT("mm(upd)", &nc_ctx->mm);

		/* Terminating conditions.  For fixed width output, we just ignore the
		 * terminating conditions (and pretend that tc1 == tc2 == false).  The
		 * the current shortcut for fixed-format output is to generate a few
		 * extra digits and use rounding (with carry) to finish the output.
		 */

		if (nc_ctx->is_fixed == 0) {
			/* free-form */
			tc1 = (bi_compare(&nc_ctx->r, &nc_ctx->mm) <= (nc_ctx->low_ok ? 0 : -1));

			bi_add(&nc_ctx->t1, &nc_ctx->r, &nc_ctx->mp);  /* t1 <- (+ r m+) */
			tc2 = (bi_compare(&nc_ctx->t1, &nc_ctx->s) >= (&nc_ctx->high_ok ? 0 : 1));

			DUK_DDDPRINT("tc1=%d, tc2=%d", tc1, tc2);
		} else {
			/* fixed-format */
			tc1 = 0;
			tc2 = 0;
		}

		/* Count is incremented before DRAGON4_OUTPUT() call on purpose.  This
		 * is taken into account by DRAGON4_OUTPUT() macro.
		 */
		count++;

		if (tc1) {
			if (tc2) {
				/* tc1 = true, tc2 = true */
				bi_mul_small(&nc_ctx->t1, &nc_ctx->r, 2);
				if (bi_compare(&nc_ctx->t1, &nc_ctx->s) < 0) {  /* (< (* r 2) s) */
					DUK_DDDPRINT("tc1=true, tc2=true, 2r > s: output d --> %d (k=%d)", d, nc_ctx->k);
					DRAGON4_OUTPUT(nc_ctx, count, d);
				} else {
					DUK_DDDPRINT("tc1=true, tc2=true, 2r <= s: output d+1 --> %d (k=%d)", d + 1, nc_ctx->k);
					DRAGON4_OUTPUT(nc_ctx, count, d + 1);
				}
				break;
			} else {
				/* tc1 = true, tc2 = false */
				DUK_DDDPRINT("tc1=true, tc2=false: output d --> %d (k=%d)", d, nc_ctx->k);
				DRAGON4_OUTPUT(nc_ctx, count, d);
				break;
			}
		} else {
			if (tc2) {
				/* tc1 = false, tc2 = true */
				DUK_DDDPRINT("tc1=false, tc2=true: output d+1 --> %d (k=%d)", d + 1, nc_ctx->k);
				DRAGON4_OUTPUT(nc_ctx, count, d + 1);
				break;
			} else {
				/* tc1 = false, tc2 = false */

				DUK_DDDPRINT("tc1=false, tc2=false: output d --> %d (k=%d)", d, nc_ctx->k);
				DRAGON4_OUTPUT(nc_ctx, count, d);

				/* r <- r    (updated above: r <- (remainder (* r B) s)
				 * s <- s
				 * m+ <- m+  (updated above: m+ <- (* m+ B)
				 * m- <- m-  (updated above: m- <- (* m- B)
				 * B, low_ok, high_ok are fixed
				 */

				/* fall through and continue for-loop */
			}
		}

		/* fixed-format termination conditions */
		if (nc_ctx->is_fixed) {
			if (nc_ctx->abs_pos) {
				int pos = nc_ctx->k - count + 1;  /* count is already incremented, take into account */
				DUK_DDDPRINT("fixed format, absolute: abs pos=%d, k=%d, count=%d, req=%d", pos, nc_ctx->k, count, nc_ctx->req_digits);
				if (pos <= nc_ctx->req_digits) {
					DUK_DDDPRINT("digit position reached req_digits, end generate loop");
					break;
				}
			} else {
				DUK_DDDPRINT("fixed format, relative: k=%d, count=%d, req=%d", nc_ctx->k, count, nc_ctx->req_digits);
				if (count >= nc_ctx->req_digits) {
					DUK_DDDPRINT("digit count reached req_digits, end generate loop");
					break;
				}
			}
		}
	}  /* for */

	nc_ctx->count = count;

	DUK_DDDPRINT("generate finished");

#ifdef DUK_USE_DDDEBUG
	{
		char buf[2048];
		int i, t;
		DUK_MEMSET(buf, 0, sizeof(buf));
		for (i = 0; i < nc_ctx->count; i++) {
			t = nc_ctx->digits[i];
			if (t < 0 || t > 36) {
				buf[i] = '?';
			} else {
				buf[i] = DIGITCHAR(t);
			}
		}
		DUK_DDDPRINT("-> generated digits; k=%d, digits='%s'", nc_ctx->k, buf);
	}
#endif
}

/* Round up digits to a given position.  If position is out-of-bounds,
 * does nothing.  If carry propagates over the first digit, a '1' is
 * prepended to digits and 'k' will be updated.  Return value indicates
 * whether carry propagated over the first digit.
 *
 * Note that nc_ctx->count is NOT updated based on the rounding position
 * (it is updated only if carry overflows over the first digit and an
 * extra digit is prepended).
 */
static int dragon4_fixed_format_round(duk_numconv_stringify_ctx *nc_ctx, int round_idx) {
	int t;
	char *p;
	int roundup_limit;
	int ret = 0;

	/*
	 *  round_idx points to the digit which is considered for rounding; the
	 *  digit to its left is the final digit of the rounded value.  If round_idx
	 *  is zero, rounding will be performed; the result will either be an empty
	 *  rounded value or if carry happens a '1' digit is generated.
	 */

	if (round_idx >= nc_ctx->count) {
		DUK_DDDPRINT("round_idx out of bounds (%d >= %d (count)) -> no rounding",
		             round_idx, nc_ctx->count);
		return 0;
	} else if (round_idx < 0) {
		DUK_DDDPRINT("round_idx out of bounds (%d < 0) -> no rounding", round_idx);
		return 0;
	}

	/*
	 *  Round-up limit.
	 *
	 *  For even values, divides evenly, e.g. 10 -> roundup_limit=5.
	 *
	 *  For odd values, rounds up, e.g. 3 -> roundup_limit=2.
	 *  If radix is 3, 0/3 -> down, 1/3 -> down, 2/3 -> up.
	 */
	roundup_limit = (nc_ctx->B + 1) / 2;

	p = &nc_ctx->digits[round_idx];
	if (*p >= roundup_limit) {
		DUK_DDDPRINT("fixed-format rounding carry required");
		/* carry */
		for (;;) {
			*p = 0;
			if (p == &nc_ctx->digits[0]) {
				DUK_DDDPRINT("carry propagated to first digit -> special case handling");
				DUK_MEMMOVE((void *) (&nc_ctx->digits[1]),
				            (void *) (&nc_ctx->digits[0]),
				            (size_t) (sizeof(char) * nc_ctx->count));
				nc_ctx->digits[0] = 1;  /* don't increase 'count' */
				nc_ctx->k++;  /* position of highest digit changed */
				nc_ctx->count++;  /* number of digits changed */
				ret = 1;
				break;
			}

			DUK_DDDPRINT("fixed-format rounding carry: B=%d, roundup_limit=%d, p=%p, digits=%p",
			             (int) nc_ctx->B, roundup_limit, (void *) p, (void *) nc_ctx->digits);
			p--;
			t = *p;
			DUK_DDDPRINT("digit before carry: %d", t);
			if (++t < nc_ctx->B) {
				DUK_DDDPRINT("rounding carry terminated");
				*p = t;
				break;
			}

			DUK_DDDPRINT("wraps, carry to next digit");
		}
	}

	return ret;
}

#define NO_EXP  (65536)  /* arbitrary marker, outside valid exp range */

static void dragon4_convert_and_push(duk_numconv_stringify_ctx *nc_ctx, duk_context *ctx, int radix, int digits, int flags, int neg) {
	int k;
	int pos, pos_end;
	int exp;
	int dig;
	char *q;
	char *buf;

	/*
	 *  The string conversion here incorporates all the necessary Ecmascript
	 *  semantics without attempting to be generic.  nc_ctx->digits contains
	 *  nc_ctx->count digits (>= 1), with the topmost digit's 'position'
	 *  indicated by nc_ctx->k as follows:
	 *
	 *    digits="123" count=3 k=0   -->   0.123
	 *    digits="123" count=3 k=1   -->   1.23
	 *    digits="123" count=3 k=5   -->   12300
	 *    digits="123" count=3 k=-1  -->   0.0123
	 *
	 *  Note that the identifier names used for format selection are different
	 *  in Burger-Dybvig paper and Ecmascript specification (quite confusingly
	 *  so, because e.g. 'k' has a totally different meaning in each).  See
	 *  documentation for discussion.
	 *
	 *  Ecmascript doesn't specify any specific behavior for format selection
	 *  (e.g. when to use exponent notation) for non-base-10 numbers.
	 *
	 *  The bigint space in the context is reused for string output, as there
	 *  is more than enough space for that (>1kB at the moment), and we avoid
	 *  allocating even more stack.
	 */

	DUK_ASSERT(NUMCONV_CTX_BIGINTS_SIZE >= MAX_FORMATTED_LENGTH);
	DUK_ASSERT(nc_ctx->count >= 1);

	k = nc_ctx->k;
	buf = (char *) &nc_ctx->f;  /* XXX: union would be more correct */
	q = buf;

	/* Exponent handling: if exponent format is used, record exponent value and
	 * fake k such that one leading digit is generated (e.g. digits=123 -> "1.23").
	 *
	 * toFixed() prevents exponent use; otherwise apply a set of criteria to
	 * match the other API calls (toString(), toPrecision, etc).
	 */

	exp = NO_EXP;
	if (!nc_ctx->abs_pos /* toFixed() */) {
		if ((flags & DUK_N2S_FLAG_FORCE_EXP) ||             /* exponential notation forced */
		    ((flags & DUK_N2S_FLAG_NO_ZERO_PAD) &&          /* fixed precision and zero padding would be required */
	             (k - digits >= 1)) ||                          /* (e.g. k=3, digits=2 -> "12X") */
		    ((k > 21 || k <= -6) && (radix == 10))) {       /* toString() conditions */
			DUK_DDDPRINT("use exponential notation: k=%d -> exp=%d", k, k - 1);
			exp = k - 1;  /* e.g. 12.3 -> digits="123" k=2 -> 1.23e1 */
			k = 1;  /* generate mantissa with a single leading whole number digit */
		}
	}

	if (neg) {
		*q++ = '-';
	}

	/* Start position (inclusive) and end position (exclusive) */
	pos = (k >= 1 ? k : 1);
	if (nc_ctx->is_fixed) {
		if (nc_ctx->abs_pos) {
			/* toFixed() */
			pos_end = -digits;
		} else {
			pos_end = k - digits;
		}
	} else {
		pos_end = k - nc_ctx->count;
	}
	if (pos_end > 0) {
		pos_end = 0;
	}

	DUK_DDDPRINT("exp=%d, k=%d, count=%d, pos=%d, pos_end=%d, is_fixed=%d, "
	             "digits=%d, abs_pos=%d",
	             exp, k, nc_ctx->count, pos, pos_end, nc_ctx->is_fixed,
	             digits, nc_ctx->abs_pos);

	/* Digit generation */
	while (pos > pos_end) {
		DUK_DDDPRINT("digit generation: pos=%d, pos_end=%d", pos, pos_end);
		if (pos == 0) {
			*q++ = '.';
		}
		if (pos > k) {
			*q++ = '0';
		} else if (pos <= k - nc_ctx->count) {
			*q++ = '0';
		} else {
			dig = nc_ctx->digits[k - pos];
			DUK_ASSERT(dig >= 0 && dig < nc_ctx->B);
			*q++ = DIGITCHAR(dig);
		} 

		pos--;
	}
	DUK_ASSERT(pos <= 1);

	/* Exponent */
	if (exp != NO_EXP) {
		/*
		 *  Exponent notation for non-base-10 numbers isn't specified in Ecmascript
		 *  specification, as it never explicitly turns up: non-decimal numbers can
		 *  only be formatted with Number.prototype.toString([radix]) and for that,
		 *  behavior is not explicitly specified.
		 *
		 *  Logical choices include formatting the exponent as decimal (e.g. binary
		 *  100000 as 1e+5) or in current radix (e.g. binary 100000 as 1e+101).
		 *  The Dragon4 algorithm (in the original paper) prints the exponent value
		 *  in the target radix B.  However, for radix values 15 and above, the
		 *  exponent separator 'e' is no longer easily parseable.  Consider, for
		 *  instance, the number "1.faecee+1c".
		 */

		size_t len;
		char exp_sign;

		*q++ = 'e';
		if (exp >= 0) {
			exp_sign = '+';
		} else {
			exp_sign = '-';
			exp = -exp;
		}
		*q++ = exp_sign;
		len = dragon4_format_uint32(q, (unsigned int) exp, radix);
		q += len;
	}

	duk_push_lstring(ctx, buf, q - buf);
}

/*
 *  Conversion helpers
 */

static void dragon4_double_to_ctx(duk_numconv_stringify_ctx *nc_ctx, double x) {
	duk_double_union u;
	duk_uint32_t tmp;
	int exp;

	/*
	 *    seeeeeee eeeeffff ffffffff ffffffff ffffffff ffffffff ffffffff ffffffff
	 *       A        B        C        D        E        F        G        H
	 *
	 *    s       sign bit
	 *    eee...  exponent field
	 *    fff...  fraction
	 *
	 *    ieee value = 1.ffff... * 2^(e - 1023)  (normal)
	 *               = 0.ffff... * 2^(-1022)     (denormal)
	 *
	 *    algorithm v = f * b^e
	 */

	DUK_DBLUNION_SET_DOUBLE(&u, x);

	nc_ctx->f.n = 2;

	tmp = DUK_DBLUNION_GET_LOW32(&u);
	nc_ctx->f.v[0] = tmp;
	tmp = DUK_DBLUNION_GET_HIGH32(&u);
	nc_ctx->f.v[1] = tmp & 0x000fffffUL;
	exp = (tmp >> 20) & 0x07ffUL;

	if (exp == 0) {
		/* denormal */
		exp = IEEE_DOUBLE_EXP_MIN - 52;
		bi_normalize(&nc_ctx->f);
	} else {
		/* normal: implicit leading 1-bit */
		nc_ctx->f.v[1] |= 0x00100000UL;
		exp = exp - IEEE_DOUBLE_EXP_BIAS - 52;
		DUK_ASSERT(bi_is_valid(&nc_ctx->f));  /* true, because v[1] has at least one bit set */
	}

	DUK_ASSERT(bi_is_valid(&nc_ctx->f));

	nc_ctx->e = exp;
}

void dragon4_ctx_to_double(duk_numconv_stringify_ctx *nc_ctx, double *x) {
	duk_double_union u;
	int exp;
	int i;
	int bitstart;
	int bitround;
	int bitidx;
	int skip_round;
	duk_uint32_t t, v;

	DUK_ASSERT(nc_ctx->count == 53 + 1);
	DUK_ASSERT(nc_ctx->digits[0] == 1);  /* zero handled by caller */

	/*
	 *  Figure out how generated digits match up with the mantissa,
	 *  and then perform rounding.  If mantissa overflows, need to
	 *  recompute the exponent (it is bumped and may overflow to
	 *  infinity).
	 *
	 *  For normal numbers the leading '1' is hidden and ignored,
	 *  and the last bit is used for rounding:
	 *
	 *                          rounding pt
	 *       <--------52------->|
	 *     1 x x x x ... x x x x|y  ==>  x x x x ... x x x x
	 *
	 *  For denormals, the leading '1' is included in the number,
	 *  and the rounding point is different:
	 *
	 *                      rounding pt
	 *     <--52 or less--->|
	 *     1 x x x x ... x x|x x y  ==>  0 0 ... 1 x x ... x x
	 *
	 *  The largest denormals will have a mantissa beginning with
	 *  a '1' (the explicit leading bit); smaller denormals will
	 *  have leading zero bits.
	 *
	 *  If the exponent would become too high, the result becomes
	 *  Infinity.  If the exponent is so small that the entire
	 *  mantissa becomes zero, the result becomes zero.
	 *
	 *  Note: the Dragon4 'k' is off-by-one with respect to the IEEE
	 *  exponent.  For instance, k==0 indicates that the leading '1'
	 *  digit is at the first binary fraction position (0.1xxx...);
	 *  the corresponding IEEE exponent would be -1.
	 */

	skip_round = 0;

 recheck_exp:

	exp = nc_ctx->k - 1;   /* IEEE exp without bias */
	if (exp > 1023) {
		/* Infinity */
		bitstart = -255;  /* needed for inf: causes mantissa to become zero,
		                   * and rounding to be skipped.
		                   */
		exp = 2047;
	} else if (exp >= -1022) {
		/* normal */
		bitstart = 1;  /* skip leading digit */
		exp += IEEE_DOUBLE_EXP_BIAS;
		DUK_ASSERT(exp >= 1 && exp <= 2046);
	} else {
		/* denormal or zero */
		bitstart = 1023 + exp;  /* exp==-1023 -> bitstart=0 (leading 1);
		                         * exp==-1024 -> bitstart=-1 (one left of leading 1), etc
		                         */
		exp = 0;
	}
	bitround = bitstart + 52;

	DUK_DDDPRINT("ieee exp=%d, bitstart=%d, bitround=%d", exp, bitstart, bitround);

	if (!skip_round) {
		if (dragon4_fixed_format_round(nc_ctx, bitround)) {
			/* Corner case: see test-numconv-parse-mant-carry.js.  We could
			 * just bump the exponent and update bitstart, but it's more robust
			 * to recompute (but avoid rounding twice).
			 */
			DUK_DDDPRINT("rounding caused exponent to be bumped, recheck exponent");
			skip_round = 1;
			goto recheck_exp;
		}
	}

	/*
	 *  Create mantissa
	 */

	t = 0;
	for (i = 0; i < 52; i++) {
		bitidx = bitstart + 52 - 1 - i;
		if (bitidx >= nc_ctx->count) {
			v = 0;
		} else if (bitidx < 0) {
			v = 0;
		} else {
			v = nc_ctx->digits[bitidx];
		}
		DUK_ASSERT(v == 0 || v == 1);
		t += v << (i % 32);
		if (i == 31) {
			/* low 32 bits is complete */
			DUK_DBLUNION_SET_LOW32(&u, t);
			t = 0;
		}
	}
	/* t has high mantissa */

	DUK_DDDPRINT("mantissa is complete: %08x %08x",
	             t,
	             (unsigned int) DUK_DBLUNION_GET_LOW32(&u));

	DUK_ASSERT(exp >= 0 && exp <= 0x7ffL);
	t += exp << 20;
#if 0  /* caller handles sign change */
	if (negative) {
		t |= 0x80000000U;
	}
#endif
	DUK_DBLUNION_SET_HIGH32(&u, t);

	DUK_DDDPRINT("number is complete: %08x %08x",
	             (unsigned int) DUK_DBLUNION_GET_HIGH32(&u),
	             (unsigned int) DUK_DBLUNION_GET_LOW32(&u));

	*x = DUK_DBLUNION_GET_DOUBLE(&u);
}

/*
 *  Exposed number-to-string API
 *
 *  Input: [ number ]
 *  Output: [ string ]
 */

void duk_numconv_stringify(duk_context *ctx, int radix, int digits, int flags) {
	double x;
	int c;
	int neg;
	unsigned int uval;
	duk_numconv_stringify_ctx nc_ctx_alloc;  /* large context; around 2kB now */
	duk_numconv_stringify_ctx *nc_ctx = &nc_ctx_alloc;

	x = duk_require_number(ctx, -1);
	duk_pop(ctx);

	/*
	 *  Handle special cases (NaN, infinity, zero).
	 */

	c = DUK_FPCLASSIFY(x);
	if (DUK_SIGNBIT(x)) {
		x = -x;
		neg = 1;
	} else {
		neg = 0;
	}
	DUK_ASSERT(DUK_SIGNBIT(x) == 0);

	if (c == DUK_FP_NAN) {
		duk_push_hstring_stridx(ctx, DUK_STRIDX_NAN);
		return;
	} else if (c == DUK_FP_INFINITE) {
		if (neg) {
			/* -Infinity */
			duk_push_hstring_stridx(ctx, DUK_STRIDX_MINUS_INFINITY);
		} else {
			/* Infinity */
			duk_push_hstring_stridx(ctx, DUK_STRIDX_INFINITY);
		}
		return;
	} else if (c == DUK_FP_ZERO) {
		/* We can't shortcut zero here if it goes through special formatting
		 * (such as forced exponential notation).
		 */
		;
	}

	/*
	 *  Handle integers in 32-bit range (that is, [-(2**32-1),2**32-1])
	 *  specially, as they're very likely for embedded programs.  This
	 *  is now done for all radix values.  We must be careful not to use
	 *  the fast path when special formatting (e.g. forced exponential)
	 *  is in force.
	 *
	 *  XXX: could save space by supporting radix 10 only and using
	 *  sprintf "%u" for the fast path and for exponent formatting.
	 */

	uval = (unsigned int) x;
	if (((double) uval) == x &&  /* integer number in range */
	    flags == 0) {            /* no special formatting */
		/* use bigint area as a temp */
		char *buf = (char *) (&nc_ctx->f);
		char *p = buf;

		DUK_ASSERT(NUMCONV_CTX_BIGINTS_SIZE >= 32 + 1);  /* max size: radix=2 + sign */
		if (neg && uval != 0) {
			/* no negative sign for zero */
			*p++ = '-';
		}
		p += dragon4_format_uint32(p, uval, radix);
		duk_push_lstring(ctx, buf, (size_t) (p - buf));
		return;
	}

	/*
	 *  Dragon4 setup.
	 *
	 *  Convert double from IEEE representation for conversion;
	 *  normal finite values have an implicit leading 1-bit.  The
	 *  slow path algorithm doesn't handle zero, so zero is special
	 *  cased here but still creates a valid nc_ctx, and goes
	 *  through normal formatting in case special formatting has
	 *  been requested (e.g. forced exponential format: 0 -> "0e+0").
	 */

	/* Would be nice to bulk clear the allocation, but the context
	 * is 1-2 kilobytes and nothing should rely on it being zeroed.
	 */
#if 0
	DUK_MEMSET((void *) nc_ctx, 0, sizeof(*nc_ctx));  /* slow init, do only for slow path cases */
#endif

	nc_ctx->is_s2n = 0;
	nc_ctx->b = 2;
	nc_ctx->B = radix;
	nc_ctx->abs_pos = 0;
	if (flags & DUK_N2S_FLAG_FIXED_FORMAT) {
		nc_ctx->is_fixed = 1;
		if (flags & DUK_N2S_FLAG_FRACTION_DIGITS) {
			/* absolute req_digits; e.g. digits = 1 -> last digit is 0,
			 * but add an extra digit for rounding.
			 */
			nc_ctx->abs_pos = 1;
			nc_ctx->req_digits = (-digits + 1) - 1;
		} else {
			nc_ctx->req_digits = digits + 1;
		}
	} else {
		nc_ctx->is_fixed = 0;
		nc_ctx->req_digits = 0;
	}

	if (c == DUK_FP_ZERO) {
		/* Zero special case: fake requested number of zero digits; ensure
		 * no sign bit is printed.  Relative and absolute fixed format
		 * require separate handling.
		 */
		int count;
		if (nc_ctx->is_fixed) {
			if (nc_ctx->abs_pos) {
				count = digits + 2;  /* lead zero + 'digits' fractions + 1 for rounding */
			} else {
				count = digits + 1;  /* + 1 for rounding */
			}
		} else {
			count = 1;
		}
		DUK_DDDPRINT("count=%d", count);
		DUK_ASSERT(count >= 1);
		DUK_MEMSET((void *) nc_ctx->digits, 0, count);
		nc_ctx->count = count;
		nc_ctx->k = 1;  /* 0.000... */
		neg = 0;
		goto zero_skip;
	}

	dragon4_double_to_ctx(nc_ctx, x);   /* -> sets 'f' and 'e' */
	BI_PRINT("f", &nc_ctx->f);
	DUK_DDDPRINT("e=%d", nc_ctx->e);

	/*
	 *  Dragon4 slow path digit generation.
	 */

	dragon4_prepare(nc_ctx);  /* setup many variables in nc_ctx */

	DUK_DDDPRINT("after prepare:");
	BI_PRINT("r", &nc_ctx->r);
	BI_PRINT("s", &nc_ctx->s);
	BI_PRINT("mp", &nc_ctx->mp);
	BI_PRINT("mm", &nc_ctx->mm);

	dragon4_scale(nc_ctx);

	DUK_DDDPRINT("after scale; k=%d", nc_ctx->k);
	BI_PRINT("r", &nc_ctx->r);
	BI_PRINT("s", &nc_ctx->s);
	BI_PRINT("mp", &nc_ctx->mp);
	BI_PRINT("mm", &nc_ctx->mm);

	dragon4_generate(nc_ctx);

	/*
	 *  Convert and push final string.
	 */

 zero_skip:

	if (flags & DUK_N2S_FLAG_FIXED_FORMAT) {
		/* Perform fixed-format rounding. */
		int roundpos;
		if (flags & DUK_N2S_FLAG_FRACTION_DIGITS) {
			/* 'roundpos' is relative to nc_ctx->k and increases to the right
			 * (opposite of how 'k' changes).
			 */
			roundpos = -digits;  /* absolute position for digit considered for rounding */
			roundpos = nc_ctx->k - roundpos;
			
		} else {
			roundpos = digits;
		}
		DUK_DDDPRINT("rounding: k=%d, count=%d, digits=%d, roundpos=%d",
		             nc_ctx->k, nc_ctx->count, digits, roundpos);
		(void) dragon4_fixed_format_round(nc_ctx, roundpos);

		/* Note: 'count' is currently not adjusted by rounding (i.e. the
		 * digits are not "chopped off".  That shouldn't matter because
		 * the digit position (absolute or relative) is passed on to the
		 * convert-and-push function.
		 */
	}

	dragon4_convert_and_push(nc_ctx, ctx, radix, digits, flags, neg);
}

/*
 *  Exposed string-to-number API
 *
 *  Input: [ string ]
 *  Output: [ number ]
 *
 *  If number parsing fails, a NaN is pushed as the result.  If number parsing
 *  fails due to an internal error, an InternalError is thrown.
 *
 *  FIXME: is this reasonable? should caller have the assurance that no error
 *  can be thrown?  Comment in documentation.
 */

void duk_numconv_parse(duk_context *ctx, int radix, int flags) {
	duk_hthread *thr = (duk_hthread *) ctx;
	duk_numconv_stringify_ctx nc_ctx_alloc;  /* large context; around 2kB now */
	duk_numconv_stringify_ctx *nc_ctx = &nc_ctx_alloc;
	double res;
	duk_hstring *h_str;
	int exp;
	int exp_neg;
	int exp_adj;
	int neg;
	int dig;
	int dig_whole;
	int dig_lzero;
	int dig_frac;
	int dig_exp;
	int dig_prec;
	const duk_exp_limits *explim;
	const unsigned char *p;
	int ch;

	DUK_DDDPRINT("parse number: %!T, radix=%d, flags=0x%08x", duk_get_tval(ctx, -1), radix, flags);

	/* FIXME: macros or explicit flag checks - check impact on code size */
	int trim_white = (flags & DUK_S2N_FLAG_TRIM_WHITE);
	int allow_exp = (flags & DUK_S2N_FLAG_ALLOW_EXP);
	int allow_garbage = (flags & DUK_S2N_FLAG_ALLOW_GARBAGE);
	int allow_plus = (flags & DUK_S2N_FLAG_ALLOW_PLUS);
	int allow_minus = (flags & DUK_S2N_FLAG_ALLOW_MINUS);
	int allow_infinity = (flags & DUK_S2N_FLAG_ALLOW_INF);
	int allow_frac = (flags & DUK_S2N_FLAG_ALLOW_FRAC);
	int allow_naked_frac = (flags & DUK_S2N_FLAG_ALLOW_NAKED_FRAC);
	int allow_empty_frac = (flags & DUK_S2N_FLAG_ALLOW_EMPTY_FRAC);
	int allow_empty = (flags & DUK_S2N_FLAG_ALLOW_EMPTY_AS_ZERO);
	int allow_leading_zero = (flags & DUK_S2N_FLAG_ALLOW_LEADING_ZERO);
	int allow_auto_hex_int = (flags & DUK_S2N_FLAG_ALLOW_AUTO_HEX_INT);
	int allow_auto_oct_int = (flags & DUK_S2N_FLAG_ALLOW_AUTO_OCT_INT);

	DUK_ASSERT(radix >= 2 && radix <= 36);
	DUK_ASSERT(radix - 2 < sizeof(str2num_digits_for_radix));

	/*
	 *  Preliminaries: trim, sign, Infinity check
	 *
	 *  We rely on the interned string having a NUL terminator, which will
	 *  cause a parse failure wherever it is encountered.  As a result, we
	 *  don't need separate pointer checks.
	 *
	 *  There is no special parsing for 'NaN' in the specification although
	 *  'Infinity' (with an optional sign) is allowed in some contexts.
	 *  Some contexts allow plus/minus sign, while others only allow the
	 *  minus sign (like JSON.parse()).
	 *
	 *  Automatic hex number detection (leading '0x' or '0X') and octal
	 *  number detection (leading '0' followed by at least one octal digit)
	 *  is done here too.
	 */

	if (trim_white) {
		/* Leading / trailing whitespace is sometimes accepted and
		 * sometimes not.  After white space trimming, all valid input
		 * characters are pure ASCII.
		 */
		duk_trim(ctx, -1);
	}
	h_str = duk_require_hstring(ctx, -1);
	DUK_ASSERT(h_str != NULL);
	p = (const unsigned char *) DUK_HSTRING_GET_DATA(h_str);

	neg = 0;
	ch = *p;
	if (ch == '+') {
		if (!allow_plus) {
			DUK_DDDPRINT("parse failed: leading plus sign not allowed");
			goto parse_fail;
		}
		p++;
	} else if (ch == '-') {
		if (!allow_minus) {
			DUK_DDDPRINT("parse failed: leading minus sign not allowed");
			goto parse_fail;
		}
		p++;
		neg = 1;
	}

	ch = *p;
	if (allow_infinity && ch == 'I') {
		/* Don't check for Infinity unless the context allows it.
		 * 'Infinity' is a valid integer literal in e.g. base-36:
		 *
		 *   parseInt('Infinity', 36)
		 *   1461559270678
		 */

		const unsigned char *q;

		/* borrow literal Infinity from builtin string */
		q = (const unsigned char *) DUK_HSTRING_GET_DATA(DUK_HTHREAD_STRING_INFINITY(thr));
		if (DUK_STRNCMP((const char *) p, (const char *) q, 8) == 0) {
			if (!allow_garbage && (p[8] != (unsigned char) 0)) {
				DUK_DDDPRINT("parse failed: trailing garbage after matching 'Infinity' not allowed");
				goto parse_fail;
			} else {
				res = DUK_DOUBLE_INFINITY;
				goto neg_and_ret;
			}
		}
	}
	if (ch == '0') {
		int detect_radix = 0;
		ch = p[1];
		if (allow_auto_hex_int && (ch == 'x' || ch == 'X')) {
			DUK_DDDPRINT("detected 0x/0X hex prefix, changing radix and preventing fractions and exponent");
			detect_radix = 16;
			allow_empty = 0;  /* interpret e.g. '0x' and '0xg' as a NaN (= parse error) */
			p += 2;
		} else if (allow_auto_oct_int && (ch >= '0' && ch <= '9')) {
			DUK_DDDPRINT("detected 0n oct prefix, changing radix and preventing fractions and exponent");
			detect_radix = 8;
			allow_empty = 1;  /* interpret e.g. '09' as '0', not NaN */
			p += 1;
		}
		if (detect_radix > 0) {
			radix = detect_radix;
			allow_exp = 0;
			allow_frac = 0;
			allow_naked_frac = 0;
			allow_empty_frac = 0;
			allow_leading_zero = 1;  /* allow e.g. '0x0009' and '00077' */
		}
	}

	/*
	 *  Scan number and setup for Dragon4.
	 *
	 *  The fast path case is detected during setup: an integer which
	 *  can be converted without rounding, no net exponent.  The fast
	 *  path could be implemented as a separate scan, but may not really
	 *  be worth it: the multiplications for building 'f' are not
	 *  expensive when 'f' is small.
	 *
	 *  The significand ('f') must contain enough bits of (apparent)
	 *  accuracy, so that Dragon4 will generate enough binary output digits.
	 *  For decimal numbers, this means generating a 20-digit significand,
	 *  which should yield enough practical accuracy to parse IEEE doubles.
	 *  In fact, the Ecmascript specification explicitly allows an
	 *  implementation to treat digits beyond 20 as zeroes (and even
	 *  to round the 20th digit upwards).  For non-decimal numbers, the
	 *  appropriate number of digits has been precomputed for comparable
	 *  accuracy.
	 *
	 *  Digit counts:
	 *
	 *    [ dig_lzero ]
	 *      |
	 *     .+-..---[ dig_prec ]----.
	 *     |  ||                   |
	 *     0000123.456789012345678901234567890e+123456
	 *     |     | |                         |  |    |
	 *     `--+--' `------[ dig_frac ]-------'  `-+--'
	 *        |                                   |
	 *    [ dig_whole ]                       [ dig_exp ]
	 *
	 *    dig_frac and dig_exp are -1 if not present
	 *    dig_lzero is only computed for whole number part
	 *
	 *  Parsing state
	 *
	 *     Parsing whole part      dig_frac < 0 AND dig_exp < 0
	 *     Parsing fraction part   dig_frac >= 0 AND dig_exp < 0
	 *     Parsing exponent part   dig_exp >= 0   (dig_frac may be < 0 or >= 0)
	 * 
	 *  Note: in case we hit an implementation limit (like exponent range),
	 *  we should throw an error, NOT return NaN or Infinity.  Even with
	 *  very large exponent (or significand) values the final result may be
	 *  finite, so NaN/Infinity would be incorrect.
	 */

	bi_set_small(&nc_ctx->f, 0);
	dig_prec = 0;
	dig_lzero = 0;
	dig_whole = 0;
	dig_frac = -1;
	dig_exp = -1;
	exp = 0;
	exp_adj = 0;  /* essentially tracks digit position of lowest 'f' digit */
	exp_neg = 0;
	for (;;) {
		ch = *p++;

		DUK_DDDPRINT("parse digits: p=%p, ch='%c' (%d), exp=%d, exp_adj=%d, "
		             "dig_whole=%d, dig_frac=%d, dig_exp=%d, dig_lzero=%d, dig_prec=%d",
		             (void *) p, (ch >= 0x20 && ch <= 0x7e) ? ch : '?', ch,
		             exp, exp_adj, dig_whole, dig_frac, dig_exp, dig_lzero, dig_prec);
		BI_PRINT("f", &nc_ctx->f);

		/* Most common cases first. */
		if (ch >= '0' && ch <= '9') {
			dig = (int) ch - '0' + 0;
		} else if (ch == '.') {
			/* A leading digit is not required in some cases, e.g. accept ".123".
			 * In other cases (JSON.parse()) a leading digit is required.  This
			 * is checked for after the loop.
			 */
			if (dig_frac >= 0 || dig_exp >= 0) {
				if (allow_garbage) {
					DUK_DDDPRINT("garbage termination (invalid period)");
					break;
				} else {
					DUK_DDDPRINT("parse failed: period not allowed");
					goto parse_fail;
				}
			}

			if (!allow_frac) {
				/* Some contexts don't allow fractions at all; this can't be a
				 * post-check because the state ('f' and exp) would be incorrect.
				 */
				if (allow_garbage) {
					DUK_DDDPRINT("garbage termination (invalid first period)");
					break;
				} else {
					DUK_DDDPRINT("parse failed: fraction part not allowed");
				}
			}

			DUK_DDDPRINT("start fraction part");
			dig_frac = 0;
			continue;
		} else if (ch == (char) 0) {
			DUK_DDDPRINT("NUL termination");
			break;
		} else if (allow_exp && dig_exp < 0 && (ch == 'e' || ch == 'E')) {
			/* Note: we don't parse back exponent notation for anything else
			 * than radix 10, so this is not an ambiguous check (e.g. hex
			 * exponent values may have 'e' either as a significand digit
			 * or as an exponent separator).
			 *
			 * If the exponent separator occurs twice, 'e' will be interpreted
			 * as a digit (= 14) and will be rejected as an invalid decimal
			 * digit.
			 */

			DUK_DDDPRINT("start exponent part");

			/* Exponent without a sign or with a +/- sign is accepted
			 * by all call sites (even JSON.parse()).
			 */
			ch = *p;
			if (ch == '-') {
				exp_neg = 1;
				p++;
			} else if (ch == '+') {
				p++;
			}
			dig_exp = 0;
			continue;
		} else if (ch >= 'a' && ch <= 'z') {
			dig = (int) ch - 'a' + 0x0a;
		} else if (ch >= 'A' && ch <= 'Z') {
			dig = (int) ch - 'A' + 0x0a;
		} else {
			dig = 255;  /* triggers garbage digit check below */
		}
		DUK_ASSERT((dig >= 0 && dig <= 35) || dig == 255);

		if (dig >= radix) {
			if (allow_garbage) {
				DUK_DDDPRINT("garbage termination");
				break;
			} else {
				DUK_DDDPRINT("parse failed: trailing garbage or invalid digit");
				goto parse_fail;
			}
		}

		if (dig_exp < 0) {
			/* whole or fraction digit */

			if (dig_prec < str2num_digits_for_radix[radix - 2]) {
				/* significant from precision perspective */

				int f_zero = bi_is_zero(&nc_ctx->f);
				if (f_zero && dig == 0) {
					/* Leading zero is not counted towards precision digits; not
					 * in the integer part, nor in the fraction part.
					 */
					if (dig_frac < 0) {
						dig_lzero++;
					}
				} else {
					/* FIXME: join these ops */
					bi_mul_small(&nc_ctx->t1, &nc_ctx->f, radix);
					bi_add_small(&nc_ctx->f, &nc_ctx->t1, dig);
					dig_prec++;
				}
			} else {
				/* Ignore digits beyond a radix-specific limit, but note them
				 * in exp_adj.
				 */
				exp_adj++;
			}
	
			if (dig_frac >= 0) {
				dig_frac++;
				exp_adj--;
			} else {
				dig_whole++;
			}
		} else {
			/* exponent digit */

			exp = exp * radix + dig;
			if (exp > DUK_S2N_MAX_EXPONENT) {
				/* impose a reasonable exponent limit, so that exp
				 * doesn't need to get tracked using a bigint.
				 */
				DUK_DDDPRINT("parse failed: exponent too large");
				goto parse_int_error;
			}
			dig_exp++;
		}
	}

	/* Leading zero. */

	if (dig_lzero > 0 && dig_whole > 1) {
		if (!allow_leading_zero) {
			DUK_DDDPRINT("parse failed: leading zeroes not allowed in integer part");
			goto parse_fail;
		}
	}

	/* Validity checks for various fraction formats ("0.1", ".1", "1.", "."). */

	if (dig_whole == 0) {
		if (dig_frac == 0) {
			/* "." is not accepted in any format */
			DUK_DDDPRINT("parse failed: plain period without leading or trailing digits");
			goto parse_fail;
		} else if (dig_frac > 0) {
			/* ".123" */
			if (!allow_naked_frac) {
				DUK_DDDPRINT("parse failed: fraction part not allowed without "
				             "leading integer digit(s)");
				goto parse_fail;
			}
		} else {
			/* empty ("") is allowed in some formats (e.g. Number(''), as zero */
			if (!allow_empty) {
				DUK_DDDPRINT("parse failed: empty string not allowed (as zero)");
				goto parse_fail;
			}
		}
	} else {
		if (dig_frac == 0) {
			/* "123." is allowed in some formats */
			if (!allow_empty_frac) {
				DUK_DDDPRINT("parse failed: empty fractions");
				goto parse_fail;
			}
		} else if (dig_frac > 0) {
			/* "123.456" */
			;
		} else {
			/* "123" */
			;
		}
	}

	/* Exponent without digits (e.g. "1e" or "1e+").  If trailing garbage is
	 * allowed, ignore exponent part as garbage (= parse as "1", i.e. exp 0).
	 */

	if (dig_exp == 0) {
		if (!allow_garbage) {
			DUK_DDDPRINT("parse failed: empty exponent");
			goto parse_fail;
		}
		DUK_ASSERT(exp == 0);
	}

	if (exp_neg) {
		exp = -exp;
	}
	DUK_DDDPRINT("exp=%d, exp_adj=%d, net exponent -> %d", exp, exp_adj, exp + exp_adj);
	exp += exp_adj;

	/* Fast path check. */

	if (nc_ctx->f.n <= 1 &&   /* 32-bit value */
	    exp == 0    /* no net exponent */) {
		/* Fast path is triggered for no exponent and also for balanced exponent
		 * and fraction parts, e.g. for "1.23e2" == "123".  Remember to respect
		 * zero sign.
		 */

		/* XXX: could accept numbers larger than 32 bits, e.g. up to 53 bits? */
		DUK_DDDPRINT("fast path number parse");
		if (nc_ctx->f.n == 1) {
			res = (double) nc_ctx->f.v[0];
		} else {
			res = 0.0;
		}
		goto neg_and_ret;
	}

	/* Significand ('f') padding. */

	while (dig_prec < str2num_digits_for_radix[radix - 2]) {
		/* Pad significand with "virtual" zero digits so that Dragon4 will
		 * have enough (apparent) precision to work with.
		 */
		DUK_DDDPRINT("dig_prec=%d, pad significand with zero", dig_prec);
		bi_mul_small_copy(&nc_ctx->f, radix, &nc_ctx->t1);
		BI_PRINT("f", &nc_ctx->f);
		exp--;
		dig_prec++;
	}

	DUK_DDDPRINT("final exponent: %d", exp);

	/* Detect zero special case. */

	if (nc_ctx->f.n == 0) {
		/* This may happen even after the fast path check, if exponent is
		 * not balanced (e.g. "0e1").  Remember to respect zero sign.
		 */
		DUK_DDDPRINT("significand is zero");
		res = 0.0;
		goto neg_and_ret;
	}


	/* Quick reject of too large or too small exponents.  This check
	 * would be incorrect for zero (e.g. "0e1000" is zero, not Infinity)
	 * so zero check must be above.
	 */

	explim = &str2num_exp_limits[radix - 2];
	if (exp > explim->upper) {
		DUK_DDDPRINT("exponent too large -> infinite");
		res = DUK_DOUBLE_INFINITY;
		goto neg_and_ret;
	} else if (exp < explim->lower) {
		DUK_DDDPRINT("exponent too small -> zero");
		res = 0.0;
		goto neg_and_ret;
	}

	nc_ctx->is_s2n = 1;
	nc_ctx->e = exp;
	nc_ctx->b = radix;
	nc_ctx->B = 2;
	nc_ctx->is_fixed = 1;
	nc_ctx->abs_pos = 0;
	nc_ctx->req_digits = 53 + 1;

	BI_PRINT("f", &nc_ctx->f);
	DUK_DDDPRINT("e=%d", nc_ctx->e);

	/*
	 *  Dragon4 slow path (binary) digit generation.
	 *  An extra digit is generated for rounding.
	 */

	dragon4_prepare(nc_ctx);  /* setup many variables in nc_ctx */

	DUK_DDDPRINT("after prepare:");
	BI_PRINT("r", &nc_ctx->r);
	BI_PRINT("s", &nc_ctx->s);
	BI_PRINT("mp", &nc_ctx->mp);
	BI_PRINT("mm", &nc_ctx->mm);

	dragon4_scale(nc_ctx);

	DUK_DDDPRINT("after scale; k=%d", nc_ctx->k);
	BI_PRINT("r", &nc_ctx->r);
	BI_PRINT("s", &nc_ctx->s);
	BI_PRINT("mp", &nc_ctx->mp);
	BI_PRINT("mm", &nc_ctx->mm);

	dragon4_generate(nc_ctx);

	DUK_ASSERT(nc_ctx->count == 53 + 1);

	/*
	 *  Convert binary digits into an IEEE double.  Need to handle
	 *  denormals and rounding correctly.
	 */

	dragon4_ctx_to_double(nc_ctx, &res);
	goto neg_and_ret;

 neg_and_ret:
	if (neg) {
		res = -res;
	}
	duk_pop(ctx);
	duk_push_number(ctx, res);
	DUK_DDDPRINT("result: %!T", duk_get_tval(ctx, -1));
	return;

 parse_fail:
	DUK_DDDPRINT("parse failed");
	duk_pop(ctx);
	duk_push_nan(ctx);
	return;

 parse_int_error:
	DUK_DDDPRINT("parse failed, internal error, can't return a value");
	DUK_ERROR(thr, DUK_ERR_INTERNAL_ERROR, "number parse error");
	return;
}

