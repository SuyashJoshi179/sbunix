#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define _FILE_EOF   0x1
#define _FILE_ERR   0x2
#define _FILE_OWNED 0x4   /* fd was opened by fopen, fclose closes it */

struct _FILE {
    int fd;
    int flags;
    int unget;   /* -1 = none, otherwise pushed-back char (one slot, ANSI required) */
};

static struct _FILE _stdin  = { 0, 0, -1 };
static struct _FILE _stdout = { 1, 0, -1 };
static struct _FILE _stderr = { 2, 0, -1 };

FILE *stdin  = &_stdin;
FILE *stdout = &_stdout;
FILE *stderr = &_stderr;

struct sink {
    char *buf;
    size_t pos;
    size_t cap;
    int    fd;
};

static void sink_put(struct sink *s, char c) {
    if (s->buf) {
        if (s->pos + 1 < s->cap) s->buf[s->pos] = c;
    } else if (s->fd >= 0) {
        write(s->fd, &c, 1);
    }
    /* Always count — supports vsnprintf(NULL, 0, ...) sizing pass. */
    s->pos++;
}

/* Conversion-flag bits (lower-cased C99 set). */
#define PF_LEFT   (1 << 0)   /* '-' : left-justify */
#define PF_PLUS   (1 << 1)   /* '+' : show sign on signed ints */
#define PF_SPACE  (1 << 2)   /* ' ' : leading space on positive signed ints */
#define PF_ALT    (1 << 3)   /* '#' : alternate form (0x for hex, 0 for octal) */
#define PF_ZERO   (1 << 4)   /* '0' : zero-pad numerics */
#define PF_UPPER  (1 << 5)   /* uppercase hex */
#define PF_SIGNED (1 << 6)   /* arg is signed */
#define PF_PREC   (1 << 7)   /* explicit precision provided */

static void emit_pad(struct sink *s, int n, char pad) {
    while (n-- > 0) sink_put(s, pad);
}

/* Render `n` (already absolute-valued for signed) into a base-N string,
 * applying width / precision / flags. `sign_ch` is either 0 or one of
 * '-' / '+' / ' ' to be emitted before any "0x" prefix and any zeros. */
static void emit_num(struct sink *s, unsigned long long n, int base, int flags,
                     int width, int precision, char sign_ch) {
    char digits[32];
    int  len = 0;
    const char *alphabet = (flags & PF_UPPER) ? "0123456789ABCDEF"
                                              : "0123456789abcdef";
    if (n == 0 && (flags & PF_PREC) && precision == 0) {
        /* "%.0d" of 0 produces no digits at all. */
    } else if (n == 0) {
        digits[len++] = '0';
    } else {
        while (n) { digits[len++] = alphabet[n % (unsigned)base]; n /= (unsigned)base; }
    }

    /* Precision: minimum number of digits (zero-pad on the left). */
    int zeros = 0;
    if ((flags & PF_PREC) && precision > len) zeros = precision - len;

    int prefix_len = sign_ch ? 1 : 0;
    int alt_hex = (flags & PF_ALT) && base == 16 && len != 0;
    int alt_oct = (flags & PF_ALT) && base == 8 && (len == 0 || digits[len-1] != '0');
    /* C99 7.19.6.1 "#"/"o": force the first digit to be 0. If a precision-
     * driven leading zero is already coming (zeros > 0), emitting our
     * own '0' prefix produces one more leading 0 than the spec calls for
     * — e.g. "%#.3o" of 8 would render "0010" instead of "010". The
     * zero==0 / precision==0 case is unaffected because zeros stays 0
     * there and the `len == 0` arm still triggers a single '0'. */
    if (alt_oct && zeros > 0) alt_oct = 0;

    int total = prefix_len + (alt_hex ? 2 : 0) + (alt_oct ? 1 : 0) + zeros + len;
    int pad = width > total ? width - total : 0;

    /* Field padding: '0' flag applies only when no precision and not left. */
    int pad_before_digits = 0;
    if (!(flags & PF_LEFT) && (flags & PF_ZERO) && !(flags & PF_PREC)) {
        pad_before_digits = pad;
        pad = 0;
    }

    if (!(flags & PF_LEFT)) emit_pad(s, pad, ' ');
    if (sign_ch) sink_put(s, sign_ch);
    if (alt_hex) { sink_put(s, '0'); sink_put(s, (flags & PF_UPPER) ? 'X' : 'x'); }
    if (alt_oct) { sink_put(s, '0'); }
    emit_pad(s, pad_before_digits, '0');
    emit_pad(s, zeros, '0');
    for (int i = len - 1; i >= 0; i--) sink_put(s, digits[i]);
    if (flags & PF_LEFT) emit_pad(s, pad, ' ');
}

