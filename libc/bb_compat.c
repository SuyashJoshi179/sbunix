#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>

/* Compatibility shims for BusyBox and other ports that rely on glibc
 * extensions or POSIX features SBUnix has not yet implemented. */

/* Locked variants are no-ops here — SBUnix is single-threaded per process,
 * stdio has no internal locking, so unlocked = locked. */
int fputs_unlocked(const char *s, FILE *stream)  { return fputs(s, stream); }
int putc_unlocked(int c, FILE *stream)           { return putc(c, stream); }
int getc_unlocked(FILE *stream)                  { return getc(stream); }
int fgetc_unlocked(FILE *stream)                 { return fgetc(stream); }
int fputc_unlocked(int c, FILE *stream)          { return fputc(c, stream); }
int feof_unlocked(FILE *stream)                  { return feof(stream); }
int ferror_unlocked(FILE *stream)                { return ferror(stream); }
int fileno_unlocked(FILE *stream)                { return fileno(stream); }

/* dprintf: format directly to a file descriptor without going through FILE. */
int vdprintf(int fd, const char *fmt, va_list ap) {
    char *buf = 0;
    int n = vasprintf(&buf, fmt, ap);
    if (n < 0) return -1;
    int written = (int)write(fd, buf, n);
    free(buf);
    return written;
}

int dprintf(int fd, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vdprintf(fd, fmt, ap);
    va_end(ap);
    return r;
}

/* strsignal: human-readable signal name. Table covers signals defined in
 * signal.h; unknown signals return a generic string in a static buffer. */
static const char *const _sig_names[] = {
    [SIGHUP]    = "Hangup",
    [SIGINT]    = "Interrupt",
    [SIGQUIT]   = "Quit",
    [SIGILL]    = "Illegal instruction",
    [SIGTRAP]   = "Trace/breakpoint trap",
    [SIGABRT]   = "Aborted",
    [SIGBUS]    = "Bus error",
    [SIGFPE]    = "Floating point exception",
    [SIGKILL]   = "Killed",
    [SIGUSR1]   = "User defined signal 1",
    [SIGSEGV]   = "Segmentation fault",
    [SIGUSR2]   = "User defined signal 2",
    [SIGPIPE]   = "Broken pipe",
    [SIGALRM]   = "Alarm clock",
    [SIGTERM]   = "Terminated",
    [SIGCHLD]   = "Child exited",
    [SIGCONT]   = "Continued",
    [SIGSTOP]   = "Stopped (signal)",
    [SIGTSTP]   = "Stopped",
    [SIGTTIN]   = "Stopped (tty input)",
    [SIGTTOU]   = "Stopped (tty output)",
    [SIGURG]    = "Urgent I/O condition",
    [SIGXCPU]   = "CPU time limit exceeded",
    [SIGXFSZ]   = "File size limit exceeded",
    [SIGVTALRM] = "Virtual timer expired",
    [SIGPROF]   = "Profiling timer expired",
    [SIGWINCH]  = "Window changed",
    [SIGIO]     = "I/O possible",
    [SIGPWR]    = "Power failure",
    [SIGSYS]    = "Bad system call",
};

static char _strsig_buf[32];

char *strsignal(int sig) {
    if (sig > 0 && sig < (int)(sizeof(_sig_names) / sizeof(_sig_names[0]))
        && _sig_names[sig]) {
        return (char *)_sig_names[sig];
    }
    /* Unknown signal — render "Unknown signal N". */
    char *p = _strsig_buf;
    const char *prefix = "Unknown signal ";
    while (*prefix) *p++ = *prefix++;
    int n = sig;
    if (n < 0) { *p++ = '-'; n = -n; }
    char digits[12];
    int d = 0;
    if (n == 0) digits[d++] = '0';
    else while (n) { digits[d++] = '0' + (n % 10); n /= 10; }
    while (d) *p++ = digits[--d];
    *p = 0;
    return _strsig_buf;
}

/* poll, ppoll, sigsuspend: not yet supported by the kernel. Surface
 * ENOSYS rather than silently returning success — callers will know
 * the feature is unavailable. Real implementations land when runtime
 * triage shows actual call paths exercise them (Stage 7 of the BB
 * port plan). */
int poll(struct pollfd *fds, nfds_t nfds, int timeout) {
    (void)fds; (void)nfds; (void)timeout;
    errno = ENOSYS;
    return -1;
}

int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *tmo,
          const sigset_t *sigmask) {
    (void)fds; (void)nfds; (void)tmo; (void)sigmask;
    errno = ENOSYS;
    return -1;
}

int sigsuspend(const sigset_t *mask) {
    (void)mask;
    /* Best-effort fallback: pause() until any signal arrives. Loses the
     * atomic mask-swap that real sigsuspend provides — a signal arriving
     * between mask install and pause would be missed. Acceptable for
     * non-preemptive single-threaded callers; revisit in Stage 7. */
    pause();
    errno = EINTR;
    return -1;
}
