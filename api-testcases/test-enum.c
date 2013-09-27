
/* basic enum success cases */
int test_1(duk_context *ctx) {
	duk_set_top(ctx, 0);

	printf("object with own properties only, enum with get_value=0\n");
	duk_eval_string(ctx, "({ 1: 1, foo: 2, bar: 3, quux: 4, 2: 5 })");
	duk_enum(ctx, -1, 0 /*enum_flags*/);  /* [ obj enum ] */
	while (duk_next(ctx, -1 /*enum_index*/, 0 /*get_value*/)) {
		printf("key: '%s'\n", duk_get_string(ctx, -1));
		duk_pop(ctx);
	}
	duk_pop_2(ctx);

	printf("object with own properties only, enum with get_value=1\n");
	duk_eval_string(ctx, "({ 1: 1, foo: 2, bar: 3, quux: 4, 2: 5 })");
	duk_enum(ctx, -1, 0 /*enum_flags*/);  /* [ obj enum ] */
	while (duk_next(ctx, -1 /*enum_index*/, 1 /*get_value*/)) {
		printf("key: '%s', value: '%s'\n", duk_get_string(ctx, -2), duk_to_string(ctx, -1));
		duk_pop_2(ctx);
	}
	duk_pop(ctx);
	duk_pop(ctx);

	printf("object with inherited, enumerable properties, enum with get_value=1\n");
	duk_eval_string(ctx, "(function () { var o = Object.create({ parent: 'inherited' }); o.foo = 'bar'; return o; })()");
	duk_enum(ctx, -1, 0 /*enum_flags*/);  /* [ obj enum ] */
	while (duk_next(ctx, -1 /*enum_index*/, 1 /*get_value*/)) {
		printf("key: '%s', value: '%s'\n", duk_get_string(ctx, -2), duk_to_string(ctx, -1));
		duk_pop_2(ctx);
	}
	duk_pop(ctx);
	duk_pop(ctx);

	printf("object with own non-enumerable properties, enum with get_value=1, don't enum inherited properties\n");
	duk_eval_string(ctx, "(function () { var o = Object.create({ parent: 'inherited' }); "
	                     "Object.defineProperty(o, 'enumerable_prop', { value: 123, writable: true, enumerable: true, configurable: true}); "
	                     "Object.defineProperty(o, 'nonenumerable_prop', { value: 234, writable: true, enumerable: false, configurable: true}); "
	                     " return o; })()");
	printf("- enum only enumerable own properties\n");
	duk_enum(ctx, -1, DUK_ENUM_OWN_PROPERTIES_ONLY /*enum_flags*/);  /* [ obj enum ] */
	while (duk_next(ctx, -1 /*enum_index*/, 1 /*get_value*/)) {
		printf("key: '%s', value: '%s'\n", duk_get_string(ctx, -2), duk_to_string(ctx, -1));
		duk_pop_2(ctx);
	}
	duk_pop(ctx);
	printf("- enum all own properties\n");
	duk_enum(ctx, -1, DUK_ENUM_OWN_PROPERTIES_ONLY | DUK_ENUM_INCLUDE_NONENUMERABLE /*enum_flags*/);  /* [ obj enum ] */
	while (duk_next(ctx, -1 /*enum_index*/, 1 /*get_value*/)) {
		printf("key: '%s', value: '%s'\n", duk_get_string(ctx, -2), duk_to_string(ctx, -1));
		duk_pop_2(ctx);
	}
	duk_pop(ctx);
	duk_pop(ctx);

	printf("object with string and array index keys, enum with get_value=1\n");
	duk_eval_string(ctx, "({ foo: 'val1', 999: 'val2', 1: 'val3', 123: 'val4', 234: 'val5', 2: 'val6', bar: 'val7' })");
	printf("- enum array indices only, not sorted\n");
	duk_enum(ctx, -1, DUK_ENUM_ARRAY_INDICES_ONLY /*enum_flags*/);  /* [ obj enum ] */
	while (duk_next(ctx, -1 /*enum_index*/, 1 /*get_value*/)) {
		printf("key: '%s', value: '%s'\n", duk_get_string(ctx, -2), duk_to_string(ctx, -1));
		duk_pop_2(ctx);
	}
	duk_pop(ctx);
	printf("- enum array indices only, sorted\n");
	duk_enum(ctx, -1, DUK_ENUM_ARRAY_INDICES_ONLY | DUK_ENUM_SORT_ARRAY_INDICES /*enum_flags*/);  /* [ obj enum ] */
	while (duk_next(ctx, -1 /*enum_index*/, 1 /*get_value*/)) {
		printf("key: '%s', value: '%s'\n", duk_get_string(ctx, -2), duk_to_string(ctx, -1));
		duk_pop_2(ctx);
	}
	duk_pop(ctx);
	duk_pop(ctx);

	/* XXX: there is no test for DUK_ENUM_INCLUDE_INTERNAL now,
	 * this is a bit difficult because internal properties are
	 * not exposed or stable.
	 */

	printf("final top: %d\n", duk_get_top(ctx));
	return 0;
}

void test(duk_context *ctx) {
	int rc;

	rc = duk_safe_call(ctx, test_1, 0, 1, DUK_INVALID_INDEX);
	printf("rc=%d, result='%s'\n", rc, duk_to_string(ctx, -1));
	duk_pop(ctx);
}

