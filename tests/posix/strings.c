/*
 * POSIX conformance test: <strings.h>
 * Reference: docs/susv5-html/basedefs/strings.h.html
 * Audited: ffs, strcasecmp, strncasecmp
 * Excluded (non-POSIX): bzero, bcmp, bcopy (legacy BSD)
 * Excluded (not implemented): ffsl, ffsll, strcasecmp_l, strncasecmp_l, index, rindex
 */
#include <strings.h>

#define PIN __attribute__((unused)) static

PIN int (*_pin_ffs)(int) = ffs;
PIN int (*_pin_strcasecmp)(const char *, const char *) = strcasecmp;
PIN int (*_pin_strncasecmp)(const char *, const char *, size_t) = strncasecmp;
