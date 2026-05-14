/*
 * POSIX conformance test: <string.h>
 *
 * Reference: docs/susv5-html/basedefs/string.h.html
 *
 * Audited POSIX functions:
 *   memchr, memcmp, memcpy, memmove, memset, stpcpy, stpncpy, strcat,
 *   strchr, strcmp, strcpy, strcspn, strdup, strerror, strlen, strncat,
 *   strncmp, strncpy, strndup, strnlen, strpbrk, strrchr, strsignal,
 *   strspn, strstr, strtok, strtok_r
 *
 * Excluded (POSIX, not in our libc): memccpy, memmem, strcoll, strcoll_l,
 *   strerror_l, strerror_r, strlcat, strlcpy, strxfrm, strxfrm_l
 *
 * Excluded (non-POSIX): memrchr, mempcpy, strsep, strchrnul, strcasestr,
 *   strverscmp (all GNU extensions)
 */
#include <string.h>

#define PIN __attribute__((unused)) static

PIN size_t (*_pin_strlen)(const char *) = strlen;
PIN size_t (*_pin_strnlen)(const char *, size_t) = strnlen;
PIN int    (*_pin_strcmp)(const char *, const char *) = strcmp;
PIN int    (*_pin_strncmp)(const char *, const char *, size_t) = strncmp;
PIN char  *(*_pin_strcpy)(char *, const char *) = strcpy;
PIN char  *(*_pin_strncpy)(char *, const char *, size_t) = strncpy;
PIN char  *(*_pin_strcat)(char *, const char *) = strcat;
PIN char  *(*_pin_strncat)(char *, const char *, size_t) = strncat;
PIN char  *(*_pin_strchr)(const char *, int) = strchr;
PIN char  *(*_pin_strrchr)(const char *, int) = strrchr;
PIN char  *(*_pin_strstr)(const char *, const char *) = strstr;
PIN char  *(*_pin_strpbrk)(const char *, const char *) = strpbrk;
PIN size_t (*_pin_strspn)(const char *, const char *) = strspn;
PIN size_t (*_pin_strcspn)(const char *, const char *) = strcspn;
PIN char  *(*_pin_strtok)(char *, const char *) = strtok;
PIN char  *(*_pin_strtok_r)(char *, const char *, char **) = strtok_r;
PIN char  *(*_pin_strdup)(const char *) = strdup;
PIN char  *(*_pin_strndup)(const char *, size_t) = strndup;
PIN char  *(*_pin_strerror)(int) = strerror;
PIN char  *(*_pin_strsignal)(int) = strsignal;
PIN char  *(*_pin_stpcpy)(char *, const char *) = stpcpy;
PIN char  *(*_pin_stpncpy)(char *, const char *, size_t) = stpncpy;
PIN void  *(*_pin_memset)(void *, int, size_t) = memset;
PIN void  *(*_pin_memcpy)(void *, const void *, size_t) = memcpy;
PIN void  *(*_pin_memmove)(void *, const void *, size_t) = memmove;
PIN int    (*_pin_memcmp)(const void *, const void *, size_t) = memcmp;
PIN void  *(*_pin_memchr)(const void *, int, size_t) = memchr;
