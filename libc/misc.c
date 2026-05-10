#include <unistd.h>
#include <sys/utsname.h>
#include <string.h>
#include <errno.h>
#include <time.h>

/* Stubs for the long tail of POSIX odds and ends. SBUnix doesnt have
 * the kernel support for most of these — return success or a sane
 * default rather than ENOSYS so portable code compiles and runs even
 * when it calls them defensively. */

static char hostname[65] = "sbunix";

int uname(struct utsname *u) {
    if (!u) { errno = EFAULT; return -1; }
    memset(u, 0, sizeof(*u));
    strcpy(u->sysname,  "SBUnix");
    strcpy(u->nodename, hostname);
    strcpy(u->release,  "0.1");
    strcpy(u->version,  "CSE506 teaching OS");
    strcpy(u->machine,  "riscv64");
    return 0;
}

int gethostname(char *buf, size_t len) {
    if (!buf || len == 0) { errno = EINVAL; return -1; }
    size_t hl = strlen(hostname);
    if (hl >= len) { errno = ENAMETOOLONG; return -1; }
    memcpy(buf, hostname, hl);
    buf[hl] = '\0';
    return 0;
}

int sethostname(const char *name, size_t len) {
    if (!name || len == 0 || len >= sizeof(hostname)) { errno = EINVAL; return -1; }
    memcpy(hostname, name, len);
    hostname[len] = '\0';
    return 0;
}

int getdomainname(char *buf, size_t len) {
    if (!buf || len == 0) { errno = EINVAL; return -1; }
    buf[0] = '\0';
    return 0;
}

void sync(void)                    { /* sbfs commits at end_op; nothing to do */ }
int  fsync(int fd)                 { (void)fd; return 0; }
int  fdatasync(int fd)             { (void)fd; return 0; }

/* alarm() implementation is in libc/syscall.c (real syscall wrapper). */

unsigned sleep(unsigned secs) {
    struct timespec req = { (int64_t)secs, 0 }, rem = { 0, 0 };
    if (nanosleep(&req, &rem) == 0) return 0;
    return (unsigned)rem.tv_sec;
}

/* truncate() / ftruncate() implementations live in libc/syscall.c. */
int chown(const char *p, uid_t u, gid_t g)    { (void)p; (void)u; (void)g; return 0; }
int fchown(int fd, uid_t u, gid_t g)          { (void)fd; (void)u; (void)g; return 0; }
int lchown(const char *p, uid_t u, gid_t g)   { (void)p; (void)u; (void)g; return 0; }
/* link() / symlink() are real syscalls — see libc/syscall.c. */
int rmdir(const char *p)                      { return unlink(p); }

char *ttyname(int fd) {
    static char buf[16];
    if (!isatty(fd)) { errno = ENOTTY; return 0; }
    strcpy(buf, "/dev/console");
    return buf;
}
int ttyname_r(int fd, char *buf, size_t len) {
    if (!isatty(fd)) return ENOTTY;
    const char *n = "/dev/console";
    size_t nl = strlen(n);
    if (nl + 1 > len) return ERANGE;
    memcpy(buf, n, nl + 1);
    return 0;
}

long pathconf(const char *path, int name)  { (void)path; (void)name; return -1; }
long fpathconf(int fd, int name)           { (void)fd;   (void)name; return -1; }

long sysconf(int name) {
    switch (name) {
    case _SC_PAGESIZE:         return 4096;
    case _SC_OPEN_MAX:         return 64;
    case _SC_NPROCESSORS_ONLN: return 1;
    case _SC_NPROCESSORS_CONF: return 1;
    case _SC_CLK_TCK:          return 100;
    default:                   errno = EINVAL; return -1;
    }
}

