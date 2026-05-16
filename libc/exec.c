#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/syscall.h>

/* SYS_execve propagates envp to the child. The execle path still ignores
 * its trailing envp (existing callers don't depend on it for env
 * delivery — they `setenv()` after start), but execve hands it to the
 * kernel. We expose `environ` as an empty list so getenv() callers on a
 * fresh exec don't read uninitialized memory; crt.S separately passes the
 * stack envp to main(int, char **, char **). */
static char *_environ_empty[] = { 0 };
char **environ = _environ_empty;

#define ARGV_STACK_MAX 64

/* execl/execlp/execle declare their varargs as `const char *`. va_arg
 * type must match what the caller actually passed, so pull `const char *`
 * and cast to char* only when storing into the argv array (which is
 * declared char *const argv[] for the underlying execv). */
static int build_argv(char *out[], int cap, const char *arg0, va_list ap) {
    int n = 0;
    out[n++] = (char *)arg0;
    while (n < cap) {
        const char *p = va_arg(ap, const char *);
        out[n++] = (char *)p;
        if (!p) return n;
    }
    return -1;
}

int execve(const char *path, char *const argv[], char *const envp[]) {
    return (int)syscall(SYS_execve, (long)path, (long)argv, (long)envp);
}

int execl(const char *path, const char *arg0, ...) {
    char *argv[ARGV_STACK_MAX];
    va_list ap; va_start(ap, arg0);
    int n = build_argv(argv, ARGV_STACK_MAX, arg0, ap);
    va_end(ap);
    if (n < 0) { errno = E2BIG; return -1; }
    return execv(path, argv);
}

int execle(const char *path, const char *arg0, ...) {
    char *argv[ARGV_STACK_MAX];
    va_list ap; va_start(ap, arg0);
    int n = build_argv(argv, ARGV_STACK_MAX, arg0, ap);
    /* swallow the trailing envp arg even though we ignore it */
    if (n >= 0) (void)va_arg(ap, char *const *);
    va_end(ap);
    if (n < 0) { errno = E2BIG; return -1; }
    return execv(path, argv);
}

/* PATH search for execvp/execlp. We dont have getenv("PATH") backed by
 * a real environment, so we hardcode the conventional dirs. The probe
 * uses open(O_RDONLY) rather than access(X_OK) so we surface ENOTDIR
 * vs ENOENT verbatim from the kernel and don't double-walk the path. */
static const char *exec_path_dirs[] = { "/bin", "/usr/bin", 0 };

static int try_exec(const char *full, char *const argv[]) {
    int fd = open(full, O_RDONLY);
    if (fd < 0) return -1;
    close(fd);
    return execv(full, argv);
}

int execvp(const char *file, char *const argv[]) {
    if (!file) { errno = EINVAL; return -1; }
    /* If the name contains a slash, treat it as a path. */
    if (strchr(file, '/')) return execv(file, argv);
    char buf[256];
    size_t flen = strlen(file);
    /* POSIX: if every candidate fails with ENOENT-class errors, return
     * ENOENT; if any candidate fails with EACCES/ENOEXEC/etc., surface
     * the most informative failure rather than masking it as ENOENT. */
    int saved = ENOENT;
    for (int i = 0; exec_path_dirs[i]; i++) {
        size_t dlen = strlen(exec_path_dirs[i]);
        if (dlen + 1 + flen + 1 > sizeof(buf)) continue;
        memcpy(buf, exec_path_dirs[i], dlen);
        buf[dlen] = '/';
        memcpy(buf + dlen + 1, file, flen + 1);
        errno = 0;
        try_exec(buf, argv);
        if (errno && errno != ENOENT && errno != ENOTDIR) saved = errno;
    }
    errno = saved;
    return -1;
}

int execvpe(const char *file, char *const argv[], char *const envp[]) {
    (void)envp;
    return execvp(file, argv);
}

int execlp(const char *file, const char *arg0, ...) {
    char *argv[ARGV_STACK_MAX];
    va_list ap; va_start(ap, arg0);
    int n = build_argv(argv, ARGV_STACK_MAX, arg0, ap);
    va_end(ap);
    if (n < 0) { errno = E2BIG; return -1; }
    return execvp(file, argv);
}
