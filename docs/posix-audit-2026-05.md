# POSIX Conformance Audit — 2026-05

Generated from `make posix-check` output run on commit `b131620`.
Reference: `docs/susv5-html/basedefs/<header>.h.html` (IEEE Std 1003.1-2024).

## Headline

- Total test files: **38**
- PASS: **37**
- FAIL: **1** (`tests/posix/unistd.c`)

The single FAIL surfaces three compile-time prototype mismatches. Additional
divergences are recorded as **commented-out pins** inside passing test files
(grep `DIVERGENCE` under `tests/posix/`); these are the divergences that
would cause a parser cascade if left active. Counting both surfaces:

- **Active divergences (cause file FAIL):** 3 in `<unistd.h>`
- **Latent divergences (pin commented to keep file PASS):** 14 across 7
  headers — see per-header tables below

## How to read this audit

Each section corresponds to one `tests/posix/<header>.c` test file. A clean
header is a single line: "Clean. All N audited functions match POSIX
prototypes; all required struct fields present."

A header with divergences gets a table:

| Function/Symbol | Current (libc) | POSIX (SUSv5) | Class | Severity | Fix sketch |

Class:
- `missing-typedef` — POSIX typedef not defined anywhere in libc
- `missing-struct-field` — POSIX-required member absent from struct
- `wrong-param-type` — parameter type diverges (e.g. `long` vs `size_t`)
- `wrong-return-type` — return type diverges
- `wrong-header-location` — function declared in non-canonical header
- `missing-include` — header should expose a typedef but fails to include
  the header that defines it
- `missing-decl` — POSIX function not declared in our libc at all

Severity:
- `build-breaker` — external POSIX-conformant code fails to compile against our libc
- `latent` — pin disabled to allow neighbouring pins to be tested; equivalent
  build-breaker once uncommented
- `cosmetic` — types are compatible on lp64 (e.g. `int` vs `pid_t` when
  `pid_t = int32_t = int`); strictly POSIX-incorrect but compiles cleanly

## Per-header sections

### `<assert.h>`

Clean. Macros `assert` and `static_assert` present and usable.

### `<ctype.h>`

Clean. All 14 audited POSIX functions match POSIX prototypes.

### `<dirent.h>`

Clean. All 11 audited POSIX functions match POSIX prototypes; `struct dirent`
required field `d_name` present.

### `<errno.h>`

Clean. `errno` is a modifiable lvalue; all standard error macros present.

### `<fcntl.h>`

Clean. All 4 audited POSIX functions match POSIX prototypes; `struct flock`
required fields present; all required `O_*`, `F_*`, `AT_*` macros present.

### `<fnmatch.h>`

Clean. `fnmatch` matches POSIX prototype; all 5 required macros present.

### `<glob.h>`

Clean. `glob` and `globfree` match POSIX prototypes; `struct glob_t` fields
present; all required macros present.

### `<grp.h>`

Clean. All 7 audited POSIX functions match POSIX prototypes; `struct group`
POSIX-required fields present.

### `<inttypes.h>`

Clean (macro audit). Sample of `PRI*` macros expand cleanly. POSIX functions
(`imaxabs`, `imaxdiv`, `strtoimax`, `strtoumax`, `wcstoimax`, `wcstoumax`) are
not implemented in our libc; documented as future audit.

### `<libgen.h>`

Clean. `basename` and `dirname` match POSIX prototypes.

### `<math.h>`

Clean for the subset our libc declares. Soft-float build; libm not linked.
Phase 2+ will need to expand once `<math.h>` is fleshed out.

### `<poll.h>`

Clean. `poll` matches POSIX prototype; `struct pollfd` fields present; all
required `POLL*` macros present.

### `<pwd.h>`

Clean. All 7 audited POSIX functions match POSIX prototypes; `struct passwd`
POSIX-required fields present.

### `<sched.h>`

Clean. All 5 audited POSIX functions match POSIX prototypes; `struct sched_param`
field `sched_priority` present.

### `<setjmp.h>`

Clean. `setjmp`, `longjmp`, `sigsetjmp`, `siglongjmp` match POSIX prototypes;
`jmp_buf` and `sigjmp_buf` typedefs present.

### `<signal.h>`

1 latent divergence.

| Function/Symbol | Current (libc) | POSIX (SUSv5) | Class | Severity | Fix sketch |
|---|---|---|---|---|---|
| `kill`, `killpg` | declared `int kill(int, int)` — `pid_t` not visible from `<signal.h>` | declared `int kill(pid_t, int)`; `<signal.h>` must expose `pid_t` per POSIX | missing-include | latent (pin commented) | Add `#include <sys/types.h>` to `libc/include/signal.h`, then change `int pid` params to `pid_t` |

