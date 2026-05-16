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

/* PATH search for execvp/execvpe/execlp. POSIX says: if PATH is present
 * in the (calling or passed-in) environment, split it on ':' — each
 * segment is a directory to search, an empty segment means cwd. If PATH
 * is unset, the default is implementation-defined; we follow the
 * historical "/bin:/usr/bin" fallback that matches what real shells
 * synthesize when launched without a PATH. */
#define EXEC_DEFAULT_PATH "/bin:/usr/bin"

/* Look up "PATH" in a caller-supplied envp[]. Mirrors getenv but works
 * on an arbitrary env array (execvpe doesn't get to use the runtime
 * `environ`). Returns the value side after the '=' or NULL. */
static const char *envp_lookup_path(char *const envp[]) {
    if (!envp) return 0;
    for (int i = 0; envp[i]; i++) {
        const char *e = envp[i];
        if (e[0] == 'P' && e[1] == 'A' && e[2] == 'T' && e[3] == 'H' &&
            e[4] == '=')
            return e + 5;
    }
    return 0;
}

/* Walk a colon-separated PATH, trying file in each directory. `do_exec`
 * is the callback that actually invokes execv or execve depending on
 * the caller — that's the only thing differing between execvp and
 * execvpe. Empty segments mean "current directory" (POSIX). Returns
 * only on failure; errno carries the most informative status, with
 * ENOENT/ENOTDIR demoted in favor of EACCES/ENOEXEC/E2BIG when any
 * candidate hits one of those. */
typedef int (*exec_fn_t)(const char *path, char *const argv[],
                         char *const envp[]);

static int path_search(const char *path_val, const char *file,
                       char *const argv[], char *const envp[],
                       exec_fn_t do_exec) {
    char buf[256];
    size_t flen = strlen(file);
    int saved = ENOENT;
    const char *p = path_val;

    while (1) {
        const char *colon = strchr(p, ':');
        size_t dlen = colon ? (size_t)(colon - p) : strlen(p);
        size_t total = (dlen ? dlen + 1 : 0) + flen + 1;
        if (total <= sizeof(buf)) {
            if (dlen) {
                memcpy(buf, p, dlen);
                buf[dlen] = '/';
                memcpy(buf + dlen + 1, file, flen + 1);
            } else {
                /* Empty segment → cwd-relative; don't prepend a slash
                 * because that would make it absolute. */
                memcpy(buf, file, flen + 1);
            }
            errno = 0;
            int fd = open(buf, O_RDONLY);
            if (fd >= 0) {
                close(fd);
                do_exec(buf, argv, envp);
                /* exec*() returned → it failed; errno set */
            }
            if (errno && errno != ENOENT && errno != ENOTDIR) saved = errno;
        }
        if (!colon) break;
        p = colon + 1;
    }
    errno = saved;
    return -1;
}

static int do_execv(const char *p, char *const argv[], char *const envp[]) {
    (void)envp;
    return execv(p, argv);
}

static int do_execve(const char *p, char *const argv[], char *const envp[]) {
    return execve(p, argv, envp);
}

int execvp(const char *file, char *const argv[]) {
    if (!file || !*file) { errno = ENOENT; return -1; }
    if (strchr(file, '/')) return execv(file, argv);
    const char *path = getenv("PATH");
    if (!path || !*path) path = EXEC_DEFAULT_PATH;
    return path_search(path, file, argv, 0, do_execv);
}

int execvpe(const char *file, char *const argv[], char *const envp[]) {
    if (!file || !*file) { errno = ENOENT; return -1; }
    if (strchr(file, '/')) return execve(file, argv, envp);
    const char *path = envp_lookup_path(envp);
    if (!path || !*path) path = EXEC_DEFAULT_PATH;
    return path_search(path, file, argv, envp, do_execve);
}

int execlp(const char *file, const char *arg0, ...) {
    char *argv[ARGV_STACK_MAX];
    va_list ap; va_start(ap, arg0);
    int n = build_argv(argv, ARGV_STACK_MAX, arg0, ap);
    va_end(ap);
    if (n < 0) { errno = E2BIG; return -1; }
    return execvp(file, argv);
}
