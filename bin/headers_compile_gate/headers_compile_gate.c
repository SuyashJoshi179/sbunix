/* Pure compile gate: every libc header included, every documented symbol
 * referenced. If a decl, typedef, or macro is missing this file fails to
 * compile. Runtime is a single printf so the binary is well-formed. */

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

/* --- typedefs must exist --- */
static size_t      g_size;
static ptrdiff_t   g_ptrdiff;
static wchar_t     g_wchar;
static int8_t      g_i8;
static uint8_t     g_u8;
static int16_t     g_i16;
static uint16_t    g_u16;
static int32_t     g_i32;
static uint32_t    g_u32;
static int64_t     g_i64;
static uint64_t    g_u64;
static intptr_t    g_iptr;
static uintptr_t   g_uptr;
static intmax_t    g_imax;
static uintmax_t   g_umax;
static pid_t       g_pid;
static uid_t       g_uid;
static gid_t       g_gid;
static off_t       g_off;
static time_t      g_time;
static suseconds_t g_susec;
static sigset_t    g_sset;
static sighandler_t g_sigh;
static FILE       *g_file;

/* --- macros must expand --- */
static const int   g_macros[] = {
    EOF, BUFSIZ, SEEK_SET, SEEK_CUR, SEEK_END,
    EXIT_SUCCESS, EXIT_FAILURE, RAND_MAX,
    O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, O_TRUNC, O_APPEND,
    EPERM, ENOENT, ESRCH, EINTR, EBADF, ECHILD, ENOMEM, EFAULT,
    ENOTDIR, EISDIR, EINVAL, ENFILE, EMFILE, ENOTTY, ENOSPC, ESPIPE,
    EROFS, EPIPE, ENAMETOOLONG, ENOSYS, EEXIST, EFBIG, ENOTSUP,
    SIGHUP, SIGINT, SIGQUIT, SIGILL, SIGTRAP, SIGABRT, SIGBUS, SIGFPE,
    SIGKILL, SIGUSR1, SIGSEGV, SIGUSR2, SIGPIPE, SIGALRM, SIGTERM,
    SIGCHLD, SIGCONT, SIGSTOP, NSIG,
    SIG_BLOCK, SIG_UNBLOCK, SIG_SETMASK,
    CLOCK_REALTIME, CLOCK_MONOTONIC,
    DT_UNKNOWN, DT_CHR, DT_DIR, DT_REG,
    S_IFMT, S_IFIFO, S_IFREG, S_IFDIR, S_IFCHR,
    STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO,
    INT8_MIN, INT8_MAX, UINT8_MAX,
    INT16_MIN, INT16_MAX, UINT16_MAX,
    INT32_MIN, INT32_MAX,
};

/* INT*_C / UINT*_C / SIZE_MAX touch */
static const uint32_t g_u32max  = UINT32_MAX;
static const uint64_t g_u64max  = UINT64_MAX;
static const size_t   g_szmax   = SIZE_MAX;
static const intmax_t g_imaxlim = INTMAX_MAX;
static const int32_t  g_c32     = INT32_C(1);
static const uint32_t g_uc32    = UINT32_C(1);

/* offsetof needs a struct with a member */
struct gate_s { int a; long b; };
static const size_t g_off2 = offsetof(struct gate_s, b);

/* SIG_DFL / SIG_IGN are sentinel pointers */
static sighandler_t g_sigdfl = SIG_DFL;
static sighandler_t g_sigign = SIG_IGN;

/* S_ISxxx are macros taking a mode_t */
static int s_is_checks(uint32_t m) {
    return S_ISREG(m) + S_ISDIR(m) + S_ISCHR(m) + S_ISFIFO(m);
}

/* --- struct fields must be present --- */
static void touch_structs(void) {
    struct stat       st  = {0}; (void)st.st_ino; (void)st.st_size; (void)st.st_mode;
    struct timespec   ts  = {0}; (void)ts.tv_sec; (void)ts.tv_nsec;
    struct timeval    tv  = {0}; (void)tv.tv_sec; (void)tv.tv_usec;
    struct dirent64   de  = {0}; (void)de.d_ino; (void)de.d_off; (void)de.d_reclen; (void)de.d_type;
    struct sigaction  sa  = {0}; (void)sa.sa_handler; (void)sa.sa_mask; (void)sa.sa_flags;
    struct termios    tio = {0}; (void)tio.c_iflag; (void)tio.c_oflag; (void)tio.c_cflag; (void)tio.c_lflag; (void)tio.c_cc;
    struct winsize    ws  = {0}; (void)ws.ws_row; (void)ws.ws_col;
}