/* Emit a string with optional precision (max bytes from string) and width.
 * NULL str rendered as "(null)" per glibc convention. */
static void emit_str(struct sink *s, const char *str, int flags,
                     int width, int precision) {
    if (!str) str = "(null)";
    int len = 0;
    if (flags & PF_PREC) {
        while (str[len] && len < precision) len++;
    } else {
        while (str[len]) len++;
    }
    int pad = width > len ? width - len : 0;
    if (!(flags & PF_LEFT)) emit_pad(s, pad, ' ');
    for (int i = 0; i < len; i++) sink_put(s, str[i]);
    if (flags & PF_LEFT) emit_pad(s, pad, ' ');
}

static int do_format(struct sink *s, const char *fmt, va_list ap) {
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { sink_put(s, *p); continue; }
        p++;

        /* Flag characters */
        int flags = 0;
        for (;; p++) {
            switch (*p) {
            case '-': flags |= PF_LEFT;  continue;
            case '+': flags |= PF_PLUS;  continue;
            case ' ': flags |= PF_SPACE; continue;
            case '#': flags |= PF_ALT;   continue;
            case '0': flags |= PF_ZERO;  continue;
            }
            break;
        }

        /* Width */
        int width = 0;
        if (*p == '*') {
            int w = va_arg(ap, int);
            if (w < 0) { flags |= PF_LEFT; w = -w; }
            width = w;
            p++;
        } else while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0'); p++;
        }

        /* Precision */
        int precision = 0;
        if (*p == '.') {
            p++;
            flags |= PF_PREC;
            if (*p == '*') {
                int q = va_arg(ap, int);
                if (q >= 0) precision = q; else flags &= ~PF_PREC;
                p++;
            } else while (*p >= '0' && *p <= '9') {
                precision = precision * 10 + (*p - '0'); p++;
            }
        }

        /* Length modifier.
         *   0 = (default) int / unsigned int
         *   1 = long / unsigned long  (also z, j, t)
         *   2 = long long / unsigned long long
         *  -1 = short / unsigned short
         *  -2 = signed char / unsigned char
         *
         * h / hh matter because default argument promotions widen
         * `(unsigned) short` and `(unsigned) char` to int (on lp64 where
         * int can hold the full unsigned-short range) before the value
         * reaches va_arg. So va_arg(ap, unsigned int) for a %hu/%hhu
         * arg is UB even though it often "works" — we must read as
         * int and narrow to the declared width. */
        int lng = 0;
        switch (*p) {
        case 'h':
            p++; lng = -1; if (*p == 'h') { p++; lng = -2; } break;
        case 'l':
            p++; lng = 1; if (*p == 'l') { p++; lng = 2; } break;
        case 'z':
        case 'j':
        case 't':
            p++; lng = 1; break;
        }

        /* Conversion */
        switch (*p) {
        case 's':
            emit_str(s, va_arg(ap, const char *), flags, width, precision);
            break;
        case 'c': {
            char c = (char)va_arg(ap, int);
            int pad = width > 1 ? width - 1 : 0;
            if (!(flags & PF_LEFT)) emit_pad(s, pad, ' ');
            sink_put(s, c);
            if (flags & PF_LEFT)  emit_pad(s, pad, ' ');
            break;
        }
        case 'd': case 'i': {
            /* lng: 0 = int, 1 = long, 2 = long long, -1 = short, -2 = char.
             * va_arg type must exactly match the *promoted* type the
             * caller passed — mismatch is UB and breaks on ABIs where
             * long != long long (ILP32, Win64). RV64 lp64 happens to
             * have long == long long == 64-bit, but this is portable
             * per-step. For h/hh the underlying short/char is promoted
             * to int by default argument promotions, so we read int
             * and narrow back via a (short)/(signed char) cast — both
             * sign-extend correctly when widened to long long. */
            long long v;
            if      (lng == 2)  v = va_arg(ap, long long);
            else if (lng == 1)  v = (long long)va_arg(ap, long);
            else if (lng == -1) v = (long long)(short)va_arg(ap, int);
            else if (lng == -2) v = (long long)(signed char)va_arg(ap, int);
            else                v = (long long)va_arg(ap, int);
            unsigned long long mag;
            char sign_ch = 0;
            if (v < 0) { mag = (unsigned long long)(-(v + 1)) + 1ULL; sign_ch = '-'; }
            else { mag = (unsigned long long)v;
                   if (flags & PF_PLUS) sign_ch = '+';
                   else if (flags & PF_SPACE) sign_ch = ' '; }
            emit_num(s, mag, 10, flags, width, precision, sign_ch);
            break;
        }
        case 'u': {
            /* h/hh: arg promoted to int; read int and truncate to the
             * requested unsigned width. Reading via va_arg(ap, unsigned
             * int) here would be UB on lp64 where the actual promoted
             * type is signed int. */
            unsigned long long v;
            if      (lng == 2)  v = va_arg(ap, unsigned long long);
            else if (lng == 1)  v = (unsigned long long)va_arg(ap, unsigned long);
            else if (lng == -1) v = (unsigned long long)(unsigned short)va_arg(ap, int);
            else if (lng == -2) v = (unsigned long long)(unsigned char)va_arg(ap, int);
            else                v = (unsigned long long)va_arg(ap, unsigned int);
            emit_num(s, v, 10, flags, width, precision, 0);
            break;
        }
        case 'o': {
            unsigned long long v;
            if      (lng == 2)  v = va_arg(ap, unsigned long long);
            else if (lng == 1)  v = (unsigned long long)va_arg(ap, unsigned long);
            else if (lng == -1) v = (unsigned long long)(unsigned short)va_arg(ap, int);
            else if (lng == -2) v = (unsigned long long)(unsigned char)va_arg(ap, int);
            else                v = (unsigned long long)va_arg(ap, unsigned int);
            emit_num(s, v, 8, flags, width, precision, 0);
            break;
        }
        case 'x': case 'X': {
            unsigned long long v;
            if      (lng == 2)  v = va_arg(ap, unsigned long long);
            else if (lng == 1)  v = (unsigned long long)va_arg(ap, unsigned long);
            else if (lng == -1) v = (unsigned long long)(unsigned short)va_arg(ap, int);
            else if (lng == -2) v = (unsigned long long)(unsigned char)va_arg(ap, int);
            else                v = (unsigned long long)va_arg(ap, unsigned int);
            int xflags = flags;
            if (*p == 'X') xflags |= PF_UPPER;
            emit_num(s, v, 16, xflags, width, precision, 0);
            break;
        }
        case 'p': {
            void *ptr = va_arg(ap, void *);
            /* %p ignores any user-supplied precision (POSIX leaves it
             * implementation-defined and most libcs print a fixed-width
             * hex representation). Clear PF_PREC so emit_str doesn't
             * truncate "(nil)" / the stub to `precision` bytes. */
            int pflags = flags & ~PF_PREC;
            if (!ptr) {
                emit_str(s, "(nil)", pflags, width, 0);
            } else {
                /* "0x" prefix + hex digits, width-aware. */
                emit_num(s, (unsigned long long)(uintptr_t)ptr, 16,
                         pflags | PF_ALT, width, 0, 0);
            }
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
            /* Toolchain is soft-float (-mabi=lp64, no F/D ext). Stub
             * float conversions to "0.000000" with width respected so
             * format strings using %f etc. don't emit a literal '%f'.
             * Clear PF_PREC: a user-supplied precision (e.g. %.2f) was
             * meant for the float formatter, not as a string-length cap
             * for the stub — otherwise %.0f would emit nothing. */
            (void)va_arg(ap, double);   /* consume the arg slot */
            emit_str(s, "0.000000", flags & ~PF_PREC, width, 0);
            break;
        }
        case '%':
            sink_put(s, '%');
            break;
        case '\0':
            /* Trailing '%': emit it literally and stop scanning. */
            sink_put(s, '%');
            p--;
            break;
        default:
            /* Unknown conversion — emit verbatim so a malformed format
             * is visible rather than silently swallowed. */
            sink_put(s, '%');
            sink_put(s, *p);
            break;
        }
    }
    if (s->buf && s->cap > 0) {
        size_t term = s->pos < s->cap ? s->pos : s->cap - 1;
        s->buf[term] = '\0';
    }
    return (int)s->pos;
}

