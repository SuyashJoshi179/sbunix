/*
 * POSIX conformance test: <ctype.h>
 * Reference: docs/susv5-html/basedefs/ctype.h.html
 * Audited: isalnum, isalpha, isblank, iscntrl, isdigit, isgraph, islower,
 *          isprint, ispunct, isspace, isupper, isxdigit, tolower, toupper
 * Excluded (not implemented): *_l variants
 * Excluded (non-POSIX): isascii, toascii (legacy, but in our libc; not tested)
 */
#include <ctype.h>

#define PIN __attribute__((unused)) static

PIN int (*_pin_isalnum)(int) = isalnum;
PIN int (*_pin_isalpha)(int) = isalpha;
PIN int (*_pin_isblank)(int) = isblank;
PIN int (*_pin_iscntrl)(int) = iscntrl;
PIN int (*_pin_isdigit)(int) = isdigit;
PIN int (*_pin_isgraph)(int) = isgraph;
PIN int (*_pin_islower)(int) = islower;
PIN int (*_pin_isprint)(int) = isprint;
PIN int (*_pin_ispunct)(int) = ispunct;
PIN int (*_pin_isspace)(int) = isspace;
PIN int (*_pin_isupper)(int) = isupper;
PIN int (*_pin_isxdigit)(int) = isxdigit;
PIN int (*_pin_tolower)(int) = tolower;
PIN int (*_pin_toupper)(int) = toupper;
