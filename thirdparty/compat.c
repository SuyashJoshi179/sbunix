/* Real implementations needed by thirdparty programs but not part of the
   student libc.  Non-stdio symbols are weak so students can override them.
   Stdio symbols are strong: thirdparty links them ahead of libc.a and
   --allow-multiple-definition silently drops any student stdio. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fnmatch.h>
#include <poll.h>
#include <time.h>
#include <dirent.h>
#include <glob.h>
#include <termios.h>
#include <errno.h>

#define W __attribute__((weak))

struct _FILE { int fd; int flags; int eof; int err; int ungot; };
#define _F_READ  1
#define _F_WRITE 2

/* getopt */
W char *optarg;
W int optind = 1, opterr = 1, optopt;
W int getopt(int argc, char *const argv[], const char *optstring) {
	static int sp = 1;
	if (optind == 0) { optind = 1; sp = 1; }
	if (optind >= argc || !argv[optind] || argv[optind][0] != '-' || argv[optind][1] == 0)
		return -1;
	if (argv[optind][1] == '-' && argv[optind][2] == 0) { optind++; return -1; }
	int c = argv[optind][sp];
	const char *cp = strchr(optstring, c);
	if (!cp) { optopt = c; if (argv[optind][++sp] == 0) { optind++; sp = 1; } return '?'; }
	if (cp[1] == ':') {
		if (argv[optind][sp + 1]) { optarg = &argv[optind][sp + 1]; }
		else if (++optind >= argc) { optopt = c; sp = 1; return '?'; }
		else optarg = argv[optind];
		optind++; sp = 1;
	} else {
		if (argv[optind][++sp] == 0) { optind++; sp = 1; }
	}
	return c;
}