int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap) {
    struct sink s = { buf, 0, n, -1 };
    return do_format(&s, fmt, ap);
}

int vsprintf(char *buf, const char *fmt, va_list ap) {
    return vsnprintf(buf, (size_t)-1 >> 1, fmt, ap);
}

int snprintf(char *buf, size_t n, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vsprintf(buf, fmt, ap);
    va_end(ap);
    return r;
}

int vfprintf(FILE *stream, const char *fmt, va_list ap) {
    int fd = stream ? stream->fd : 1;
    struct sink s = { 0, 0, 0, fd };
    return do_format(&s, fmt, ap);
}

int vprintf(const char *fmt, va_list ap) {
    return vfprintf(stdout, fmt, ap);
}

int fprintf(FILE *stream, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vfprintf(stream, fmt, ap);
    va_end(ap);
    return r;
}

int printf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return r;
}

int fputs(const char *str, FILE *stream) {
    int fd = stream ? stream->fd : 1;
    size_t len = strlen(str);
    long w = write(fd, str, (long)len);
    if (w < 0 || (size_t)w != len) return EOF;
    return (int)len;
}

int puts(const char *str) {
    if (fputs(str, stdout) == EOF) return EOF;
    if (write(1, "\n", 1) != 1) return EOF;
    return 0;
}

