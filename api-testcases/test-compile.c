/*===
*** test_1 (duk_safe_call)
program
program result: 123.000000
final top: 0
==> rc=0, result='undefined'
*** test_2 (duk_safe_call)
eval result: 5.000000
final top: 0
==> rc=0, result='undefined'
*** test_3 (duk_safe_call)
function result: 11.000000
final top: 0
==> rc=0, result='undefined'
===*/

int test_1(duk_context *ctx) {
	duk_set_top(ctx, 0);

	duk_push_string(ctx, "print('program');\n"
	                     "function hello() { print('Hello world!'); }\n"
	                     "123;");
	duk_push_string(ctx, "program");
	duk_compile(ctx, 0);
	duk_call(ctx, 0);      /* [ func filename ] -> [ result ] */
	printf("program result: %lf\n", duk_get_number(ctx, -1));
	duk_pop(ctx);

	printf("final top: %d\n", duk_get_top(ctx));
	return 0;
}

int test_2(duk_context *ctx) {
	duk_set_top(ctx, 0);

	duk_push_string(ctx, "2+3");
	duk_push_string(ctx, "eval");
	duk_compile(ctx, DUK_COMPILE_EVAL);
	duk_call(ctx, 0);      /* [ func ] -> [ result ] */
	printf("eval result: %lf\n", duk_get_number(ctx, -1));
	duk_pop(ctx);

	printf("final top: %d\n", duk_get_top(ctx));
	return 0;
}

int test_3(duk_context *ctx) {
	duk_set_top(ctx, 0);

	duk_push_string(ctx, "function (x,y) { return x+y; }");
	duk_push_string(ctx, "function");
	duk_compile(ctx, DUK_COMPILE_FUNCTION);
	duk_push_int(ctx, 5);
	duk_push_int(ctx, 6);
	duk_call(ctx, 2);      /* [ func 5 6 ] -> [ result ] */
	printf("function result: %lf\n", duk_get_number(ctx, -1));
	duk_pop(ctx);

	printf("final top: %d\n", duk_get_top(ctx));
	return 0;
}

void test(duk_context *ctx) {
	TEST_SAFE_CALL(test_1);
	TEST_SAFE_CALL(test_2);
	TEST_SAFE_CALL(test_3);
}

