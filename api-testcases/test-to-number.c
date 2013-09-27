/*===
top: 29
index 0, number: nan
index 1, number: 0.000000
index 2, number: 1.000000
index 3, number: 0.000000
index 4, number: 1.000000
index 5, number: -123.456000
index 6, number: nan
index 7, number: inf
index 8, number: 0.000000
index 9, number: nan
index 10, number: 123.000000
index 11, number: 123.456000
index 12, number: 123456.000000
index 13, number: -123456.000000
index 14, number: nan
index 15, number: -inf
index 16, number: inf
index 17, number: inf
index 18, number: nan
index 19, number: nan
index 20, number: inf
index 21, number: nan
index 22, number: nan
index 23, number: 0.000000
index 24, number: 1.000000
index 25, number: 0.000000
index 26, number: 1.000000
index 27, number: 0.000000
index 28, number: 1.000000
rc=0, result=undefined
rc=1, result=Error: index out of bounds
rc=1, result=Error: index out of bounds
===*/

int test_1(duk_context *ctx) {
	int i, n;

	duk_set_top(ctx, 0);
	duk_push_undefined(ctx);
	duk_push_null(ctx);
	duk_push_true(ctx);
	duk_push_false(ctx);
	duk_push_int(ctx, 1);
	duk_push_number(ctx, -123.456);
	duk_push_nan(ctx);
	duk_push_number(ctx, INFINITY);
	duk_push_string(ctx, "");
	duk_push_string(ctx, "foo");
	duk_push_string(ctx, "123");
	duk_push_string(ctx, "123.456");
	duk_push_string(ctx, "123.456e3");
	duk_push_string(ctx, "  -123.456e+3  ");
	duk_push_string(ctx, "NaN");
	duk_push_string(ctx, "-Infinity");
	duk_push_string(ctx, "+Infinity");
	duk_push_string(ctx, "Infinity");
	duk_push_string(ctx, "Infinityx");
	duk_push_string(ctx, "xInfinity");
	duk_push_string(ctx, "  Infinity  ");
	duk_push_object(ctx);
	duk_push_thread(ctx);
	duk_push_fixed_buffer(ctx, 0);
	duk_push_fixed_buffer(ctx, 1024);
	duk_push_dynamic_buffer(ctx, 0);
	duk_push_dynamic_buffer(ctx, 1024);
	duk_push_pointer(ctx, (void *) NULL);
	duk_push_pointer(ctx, (void *) 0xdeadbeef);

	n = duk_get_top(ctx);
	printf("top: %d\n", n);
	for (i = 0; i < n; i++) {
		printf("index %d, number: %lf\n", i, duk_to_number(ctx, i));
	}

	return 0;
}

int test_2(duk_context *ctx) {
	duk_set_top(ctx, 0);
	duk_to_number(ctx, 3);
	printf("index 3 OK\n");
	return 0;
}

int test_3(duk_context *ctx) {
	duk_set_top(ctx, 0);
	duk_to_number(ctx, DUK_INVALID_INDEX);
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

	rc = duk_safe_call(ctx, test_3, 0, 1, DUK_INVALID_INDEX);
	printf("rc=%d, result=%s\n", rc, duk_to_string(ctx, -1));
	duk_pop(ctx);
}