/* ctype */
W int isdigit(int c) { return c >= '0' && c <= '9'; }
W int isalpha(int c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
W int isalnum(int c) { return isalpha(c) || isdigit(c); }
W int isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
W int isupper(int c) { return c >= 'A' && c <= 'Z'; }
W int islower(int c) { return c >= 'a' && c <= 'z'; }
W int isprint(int c) { return c >= 0x20 && c <= 0x7e; }
W int isgraph(int c) { return c > 0x20 && c <= 0x7e; }
W int iscntrl(int c) { return (c >= 0 && c < 0x20) || c == 0x7f; }
W int ispunct(int c) { return isgraph(c) && !isalnum(c); }
W int isxdigit(int c) { return isdigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }
W int isascii(int c) { return (unsigned)c <= 0x7f; }
W int isblank(int c) { return c == ' ' || c == '\t'; }
W int toupper(int c) { return islower(c) ? c - 32 : c; }
W int tolower(int c) { return isupper(c) ? c + 32 : c; }
W int toascii(int c) { return c & 0x7f; }

/* string */
static inline int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
W int strcasecmp(const char *a, const char *b) {
	while (*a && lc(*(unsigned char *)a) == lc(*(unsigned char *)b)) { a++; b++; }
	return lc(*(unsigned char *)a) - lc(*(unsigned char *)b);
}
W int strncasecmp(const char *a, const char *b, size_t n) {
	while (n && *a && lc(*(unsigned char *)a) == lc(*(unsigned char *)b)) { a++; b++; n--; }
	return n ? lc(*(unsigned char *)a) - lc(*(unsigned char *)b) : 0;
}
W char *strndup(const char *s, size_t n) {
	size_t len = strnlen(s, n);
	char *d = malloc(len + 1);
	if (d) { memcpy(d, s, len); d[len] = 0; }
	return d;
}
W char *strpbrk(const char *s, const char *accept) {
	for (; *s; s++) if (strchr(accept, *s)) return (char *)s;
	return NULL;
}
W size_t strcspn(const char *s, const char *reject) {
	size_t n = 0;
	while (s[n] && !strchr(reject, s[n])) n++;
	return n;
}
W char *strtok_r(char *s, const char *delim, char **saveptr) {
	if (s) *saveptr = s;
	if (!*saveptr) return NULL;
	char *p = *saveptr;
	while (*p && strchr(delim, *p)) p++;
	if (!*p) { *saveptr = NULL; return NULL; }
	char *start = p;
	while (*p && !strchr(delim, *p)) p++;
	if (*p) *p++ = 0;
	*saveptr = p;
	return start;
}
W char *strsep(char **sp, const char *delim) {
	if (!*sp) return NULL;
	char *start = *sp, *p = start;
	while (*p && !strchr(delim, *p)) p++;
	if (*p) *p++ = 0; else p = NULL;
	*sp = p;
	return start;
}
W char *stpcpy(char *dst, const char *src) {
	while ((*dst = *src)) { dst++; src++; }
	return dst;
}
W char *stpncpy(char *dst, const char *src, size_t n) {
	while (n && *src) { *dst++ = *src++; n--; }
	char *end = dst;
	while (n--) *dst++ = 0;
	return end;
}
W char *dirname(char *path) {
	static char dot[] = ".";
	if (!path || !*path) return dot;
	char *p = path + strlen(path) - 1;
	while (p > path && *p == '/') *p-- = 0;
	p = strrchr(path, '/');
	if (!p) return dot;
	if (p == path) return "/";
	*p = 0;
	return path;
}
W char *basename(char *path) {
	if (!path || !*path) return ".";
	char *p = path + strlen(path) - 1;
	while (p > path && *p == '/') *p-- = 0;
	p = strrchr(path, '/');
	return p ? p + 1 : path;
}
W char *strerror(int errnum) {
	static const char *errnames[] = {
		[0] = "Success",
		[EPERM] = "Operation not permitted",
		[ENOENT] = "No such file or directory",
		[ESRCH] = "No such process",
		[EINTR] = "Interrupted system call",
		[EIO] = "I/O error",
		[ENOMEM] = "Out of memory",
		[EACCES] = "Permission denied",
		[EEXIST] = "File exists",
		[ENOTDIR] = "Not a directory",
		[EISDIR] = "Is a directory",
		[EINVAL] = "Invalid argument",
		[EBADF] = "Bad file descriptor",
		[ENOSYS] = "Function not implemented",
		[ERANGE] = "Result too large",
		[ENAMETOOLONG] = "File name too long",
		[ENOTEMPTY] = "Directory not empty",
		[ELOOP] = "Too many levels of symbolic links",
	};
	if (errnum >= 0 && errnum < (int)(sizeof(errnames)/sizeof(errnames[0])) && errnames[errnum])
		return (char *)errnames[errnum];
	return "Unknown error";
}

/* stdlib */
W unsigned long strtoul(const char *s, char **endp, int base) {
	while (*s == ' ' || (*s >= '\t' && *s <= '\r')) s++;
	int neg = 0;
	if (*s == '-') { neg = 1; s++; }
	else if (*s == '+') s++;
	if (base == 0) {
		if (*s == '0') { s++; if (*s == 'x' || *s == 'X') { base = 16; s++; } else base = 8; }
		else base = 10;
	} else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
	unsigned long v = 0;
	while (*s) {
		int d;
		if (*s >= '0' && *s <= '9') d = *s - '0';
		else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
		else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
		else break;
		if (d >= base) break;
		v = v * base + d;
		s++;
	}
	if (endp) *endp = (char *)s;
	return neg ? -v : v;
}
W long strtol(const char *s, char **endp, int base) { return (long)strtoul(s, endp, base); }
W unsigned long long strtoull(const char *s, char **endp, int base) { return (unsigned long long)strtoul(s, endp, base); }
W int abs(int x) { return x < 0 ? -x : x; }
W long labs(long x) { return x < 0 ? -x : x; }
W long atol(const char *s) { return strtol(s, NULL, 10); }
W double strtod(const char *s, char **endp) {
	while (*s == ' ' || (*s >= '\t' && *s <= '\r')) s++;
	int neg = 0;
	if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
	unsigned long long iv = 0;
	while (*s >= '0' && *s <= '9') iv = iv * 10 + (*s++ - '0');
	unsigned long long fv = 0;
	long long fdiv = 1;
	if (*s == '.') {
		s++;
		while (*s >= '0' && *s <= '9') { fv = fv * 10 + (*s++ - '0'); fdiv *= 10; }
	}
	double r = (double)iv + (double)fv / (double)fdiv;
	if (*s == 'e' || *s == 'E') {
		s++;
		int eneg = 0, ev = 0;
		if (*s == '-') { eneg = 1; s++; } else if (*s == '+') s++;
		while (*s >= '0' && *s <= '9') ev = ev * 10 + (*s++ - '0');
		double mul = 1.0;
		for (int i = 0; i < ev; i++) mul *= 10.0;
		r = eneg ? r / mul : r * mul;
	}
	if (endp) *endp = (char *)s;
	return neg ? -r : r;
}
W float strtof(const char *s, char **endp) { return (float)strtod(s, endp); }
W long double strtold(const char *s, char **endp) { return (long double)strtod(s, endp); }
W int rename(const char *o, const char *n) { (void)o; (void)n; return -1; }
W char *realpath(const char *path, char *resolved) {
	if (!resolved) resolved = malloc(4096);
	if (!resolved) return NULL;
	if (path[0] == '/') { strcpy(resolved, path); return resolved; }
	if (!getcwd(resolved, 4096)) return NULL;
	strcat(resolved, "/");
	strcat(resolved, path);
	return resolved;
}

/* qsort */
static void swap(char *a, char *b, size_t size) {
	char tmp;
	while (size--) { tmp = *a; *a++ = *b; *b++ = tmp; }
}
W void qsort(void *base, size_t nmemb, size_t size, int (*cmp)(const void *, const void *)) {
	if (nmemb < 2) return;
	char *b = base;
	char *pivot = b + (nmemb - 1) * size;
	size_t i = 0;
	for (size_t j = 0; j < nmemb - 1; j++)
		if (cmp(b + j * size, pivot) <= 0) { swap(b + i * size, b + j * size, size); i++; }
	swap(b + i * size, pivot, size);
	qsort(b, i, size, cmp);
	qsort(b + (i + 1) * size, nmemb - i - 1, size, cmp);
}

/* bsearch */
W void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
                int (*cmp)(const void *, const void *)) {
	while (nmemb > 0) {
		size_t mid = nmemb / 2;
		const void *p = (const char *)base + mid * size;
		int r = cmp(key, p);
		if (r == 0) return (void *)p;
		if (r > 0) { base = (const char *)p + size; nmemb -= mid + 1; }
		else nmemb = mid;
	}
	return NULL;
}