int fputc(int c, FILE *stream) {
    int fd = stream ? stream->fd : 1;
    char ch = (char)c;
    if (write(fd, &ch, 1) != 1) return EOF;
    return (unsigned char)ch;
}

int putc(int c, FILE *stream)   { return fputc(c, stream); }
int putchar(int c)              { return fputc(c, stdout); }

int fgetc(FILE *stream) {
    if (!stream) return EOF;
    if (stream->unget >= 0) {
        int c = stream->unget;
        stream->unget = -1;
        return c;
    }
    char ch;
    long r = read(stream->fd, &ch, 1);
    if (r == 1) return (unsigned char)ch;
    stream->flags |= (r == 0) ? _FILE_EOF : _FILE_ERR;
    return EOF;
}
int getc(FILE *stream)  { return fgetc(stream); }
int getchar(void)       { return fgetc(stdin); }

char *fgets(char *buf, int n, FILE *stream) {
    if (!buf || n <= 0 || !stream) return NULL;
    int i = 0;
    while (i < n - 1) {
        int c = fgetc(stream);
        if (c == EOF) {
            if (i == 0) return NULL;
            break;
        }
        buf[i++] = (char)c;
        if (c == '\n') break;
    }
    buf[i] = '\0';
    return buf;
}

long getline(char **lineptr, unsigned long *n, FILE *stream) {
    if (!lineptr || !n || !stream) return -1;
    if (!*lineptr || *n == 0) {
        unsigned long cap = 128;
        char *p = realloc(*lineptr, cap);
        if (!p) return -1;
        *lineptr = p;
        *n = cap;
    }
    unsigned long len = 0;
    for (;;) {
        int c = fgetc(stream);
        if (c == EOF) {
            if (len == 0) return -1;
            break;
        }
        if (len + 1 >= *n) {
            unsigned long cap = *n * 2;
            if (cap < *n) return -1;
            char *p = realloc(*lineptr, cap);
            if (!p) return -1;
            *lineptr = p;
            *n = cap;
        }
        (*lineptr)[len++] = (char)c;
        if (c == '\n') break;
    }
    (*lineptr)[len] = '\0';
    return (long)len;
}

