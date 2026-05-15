/*
 * POSIX conformance test: <setjmp.h>
 * Reference: docs/susv5-html/basedefs/setjmp.h.html
 * Audited: setjmp, longjmp, sigsetjmp, siglongjmp
 * Required typedefs: jmp_buf, sigjmp_buf
 *
 * Excluded (non-POSIX): _setjmp, _longjmp (BSD aliases, our libc defines them)
 */
#include <setjmp.h>

#define PIN __attribute__((unused)) static

/* setjmp is required to be usable as a macro OR function per POSIX.
 * Our libc declares it as a function; pin against function-pointer type. */
PIN int  (*_pin_setjmp)(jmp_buf) = setjmp;
PIN void (*_pin_longjmp)(jmp_buf, int) __attribute__((noreturn)) = longjmp;
PIN int  (*_pin_sigsetjmp)(sigjmp_buf, int) = sigsetjmp;
PIN void (*_pin_siglongjmp)(sigjmp_buf, int) __attribute__((noreturn)) = siglongjmp;