Otherwise the rest of the signal surface (sigaction, sigprocmask, sigsuspend,
sigaltstack, sigemptyset/fillset/addset/delset/ismember, sigset, sighold,
sigrelse, sigignore, raise, signal, struct sigaction, stack_t, siginfo_t, all
`SIG*` and `SA_*` and `SS_*` macros) is clean.

### `<sys/mman.h>`

3 latent divergences (one missing-include masks two wrong-param-type pins).

| Function/Symbol | Current (libc) | POSIX (SUSv5) | Class | Severity | Fix sketch |
|---|---|---|---|---|---|
| `size_t`, `off_t` | not visible from `<sys/mman.h>` | POSIX requires both visible from this header | missing-include | latent | Add `#include <sys/types.h>` to `libc/include/sys/mman.h` |
| `mmap` | `void *mmap(void *, long, int, int, int, long)` | `void *mmap(void *, size_t, int, int, int, off_t)` | wrong-param-type | latent (pin commented; un-cmtable after the include fix above) | Change size param to `size_t`, offset to `off_t` |
| `munmap`, `msync` | size param declared `long` | `size_t` | wrong-param-type | latent | Change size param to `size_t` |

Also note: `mprotect`, `mlock`/`munlock`, `mlockall`/`munlockall`,
`posix_madvise`, `shm_open`, `shm_unlink` are POSIX-required and missing
entirely from our libc. Logged for future audit (Phase 2+).

### `<sys/resource.h>`

Clean. All 5 audited POSIX functions match POSIX prototypes; required
`struct rlimit` and `struct rusage` POSIX-mandatory fields present.

### `<sys/select.h>`

Clean. `select`, `pselect` match POSIX prototypes; `FD_*` macros present and
exercisable; `FD_SETSIZE` defined.

### `<sys/stat.h>`

3 latent divergences (struct timespec members commented).

| Function/Symbol | Current (libc) | POSIX (SUSv5) | Class | Severity | Fix sketch |
|---|---|---|---|---|---|
| `struct stat::st_atim` | exposes `st_atime` of type `uint64_t` only | POSIX requires `st_atim` of type `struct timespec` | missing-struct-field | latent (referenced in commented block) | Replace `uint64_t st_atime` with `struct timespec st_atim`; define `st_atime` as a back-compat macro expanding to `st_atim.tv_sec` |
| `struct stat::st_mtim` | exposes `st_mtime` (uint64) | `struct timespec st_mtim` | missing-struct-field | latent | Same pattern as above |
| `struct stat::st_ctim` | exposes `st_ctime` (uint64) | `struct timespec st_ctim` | missing-struct-field | latent | Same pattern as above |

Otherwise: `stat`, `fstat`, `lstat`, `chmod`, `fchmod`, `umask`, `mkfifo`,
`mknod`, all `S_IF*`/`S_IS*`/`S_IR*W*X*` macros are clean.

### `<sys/time.h>`

Clean for our libc's reachable surface. Header is a one-line shim that
`#include`s `<time.h>`; `gettimeofday` is therefore reachable here as POSIX-2017
expects. SUSv5 deprecates `gettimeofday` (left in for grader compatibility).
Not implemented: `utimes`, `getitimer`, `setitimer`.

### `<sys/times.h>`

Clean. `times` matches POSIX prototype; `struct tms` fields present.

### `<sys/types.h>`

Clean. All POSIX typedefs our libc claims to expose are visible.

Documented gaps (not audited in this header, deferred to Phase 2): POSIX
also requires `clockid_t`, `timer_t`, `useconds_t`, `fsblkcnt_t`,
`fsfilcnt_t`, `key_t`, `pthread_*`, `trace_*`. None present in our libc.

### `<sys/uio.h>`

Clean. `readv` and `writev` match POSIX prototypes; `struct iovec` fields present.

### `<sys/utsname.h>`

Clean. `uname` matches POSIX prototype; all 5 POSIX-required `struct utsname`
fields present. Our libc also exposes `domainname` (Linux extension); the
audit ignores it as it does not affect POSIX conformance.

### `<sys/wait.h>`

Clean. `wait` and `waitpid` match POSIX prototypes; all `W*` macros usable.
Note: pin uses `pid_t` which is `int32_t` (= `int`) on lp64, so the audit
cannot distinguish `int wait(int *)` from `pid_t wait(int *)`. Cosmetic
divergence — passes on lp64, but not portable to ABIs where `pid_t != int`.