int fflush(FILE *stream) { (void)stream; return 0; }
int fileno(FILE *stream) { return stream ? stream->fd : -1; }
int feof(FILE *stream)   { return stream ? (stream->flags & _FILE_EOF) != 0 : 0; }
int ferror(FILE *stream) { return stream ? (stream->flags & _FILE_ERR) != 0 : 0; }
void clearerr(FILE *stream) { if (stream) stream->flags &= ~(_FILE_EOF | _FILE_ERR); }

void perror(const char *s) {
    if (s && *s) { fputs(s, stderr); fputs(": ", stderr); }
    fputs("error\n", stderr);
}

FILE *fopen(const char *path, const char *mode) {
    if (!path || !mode) return NULL;
    int flags = 0;
    int has_plus = 0;
    int has_excl = 0;
    for (const char *p = mode; *p; p++) {
        if (*p == '+') has_plus = 1;
        else if (*p == 'x') has_excl = 1;
    }
    switch (mode[0]) {
    case 'r': flags = has_plus ? O_RDWR : O_RDONLY; break;
    case 'w': flags = (has_plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC; break;
    case 'a': flags = (has_plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND; break;
    default: return NULL;
    }
    /* C11 'x' (exclusive create): only valid with 'w' since 'r' doesn't
     * create and 'a' would silently succeed on an existing file even
     * with O_EXCL semantics. */
    if (has_excl) {
        if (mode[0] != 'w') return NULL;
        flags |= O_EXCL;
    }
    int fd = open(path, flags);
    if (fd < 0) return NULL;
    FILE *f = malloc(sizeof(FILE));
    if (!f) { close(fd); return NULL; }
    f->fd = fd;
    f->flags = _FILE_OWNED;
    f->unget = -1;
    return f;
}

int fclose(FILE *stream) {
    if (!stream) return EOF;
    int r = 0;
    if (stream->flags & _FILE_OWNED) {
        if (close(stream->fd) < 0) r = EOF;
        free(stream);
    }
    return r;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!ptr || !stream || size == 0 || nmemb == 0) return 0;
    size_t total = size * nmemb;
    size_t got = 0;
    char *p = ptr;
    if (stream->unget >= 0 && got < total) {
        p[got++] = (char)stream->unget;
        stream->unget = -1;
    }
    while (got < total) {
        long r = read(stream->fd, p + got, (long)(total - got));
        if (r < 0) { stream->flags |= _FILE_ERR; break; }
        if (r == 0) { stream->flags |= _FILE_EOF; break; }
        got += (size_t)r;
    }
    return got / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!ptr || !stream || size == 0 || nmemb == 0) return 0;
    size_t total = size * nmemb;
    size_t put = 0;
    const char *p = ptr;
    while (put < total) {
        long r = write(stream->fd, p + put, (long)(total - put));
        if (r < 0) { stream->flags |= _FILE_ERR; break; }
        if (r == 0) break;
        put += (size_t)r;
    }
    return put / size;
}

int fseek(FILE *stream, long off, int whence) {
    if (!stream) return -1;
    long r = lseek(stream->fd, off, whence);
    if (r < 0) { stream->flags |= _FILE_ERR; return -1; }
    stream->flags &= ~_FILE_EOF;
    stream->unget = -1;
    return 0;
}

long ftell(FILE *stream) {
    if (!stream) return -1;
    long r = lseek(stream->fd, 0, SEEK_CUR);
    if (r < 0) return -1;
    if (stream->unget >= 0 && r > 0) r--;
    return r;
}

int fseeko(FILE *stream, off_t off, int whence) {
    return fseek(stream, (long)off, whence);
}

off_t ftello(FILE *stream) {
    return (off_t)ftell(stream);
}

void rewind(FILE *stream) {
    if (!stream) return;
    lseek(stream->fd, 0, SEEK_SET);
    stream->flags &= ~(_FILE_EOF | _FILE_ERR);
    stream->unget = -1;
}

int remove(const char *path) {
    return unlink(path);
}

/* rename() is now a real syscall — see libc/syscall.c. */

