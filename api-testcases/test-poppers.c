/*===
top: 2
top: 1
top: 0
test_pop_1 -> top=1, rc=0, ret='undefined'
top: 2
top: 1
top: 0
test_pop_b -> top=1, rc=1, ret='Error: attempt to pop too many entries'
top: 5
top: 3
top: 1
test_pop_2a -> top=1, rc=0, ret='undefined'
top: 5
top: 3
top: 1
test_pop_2b -> top=1, rc=1, ret='Error: attempt to pop too many entries'
top: 7
top: 4
top: 1
test_pop_3a -> top=1, rc=0, ret='undefined'
top: 7
top: 4
top: 1
test_pop_3b -> top=1, rc=1, ret='Error: attempt to pop too many entries'
top: 11
top: 11
top: 10
top: 0
test_pop_na -> top=1, rc=0, ret='undefined'
top: 11
test_pop_nb -> top=1, rc=1, ret='Error: attempt to pop too many entries'
top: 1
===*/

#define  SETUP(n)    do { duk_set_top(ctx, (n)); } while (0)
#define  PRINTTOP()  do { printf("top: %d\n", duk_get_top(ctx)); } while (0)

int test_pop_a(duk_context *ctx) {
	duk_pop(ctx);
	PRINTTOP();
	duk_pop(ctx);
	PRINTTOP();
	return 0;
}

int test_pop_b(duk_context *ctx) {
	duk_pop(ctx);
	PRINTTOP();
	duk_pop(ctx);
	PRINTTOP();

	/* no more entries, causes error */
	duk_pop(ctx);
	PRINTTOP();
	return 0;
}

int test_pop_2a(duk_context *ctx) {
	duk_pop_2(ctx);
	PRINTTOP();
	duk_pop_2(ctx);
	PRINTTOP();
	return 0;
}

int test_pop_2b(duk_context *ctx) {
	duk_pop_2(ctx);
	PRINTTOP();
	duk_pop_2(ctx);
	PRINTTOP();

	/* no more entries, causes error */
	duk_pop_2(ctx);
	PRINTTOP();
	return 0;
}

int test_pop_3a(duk_context *ctx) {
	duk_pop_3(ctx);
	PRINTTOP();
	duk_pop_3(ctx);
	PRINTTOP();
	return 0;
}

int test_pop_3b(duk_context *ctx) {
	duk_pop_3(ctx);
	PRINTTOP();
	duk_pop_3(ctx);
	PRINTTOP();

	/* no more entries, causes error */
	duk_pop_3(ctx);
	PRINTTOP();
	return 0;
}

int test_pop_na(duk_context *ctx) {
	duk_pop_n(ctx, 0);
	PRINTTOP();

	duk_pop_n(ctx, 1);
	PRINTTOP();

	duk_pop_n(ctx, 10);
	PRINTTOP();

	return 0;
}

int test_pop_nb(duk_context *ctx) {
	/* Since duk_pop_n() count argument is unsigned int, this will attempt
	 * to pop too many entries and result in an error.
	 */
	duk_pop_n(ctx, -1);
	PRINTTOP();
	return 0;
}

void test(duk_context *ctx) {
	int rc;

	SETUP(2); PRINTTOP();
	rc = duk_safe_call(ctx, test_pop_a, 2, 1, DUK_INVALID_INDEX);
	printf("test_pop_1 -> top=%d, rc=%d, ret='%s'\n", duk_get_top(ctx), rc, duk_to_string(ctx, -1));

	SETUP(2); PRINTTOP();
	rc = duk_safe_call(ctx, test_pop_b, 2, 1, DUK_INVALID_INDEX);
	printf("test_pop_b -> top=%d, rc=%d, ret='%s'\n", duk_get_top(ctx), rc, duk_to_string(ctx, -1));

	SETUP(5); PRINTTOP();
	rc = duk_safe_call(ctx, test_pop_2a, 5, 1, DUK_INVALID_INDEX);
	printf("test_pop_2a -> top=%d, rc=%d, ret='%s'\n", duk_get_top(ctx), rc, duk_to_string(ctx, -1));

	SETUP(5); PRINTTOP();
	rc = duk_safe_call(ctx, test_pop_2b, 5, 1, DUK_INVALID_INDEX);
	printf("test_pop_2b -> top=%d, rc=%d, ret='%s'\n", duk_get_top(ctx), rc, duk_to_string(ctx, -1));

	SETUP(7); PRINTTOP();
	rc = duk_safe_call(ctx, test_pop_3a, 7, 1, DUK_INVALID_INDEX);
	printf("test_pop_3a -> top=%d, rc=%d, ret='%s'\n", duk_get_top(ctx), rc, duk_to_string(ctx, -1));

	SETUP(7); PRINTTOP();
	rc = duk_safe_call(ctx, test_pop_3b, 7, 1, DUK_INVALID_INDEX);
	printf("test_pop_3b -> top=%d, rc=%d, ret='%s'\n", duk_get_top(ctx), rc, duk_to_string(ctx, -1));

	SETUP(11); PRINTTOP();
	rc = duk_safe_call(ctx, test_pop_na, 11, 1, DUK_INVALID_INDEX);
	printf("test_pop_na -> top=%d, rc=%d, ret='%s'\n", duk_get_top(ctx), rc, duk_to_string(ctx, -1));

	SETUP(11); PRINTTOP();
	rc = duk_safe_call(ctx, test_pop_nb, 11, 1, DUK_INVALID_INDEX);
	printf("test_pop_nb -> top=%d, rc=%d, ret='%s'\n", duk_get_top(ctx), rc, duk_to_string(ctx, -1));

	PRINTTOP();
}