/* stdio - FILE* */
static FILE _stdin_  = { .fd = 0, .flags = _F_READ };
static FILE _stdout_ = { .fd = 1, .flags = _F_WRITE };
static FILE _stderr_ = { .fd = 2, .flags = _F_WRITE };
FILE *stdin  = &_stdin_;
FILE *stdout = &_stdout_;
FILE *stderr = &_stderr_;
static FILE ftable[16];
FILE *fopen(const char *path, const char *mode) {
	int flags = O_RDONLY, fflags = _F_READ;
	if (mode[0] == 'w') { flags = O_WRONLY | O_CREAT | O_TRUNC; fflags = _F_WRITE; }
	else if (mode[0] == 'a') { flags = O_WRONLY | O_CREAT | O_APPEND; fflags = _F_WRITE; }
	if (mode[1] == '+' || (mode[1] && mode[2] == '+')) fflags = _F_READ | _F_WRITE;
	int fd = open(path, flags);
	if (fd < 0) return NULL;
	for (int i = 0; i < 16; i++) {
		if (!ftable[i].flags) {
			ftable[i].fd = fd; ftable[i].flags = fflags;
			ftable[i].eof = 0; ftable[i].err = 0; ftable[i].ungot = -1;
			return &ftable[i];
		}
	}
	close(fd);
	return NULL;
}
int fclose(FILE *f) { int r = close(f->fd); f->flags = 0; return r; }
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f) {
	size_t total = size * nmemb, got = 0;
	char *p = ptr;
	while (got < total) {
		ssize_t r = read(f->fd, p + got, total - got);
		if (r <= 0) { if (r == 0) f->eof = 1; else f->err = 1; break; }
		got += r;
	}
	return got / size;
}
int fputc(int c, FILE *f) { char ch = c; if (write(f->fd, &ch, 1) != 1) return EOF; return (unsigned char)c; }
int fputs(const char *s, FILE *f) { size_t len = strlen(s); return write(f->fd, s, len) == (ssize_t)len ? 0 : EOF; }
int fgetc(FILE *f) {
	if (f->ungot >= 0) { int c = f->ungot; f->ungot = -1; return c; }
	unsigned char c;
	ssize_t r = read(f->fd, &c, 1);
	if (r <= 0) { if (r == 0) f->eof = 1; return EOF; }
	return c;
}
char *fgets(char *s, int n, FILE *f) {
	int i = 0;
	while (i < n - 1) {
		int c = fgetc(f);
		if (c == EOF) { if (i == 0) return NULL; break; }
		s[i++] = c;
		if (c == '\n') break;
	}
	s[i] = 0;
	return s;
}
int ferror(FILE *f) { return f->err; }
void clearerr(FILE *f) { f->eof = f->err = 0; }

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f) {
	size_t total = size * nmemb, done = 0;
	const char *p = ptr;
	while (done < total) {
		ssize_t r = write(f->fd, p + done, total - done);
		if (r <= 0) { f->err = 1; break; }
		done += r;
	}
	return done / size;
}
int fseek(FILE *f, long offset, int whence) {
	f->ungot = -1; f->eof = 0;
	return lseek(f->fd, offset, whence) < 0 ? -1 : 0;
}
long ftell(FILE *f) { return (long)lseek(f->fd, 0, SEEK_CUR); }
int feof(FILE *f) { return f->eof; }
W int remove(const char *path) { return unlink(path); }
int setvbuf(FILE *f, char *buf, int mode, size_t size) {
	(void)f; (void)buf; (void)mode; (void)size; return 0;
}
void perror(const char *s) {
	if (s && *s) { write(2, s, strlen(s)); write(2, ": ", 2); }
	const char *m = strerror(errno);
	write(2, m, strlen(m)); write(2, "\n", 1);
}

