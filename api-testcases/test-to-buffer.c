/*===
top: 19
index 0, type 1 -> 7, ptr-is-NULL 0, size 9
buffer: dynamic=0, size=9: undefined
index 1, type 2 -> 7, ptr-is-NULL 0, size 4
buffer: dynamic=0, size=4: null
index 2, type 3 -> 7, ptr-is-NULL 0, size 4
buffer: dynamic=0, size=4: true
index 3, type 3 -> 7, ptr-is-NULL 0, size 5
buffer: dynamic=0, size=5: false
index 4, type 4 -> 7, ptr-is-NULL 0, size 3
buffer: dynamic=0, size=3: NaN
index 5, type 4 -> 7, ptr-is-NULL 0, size 9
buffer: dynamic=0, size=9: -Infinity
index 6, type 4 -> 7, ptr-is-NULL 0, size 8
buffer: dynamic=0, size=8: Infinity
index 7, type 4 -> 7, ptr-is-NULL 0, size 1
buffer: dynamic=0, size=1: 0
index 8, type 4 -> 7, ptr-is-NULL 0, size 1
buffer: dynamic=0, size=1: 0
index 9, type 4 -> 7, ptr-is-NULL 0, size 3
buffer: dynamic=0, size=3: 123
index 10, type 5 -> 7, ptr-is-NULL 0, size 3
buffer: dynamic=0, size=3: foo
index 11, type 5 -> 7, ptr-is-NULL 0, size 7
buffer: dynamic=0, size=7: foo\x00bar
index 12, type 6 -> 7, ptr-is-NULL 0, size 15
buffer: dynamic=0, size=15: [object Object]
index 13, type 7 -> 7, ptr-is-NULL -1, size 0
buffer: dynamic=0, size=0: 
index 14, type 7 -> 7, ptr-is-NULL 0, size 16
buffer: dynamic=0, size=16: \x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f
index 15, type 7 -> 7, ptr-is-NULL -1, size 0
buffer: dynamic=1, size=0: 
index 16, type 7 -> 7, ptr-is-NULL 0, size 16
buffer: dynamic=1, size=16: \x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f
index 17, type 8 -> 7, ptr-is-NULL 0, size 5
buffer: dynamic=0, size=5: (nil)
index 18, type 8 -> 7, ptr-is-NULL 0, size 10
buffer: dynamic=0, size=10: 0xdeadbeef
rc=0, result=undefined
rc=1, result=Error: invalid index: 3
rc=1, result=Error: invalid index: -2147483648
===*/

void dump_buffer(duk_context *ctx) {
	unsigned char *ptr;
	size_t sz;
	int i;

	ptr = (unsigned char *) duk_get_buffer(ctx, -1, &sz);
	printf("buffer: dynamic=%d, size=%d: ", duk_is_dynamic(ctx, -1), (int) sz);
	for (i = 0; i < (int) sz; i++) {
		unsigned char c = ptr[i];
		if (c >= 0x20 && c <= 0x7e) {
			printf("%c", c);
		} else {
			printf("\\x%02x", (int) c);
		}
	}
	printf("\n");
}

int test_1(duk_context *ctx) {
	int i, n;
	char *buf;

	duk_set_top(ctx, 0);
	duk_push_undefined(ctx);
	duk_push_null(ctx);
	duk_push_true(ctx);
	duk_push_false(ctx);
	duk_push_nan(ctx);
	duk_push_number(ctx, -INFINITY);
	duk_push_number(ctx, +INFINITY);
	duk_push_number(ctx, -0.0);
	duk_push_number(ctx, +0.0);
	duk_push_int(ctx, 123);
	duk_push_string(ctx, "foo");
	duk_push_lstring(ctx, "foo\0bar", 7);  /* internal NULs are kept */
	duk_push_object(ctx);
	buf = (char *) duk_push_fixed_buffer(ctx, 0);
	buf = (char *) duk_push_fixed_buffer(ctx, 16);
	for (i = 0; i < 16; i++) {
		buf[i] = i;
	}
	buf = (char *) duk_push_dynamic_buffer(ctx, 0);
	buf = (char *) duk_push_dynamic_buffer(ctx, 16);
	for (i = 0; i < 16; i++) {
		buf[i] = i;
	}
	duk_push_pointer(ctx, (void *) NULL);
	duk_push_pointer(ctx, (void *) 0xdeadbeef);

	n = duk_get_top(ctx);
	printf("top: %d\n", n);
	for (i = 0; i < n; i++) {
		int t1, t2;
		void *ptr;
		size_t sz;

		duk_dup(ctx, i);
		t1 = duk_get_type(ctx, -1);
		sz = (size_t) 0xdeadbeef;
		ptr = duk_to_buffer(ctx, -1, &sz);
		t2 = duk_get_type(ctx, -1);
		printf("index %d, type %d -> %d, ptr-is-NULL %d, size %u\n",
		       i, t1, t2, (sz == 0 ? -1 : (ptr == NULL ? 1 : 0)), (unsigned int) sz);
		dump_buffer(ctx);
		duk_pop(ctx);

		/* just check that this doesn't break */
		duk_dup(ctx, i);
		ptr = duk_to_buffer(ctx, -1, NULL);
		duk_pop(ctx);
	}

	return 0;
}

int test_2(duk_context *ctx) {
	duk_set_top(ctx, 0);
	(void) duk_to_buffer(ctx, 3, NULL);
	printf("index 3 OK\n");
	return 0;
}

int test_3(duk_context *ctx) {
	duk_set_top(ctx, 0);
	(void) duk_to_buffer(ctx, DUK_INVALID_INDEX, NULL);
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
