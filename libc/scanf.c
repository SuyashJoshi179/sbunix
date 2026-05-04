#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* scanf family — string and stream sources. Handles %d %i %u %o %x %X %s %c
 * with optional length modifier (h, hh, l, ll, z) and field width. Supports
 * suppression with '*'. No %f / %e / %g (no float support). No %[ class set.
 * No %n — a real character counter would need to thread through every
 * get/unget path; callers that need it should compute offsets themselves.
 *
 * Source abstraction: caller supplies get/unget callbacks plus a cookie.
 * Used for sscanf (string cookie) and fscanf (FILE cookie). */

struct scan_src {
    int (*get)(void *cookie);
    int (*unget)(int c, void *cookie);
    void *cookie;
    int eof_or_err;     /* set when get returns EOF without prior conversion */
};

/* String source: cookie = char ** (advances on read). Single pushback slot. */
struct str_src {
    const char *p;
    int unget;
};

static int str_get(void *c) {
    struct str_src *s = c;
    if (s->unget >= 0) { int r = s->unget; s->unget = -1; return r; }
    int ch = (unsigned char)*s->p;
    if (!ch) return EOF;
    s->p++;
    return ch;
}

static int str_unget(int c, void *cookie) {
    struct str_src *s = cookie;
    if (c == EOF) return EOF;
    s->unget = c & 0xff;
    return c & 0xff;
}

/* FILE source. */
static int file_get(void *c) {
    return fgetc((FILE *)c);
}

static int file_unget(int c, void *cookie) {
    return ungetc(c, (FILE *)cookie);
}

static int skip_ws(struct scan_src *s) {
    int c;
    do { c = s->get(s->cookie); } while (c != EOF && isspace(c));
    if (c == EOF) { s->eof_or_err = 1; return EOF; }
    s->unget(c, s->cookie);
    return 0;
}

static int parse_int(struct scan_src *s, int base, int width,
                     unsigned long long *out, int *neg) {
    int c = s->get(s->cookie);
    int saw = 0;
    *neg = 0;
    if (c == '+' || c == '-') {
        *neg = (c == '-');
        if (width > 0) width--;
        c = s->get(s->cookie);
    }
    /* %i auto-detect 0/0x/0X */
    if (base == 0) {
        base = 10;
        if (c == '0') {
            saw = 1;
            int c2 = s->get(s->cookie);
            if ((c2 == 'x' || c2 == 'X')) {
                base = 16;
                c = s->get(s->cookie);
                if (width > 0) width -= 2;
            } else {
                base = 8;
                if (c2 != EOF) s->unget(c2, s->cookie);
            }
        }
    } else if (base == 16) {
        if (c == '0') {
            int c2 = s->get(s->cookie);
            if (c2 == 'x' || c2 == 'X') {
                c = s->get(s->cookie);
                if (width > 0) width -= 2;
            } else {
                if (c2 != EOF) s->unget(c2, s->cookie);
                /* leading '0' is a valid digit */
            }
        }
    }
    unsigned long long v = 0;
    while (c != EOF && width != 0) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * (unsigned)base + (unsigned)d;
        saw = 1;
        if (width > 0) width--;
        c = s->get(s->cookie);
    }
    if (c != EOF) s->unget(c, s->cookie);
    if (!saw) { if (c == EOF) s->eof_or_err = 1; return -1; }
    *out = v;
    return 0;
}