/* stdio - vsnprintf engine */
struct strbuf { char *buf; int pos; int max; };
static void sbuf_putc(struct strbuf *sb, char c) {
	if (sb->pos < sb->max - 1) sb->buf[sb->pos] = c;
	sb->pos++;
}
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
	struct strbuf sb = { buf, 0, (int)size };
	for (; *fmt; fmt++) {
		if (*fmt != '%') { sbuf_putc(&sb, *fmt); continue; }
		fmt++;
		int width = 0, zeropad = 0, ljust = 0, islong = 0;
		if (*fmt == '-') { ljust = 1; fmt++; }
		if (*fmt == '0') { zeropad = 1; fmt++; }
		if (*fmt == '*') { width = va_arg(ap, int); if (width < 0) { ljust = 1; width = -width; } fmt++; }
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
			if (!ljust) for (int j = slen; j < width; j++) sbuf_putc(&sb, ' ');
			for (int j = 0; j < slen; j++) sbuf_putc(&sb, s[j]);
			if (ljust) for (int j = slen; j < width; j++) sbuf_putc(&sb, ' ');
			break;
		}
		case 'd': case 'i': {
			long v = islong ? va_arg(ap, long) : va_arg(ap, int);
			char tmp[20]; int i = 0, neg = 0;
			unsigned long uv;
			if (v < 0) { neg = 1; uv = -v; } else uv = v;
			do { tmp[i++] = '0' + uv % 10; uv /= 10; } while (uv);
			int numlen = i + neg;
			if (!zeropad) for (int j = numlen; j < width; j++) sbuf_putc(&sb, ' ');
			if (neg) sbuf_putc(&sb, '-');
			if (zeropad) for (int j = numlen; j < width; j++) sbuf_putc(&sb, '0');
			while (i--) sbuf_putc(&sb, tmp[i]);
			break;
		}
		case 'u': case 'x': case 'X': case 'o': {
			unsigned long v = islong ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
			int base = (*fmt == 'x' || *fmt == 'X') ? 16 : (*fmt == 'o') ? 8 : 10;
			const char *digits = (*fmt == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
			char tmp[20]; int i = 0;
			do { tmp[i++] = digits[v % base]; v /= base; } while (v);
			if (!zeropad) for (int j = i; j < width; j++) sbuf_putc(&sb, ' ');
			if (zeropad) for (int j = i; j < width; j++) sbuf_putc(&sb, '0');
			while (i--) sbuf_putc(&sb, tmp[i]);
			break;
		}
		case 'p':
			sbuf_putc(&sb, '0'); sbuf_putc(&sb, 'x');
			{ unsigned long v = va_arg(ap, unsigned long);
			char tmp[16]; int i = 0;
			do { tmp[i++] = "0123456789abcdef"[v % 16]; v /= 16; } while (v);
			while (i--) sbuf_putc(&sb, tmp[i]); }
			break;
		case 'c': {
			char ch = va_arg(ap, int);
			if (!ljust) for (int j = 1; j < width; j++) sbuf_putc(&sb, ' ');
			sbuf_putc(&sb, ch);
			if (ljust) for (int j = 1; j < width; j++) sbuf_putc(&sb, ' ');
			break;
		}
		case '%': sbuf_putc(&sb, '%'); break;
		default: sbuf_putc(&sb, *fmt); break;
		}
	}
	if (size > 0) buf[sb.pos < (int)size ? sb.pos : (int)size - 1] = 0;
	return sb.pos;
}
int snprintf(char *buf, size_t size, const char *fmt, ...) {
	va_list ap; va_start(ap, fmt); int r = vsnprintf(buf, size, fmt, ap); va_end(ap); return r;
}
int sprintf(char *buf, const char *fmt, ...) {
	va_list ap; va_start(ap, fmt); int r = vsnprintf(buf, (size_t)-1, fmt, ap); va_end(ap); return r;
}
int vfprintf(FILE *f, const char *fmt, va_list ap) {
	char buf[1024]; int n = vsnprintf(buf, sizeof(buf), fmt, ap); write(f->fd, buf, n < (int)sizeof(buf) ? n : (int)sizeof(buf)); return n;
}
int fprintf(FILE *f, const char *fmt, ...) {
	va_list ap; va_start(ap, fmt); int r = vfprintf(f, fmt, ap); va_end(ap); return r;
}
int vprintf(const char *fmt, va_list ap) { return vfprintf(stdout, fmt, ap); }
int dprintf(int fd, const char *fmt, ...) {
	FILE f = { .fd = fd, .flags = _F_WRITE };
	va_list ap; va_start(ap, fmt); int r = vfprintf(&f, fmt, ap); va_end(ap); return r;
}
int puts(const char *s) { int n = strlen(s); write(1, s, n); write(1, "\n", 1); return n + 1; }
int putchar(int c) { char ch = c; write(1, &ch, 1); return c; }

