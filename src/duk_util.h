/*
 *  Utilities
 */

#ifndef DUK_UTIL_H_INCLUDED
#define DUK_UTIL_H_INCLUDED

#define  DUK_UTIL_MIN_HASH_PRIME  17  /* must match genhashsizes.py */

#define  DUK_UTIL_GET_HASH_PROBE_STEP(hash)  (duk_util_probe_steps[(hash) & 0x1f])

/*
 *  Bitstream decoder
 */

struct duk_bitdecoder_ctx {
	duk_u8 *data;
	duk_u32 offset;
	duk_u32 length;
	duk_u32 currval;
	duk_u32 currbits;
};

/*
 *  Bitstream encoder
 */

struct duk_bitencoder_ctx {
	duk_u8 *data;
	duk_u32 offset;
	duk_u32 length;
	duk_u32 currval;
	duk_u32 currbits;
	int truncated;
};

/*
 *  Externs and prototypes
 */

extern char duk_lc_digits[36];
extern char duk_uc_nybbles[16];

/* Note: assumes that duk_util_probe_steps size is 32 */
extern duk_u8 duk_util_probe_steps[32];

duk_u32 duk_util_hashbytes(duk_u8 *data, duk_u32 len, duk_u32 seed);

duk_u32 duk_util_get_hash_prime(duk_u32 size);

duk_u32 duk_bd_decode(duk_bitdecoder_ctx *ctx, int bits);
int duk_bd_decode_flag(duk_bitdecoder_ctx *ctx);

void duk_be_encode(duk_bitencoder_ctx *ctx, duk_u32 data, int bits);
void duk_be_finish(duk_bitencoder_ctx *ctx);

void duk_util_base64_encode(const unsigned char *src, unsigned char *dst, size_t len);

duk_u32 duk_util_tinyrandom_get_bits(duk_hthread *thr, int n);
double duk_util_tinyrandom_get_double(duk_hthread *thr);

#endif  /* DUK_UTIL_H_INCLUDED */

