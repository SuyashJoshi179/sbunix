/* Weak stubs for functions required at compile/link time but eliminated by
   --gc-sections from all final binaries.  If a real implementation is later
   added to libc/, the strong symbol wins automatically. */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/times.h>
#include <time.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <grp.h>

#define W __attribute__((weak))

/* net */
W int socket(int d, int t, int p) { (void)d; (void)t; (void)p; errno = ENOSYS; return -1; }
W int bind(int fd, const struct sockaddr *a, socklen_t l) { (void)fd; (void)a; (void)l; errno = ENOSYS; return -1; }
W int listen(int fd, int b) { (void)fd; (void)b; errno = ENOSYS; return -1; }
W ssize_t sendto(int fd, const void *b, size_t n, int f, const struct sockaddr *a, socklen_t l) {
	(void)fd; (void)b; (void)n; (void)f; (void)a; (void)l; errno = ENOSYS; return -1;
}
W int getsockname(int fd, struct sockaddr *a, socklen_t *l) { (void)fd; (void)a; (void)l; return -1; }

/* unistd */
W int chroot(const char *p) { (void)p; return -1; }
W int execv(const char *path, char *const argv[]) { (void)path; (void)argv; errno = ENOSYS; return -1; }
W int fchdir(int fd) { (void)fd; return -1; }
W int chown(const char *p, uid_t o, gid_t g) { (void)p; (void)o; (void)g; return -1; }
W int lchown(const char *p, uid_t o, gid_t g) { (void)p; (void)o; (void)g; return -1; }
W int link(const char *o, const char *n) { (void)o; (void)n; return -1; }
W int symlink(const char *t, const char *l) { (void)t; (void)l; return -1; }
W int fsync(int fd) { (void)fd; return 0; }
W int mknod(const char *p, mode_t m, dev_t d) { (void)p; (void)m; (void)d; return -1; }
W int getgroups(int size, gid_t list[]) { (void)size; (void)list; return 0; }
W int getpagesize(void) { return 4096; }
W int setuid(uid_t u) { (void)u; return 0; }
W int setgid(gid_t g) { (void)g; return 0; }
W int seteuid(uid_t u) { (void)u; return 0; }
W int setegid(gid_t g) { (void)g; return 0; }
#ifndef HAVE_USLEEP
W int usleep(useconds_t usec) { (void)usec; return 0; }
#endif

/* stdio */
W void rewind(FILE *f) { (void)f; }
W int fflush(FILE *f) { (void)f; return 0; }
W int sscanf(const char *s, const char *fmt, ...) { (void)s; (void)fmt; return 0; }

/* time */
W struct tm *localtime_r(const time_t *t, struct tm *r) { (void)t; (void)r; return NULL; }
W time_t mktime(struct tm *tm) { (void)tm; return -1; }
#ifndef HAVE_SETTIMEOFDAY
W int settimeofday(const struct timeval *tv, const struct timezone *tz) { (void)tv; (void)tz; return 0; }
#endif
W int utimes(const char *p, const struct timeval tv[2]) { (void)p; (void)tv; return 0; }
W clock_t times(struct tms *buf) { (void)buf; return 0; }

/* stdlib */
W int mkstemp(char *t) { (void)t; return -1; }
W void srand(unsigned int seed) { (void)seed; }
W int rand(void) { return 0; }
W long long strtoll(const char *s, char **endp, int base) { (void)s; (void)endp; (void)base; return 0; }

/* string */
W int strcoll(const char *a, const char *b) {
	while (*a && *a == *b) { a++; b++; }
	return *(const unsigned char *)a - *(const unsigned char *)b;
}
W size_t strspn(const char *s, const char *accept) { (void)s; (void)accept; return 0; }

/* signal */
W int sigsuspend(const sigset_t *mask) { (void)mask; return -1; }

/* misc */
W int setgroups(size_t size, const gid_t *list) { (void)size; (void)list; return 0; }