static int do_scanf(struct scan_src *s, const char *fmt, va_list ap) {
    int matched = 0;
    int total_chars = 0;
    /* total_chars tracks consumed bytes for %n; we count via wrappers below. */
    (void)total_chars;
    for (const char *p = fmt; *p; p++) {
        if (isspace((unsigned char)*p)) {
            if (skip_ws(s) == EOF) goto done;
            continue;
        }
        if (*p != '%') {
            int c = s->get(s->cookie);
            if (c != *p) {
                if (c == EOF) s->eof_or_err = 1;
                else s->unget(c, s->cookie);
                goto done;
            }
            continue;
        }
        p++;
        int suppress = 0;
        if (*p == '*') { suppress = 1; p++; }
        int width = -1;
        if (isdigit((unsigned char)*p)) {
            width = 0;
            while (isdigit((unsigned char)*p)) { width = width * 10 + (*p - '0'); p++; }
        }
        int len_mod = 0;  /* 0=int, 1=short, 2=long, 3=long long, 4=size_t, 5=hh */
        if (*p == 'h') { len_mod = 1; p++; if (*p == 'h') { len_mod = 5; p++; } }
        else if (*p == 'l') { len_mod = 2; p++; if (*p == 'l') { len_mod = 3; p++; } }
        else if (*p == 'z') { len_mod = 4; p++; }
        else if (*p == 'j') { len_mod = 3; p++; }

        switch (*p) {
        case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': case 'p': {
            if (skip_ws(s) == EOF) goto done;
            int base = (*p == 'd' || *p == 'u') ? 10
                     : (*p == 'o') ? 8
                     : (*p == 'x' || *p == 'X' || *p == 'p') ? 16
                     : 0;
            unsigned long long v = 0;
            int neg = 0;
            if (parse_int(s, base, width, &v, &neg) < 0) goto done;
            if (neg) v = (unsigned long long)-(long long)v;
            int is_signed = (*p == 'd' || *p == 'i');
            if (!suppress) {
                if (*p == 'p') {
                    *va_arg(ap, void **) = (void *)(uintptr_t)v;
                } else if (is_signed) {
                    if      (len_mod == 5) *va_arg(ap, signed char *)  = (signed char)v;
                    else if (len_mod == 1) *va_arg(ap, short *)        = (short)v;
                    else if (len_mod == 2) *va_arg(ap, long *)         = (long)v;
                    else if (len_mod == 3) *va_arg(ap, long long *)    = (long long)v;
                    else if (len_mod == 4) *va_arg(ap, size_t *)       = (size_t)v;  /* %zd – signed view of size_t */
                    else                   *va_arg(ap, int *)          = (int)v;
                } else {
                    if      (len_mod == 5) *va_arg(ap, unsigned char *)      = (unsigned char)v;
                    else if (len_mod == 1) *va_arg(ap, unsigned short *)     = (unsigned short)v;
                    else if (len_mod == 2) *va_arg(ap, unsigned long *)      = (unsigned long)v;
                    else if (len_mod == 3) *va_arg(ap, unsigned long long *) = (unsigned long long)v;
                    else if (len_mod == 4) *va_arg(ap, size_t *)             = (size_t)v;
                    else                   *va_arg(ap, unsigned int *)       = (unsigned int)v;
                }
                matched++;
            }
            break;
        }
        case 's': {
            if (skip_ws(s) == EOF) goto done;
            char *out = suppress ? 0 : va_arg(ap, char *);
            int n = 0;
            int c;
            while ((c = s->get(s->cookie)) != EOF && !isspace(c)) {
                if (width >= 0 && n >= width) { s->unget(c, s->cookie); break; }
                if (out) out[n] = (char)c;
                n++;
            }
            if (c == EOF && n == 0) { s->eof_or_err = 1; goto done; }
            if (c != EOF && (width < 0 || n < width)) s->unget(c, s->cookie);
            if (out) out[n] = 0;
            if (!suppress) matched++;
            break;
        }
        case 'c': {
            int n = (width < 0) ? 1 : width;
            char *out = suppress ? 0 : va_arg(ap, char *);
            for (int i = 0; i < n; i++) {
                int c = s->get(s->cookie);
                if (c == EOF) {
                    if (i == 0) { s->eof_or_err = 1; goto done; }
                    break;
                }
                if (out) out[i] = (char)c;
            }
            if (!suppress) matched++;
            break;
        }
        case '%': {
            int c = s->get(s->cookie);
            if (c != '%') {
                if (c == EOF) s->eof_or_err = 1;
                else s->unget(c, s->cookie);
                goto done;
            }
            break;
        }
        /* %n intentionally not implemented: a real char counter would have to
         * thread through every get/unget path. Fall through to default so a
         * caller using %n gets a clear "format failure" instead of silent
         * zeros. */
        default:
            goto done;
        }
    }
done:
    return matched ? matched : (s->eof_or_err ? EOF : 0);
}

int vsscanf(const char *str, const char *fmt, va_list ap) {
    struct str_src ss = { str, -1 };
    struct scan_src s = { str_get, str_unget, &ss, 0 };
    return do_scanf(&s, fmt, ap);
}

int sscanf(const char *str, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vsscanf(str, fmt, ap);
    va_end(ap);
    return r;
}

int vfscanf(FILE *stream, const char *fmt, va_list ap) {
    struct scan_src s = { file_get, file_unget, stream, 0 };
    return do_scanf(&s, fmt, ap);
}

int fscanf(FILE *stream, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vfscanf(stream, fmt, ap);
    va_end(ap);
    return r;
}

int vscanf(const char *fmt, va_list ap) {
    return vfscanf(stdin, fmt, ap);
}

int scanf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vfscanf(stdin, fmt, ap);
    va_end(ap);
    return r;
}
