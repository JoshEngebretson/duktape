/*===
top: 20
index 0, string: 'undefined'
index 1, string: 'null'
index 2, string: 'true'
index 3, string: 'false'
index 4, string: '1'
index 5, string: '-123.456'
index 6, string: 'NaN'
index 7, string: 'Infinity'
index 8, string: '-Infinity'
index 9, string: ''
index 10, string: 'foo'
index 11, string: 'foo'
index 12, string: '[object Object]'
index 13, string: '[object Thread]'
index 14, string: ''
index 15, string: 'abcdefghijklmnop'
index 16, string: ''
index 17, string: 'abcdefghijklmnop'
index 18, string: '(nil)'
index 19, string: '0xdeadbeef'
rc=0, result=undefined
rc=1, result=Error: invalid index: 3
rc=1, result=Error: invalid index: -2147483648
===*/

int test_1(duk_context *ctx) {
	int i, n;
	char *ptr;

	duk_set_top(ctx, 0);
	duk_push_undefined(ctx);
	duk_push_null(ctx);
	duk_push_true(ctx);
	duk_push_false(ctx);
	duk_push_int(ctx, 1);
	duk_push_number(ctx, -123.456);
	duk_push_nan(ctx);
	duk_push_number(ctx, INFINITY);
	duk_push_number(ctx, -INFINITY);
	duk_push_string(ctx, "");
	duk_push_string(ctx, "foo");
	duk_push_lstring(ctx, "foo\0bar", 7);
	duk_push_object(ctx);
	duk_push_thread(ctx);
	(void) duk_push_fixed_buffer(ctx, 0);
	ptr = (char *) duk_push_fixed_buffer(ctx, 16);
	for (i = 0; i < 16; i++) {
		ptr[i] = (char) ('a' + i);
	}
	(void) duk_push_dynamic_buffer(ctx, 0);
	ptr = (char *) duk_push_dynamic_buffer(ctx, 16);
	for (i = 0; i < 16; i++) {
		ptr[i] = (char) ('a' + i);
	}
	duk_push_pointer(ctx, (void *) NULL);
	duk_push_pointer(ctx, (void *) 0xdeadbeef);

	n = duk_get_top(ctx);
	printf("top: %d\n", n);
	for (i = 0; i < n; i++) {
		printf("index %d, string: '%s'\n", i, duk_to_string(ctx, i));
	}

	return 0;
}

int test_2(duk_context *ctx) {
	duk_set_top(ctx, 0);
	duk_to_string(ctx, 3);
	printf("index 3 OK\n");
	return 0;
}

int test_3(duk_context *ctx) {
	duk_set_top(ctx, 0);
	duk_to_string(ctx, DUK_INVALID_INDEX);
	printf("index DUK_INVALID_INDEX OK\n");
	return 0;
}

void test(duk_context *ctx) {
	int rc;

	rc = duk_safe_call(ctx, test_1, 0, 1, DUK_INVALID_INDEX);
	printf("rc=%d, result=%s\n", rc, duk_to_string(ctx, -1));
	duk_pop(ctx);

	rc = duk_safe_call(ctx, test_2, 0, 1, DUK_INVALID_INDEX);
	printf("rc=%d, result=%s\n", rc, duk_to_string(ctx, -1));
	duk_pop(ctx);

	/* FIXME: this testcase currently exposes the DUK_INVALID_INDEX
	 * constant in the error message and is thus not portable.
	 */
	rc = duk_safe_call(ctx, test_3, 0, 1, DUK_INVALID_INDEX);
	printf("rc=%d, result=%s\n", rc, duk_to_string(ctx, -1));
	duk_pop(ctx);
}