/* signal */
W sighandler_t signal(int sig, sighandler_t handler) {
	struct sigaction act = {0}, oldact;
	act.sa_handler = handler;
	act.sa_flags = SA_RESTART;
	if (sigaction(sig, &act, &oldact) < 0) return SIG_ERR;
	return oldact.sa_handler;
}
/* sigemptyset/sigfillset/sigaddset weak fallbacks live in sigset_fallback.c
 * (separate TU, no <signal.h>) so they don't conflict with libcs that define
 * these as `static inline` in their signal.h header. */
W int raise(int sig) { return kill(getpid(), sig); }

/* time */
static const int mdays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
static struct tm tm_buf;
static int is_leap(int y) { return !(y % 4) && ((y % 100) || !(y % 400)); }
W struct tm *gmtime_r(const time_t *t, struct tm *r) {
	*r = (struct tm){0};
	long s = *t;
	int days = s / 86400; s %= 86400;
	if (s < 0) { s += 86400; days--; }
	r->tm_hour = s / 3600; s %= 3600;
	r->tm_min = s / 60; r->tm_sec = s % 60;
	r->tm_wday = (days + 4) % 7;
	if (r->tm_wday < 0) r->tm_wday += 7;
	int y = 1970;
	while (days >= 365 + is_leap(y)) { days -= 365 + is_leap(y); y++; }
	while (days < 0) { y--; days += 365 + is_leap(y); }
	r->tm_year = y - 1900; r->tm_yday = days;
	for (int m = 0; m < 12; m++) {
		int d = mdays[m] + (m == 1 && is_leap(y));
		if (days < d) { r->tm_mon = m; r->tm_mday = days + 1; break; }
		days -= d;
	}
	return r;
}
W struct tm *localtime(const time_t *t) { return gmtime_r(t, &tm_buf); }
static void fmtpad(char **p, char *end, int val, int w) {
	char tmp[16]; int i = 0;
	do { tmp[i++] = '0' + val % 10; val /= 10; } while (val);
	while (i < w) tmp[i++] = '0';
	while (i-- && *p < end) *(*p)++ = tmp[i];
}
W size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm) {
	char *p = s, *end = s + max - 1;
	static const char *wdays[] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };
	static const char *mons[] = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
	for (; *fmt && p < end; fmt++) {
		if (*fmt != '%') { *p++ = *fmt; continue; }
		fmt++;
		switch (*fmt) {
		case 'Y': fmtpad(&p, end, tm->tm_year + 1900, 4); break;
		case 'm': fmtpad(&p, end, tm->tm_mon + 1, 2); break;
		case 'd': fmtpad(&p, end, tm->tm_mday, 2); break;
		case 'H': fmtpad(&p, end, tm->tm_hour, 2); break;
		case 'M': fmtpad(&p, end, tm->tm_min, 2); break;
		case 'S': fmtpad(&p, end, tm->tm_sec, 2); break;
		case 'a': { const char *w = wdays[tm->tm_wday]; while (*w && p < end) *p++ = *w++; break; }
		case 'b': case 'h': { const char *m = mons[tm->tm_mon]; while (*m && p < end) *p++ = *m++; break; }
		case 'e': if (tm->tm_mday < 10) { *p++ = ' '; if (p<end) *p++ = '0'+tm->tm_mday; } else fmtpad(&p, end, tm->tm_mday, 2); break;
		case 'j': fmtpad(&p, end, tm->tm_yday + 1, 3); break;
		case 'Z': { const char *z = "UTC"; while (*z && p < end) *p++ = *z++; break; }
		case 'n': *p++ = '\n'; break;
		case 't': *p++ = '\t'; break;
		case '%': *p++ = '%'; break;
		default: *p++ = '%'; if (p < end) *p++ = *fmt; break;
		}
	}
	*p = 0;
	return p - s;
}