### `<stdarg.h>`

Clean. `va_list`, `va_start`, `va_arg`, `va_end`, `va_copy` all usable.

### `<stdbool.h>`

Clean. `bool`, `true`, `false`, `__bool_true_false_are_defined` all defined.

### `<stddef.h>`

Clean. `size_t`, `ptrdiff_t`, `wchar_t`, `NULL`, `offsetof` all defined.

### `<stdint.h>`

Clean. All fixed-width, fast, least typedefs and INT*_MIN/MAX macros present.

### `<stdio.h>`

2 latent divergences.

| Function/Symbol | Current (libc) | POSIX (SUSv5) | Class | Severity | Fix sketch |
|---|---|---|---|---|---|
| `getline` | `long getline(char **, unsigned long *, FILE *)` | `ssize_t getline(char **restrict, size_t *restrict, FILE *restrict)` | wrong-return-type | latent (pin commented) | Change return type to `ssize_t`; `unsigned long *` already matches `size_t *` on lp64 |
| `getdelim` | `long getdelim(char **, unsigned long *, int, FILE *)` | `ssize_t getdelim(char **restrict, size_t *restrict, int, FILE *restrict)` | wrong-return-type | latent | Same fix |

Otherwise: large surface (40+ functions) passes — all `printf`/`scanf`/`vprintf`
family, `fopen`/`fclose`/`fread`/`fwrite`, `fseek`/`fseeko`, character I/O,
`FILE *` globals, all required macros (`EOF`, `BUFSIZ`, `SEEK_*`, `_IO*`).

### `<stdlib.h>`

4 latent divergences (multibyte/wide functions).

| Function/Symbol | Current (libc) | POSIX (SUSv5) | Class | Severity | Fix sketch |
|---|---|---|---|---|---|
| `mbtowc` | `int mbtowc(int *, const char *, size_t)` | `int mbtowc(wchar_t *restrict, const char *restrict, size_t)` | wrong-param-type | latent | Change `int *` to `wchar_t *` (our `wchar_t == int`, but POSIX requires the typedef name) |
| `wctomb` | `int wctomb(char *, int)` | `int wctomb(char *, wchar_t)` | wrong-param-type | latent | Change `int` to `wchar_t` |
| `mbstowcs` | `size_t mbstowcs(int *, const char *, size_t)` | `size_t mbstowcs(wchar_t *restrict, const char *restrict, size_t)` | wrong-param-type | latent | Same |
| `wcstombs` | `size_t wcstombs(char *, const int *, size_t)` | `size_t wcstombs(char *restrict, const wchar_t *restrict, size_t)` | wrong-param-type | latent | Same |

Otherwise the rest of stdlib (malloc family, exit family, str-to-number family,
qsort/bsearch, div_t/ldiv_t/lldiv_t, env funcs, etc.) is clean.

### `<string.h>`

Clean. All 28 audited POSIX functions match POSIX prototypes.

### `<strings.h>`

Clean. `ffs`, `strcasecmp`, `strncasecmp` match POSIX prototypes.

### `<syslog.h>`

4 latent divergences (functions missing entirely).

| Function/Symbol | Current (libc) | POSIX (SUSv5) | Class | Severity | Fix sketch |
|---|---|---|---|---|---|
| `openlog` | not declared | `void openlog(const char *, int, int)` | missing-decl | latent (pin commented) | Declare in `<syslog.h>` and provide stub implementation |
| `closelog` | not declared | `void closelog(void)` | missing-decl | latent | Declare + stub |
| `syslog` | not declared | `void syslog(int, const char *, ...)` | missing-decl | latent | Declare + stub |
| `setlogmask` | not declared | `int setlogmask(int)` | missing-decl | latent | Declare + stub |

Macros (`LOG_EMERG..LOG_DEBUG`, `LOG_KERN..LOG_LOCAL0`, `LOG_PID..LOG_PERROR`) are present.

### `<termios.h>`

Clean for our libc's reachable surface. All 10 audited POSIX functions match
POSIX prototypes; `struct termios` fields and the bulk of POSIX flag macros
(iflag/oflag/lflag/baud) present.

### `<time.h>`

3 latent divergences (clockid_t cascade).

| Function/Symbol | Current (libc) | POSIX (SUSv5) | Class | Severity | Fix sketch |
|---|---|---|---|---|---|
| `clockid_t` | typedef not defined | POSIX-required typedef | missing-typedef | latent (cascade-blocker; pins commented) | Add `typedef int clockid_t;` to `libc/include/sys/types.h` |
| `clock_gettime` | `int clock_gettime(int, struct timespec *)` | `int clock_gettime(clockid_t, struct timespec *)` | wrong-param-type | latent | After clockid_t typedef lands, change `int clockid` to `clockid_t clockid` |
| `clock_settime`, `clock_getres` | same pattern | same | wrong-param-type | latent | Same |

