/* Pure compile gate: every libc header included, every documented symbol
 * referenced. If a decl, typedef, or macro is missing this file fails to
 * compile. Runtime is a single printf so the binary is well-formed. */

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <ctype.h>
#include <setjmp.h>
#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <inttypes.h>
#include <math.h>
#include <getopt.h>
#include <libgen.h>
#include <pwd.h>
#include <grp.h>
#include <syslog.h>
#include <netdb.h>
#include <mntent.h>
#include <sys/utsname.h>
#include <sys/resource.h>
#include <sys/statfs.h>

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
    EAGAIN, EWOULDBLOCK, ERANGE, EDOM, EILSEQ, ENOEXEC, EBUSY,
    ENXIO, EXDEV, ENODEV, ELOOP, E2BIG, ENOTEMPTY, EACCES,
    SIGHUP, SIGINT, SIGQUIT, SIGILL, SIGTRAP, SIGABRT, SIGBUS, SIGFPE,
    SIGKILL, SIGUSR1, SIGSEGV, SIGUSR2, SIGPIPE, SIGALRM, SIGTERM,
    SIGCHLD, SIGCONT, SIGSTOP, NSIG,
    SIG_BLOCK, SIG_UNBLOCK, SIG_SETMASK,
    CLOCK_REALTIME, CLOCK_MONOTONIC,
    DT_UNKNOWN, DT_CHR, DT_DIR, DT_REG,
    S_IFMT, S_IFIFO, S_IFREG, S_IFDIR, S_IFCHR,
    STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO,
    F_OK, R_OK, W_OK, X_OK,
    WNOHANG, WUNTRACED,
    _IOFBF, _IOLBF, _IONBF, L_tmpnam, FILENAME_MAX, FOPEN_MAX, TMP_MAX,
    INT8_MIN, INT8_MAX, UINT8_MAX,
    INT16_MIN, INT16_MAX, UINT16_MAX,
    INT32_MIN, INT32_MAX,
    CHAR_BIT, SCHAR_MIN, SCHAR_MAX, UCHAR_MAX,
    SHRT_MIN, SHRT_MAX, USHRT_MAX,
    INT_MIN, INT_MAX,
    PATH_MAX, NAME_MAX, ARG_MAX, OPEN_MAX, PIPE_BUF,
    MB_LEN_MAX, MB_CUR_MAX,
    FP_NAN, FP_INFINITE, FP_ZERO, FP_SUBNORMAL, FP_NORMAL,
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
    div_t             dv  = {0}; (void)dv.quot; (void)dv.rem;
    ldiv_t            ld  = {0}; (void)ld.quot; (void)ld.rem;
    lldiv_t           lld = {0}; (void)lld.quot; (void)lld.rem;
    imaxdiv_t         id  = {0}; (void)id.quot; (void)id.rem;
    jmp_buf           jb  = {0}; (void)jb[0];
    sigjmp_buf        sjb = {0}; (void)sjb[0];

    /* W* macros must compile against an int status. */
    int s = 0;
    (void)WIFEXITED(s); (void)WEXITSTATUS(s);
    (void)WIFSIGNALED(s); (void)WTERMSIG(s);
    (void)WCOREDUMP(s); (void)WIFSTOPPED(s); (void)WSTOPSIG(s);

    /* bool from stdbool.h. */
    bool b = true; b = false; (void)b;

    /* Format-string macros from inttypes.h. */
    char buf[64];
    snprintf(buf, sizeof(buf), "%" PRId64 " %" PRIu64 " %" PRIx64,
             (int64_t)1, (uint64_t)2, (uint64_t)0xff);
    (void)buf;
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
    (vp_t)strlen, (vp_t)strnlen, (vp_t)strcmp, (vp_t)strncmp,
    (vp_t)strcpy, (vp_t)strncpy, (vp_t)strchr, (vp_t)strrchr,
    (vp_t)strcat, (vp_t)strncat,
    (vp_t)strstr, (vp_t)strpbrk, (vp_t)strspn, (vp_t)strcspn,
    (vp_t)strtok, (vp_t)strtok_r,
    (vp_t)strdup, (vp_t)strndup, (vp_t)strerror,
    (vp_t)memset, (vp_t)memcpy, (vp_t)memmove,
    (vp_t)memcmp, (vp_t)memchr,
    /* strings */
    (vp_t)bzero, (vp_t)bcmp, (vp_t)bcopy,
    (vp_t)ffs, (vp_t)ffsl, (vp_t)ffsll,
    (vp_t)strcasecmp, (vp_t)strncasecmp,
    /* ctype */
    (vp_t)isalnum, (vp_t)isalpha, (vp_t)isascii, (vp_t)isblank,
    (vp_t)iscntrl, (vp_t)isdigit, (vp_t)isgraph, (vp_t)islower,
    (vp_t)isprint, (vp_t)ispunct, (vp_t)isspace, (vp_t)isupper,
    (vp_t)isxdigit,
    (vp_t)toascii, (vp_t)tolower, (vp_t)toupper,
    /* setjmp */
    (vp_t)setjmp, (vp_t)longjmp,
    (vp_t)sigsetjmp, (vp_t)siglongjmp,
    /* errno */
    (vp_t)__errno_location,
    /* assert */
    (vp_t)__assert_fail,
    /* unistd */
    (vp_t)read, (vp_t)write, (vp_t)open, (vp_t)close,
    (vp_t)getpid, (vp_t)getppid, (vp_t)fork, (vp_t)wait,
    (vp_t)sched_yield, (vp_t)sleep_ms, (vp_t)usleep,
    (vp_t)dup, (vp_t)dup2, (vp_t)lseek, (vp_t)fstat, (vp_t)getdents64,
    (vp_t)chdir, (vp_t)getcwd, (vp_t)mkdir, (vp_t)unlink,
    (vp_t)pipe, (vp_t)execv, (vp_t)sbrk, (vp_t)meminfo,
    (vp_t)getuid, (vp_t)geteuid, (vp_t)getgid, (vp_t)getegid,
    (vp_t)setuid, (vp_t)setgid,
    (vp_t)_exit, (vp_t)isatty, (vp_t)access, (vp_t)readlink,
    /* sys/wait */
    (vp_t)waitpid,
    /* stdio additions */
    (vp_t)ungetc, (vp_t)setbuf, (vp_t)setvbuf, (vp_t)tmpnam,
    /* stdlib additions */
    (vp_t)atof, (vp_t)strtoll, (vp_t)strtoull,
    (vp_t)strtod, (vp_t)strtof,
    (vp_t)llabs, (vp_t)div, (vp_t)ldiv, (vp_t)lldiv,
    (vp_t)setenv, (vp_t)unsetenv, (vp_t)putenv, (vp_t)system,
    (vp_t)mblen, (vp_t)mbtowc, (vp_t)wctomb,
    (vp_t)mbstowcs, (vp_t)wcstombs,
    /* inttypes */
    (vp_t)imaxabs, (vp_t)imaxdiv, (vp_t)strtoimax, (vp_t)strtoumax,
    /* time */
    (vp_t)clock_gettime, (vp_t)gettimeofday, (vp_t)nanosleep, (vp_t)time,
    (vp_t)clock, (vp_t)difftime,
    (vp_t)gmtime, (vp_t)gmtime_r, (vp_t)localtime, (vp_t)localtime_r,
    (vp_t)mktime, (vp_t)timegm,
    (vp_t)asctime, (vp_t)asctime_r, (vp_t)ctime, (vp_t)ctime_r,
    (vp_t)strftime, (vp_t)tzset,
    /* signal */
    (vp_t)kill, (vp_t)sigaction, (vp_t)signal, (vp_t)sigprocmask,
    (vp_t)raise, (vp_t)pause,
    /* dirent */
    (vp_t)opendir, (vp_t)readdir, (vp_t)closedir, (vp_t)rewinddir,
    (vp_t)telldir, (vp_t)seekdir, (vp_t)dirfd, (vp_t)fdopendir,
    /* getopt / libgen */
    (vp_t)getopt, (vp_t)getopt_long,
    (vp_t)basename, (vp_t)dirname,
    /* pwd / grp */
    (vp_t)getpwuid, (vp_t)getpwnam, (vp_t)getpwent,
    (vp_t)setpwent, (vp_t)endpwent,
    (vp_t)getgrgid, (vp_t)getgrnam, (vp_t)getgrent,
    (vp_t)setgrent, (vp_t)endgrent, (vp_t)getgroups,
    /* termios */
    (vp_t)tcgetattr, (vp_t)tcsetattr, (vp_t)tcflush, (vp_t)tcdrain,
    (vp_t)cfmakeraw, (vp_t)cfsetispeed, (vp_t)cfsetospeed,
    (vp_t)tcgetpgrp, (vp_t)tcsetpgrp,
    /* exec wrappers */
    (vp_t)execve, (vp_t)execvp, (vp_t)execvpe,
    (vp_t)execl, (vp_t)execlp, (vp_t)execle,
    /* process groups */
    (vp_t)getpgrp, (vp_t)getpgid, (vp_t)setpgid,
    (vp_t)setsid, (vp_t)getsid, (vp_t)tcgetsid,
    /* resource */
    (vp_t)getrlimit, (vp_t)setrlimit, (vp_t)getrusage,
    (vp_t)getpriority, (vp_t)setpriority,
    /* misc */
    (vp_t)uname, (vp_t)gethostname, (vp_t)sethostname,
    (vp_t)sync, (vp_t)fsync, (vp_t)alarm, (vp_t)sleep,
    (vp_t)sysconf, (vp_t)pathconf, (vp_t)fpathconf,
    (vp_t)ttyname, (vp_t)ttyname_r,
    (vp_t)chmod, (vp_t)fchmod, (vp_t)umask, (vp_t)stat, (vp_t)lstat,
    (vp_t)link, (vp_t)symlink, (vp_t)rmdir,
    (vp_t)chown, (vp_t)fchown, (vp_t)lchown,
    (vp_t)fcntl, (vp_t)creat, (vp_t)openat,
    /* syslog / netdb / mntent / statfs */
    (vp_t)openlog, (vp_t)closelog, (vp_t)syslog, (vp_t)setlogmask,
    (vp_t)gethostbyname, (vp_t)getaddrinfo, (vp_t)freeaddrinfo, (vp_t)gai_strerror,
    (vp_t)setmntent, (vp_t)endmntent, (vp_t)getmntent, (vp_t)hasmntopt,
    (vp_t)statfs, (vp_t)fstatfs, (vp_t)statvfs, (vp_t)fstatvfs,
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
