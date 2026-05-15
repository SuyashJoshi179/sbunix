/*
 * POSIX conformance test: <unistd.h>
 *
 * Reference: docs/susv5-html/basedefs/unistd.h.html
 *
 * Audited POSIX functions (subset; only those POSIX assigns to unistd.h
 * AND our libc declares):
 *   - access, alarm, chdir, chown, close, dup, dup2, _exit, execl, execle,
 *     execlp, execv, execve, execvp, fchown, fdatasync, fork, fpathconf,
 *     fsync, ftruncate, getcwd, getegid, geteuid, getgid, gethostname,
 *     getpgid, getpgrp, getpid, getppid, getsid, getuid, isatty, lchown,
 *     link, lseek, pathconf, pipe, read, readlink, rmdir, setgid, setpgid,
 *     setsid, setuid, sleep, symlink, sync, sysconf, truncate, ttyname,
 *     ttyname_r, unlink, write
 *
 * Excluded (functions our libc declares in <unistd.h> but POSIX assigns
 * to a different header — audited in their canonical test files):
 *   - open, openat — POSIX: <fcntl.h>
 *   - fstat, lstat, mkdir — POSIX: <sys/stat.h>
 *   - wait — POSIX: <sys/wait.h>
 *   - sched_yield — POSIX: <sched.h>
 *
 * Excluded (non-POSIX, our extensions):
 *   - sleep_ms, getdents64, meminfo, execvpe, sbrk, sethostname,
 *     getdomainname, tcgetsid (POSIX: <termios.h>), setpgrp (POSIX has
 *     setpgrp but obsolescent and signature differs across editions)
 *
 * Excluded (POSIX functions not in our libc):
 *   - confstr, crypt, encrypt, faccessat, fchdir, fchownat, fexecve,
 *     getentropy, getgroups, gethostid, getlogin, getlogin_r, getopt,
 *     getresgid, getresuid, lockf, nice, pause, posix_close, pread,
 *     pwrite, linkat, readlinkat, setegid, seteuid, setregid, setresgid,
 *     setresuid, setreuid, swab, symlinkat, tcgetpgrp, tcsetpgrp,
 *     unlinkat, _Fork, dup3, _PATH_*
 *
 * Commented-out pins below carry DIVERGENCE notes; uncomment in Phase 2
 * once the listed fix lands.
 */
#include <unistd.h>

#define PIN __attribute__((unused)) static

/* --- File I/O --- */
PIN ssize_t (*_pin_read)(int, void *, size_t) = read;
PIN ssize_t (*_pin_write)(int, const void *, size_t) = write;
PIN int     (*_pin_close)(int) = close;
PIN off_t   (*_pin_lseek)(int, off_t, int) = lseek;
PIN int     (*_pin_ftruncate)(int, off_t) = ftruncate;
PIN int     (*_pin_truncate)(const char *, off_t) = truncate;
PIN int     (*_pin_dup)(int) = dup;
PIN int     (*_pin_dup2)(int, int) = dup2;
PIN int     (*_pin_isatty)(int) = isatty;
PIN int     (*_pin_pipe)(int [2]) = pipe;
PIN int     (*_pin_access)(const char *, int) = access;
PIN ssize_t (*_pin_readlink)(const char *, char *, size_t) = readlink;
PIN int     (*_pin_link)(const char *, const char *) = link;
PIN int     (*_pin_symlink)(const char *, const char *) = symlink;
PIN int     (*_pin_unlink)(const char *) = unlink;
PIN int     (*_pin_rmdir)(const char *) = rmdir;
PIN int     (*_pin_chdir)(const char *) = chdir;
PIN char   *(*_pin_getcwd)(char *, size_t) = getcwd;
PIN int     (*_pin_chown)(const char *, uid_t, gid_t) = chown;
PIN int     (*_pin_fchown)(int, uid_t, gid_t) = fchown;
PIN int     (*_pin_lchown)(const char *, uid_t, gid_t) = lchown;
PIN void    (*_pin_sync)(void) = sync;
PIN int     (*_pin_fsync)(int) = fsync;
PIN int     (*_pin_fdatasync)(int) = fdatasync;
PIN char   *(*_pin_ttyname)(int) = ttyname;
PIN int     (*_pin_ttyname_r)(int, char *, size_t) = ttyname_r;

/* --- Process / session --- */
PIN pid_t   (*_pin_fork)(void) = fork;
PIN pid_t   (*_pin_getpid)(void) = getpid;
PIN pid_t   (*_pin_getppid)(void) = getppid;
PIN pid_t   (*_pin_getpgrp)(void) = getpgrp;
PIN pid_t   (*_pin_getpgid)(pid_t) = getpgid;
PIN int     (*_pin_setpgid)(pid_t, pid_t) = setpgid;
PIN pid_t   (*_pin_setsid)(void) = setsid;
PIN pid_t   (*_pin_getsid)(pid_t) = getsid;
PIN void    (*_pin__exit)(int) __attribute__((noreturn)) = _exit;

/* --- exec family --- */
PIN int     (*_pin_execv)(const char *, char *const []) = execv;
PIN int     (*_pin_execve)(const char *, char *const [], char *const []) = execve;
PIN int     (*_pin_execvp)(const char *, char *const []) = execvp;
PIN int     (*_pin_execl)(const char *, const char *, ...) = execl;
PIN int     (*_pin_execlp)(const char *, const char *, ...) = execlp;
PIN int     (*_pin_execle)(const char *, const char *, ...) = execle;

/* --- uid/gid --- */
PIN uid_t   (*_pin_getuid)(void) = getuid;
PIN uid_t   (*_pin_geteuid)(void) = geteuid;
PIN gid_t   (*_pin_getgid)(void) = getgid;
PIN gid_t   (*_pin_getegid)(void) = getegid;
PIN int     (*_pin_setuid)(uid_t) = setuid;
PIN int     (*_pin_setgid)(gid_t) = setgid;

/* --- Misc --- */
PIN unsigned (*_pin_alarm)(unsigned) = alarm;
PIN unsigned (*_pin_sleep)(unsigned) = sleep;
PIN int      (*_pin_gethostname)(char *, size_t) = gethostname;
PIN long     (*_pin_sysconf)(int) = sysconf;
PIN long     (*_pin_pathconf)(const char *, int) = pathconf;
PIN long     (*_pin_fpathconf)(int, int) = fpathconf;
/* extern char **environ; pin: take the address, verify type is char **. */
extern char **environ;
PIN char ***_pin_environ = &environ;

/* DIVERGENCE: usleep takes useconds_t per POSIX, but our libc declares it
 * with unsigned long and our <sys/types.h> defines suseconds_t (signed)
 * but not useconds_t. Uncomment after Phase 2 adds useconds_t typedef
 * and updates usleep signature.
 * PIN int (*_pin_usleep)(useconds_t) = usleep;
 */

/* Macro pins for POSIX-required constants. */
__attribute__((unused))
static void _macro_checks(void) {
    int x = 0;
    x |= STDIN_FILENO; x |= STDOUT_FILENO; x |= STDERR_FILENO;
    x |= SEEK_SET; x |= SEEK_CUR; x |= SEEK_END;
    x |= F_OK; x |= R_OK; x |= W_OK; x |= X_OK;
    (void)x;
}
