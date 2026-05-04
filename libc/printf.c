#include <stdarg.h>
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

static void sink_num(struct sink *s, unsigned long n, int base, int sign) {
    if (sign && (long)n < 0) {
        sink_put(s, '-');
        n = (unsigned long)(-(long)n);
    }
    char tmp[24];
    int  len = 0;
    if (n == 0) tmp[len++] = '0';
    else while (n) { tmp[len++] = "0123456789abcdef"[n % (unsigned)base]; n /= (unsigned)base; }
    for (int i = len - 1; i >= 0; i--) sink_put(s, tmp[i]);
}

static int do_format(struct sink *s, const char *fmt, va_list ap) {
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { sink_put(s, *p); continue; }
        p++;
        int lng = 0;
        if (*p == 'l') { lng = 1; p++; if (*p == 'l') p++; }
        else if (*p == 'z') { lng = 1; p++; }
        switch (*p) {
        case 's': {
            const char *str = va_arg(ap, const char *);
            if (!str) str = "(null)";
            while (*str) sink_put(s, *str++);
            break;
        }
        case 'd': case 'i':
            sink_num(s, lng ? (unsigned long)va_arg(ap, long)
                            : (unsigned long)va_arg(ap, int), 10, 1);
            break;
        case 'u':
            sink_num(s, lng ? va_arg(ap, unsigned long)
                            : (unsigned long)va_arg(ap, unsigned int), 10, 0);
            break;
        case 'x': case 'X':
            sink_num(s, lng ? va_arg(ap, unsigned long)
                            : (unsigned long)va_arg(ap, unsigned int), 16, 0);
            break;
        case 'p':
            sink_put(s, '0'); sink_put(s, 'x');
            sink_num(s, (unsigned long)va_arg(ap, void *), 16, 0);
            break;
        case 'c':
            sink_put(s, (char)va_arg(ap, int));
            break;
        case '%':
            sink_put(s, '%');
            break;
        default:
            sink_put(s, '%');
            if (lng) sink_put(s, 'l');
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
    write(fd, str, (long)len);
    return (int)len;
}

int puts(const char *str) {
    fputs(str, stdout);
    write(1, "\n", 1);
    return 0;
}

int fputc(int c, FILE *stream) {
    int fd = stream ? stream->fd : 1;
    char ch = (char)c;
    write(fd, &ch, 1);
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
    for (const char *p = mode; *p; p++) if (*p == '+') has_plus = 1;
    switch (mode[0]) {
    case 'r': flags = has_plus ? O_RDWR : O_RDONLY; break;
    case 'w': flags = (has_plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC; break;
    case 'a': flags = (has_plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND; break;
    default: return NULL;
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

char *tmpnam(char *s) {
    (void)s;
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

