#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

static void buf_put(char *buf, int *pos, int cap, char c) {
    if (*pos < cap) buf[(*pos)++] = c;
}

static void buf_num(char *buf, int *pos, int cap,
                    unsigned long n, int base, int sign) {
    if (sign && (long)n < 0) {
        buf_put(buf, pos, cap, '-');
        n = (unsigned long)(-(long)n);
    }
    char tmp[20];
    int  len = 0;
    if (n == 0) {
        tmp[len++] = '0';
    } else {
        while (n) {
            tmp[len++] = "0123456789abcdef"[n % (unsigned)base];
            n /= (unsigned)base;
        }
    }
    for (int i = len - 1; i >= 0; i--)
        buf_put(buf, pos, cap, tmp[i]);
}

int printf(const char *fmt, ...) {
    char buf[512];
    int  pos = 0;
    int  cap = (int)sizeof(buf);

    va_list ap;
    va_start(ap, fmt);

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { buf_put(buf, &pos, cap, *p); continue; }
        p++;
        int lng = 0;
        if (*p == 'l') { lng = 1; p++; }
        switch (*p) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (*s) buf_put(buf, &pos, cap, *s++);
            break;
        }
        case 'd':
            buf_num(buf, &pos, cap,
                    lng ? (unsigned long)va_arg(ap, long)
                        : (unsigned long)va_arg(ap, int),
                    10, 1);
            break;
        case 'u':
            buf_num(buf, &pos, cap,
                    lng ? va_arg(ap, unsigned long)
                        : (unsigned long)va_arg(ap, unsigned int),
                    10, 0);
            break;
        case 'x':
            buf_num(buf, &pos, cap,
                    lng ? va_arg(ap, unsigned long)
                        : (unsigned long)va_arg(ap, unsigned int),
                    16, 0);
            break;
        case 'c':
            buf_put(buf, &pos, cap, (char)va_arg(ap, int));
            break;
        case '%':
            buf_put(buf, &pos, cap, '%');
            break;
        default:
            buf_put(buf, &pos, cap, '%');
            if (lng) buf_put(buf, &pos, cap, 'l');
            buf_put(buf, &pos, cap, *p);
            break;
        }
    }

    va_end(ap);
    if (pos > 0) write(1, buf, (long)pos);
    return pos;
}
