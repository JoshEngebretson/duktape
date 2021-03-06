/*
 *  Error macro wrapper implementations.
 */

#include "duk_internal.h"

#ifdef DUK_USE_VERBOSE_ERRORS

#define BUFSIZE  256  /* size for formatting buffers */

#ifdef DUK_USE_VARIADIC_MACROS
void duk_err_handle_error(const char *filename, int line, duk_hthread *thr, int code, const char *fmt, ...) {
	va_list ap;
	char msg[BUFSIZE];
	va_start(ap, fmt);
	(void) DUK_VSNPRINTF(msg, sizeof(msg), fmt, ap);
	msg[sizeof(msg) - 1] = (char) 0;
	duk_err_create_and_throw(thr, code, msg, filename, line);
	va_end(ap);  /* dead code, but ensures portability (see Linux man page notes) */
}

void duk_err_handle_panic(const char *filename, int line, int code, const char *fmt, ...) {
	va_list ap;
	char msg1[BUFSIZE];
	char msg2[BUFSIZE];
	va_start(ap, fmt);
	(void) DUK_VSNPRINTF(msg1, sizeof(msg1), fmt, ap);
	msg1[sizeof(msg1) - 1] = (char) 0;
	(void) DUK_SNPRINTF(msg2, sizeof(msg2), "(%s:%d): %s", filename ? filename : "null", line, msg1);
	msg2[sizeof(msg2) - 1] = (char) 0;
	DUK_PANIC_HANDLER(code, msg2);
	va_end(ap);  /* dead code */
}
#else  /* DUK_USE_VARIADIC_MACROS */
const char *duk_err_file_stash = NULL;
int duk_err_line_stash = 0;

DUK_NORETURN(static void _handle_error(const char *filename, int line, duk_hthread *thr, int code, const char *fmt, va_list ap));

static void _handle_error(const char *filename, int line, duk_hthread *thr, int code, const char *fmt, va_list ap) {
	char msg[BUFSIZE];
	(void) DUK_VSNPRINTF(msg, sizeof(msg), fmt, ap);
	msg[sizeof(msg) - 1] = (char) 0;
	duk_err_create_and_throw(thr, code, msg, filename, line);
}

DUK_NORETURN(static void _handle_panic(const char *filename, int line, int code, const char *fmt, va_list ap));

static void _handle_panic(const char *filename, int line, int code, const char *fmt, va_list ap) {
	char msg1[BUFSIZE];
	char msg2[BUFSIZE];
	(void) DUK_VSNPRINTF(msg1, sizeof(msg1), fmt, ap);
	msg1[sizeof(msg1) - 1] = (char) 0;
	(void) DUK_SNPRINTF(msg2, sizeof(msg2), "(%s:%d): %s", filename ? filename : "null", line, msg1);
	msg2[sizeof(msg2) - 1] = (char) 0;
	DUK_PANIC_HANDLER(code, msg2);
}

void duk_err_handle_error(const char *filename, int line, duk_hthread *thr, int code, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	_handle_error(filename, line, thr, code, fmt, ap);
	va_end(ap);  /* dead code */
}

void duk_err_handle_error_stash(duk_hthread *thr, int code, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	_handle_error(duk_err_file_stash, duk_err_line_stash, thr, code, fmt, ap);
	va_end(ap);  /* dead code */
}

void duk_err_handle_panic(const char *filename, int line, int code, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	_handle_panic(filename, line, code, fmt, ap);
	va_end(ap);  /* dead code */
}

void duk_err_handle_panic_stash(int code, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	_handle_panic(duk_err_file_stash, duk_err_line_stash, code, fmt, ap);
	va_end(ap);  /* dead code */
}
#endif  /* DUK_USE_VARIADIC_MACROS */

#else  /* DUK_USE_VERBOSE_ERRORS */

#ifdef DUK_USE_VARIADIC_MACROS
void duk_err_handle_error(duk_hthread *thr, int code) {
	duk_err_create_and_throw(thr, code);
}

void duk_err_handle_panic(int code) {
	DUK_PANIC_HANDLER(code, NULL);
}
#else  /* DUK_USE_VARIADIC_MACROS */
void duk_err_handle_error_nonverbose1(duk_hthread *thr, int code, const char *fmt, ...) {
	duk_err_create_and_throw(thr, code);
}

void duk_err_handle_error_nonverbose2(const char *filename, int line, duk_hthread *thr, int code, const char *fmt, ...) {
	duk_err_create_and_throw(thr, code);
}

void duk_err_handle_panic_nonverbose1(int code, const char *fmt, ...) {
	DUK_PANIC_HANDLER(code, NULL);
}

void duk_err_handle_panic_nonverbose2(const char *filename, int line, int code, const char *fmt, ...) {
	DUK_PANIC_HANDLER(code, NULL);
}
#endif  /* DUK_USE_VARIADIC_MACROS */

#endif  /* DUK_USE_VERBOSE_ERRORS */