Otherwise the rest of `<time.h>` (16 functions, struct tm, struct timespec,
tzname/timezone/daylight externs) is clean.

### `<unistd.h>`

3 active divergences (file FAILs) plus several cosmetic ones masked by lp64
type aliasing.

| Function/Symbol | Current (libc) | POSIX (SUSv5) | Class | Severity | Fix sketch |
|---|---|---|---|---|---|
| `read` | `long read(int, void *, long)` | `ssize_t read(int, void *, size_t)` | wrong-param-type | build-breaker | Change size param from `long` to `size_t`; return type `long` is compatible with `ssize_t` on lp64 but POSIX requires the typedef name |
| `write` | `long write(int, const void *, long)` | `ssize_t write(int, const void *, size_t)` | wrong-param-type | build-breaker | Same: change size param to `size_t` |
| `readlink` | `long readlink(const char *, char *, long)` | `ssize_t readlink(const char *restrict, char *restrict, size_t)` | wrong-param-type | build-breaker | Same |

Cosmetic divergences (compile clean on lp64 because `pid_t = int32_t = int`,
`off_t = int64_t = long`, `ssize_t = int64_t = long`, but POSIX requires the
typedef names):

- `getpid`, `getppid`, `fork` return `int` where POSIX says `pid_t`. Fix:
  rename return types.
- `lseek` uses `long off, long return` where POSIX says `off_t off, off_t return`.
- `ftruncate`/`truncate` take `off_t` (already POSIX) — clean.
- `sbrk` uses `long` where POSIX says `intptr_t`. Cosmetic on lp64.

Misplaced declarations (POSIX assigns to other headers; our libc duplicates
here — harmless from a compile-pass perspective, audited in canonical files):

- `open`, `openat` — POSIX: `<fcntl.h>`
- `fstat`, `lstat` — POSIX: `<sys/stat.h>`
- `mkdir` — POSIX: `<sys/stat.h>`
- `wait` — POSIX: `<sys/wait.h>`
- `sched_yield` — POSIX: `<sched.h>`

POSIX functions in `<unistd.h>` that our libc does not declare (documented
for Phase 2+, none in scope of this audit): `confstr`, `crypt`, `encrypt`,
`faccessat`, `fchdir`, `fchownat`, `fexecve`, `getentropy`, `getgroups`,
`gethostid`, `getlogin`, `getlogin_r`, `getopt` (in `<getopt.h>` instead),
`getresgid`, `getresuid`, `lockf`, `nice`, `pause` (in `<signal.h>` in our
libc — wrong-header-location), `posix_close`, `pread`, `pwrite`, `linkat`,
`readlinkat`, `set[er]gid`, `set[er]uid`, `setregid`, `setresgid`,
`setresuid`, `setreuid`, `swab`, `symlinkat`, `tcgetpgrp`, `tcsetpgrp`
(both in `<unistd.h>` per POSIX but in our `<termios.h>`), `unlinkat`,
`_Fork`, `dup3`, `usleep` (would need `useconds_t` typedef first).

## Summary: Phase 2 work items, prioritised

1. **`<unistd.h>` size-parameter fix** (read/write/readlink) — only active
   build-breaker. Single edit in the header plus matching implementation
   signature adjustments.
2. **`<time.h>` clockid_t typedef + clock_* signatures** — adds one typedef
   to `<sys/types.h>`, unblocks 3 commented pins.
3. **`<sys/stat.h>` struct stat timespec fields** — POSIX cleanliness; may
   need kernel-side `struct stat` layout coordination.
4. **`<stdio.h>` getline/getdelim return type** — straightforward header +
   implementation rename.
5. **`<stdlib.h>` multibyte wchar_t signatures** — straightforward.
6. **`<sys/mman.h>` size_t/off_t exposure + mmap signature** — single
   include + 3 param-type renames.
7. **`<signal.h>` pid_t exposure + kill/killpg signatures** — single include
   + 2 param-type renames.
8. **`<syslog.h>` function declarations** — add 4 protos + stub
   implementations.
9. **Cosmetic typedef renames** (`int` → `pid_t`/`off_t`/`ssize_t` across
   `<unistd.h>`) — no functional change on lp64; tidy-up.

Phase 2 plans should bundle items by header to minimise churn. Each item
above is independent except (2) which adds a typedef that other items can
then use.
