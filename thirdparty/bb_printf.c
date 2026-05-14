#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>

/*
 * Full-featured printf for BusyBox.  BusyBox code calls these via
 * #define printf bb_printf  (etc.) in its platform.h patch.
 */

static int bb_vfprintf_fd(int fd, const char *fmt, va_list ap) {
	int cnt = 0;
	for (; *fmt; fmt++) {
		if (*fmt != '%') { write(fd, fmt, 1); cnt++; continue; }
		fmt++;
		int left = 0, zero = 0, width = 0, islong = 0;
		if (*fmt == '-') { left = 1; fmt++; }
		if (*fmt == '0') { zero = 1; fmt++; }
		if (*fmt == '*') { width = va_arg(ap, int); if (width < 0) { left = 1; width = -width; } fmt++; }
		else while (*fmt >= '0' && *fmt <= '9') width = width * 10 + *fmt++ - '0';
		int prec = -1;
		if (*fmt == '.') { fmt++; if (*fmt == '*') { prec = va_arg(ap, int); fmt++; } else { prec = 0; while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + *fmt++ - '0'; } }
		if (*fmt == 'l') { islong = 1; fmt++; if (*fmt == 'l') fmt++; }
		else if (*fmt == 'z' || *fmt == 'j') { islong = 1; fmt++; }
		else if (*fmt == 'h') { fmt++; if (*fmt == 'h') fmt++; }
		switch (*fmt) {
		case 's': {
			const char *s = va_arg(ap, const char *);
			if (!s) s = "(null)";
			int slen = strlen(s);
			if (prec >= 0 && slen > prec) slen = prec;
			int pad = width > slen ? width - slen : 0;
			if (!left) for (int j = 0; j < pad; j++) { write(fd, " ", 1); cnt++; }
			write(fd, s, slen); cnt += slen;
			if (left) for (int j = 0; j < pad; j++) { write(fd, " ", 1); cnt++; }
			break;
		}
		case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': {
			unsigned long v;
			int neg = 0, base = 10;
			if (*fmt == 'x' || *fmt == 'X') base = 16;
			else if (*fmt == 'o') base = 8;
			const char *digits = (*fmt == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
			if (*fmt == 'd' || *fmt == 'i') {
				long sv = islong ? va_arg(ap, long) : va_arg(ap, int);
				if (sv < 0) { neg = 1; v = -sv; } else v = sv;
			} else {
				v = islong ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
			}
			char buf[20]; int i = 0;
			do { buf[i++] = digits[v % base]; v /= base; } while (v);
			int len = i + neg;
			char pc = zero ? '0' : ' ';
			if (neg && zero) { write(fd, "-", 1); cnt++; }
			if (!left) while (len < width) { write(fd, &pc, 1); cnt++; len++; }
			if (neg && !zero) { write(fd, "-", 1); cnt++; }
			while (i--) { write(fd, &buf[i], 1); cnt++; }
			if (left) while (len < width) { write(fd, " ", 1); cnt++; len++; }
			break;
		}
		case 'p':
			write(fd, "0x", 2); cnt += 2;
			{ unsigned long v = va_arg(ap, unsigned long);
			  char buf[20]; int i = 0;
			  do { buf[i++] = "0123456789abcdef"[v % 16]; v /= 16; } while (v);
			  while (i--) { write(fd, &buf[i], 1); cnt++; } }
			break;
		case 'c': {
			char c = va_arg(ap, int);
			if (!left) for (int j = 1; j < width; j++) { write(fd, " ", 1); cnt++; }
			write(fd, &c, 1); cnt++;
			if (left) for (int j = 1; j < width; j++) { write(fd, " ", 1); cnt++; }
			break;
		}
		case '%': write(fd, "%", 1); cnt++; break;
		case 'n': break;
		default: break;
		}
	}
	return cnt;
}