/* unistd */
W int execvp(const char *file, char *const argv[]) {
	if (strchr(file, '/')) return execve(file, argv, environ);
	const char *paths[] = { "/bin/", NULL };
	char buf[128];
	for (int i = 0; paths[i]; i++) {
		strcpy(buf, paths[i]);
		strcat(buf, file);
		execve(buf, argv, environ);
	}
	return -1;
}
W char *getenv(const char *name) {
	if (!environ) return NULL;
	size_t len = strlen(name);
	for (char **e = environ; *e; e++)
		if (strncmp(*e, name, len) == 0 && (*e)[len] == '=')
			return *e + len + 1;
	return NULL;
}
W pid_t vfork(void) { return fork(); }

/* stat stubs */
W int fstat(int fd, struct stat *st) { (void)fd; (void)st; return -1; }
W int chmod(const char *path, mode_t mode) { (void)path; (void)mode; return -1; }
W mode_t umask(mode_t mask) { (void)mask; return 022; }

/* fnmatch */
static int fnm(const char *p, const char *s, int flags) {
	for (;;) {
		if (!*p) return *s ? FNM_NOMATCH : 0;
		if (*p == '*') {
			p++;
			while (*p == '*') p++;
			if (!*p) return 0;
			for (; *s; s++) {
				if ((flags & FNM_PATHNAME) && *s == '/') return FNM_NOMATCH;
				if (!fnm(p, s, flags)) return 0;
			}
			return fnm(p, s, flags);
		}
		if (!*s) return FNM_NOMATCH;
		if (*p == '?') {
			if ((flags & FNM_PATHNAME) && *s == '/') return FNM_NOMATCH;
		} else if (*p == '[') {
			int inv = 0, match = 0;
			p++;
			if (*p == '!' || *p == '^') { inv = 1; p++; }
			for (; *p && *p != ']'; p++) {
				if (p[1] == '-' && p[2] && p[2] != ']') {
					if (*s >= *p && *s <= p[2]) match = 1;
					p += 2;
				} else if (*s == *p) match = 1;
			}
			if (*p == ']') p++;
			if (match == inv) return FNM_NOMATCH;
			s++;
			continue;
		} else if (*p != *s) return FNM_NOMATCH;
		p++; s++;
	}
}
W int fnmatch(const char *pattern, const char *string, int flags) {
	return fnm(pattern, string, flags);
}

