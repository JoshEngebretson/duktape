/*===
*** test_1 (duk_safe_call)
top: 3, top index: 2
final top: 3
==> rc=0, result='undefined'
*** test_2 (duk_safe_call)
top: 0, top index is DUK_INVALID: 1
final top: 0
==> rc=0, result='undefined'
*** test_3 (duk_safe_call)
top: 3, top index: 2
final top: 3
==> rc=0, result='undefined'
*** test_4 (duk_safe_call)
==> rc=1, result='Error: invalid index'
===*/

int test_1(duk_context *ctx) {
	duk_set_top(ctx, 0);
	duk_push_int(ctx, 123);
	duk_push_int(ctx, 123);
	duk_push_int(ctx, 123);
	printf("top: %d, top index: %d\n", duk_get_top(ctx), duk_get_top_index(ctx));
	printf("final top: %d\n", duk_get_top(ctx));
	return 0;
}

int test_2(duk_context *ctx) {
	duk_set_top(ctx, 0);
	printf("top: %d, top index is DUK_INVALID: %d\n", duk_get_top(ctx), duk_get_top_index(ctx) == DUK_INVALID_INDEX ? 1 : 0);
	printf("final top: %d\n", duk_get_top(ctx));
	return 0;
}

int test_3(duk_context *ctx) {
	duk_set_top(ctx, 0);
	duk_push_int(ctx, 123);
	duk_push_int(ctx, 123);
	duk_push_int(ctx, 123);
	printf("top: %d, top index: %d\n", duk_get_top(ctx), duk_require_top_index(ctx));
	printf("final top: %d\n", duk_get_top(ctx));
	return 0;
}

int test_4(duk_context *ctx) {
	duk_set_top(ctx, 0);
	printf("top: %d, top index: %d\n", duk_get_top(ctx), duk_require_top_index(ctx));
	printf("final top: %d\n", duk_get_top(ctx));
	return 0;
}

void test(duk_context *ctx) {
	TEST_SAFE_CALL(test_1);
	TEST_SAFE_CALL(test_2);
	TEST_SAFE_CALL(test_3);
	TEST_SAFE_CALL(test_4);
}

