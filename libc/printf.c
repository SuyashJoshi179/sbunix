#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

struct _FILE {
    int fd;
};

static struct _FILE _stdin  = { 0 };
static struct _FILE _stdout = { 1 };
static struct _FILE _stderr = { 2 };

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
        s->pos++;
    } else {
        write(s->fd, &c, 1);
        s->pos++;
    }
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
    int fd = stream ? stream->fd : 0;
    char ch;
    if (read(fd, &ch, 1) != 1) return EOF;
    return (unsigned char)ch;
}
int getc(FILE *stream)  { return fgetc(stream); }
int getchar(void)       { return fgetc(stdin); }

int fflush(FILE *stream) { (void)stream; return 0; }
int fileno(FILE *stream) { return stream ? stream->fd : -1; }

void perror(const char *s) {
    if (s && *s) { fputs(s, stderr); fputs(": ", stderr); }
    fputs("error\n", stderr);
}