/* poll */
W int poll(struct pollfd *fds, nfds_t nfds, int timeout) {
	if (timeout > 0) {
		struct timespec ts = { timeout / 1000, (timeout % 1000) * 1000000L };
		nanosleep(&ts, NULL);
	}
	for (nfds_t i = 0; i < nfds; i++)
		fds[i].revents = fds[i].events;
	return nfds;
}
W int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *tmo, const sigset_t *sigmask) {
	(void)sigmask;
	int timeout = tmo ? (tmo->tv_sec * 1000 + tmo->tv_nsec / 1000000) : -1;
	return poll(fds, nfds, timeout);
}

/* fdopen */
FILE *fdopen(int fd, const char *mode) {
	static FILE fdt[8];
	int fflags = (mode[0] == 'r') ? _F_READ : _F_WRITE;
	for (int i = 0; i < 8; i++) {
		if (!fdt[i].flags) {
			fdt[i].fd = fd; fdt[i].flags = fflags;
			fdt[i].eof = 0; fdt[i].err = 0; fdt[i].ungot = -1;
			return &fdt[i];
		}
	}
	return NULL;
}
int fileno(FILE *f) { return f->fd; }

/* popen/pclose */
static pid_t popen_pid;
FILE *popen(const char *cmd, const char *mode) {
	int pfd[2];
	if (pipe(pfd) < 0) return NULL;
	pid_t pid = fork();
	if (pid < 0) { close(pfd[0]); close(pfd[1]); return NULL; }
	if (pid == 0) {
		if (mode[0] == 'r') { close(pfd[0]); dup2(pfd[1], 1); close(pfd[1]); }
		else { close(pfd[1]); dup2(pfd[0], 0); close(pfd[0]); }
		const char *argv[] = { "/bin/sh", "-c", cmd, NULL };
		execve("/bin/sh", (char *const *)argv, environ);
		_exit(127);
	}
	popen_pid = pid;
	if (mode[0] == 'r') { close(pfd[1]); return fdopen(pfd[0], "r"); }
	else { close(pfd[0]); return fdopen(pfd[1], "w"); }
}
int pclose(FILE *f) {
	fclose(f);
	int status;
	while (waitpid(popen_pid, &status, 0) < 0)
		if (errno != EINTR) return -1;
	return status;
}

