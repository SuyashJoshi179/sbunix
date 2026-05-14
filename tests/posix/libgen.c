/*
 * POSIX conformance test: <libgen.h>
 * Reference: docs/susv5-html/basedefs/libgen.h.html
 * Audited: basename, dirname
 */
#include <libgen.h>

#define PIN __attribute__((unused)) static

PIN char *(*_pin_basename)(char *) = basename;
PIN char *(*_pin_dirname)(char *) = dirname;