int ungetc(int c, FILE *stream) {
    if (!stream || c == EOF || stream->unget >= 0) return EOF;
    stream->unget = c & 0xff;
    stream->flags &= ~_FILE_EOF;
    return c & 0xff;
}

void setbuf(FILE *stream, char *buf) {
    (void)stream; (void)buf;
}

int setvbuf(FILE *stream, char *buf, int mode, size_t size) {
    (void)stream; (void)buf; (void)mode; (void)size;
    return 0;
}

/* tmpnam: build "/tmp/tmp.<pid>.<counter>" into the caller-supplied
 * buffer (or a static slot when s == NULL). Each call advances the
 * counter so two successive tmpnam invocations don't collide. */
char *tmpnam(char *s) {
    static char buf[L_tmpnam];
    static unsigned counter = 0;
    char *out = s ? s : buf;

    long pid = getpid();
    int i = 0;
    static const char prefix[] = "/tmp/tmp.";
    for (unsigned k = 0; k < sizeof(prefix) - 1 && i < L_tmpnam - 1; k++)
        out[i++] = prefix[k];
    /* pid as decimal */
    char num[12];
    int nl = 0;
    if (pid <= 0) num[nl++] = '0';
    else { unsigned long v = (unsigned long)pid;
        while (v) { num[nl++] = (char)('0' + v % 10); v /= 10; } }
    while (nl > 0 && i < L_tmpnam - 1) out[i++] = num[--nl];
    if (i < L_tmpnam - 1) out[i++] = '.';
    unsigned c = ++counter;
    nl = 0;
    if (c == 0) num[nl++] = '0';
    else { while (c) { num[nl++] = (char)('0' + c % 10); c /= 10; } }
    while (nl > 0 && i < L_tmpnam - 1) out[i++] = num[--nl];
    out[i] = '\0';
    return out;
}

/* tmpfile: create+open a fresh /tmp file, unlink it immediately so it
 * vanishes when the last fd closes. We rely on /tmp being mounted as
 * tmpfs at boot; if it isn't, fopen fails and we return NULL. */
FILE *tmpfile(void) {
    /* Try a handful of names so a concurrent caller's collision doesn't
     * defeat us. */
    for (int attempt = 0; attempt < 16; attempt++) {
        char name[L_tmpnam];
        (void)tmpnam(name);
        FILE *f = fopen(name, "w+");
        if (f) {
            unlink(name);
            return f;
        }
    }
    return NULL;
}

int vasprintf(char **strp, const char *fmt, va_list ap) {
    if (!strp) return -1;
    va_list ap2;
    va_copy(ap2, ap);
    /* Sizing pass: NULL buf + fd=-1 makes sink_put count without writing. */
    struct sink probe = { 0, 0, 0, -1 };
    int n = do_format(&probe, fmt, ap2);
    va_end(ap2);
    if (n < 0) return -1;
    char *buf = malloc((size_t)n + 1);
    if (!buf) return -1;
    int r = vsnprintf(buf, (size_t)n + 1, fmt, ap);
    if (r < 0) { free(buf); return -1; }
    *strp = buf;
    return r;
}

int asprintf(char **strp, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vasprintf(strp, fmt, ap);
    va_end(ap);
    return r;
}

long getdelim(char **lineptr, unsigned long *n, int delim, FILE *stream) {
    if (!lineptr || !n || !stream) return -1;
    if (!*lineptr || *n == 0) {
        unsigned long cap = 128;
        char *p = realloc(*lineptr, cap);
        if (!p) return -1;
        *lineptr = p;
        *n = cap;
    }
    unsigned long len = 0;
    for (;;) {
        int c = fgetc(stream);
        if (c == EOF) {
            if (len == 0) return -1;
            break;
        }
        if (len + 1 >= *n) {
            unsigned long cap = *n * 2;
            if (cap < *n) return -1;
            char *p = realloc(*lineptr, cap);
            if (!p) return -1;
            *lineptr = p;
            *n = cap;
        }
        (*lineptr)[len++] = (char)c;
        if (c == delim) break;
    }
    (*lineptr)[len] = '\0';
    return (long)len;
}

