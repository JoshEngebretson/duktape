/*
 *  Try different format string lengths.
 */

/*===
length sum: 2147713027.000000
===*/

void test(duk_context *ctx) {
	char buf[65536 + 1024];
	int fmt_len, i;
	double len_sum = 0.0;

	for (fmt_len = 0;
	     fmt_len <= 65536;
	     fmt_len ++) {
		for (i = 0; i < fmt_len; i++) {
			buf[i] = 'x';
		}
		buf[fmt_len + 0] = '%';
		buf[fmt_len + 1] = 'd';
		buf[fmt_len + 2] = '\0';

		duk_push_sprintf(ctx, buf, 123);
		len_sum += (double) duk_get_length(ctx, -1);  /* trivial "checksum" */
		duk_pop(ctx);
	}

	/* Length sequence is 3, 4, ..., 65539 ->
	 * (65539 + 3)/2 * (65539 - 3 + 1) = 2147713027
	 *
	 * >>> res = 0
	 * >>> for i in xrange(0, 65536+1):
	 * ...   res += len( (('x' * i) + '%d') % 123 )
	 * ... 
	 * >>> res
	 * 2147713027
	 */

	printf("length sum: %lf\n", len_sum);
}
