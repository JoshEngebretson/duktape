/*===
duk_is_object(1) = 1
json encoded: {"meaningOfLife":42}
top=2
===*/

void test(duk_context *ctx) {
	int obj_idx;

	duk_push_int(ctx, 123);  /* dummy */

	obj_idx = duk_push_object(ctx);
	duk_push_int(ctx, 42);
	duk_put_prop_string(ctx, obj_idx, "meaningOfLife");

	/* object is now: { "meaningOfLife": 42 } */

	printf("duk_is_object(%d) = %d\n", obj_idx, duk_is_object(ctx, obj_idx));

	duk_json_encode(ctx, obj_idx);  /* in-place */

	printf("json encoded: %s\n", duk_get_string(ctx, obj_idx));

	printf("top=%d\n", duk_get_top(ctx));
}