/* system */
W int system(const char *cmd) {
	if (!cmd) return 1;
	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) {
		const char *argv[] = { "/bin/sh", "-c", cmd, NULL };
		execve("/bin/sh", (char *const *)argv, environ);
		_exit(127);
	}
	int status;
	while (waitpid(pid, &status, 0) < 0)
		if (errno != EINTR) return -1;
	return status;
}

/* confstr */
W size_t confstr(int name, char *buf, size_t len) {
	(void)name;
	const char *val = "/bin";
	size_t n = strlen(val) + 1;
	if (buf && len) { strncpy(buf, val, len); buf[len - 1] = 0; }
	return n;
}

/* clock_gettime */
W int clock_gettime(int clk_id, struct timespec *tp) {
	(void)clk_id;
	tp->tv_sec = time(NULL);
	tp->tv_nsec = 0;
	return 0;
}

/* utimensat */
W int utimensat(int dirfd, const char *path, const struct timespec times[2], int flags) {
	(void)dirfd; (void)path; (void)times; (void)flags;
	errno = ENOSYS;
	return -1;
}

/* glob */
W int glob(const char *pattern, int flags,
           int (*errfunc)(const char *, int), glob_t *pglob) {
	(void)flags; (void)errfunc;
	pglob->gl_pathc = 0;
	pglob->gl_pathv = NULL;
	/* split pattern into directory and basename */
	char patbuf[256];
	strncpy(patbuf, pattern, sizeof(patbuf) - 1);
	patbuf[sizeof(patbuf) - 1] = 0;
	char *slash = strrchr(patbuf, '/');
	const char *dir, *base;
	if (slash) { *slash = 0; dir = patbuf; base = slash + 1; }
	else { dir = "."; base = patbuf; }
	DIR *d = opendir(dir);
	if (!d) return GLOB_NOMATCH;
	size_t cap = 0;
	struct dirent *de;
	while ((de = readdir(d)) != NULL) {
		if (fnmatch(base, de->d_name, 0) != 0) continue;
		if (pglob->gl_pathc >= cap) {
			cap = cap ? cap * 2 : 8;
			pglob->gl_pathv = realloc(pglob->gl_pathv, (cap + 1) * sizeof(char *));
		}
		size_t dlen = strlen(dir), nlen = strlen(de->d_name);
		char *p;
		if (slash) {
			p = malloc(dlen + 1 + nlen + 1);
			memcpy(p, dir, dlen); p[dlen] = '/';
			memcpy(p + dlen + 1, de->d_name, nlen + 1);
		} else {
			p = malloc(nlen + 1);
			memcpy(p, de->d_name, nlen + 1);
		}
		pglob->gl_pathv[pglob->gl_pathc++] = p;
	}
	closedir(d);
	if (pglob->gl_pathc == 0) { free(pglob->gl_pathv); pglob->gl_pathv = NULL; return GLOB_NOMATCH; }
	pglob->gl_pathv[pglob->gl_pathc] = NULL;
	return 0;
}
W void globfree(glob_t *pglob) {
	for (size_t i = 0; i < pglob->gl_pathc; i++) free(pglob->gl_pathv[i]);
	free(pglob->gl_pathv);
	pglob->gl_pathv = NULL;
	pglob->gl_pathc = 0;
}

/* termios stubs */
W int tcgetattr(int fd, struct termios *t) { (void)fd; memset(t, 0, sizeof(*t)); return 0; }
W int tcsetattr(int fd, int actions, const struct termios *t) { (void)fd; (void)actions; (void)t; return 0; }
