/*===
*** test_put (duk_safe_call)
put rc=0
final top: 0
==> rc=0, result='undefined'
*** test_put (duk_pcall)
==> rc=1, result='TypeError: non-object base reference'
===*/

int test_put(duk_context *ctx) {
	int rc;

	/* In Ecmascript, '(0).foo = "bar"' should work and evaluate to "bar"
	 * in non-strict mode, but cause an error to be thrown in strict mode
	 * (E5.1, Section 8.7.2, special [[Put]] variant, step 7.
	 */

	duk_push_int(ctx, 0);
	duk_push_string(ctx, "foo");
	duk_push_string(ctx, "bar");
	rc = duk_put_prop(ctx, -3);

	printf("put rc=%d\n", rc);

	printf("final top: %d\n", rc);
	return 0;
}

void test(duk_context *ctx) {
	TEST_SAFE_CALL(test_put);  /* outside: non-strict */
	TEST_PCALL(test_put);      /* inside: strict */
}