int bb_vsnprintf(char *buf, size_t sz, const char *fmt, va_list ap) {
	int pos = 0;
#define PUT(c) do { if ((size_t)pos < sz - 1) buf[pos] = (c); pos++; } while (0)
	for (; *fmt; fmt++) {
		if (*fmt != '%') { PUT(*fmt); continue; }
		fmt++;
		int left = 0, zero = 0, width = 0, islong = 0;
		if (*fmt == '-') { left = 1; fmt++; }
		if (*fmt == '0') { zero = 1; fmt++; }
		if (*fmt == '*') { width = va_arg(ap, int); if (width < 0) { left = 1; width = -width; } fmt++; }
		else while (*fmt >= '0' && *fmt <= '9') width = width * 10 + *fmt++ - '0';
		int prec = -1;
		if (*fmt == '.') { fmt++; if (*fmt == '*') { prec = va_arg(ap, int); fmt++; } else { prec = 0; while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + *fmt++ - '0'; } }
		if (*fmt == 'l') { islong = 1; fmt++; if (*fmt == 'l') fmt++; }
		else if (*fmt == 'z' || *fmt == 'j') { islong = 1; fmt++; }
		else if (*fmt == 'h') { fmt++; if (*fmt == 'h') fmt++; }
		switch (*fmt) {
		case 's': {
			const char *s = va_arg(ap, const char *);
			if (!s) s = "(null)";
			int slen = strlen(s);
			if (prec >= 0 && slen > prec) slen = prec;
			int pad = width > slen ? width - slen : 0;
			if (!left) for (int j = 0; j < pad; j++) PUT(' ');
			for (int j = 0; j < slen; j++) PUT(s[j]);
			if (left) for (int j = 0; j < pad; j++) PUT(' ');
			break;
		}
		case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': {
			unsigned long v;
			int neg = 0, base = 10;
			if (*fmt == 'x' || *fmt == 'X') base = 16;
			else if (*fmt == 'o') base = 8;
			const char *digits = (*fmt == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
			if (*fmt == 'd' || *fmt == 'i') {
				long sv = islong ? va_arg(ap, long) : va_arg(ap, int);
				if (sv < 0) { neg = 1; v = -sv; } else v = sv;
			} else {
				v = islong ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
			}
			char tmp[20]; int i = 0;
			do { tmp[i++] = digits[v % base]; v /= base; } while (v);
			int len = i + neg;
			char pc = zero ? '0' : ' ';
			if (neg && zero) PUT('-');
			if (!left) while (len < width) { PUT(pc); len++; }
			if (neg && !zero) PUT('-');
			while (i--) PUT(tmp[i]);
			if (left) while (len < width) { PUT(' '); len++; }
			break;
		}
		case 'p':
			PUT('0'); PUT('x');
			{ unsigned long v = va_arg(ap, unsigned long);
			  char tmp[20]; int i = 0;
			  do { tmp[i++] = "0123456789abcdef"[v % 16]; v /= 16; } while (v);
			  while (i--) PUT(tmp[i]); }
			break;
		case 'c': PUT(va_arg(ap, int)); break;
		case '%': PUT('%'); break;
		case 'n': break;
		default: break;
		}
	}
#undef PUT
	if (sz > 0) buf[pos < (int)sz ? pos : (int)sz - 1] = 0;
	return pos;
}

int bb_vfprintf(FILE *f, const char *fmt, va_list ap) {
	return bb_vfprintf_fd(f ? fileno(f) : 2, fmt, ap);
}

int bb_printf(const char *fmt, ...) {
	va_list ap; va_start(ap, fmt);
	int r = bb_vfprintf_fd(1, fmt, ap);
	va_end(ap); return r;
}

int bb_fprintf(FILE *f, const char *fmt, ...) {
	va_list ap; va_start(ap, fmt);
	int r = bb_vfprintf_fd(f ? fileno(f) : 2, fmt, ap);
	va_end(ap); return r;
}

int bb_vprintf(const char *fmt, va_list ap) {
	return bb_vfprintf_fd(1, fmt, ap);
}

int bb_snprintf(char *buf, size_t sz, const char *fmt, ...) {
	va_list ap; va_start(ap, fmt);
	int r = bb_vsnprintf(buf, sz, fmt, ap);
	va_end(ap); return r;
}

int bb_sprintf(char *buf, const char *fmt, ...) {
	va_list ap; va_start(ap, fmt);
	int r = bb_vsnprintf(buf, (size_t)-1, fmt, ap);
	va_end(ap); return r;
}

int bb_dprintf(int fd, const char *fmt, ...) {
	va_list ap; va_start(ap, fmt);
	int r = bb_vfprintf_fd(fd, fmt, ap);
	va_end(ap); return r;
}