/* --- function decls must exist (take address; skipped at runtime) --- */
typedef void (*vp_t)(void);
static const vp_t g_fns[] = {
    /* stdio */
    (vp_t)printf, (vp_t)fprintf, (vp_t)sprintf, (vp_t)snprintf,
    (vp_t)vprintf, (vp_t)vfprintf, (vp_t)vsprintf, (vp_t)vsnprintf,
    (vp_t)puts, (vp_t)fputs, (vp_t)putchar, (vp_t)putc, (vp_t)fputc,
    (vp_t)getchar, (vp_t)getc, (vp_t)fgetc, (vp_t)fgets,
    (vp_t)fopen, (vp_t)fclose, (vp_t)fflush,
    (vp_t)fread, (vp_t)fwrite,
    (vp_t)fseek, (vp_t)ftell, (vp_t)rewind,
    (vp_t)feof, (vp_t)ferror, (vp_t)clearerr, (vp_t)fileno,
    (vp_t)perror, (vp_t)remove, (vp_t)rename,
    /* stdlib */
    (vp_t)exit, (vp_t)_Exit, (vp_t)abort, (vp_t)atexit,
    (vp_t)malloc, (vp_t)calloc, (vp_t)realloc, (vp_t)free,
    (vp_t)atoi, (vp_t)atol, (vp_t)atoll,
    (vp_t)strtol, (vp_t)strtoul,
    (vp_t)abs, (vp_t)labs,
    (vp_t)rand, (vp_t)srand, (vp_t)getenv,
    (vp_t)qsort, (vp_t)bsearch,
    /* string */
    (vp_t)strlen, (vp_t)strcmp, (vp_t)strncmp,
    (vp_t)strcpy, (vp_t)strncpy, (vp_t)strchr, (vp_t)strrchr,
    (vp_t)memset, (vp_t)memcpy, (vp_t)memmove,
    /* unistd */
    (vp_t)read, (vp_t)write, (vp_t)open, (vp_t)close,
    (vp_t)getpid, (vp_t)getppid, (vp_t)fork, (vp_t)wait,
    (vp_t)sched_yield, (vp_t)sleep_ms, (vp_t)usleep,
    (vp_t)dup, (vp_t)dup2, (vp_t)lseek, (vp_t)fstat, (vp_t)getdents64,
    (vp_t)chdir, (vp_t)getcwd, (vp_t)mkdir, (vp_t)unlink,
    (vp_t)pipe, (vp_t)execv, (vp_t)sbrk, (vp_t)meminfo,
    (vp_t)getuid, (vp_t)geteuid, (vp_t)getgid, (vp_t)getegid,
    (vp_t)setuid, (vp_t)setgid,
    /* time */
    (vp_t)clock_gettime, (vp_t)gettimeofday, (vp_t)nanosleep, (vp_t)time,
    /* signal */
    (vp_t)kill, (vp_t)sigaction, (vp_t)signal, (vp_t)sigprocmask,
    (vp_t)raise, (vp_t)pause,
};

/* va_list / va_start / va_arg / va_end / va_copy must work */
static int va_check(int n, ...) {
    va_list ap, ap2;
    va_start(ap, n);
    va_copy(ap2, ap);
    int sum = 0;
    for (int i = 0; i < n; i++) sum += va_arg(ap, int);
    int sum2 = 0;
    for (int i = 0; i < n; i++) sum2 += va_arg(ap2, int);
    va_end(ap2);
    va_end(ap);
    return sum + sum2;
}

int main(void) {
    /* Reference everything once so the optimizer cannot drop it. */
    g_size = 0; g_ptrdiff = 0; g_wchar = 0;
    g_i8 = 0; g_u8 = 0; g_i16 = 0; g_u16 = 0;
    g_i32 = 0; g_u32 = 0; g_i64 = 0; g_u64 = 0;
    g_iptr = 0; g_uptr = 0; g_imax = 0; g_umax = 0;
    g_pid = 0; g_uid = 0; g_gid = 0; g_off = 0;
    g_time = 0; g_susec = 0; g_sset = 0;
    g_sigh = g_sigdfl ? g_sigdfl : g_sigign;
    g_file = stdin; g_file = stdout; g_file = stderr;

    int macro_sum = 0;
    for (size_t i = 0; i < sizeof(g_macros)/sizeof(g_macros[0]); i++)
        macro_sum += g_macros[i] != 0;
    (void)macro_sum;

    (void)g_u32max; (void)g_u64max; (void)g_szmax; (void)g_imaxlim;
    (void)g_c32; (void)g_uc32; (void)g_off2;

    touch_structs();
    (void)s_is_checks(S_IFREG);

    int n_fns = (int)(sizeof(g_fns) / sizeof(g_fns[0]));
    int va = va_check(3, 1, 2, 3);

    printf("compile_gate: %d fns, va=%d, NULL=%p, headers OK\n",
           n_fns, va, (void *)NULL);
    return 0;
}
