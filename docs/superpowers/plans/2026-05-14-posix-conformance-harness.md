# POSIX Conformance Harness — Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a compile-only harness that catches grader-style POSIX-header bugs across every libc-exposed POSIX function, then produce a divergence catalog as the input for Phase 2 fix plans.

**Architecture:** One C test file per POSIX header in `tests/posix/`. Each file `#include`s only the POSIX-mandated header and contains per-function checks that pin our libc's declared prototype to the SUSv5 prototype via a function-pointer assignment, plus struct-field references where applicable. A new `make posix-check` target compiles every test in strict mode (`-Werror -Wstrict-prototypes -Wmissing-prototypes -pedantic -std=c99`). The audit doc is hand-written from the harness output and the existing `docs/susv5-html/basedefs/<header>.h.html` POSIX reference.

**Tech Stack:** `riscv64-unknown-elf-gcc`, GNU Make, ISO C99. Reference: `docs/susv5-html/` (SUSv5, IEEE Std 1003.1-2024).

**Spec:** `docs/superpowers/specs/2026-05-14-posix-conformance-harness-design.md` (commit `6587e9d`).

---

## File Structure

**Created:**

- `tests/posix/README.md` — explains the harness and the per-file pattern.
- `tests/posix/<header>.c` — one file per audited POSIX header (~30 files). Each is self-contained and compiles in isolation. Layout mirrors the header path: `<unistd.h>` → `tests/posix/unistd.c`, `<sys/stat.h>` → `tests/posix/sys_stat.c` (underscore replaces slash because the test dir is flat).
- `docs/posix-audit-2026-05.md` — divergence catalog, written from harness output.

**Modified:**

- `Makefile` — add `posix-check` phony target (and add `tests/posix/` to release-strip rules implicitly: it lives under `tests/` which is already on the strip list per `docs/release-process.md` step 8).

**Headers in scope for Phase 1 (one test file per entry):**

`assert`, `ctype`, `dirent`, `errno`, `fcntl`, `fnmatch`, `glob`, `grp`, `inttypes`, `libgen`, `math`, `poll`, `pwd`, `sched`, `setjmp`, `signal`, `stdarg`, `stdbool`, `stddef`, `stdint`, `stdio`, `stdlib`, `string`, `strings`, `syslog`, `termios`, `time`, `unistd`, `sys/mman`, `sys/resource`, `sys/select`, `sys/stat`, `sys/time`, `sys/times`, `sys/types`, `sys/uio`, `sys/utsname`, `sys/wait`.

**Deferred (per spec scope boundary):** `limits.h`, all network headers (`arpa/`, `net/`, `netdb.h`, `netinet/`, `sys/socket.h`, `sys/un.h`).

---

## Canonical Test Pattern

Every test file follows this exact pattern. Task 2 builds the first one (canary) verbatim; subsequent tasks apply it.

```c
/*
 * POSIX conformance test: <HEADER>
 *
 * Reference: docs/susv5-html/basedefs/<HEADER>.h.html
 *
 * Audited POSIX functions:
 *   - func_a
 *   - func_b
 *   - ...
 *
 * Excluded (non-POSIX functions our libc also declares in this header):
 *   - some_func — Linux extension; not in SUSv5
 *   - ...
 *
 * Excluded (POSIX functions we don't implement; will be tested when added):
 *   - other_func
 *   - ...
 */
#include <HEADER>

/* Prototype pins: each line forces the compiler to check that our libc's
 * declared prototype matches the SUSv5 prototype. Any mismatch (different
 * return type, missing/extra parameter, wrong parameter type) is a
 * compile error. Type names on the left-hand side must be POSIX-mandated
 * typedefs and tags exactly as written in docs/susv5-html/basedefs/<HEADER>.h.html. */
static int (*_pin_func_a)(int, const char *) = func_a;
static ssize_t (*_pin_func_b)(int, void *, size_t) = func_b;

/* Struct-field references: forces resolution of POSIX-required member names
 * from the aggregate types this header declares or exposes. Absence of a
 * required field is a compile error. Skip this block if the header declares
 * no struct types. */
static void _struct_fields(void) {
    struct some_struct s = {0};
    (void)s.required_field_a;
    (void)s.required_field_b;
}

/* Suppress unused-static warnings for the file-scope pointers above. */
static void _refs(void) {
    (void)_pin_func_a;
    (void)_pin_func_b;
    (void)_struct_fields;
}
```

**Why function-pointer pins instead of bare sibling decls:** A bare sibling
declaration `int sib(struct foo *)` introduces `struct foo` as a forward
declaration in local scope. If our header is missing the struct definition, the
sibling decl compiles fine — the bug only surfaces later when something tries
to *use* the struct. A function-pointer assignment forces the compiler to check
that two function types are compatible *now*, which requires that all tag
names resolve to the same type. This is strictly stronger than a sibling decl
and is the only form that reliably catches the grader's specific bug
(opaque-pointer `struct timezone`).

**Spec note:** The spec describes this check as a "sibling declaration." The
function-pointer form is a stronger implementation of the same idea — it forces
the type resolution the spec wants but additionally pins the libc proto to the
POSIX proto. No spec change needed.

---

## Task Ordering Rationale

Foundation first (1–3), then headers ordered by importance to grader-likely test paths (process / FS / signals / IO), then quieter surfaces, then the run-and-document closeout. Each header task is independent after Task 3, so subagent execution can run several in parallel if dispatched that way.

---

## Task 1: Bootstrap — `make posix-check` target + skeleton

**Files:**
- Create: `tests/posix/README.md`
- Create: `tests/posix/.gitkeep`
- Modify: `Makefile` (add posix-check rule)

- [ ] **Step 1: Create `tests/posix/README.md`**

```markdown
# POSIX Conformance Harness

Compile-only tests that pin our libc's declared prototypes to SUSv5 prototypes.
One C file per POSIX header. Run with `make posix-check`.

## How it works

Each `tests/posix/<header>.c` includes one POSIX header and contains:

1. A manifest comment listing audited and excluded functions.
2. Prototype "pins" — function-pointer assignments that force the compiler to
   verify our libc's declaration matches the SUSv5 prototype.
3. Optional struct-field references that fail to compile if a POSIX-required
   field is missing.

A test fails iff our header diverges from POSIX. Failures are expected output
in Phase 1; Phase 2 fix plans land remediations.

## Reference

POSIX prototypes are pulled from `docs/susv5-html/basedefs/<header>.h.html`.
The harness uses strict compile flags so any warning is a failure.
```

- [ ] **Step 2: Create `tests/posix/.gitkeep` (empty file)**

Run: `touch tests/posix/.gitkeep`

Purpose: keep the empty directory in git until Task 2 lands the first test file.

- [ ] **Step 3: Add `posix-check` rule to `Makefile`**

Insert this block immediately after the `.PHONY: all qemu clean thirdparty submit` line and update that line to include `posix-check`:

```makefile
.PHONY: all qemu clean thirdparty submit posix-check

# POSIX conformance harness — compile-only.
# Each tests/posix/*.c is independent; we compile in strict mode and report
# per-file PASS/FAIL. Non-zero exit on any failure. Never a dependency of `all`.
POSIX_TESTS := $(wildcard tests/posix/*.c)
POSIX_CFLAGS := -ffreestanding -fno-builtin -nostdlib -nostdinc \
                -isystem libc/include \
                -march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany \
                -Werror -Wstrict-prototypes -Wmissing-prototypes \
                -Wall -Wextra -pedantic -std=c99

posix-check:
	@fail=0; pass=0; \
	for t in $(POSIX_TESTS); do \
	    if $(CC) $(POSIX_CFLAGS) -c $$t -o /dev/null 2>/tmp/posix-check.err; then \
	        echo "PASS  $$t"; pass=$$((pass+1)); \
	    else \
	        echo "FAIL  $$t"; \
	        sed 's/^/    /' /tmp/posix-check.err; \
	        fail=$$((fail+1)); \
	    fi; \
	done; \
	echo ""; echo "posix-check: $$pass passed, $$fail failed"; \
	rm -f /tmp/posix-check.err; \
	[ $$fail -eq 0 ]
```

- [ ] **Step 4: Verify the rule works with zero test files**

Run: `make posix-check`

Expected output:
```
posix-check: 0 passed, 0 failed
```
Exit status: 0.

- [ ] **Step 5: Commit**

```bash
git add tests/posix/README.md tests/posix/.gitkeep Makefile
git commit -m "feat(tests): add posix-check Makefile target and tests/posix skeleton

Phase 1 of POSIX conformance work. Adds a compile-only harness target
that will run per-header tests in strict mode. No tests authored yet."
```

---

## Task 2: Canary test — `tests/posix/time.c`

This is the FIRST real test and the canonical example. It targets `<time.h>`,
where `gettimeofday` / `struct timezone` were already fixed in PR #96, so
end-to-end PASS is achievable. Subsequent tasks copy this file's structure.

**Files:**
- Create: `tests/posix/time.c`
- Delete: `tests/posix/.gitkeep` (no longer needed once a real file lands)

- [ ] **Step 1: Read POSIX surface for `<time.h>`**

Run: `grep -E "^(int|void|char|time_t|size_t|clock_t|struct|extern)" docs/susv5-html/basedefs/time.h.html`

Note every POSIX prototype shown. The exact return types and parameter types
on the left side become the LHS of each pin.

- [ ] **Step 2: Read our libc's `<time.h>`**

Run: `grep -E "^(int|void|char|time_t|size_t|clock_t|struct|extern)" libc/include/time.h`

Cross-reference: a function that appears in both lists gets a pin. A function
that appears only in our header but not in POSIX goes to the "Excluded:
Linux extension" manifest section. A POSIX function we don't yet implement
goes to "Excluded: not implemented".

- [ ] **Step 3: Create `tests/posix/time.c`**

```c
/*
 * POSIX conformance test: <time.h>
 *
 * Reference: docs/susv5-html/basedefs/time.h.html
 *
 * Audited POSIX functions:
 *   - clock_gettime
 *   - clock_settime
 *   - clock_getres
 *   - nanosleep
 *   - time
 *   - gmtime
 *   - gmtime_r
 *   - localtime
 *   - localtime_r
 *   - mktime
 *   - asctime
 *   - asctime_r
 *   - ctime
 *   - ctime_r
 *   - strftime
 *   - tzset
 *
 * Excluded (non-POSIX functions our libc declares here):
 *   - timegm — non-standard (GNU/BSD); not in SUSv5
 *
 * Excluded (POSIX functions not yet implemented, audit when added):
 *   - clock — ISO C, our libc may expose elsewhere
 *   - difftime
 *   - strptime
 *   - getdate
 *   - clock_nanosleep
 *   - timer_create / timer_delete / timer_settime / timer_gettime
 *
 * Note: gettimeofday() is intentionally NOT audited here. POSIX assigns it
 * to <sys/time.h>; our <time.h> currently re-declares it, which is itself a
 * divergence that the sys/time.c test surfaces. Keeping it out of this file
 * keeps the divergence localised.
 */
#include <time.h>

/* Prototype pins: LHS types come verbatim from
 * docs/susv5-html/basedefs/time.h.html. */
static int        (*_pin_clock_gettime)(clockid_t, struct timespec *) = clock_gettime;
static int        (*_pin_clock_settime)(clockid_t, const struct timespec *) = clock_settime;
static int        (*_pin_clock_getres)(clockid_t, struct timespec *) = clock_getres;
static int        (*_pin_nanosleep)(const struct timespec *, struct timespec *) = nanosleep;
static time_t     (*_pin_time)(time_t *) = time;
static struct tm *(*_pin_gmtime)(const time_t *) = gmtime;
static struct tm *(*_pin_gmtime_r)(const time_t *, struct tm *) = gmtime_r;
static struct tm *(*_pin_localtime)(const time_t *) = localtime;
static struct tm *(*_pin_localtime_r)(const time_t *, struct tm *) = localtime_r;
static time_t     (*_pin_mktime)(struct tm *) = mktime;
static char      *(*_pin_asctime)(const struct tm *) = asctime;
static char      *(*_pin_asctime_r)(const struct tm *, char *) = asctime_r;
static char      *(*_pin_ctime)(const time_t *) = ctime;
static char      *(*_pin_ctime_r)(const time_t *, char *) = ctime_r;
static size_t     (*_pin_strftime)(char *, size_t, const char *, const struct tm *) = strftime;
static void       (*_pin_tzset)(void) = tzset;

/* Struct-field references — POSIX-required members of struct tm and
 * struct timespec must be visible from <time.h>. */
static void _struct_fields(void) {
    struct tm t = {0};
    (void)t.tm_sec; (void)t.tm_min; (void)t.tm_hour;
    (void)t.tm_mday; (void)t.tm_mon; (void)t.tm_year;
    (void)t.tm_wday; (void)t.tm_yday; (void)t.tm_isdst;

    struct timespec ts = {0};
    (void)ts.tv_sec; (void)ts.tv_nsec;
}

/* External-variable pins — POSIX names that <time.h> must expose. */
static char **_pin_tzname  = (char **)&tzname[0];
static long  *_pin_timezone = &timezone;
static int   *_pin_daylight = &daylight;

static void _refs(void) {
    (void)_pin_clock_gettime; (void)_pin_clock_settime; (void)_pin_clock_getres;
    (void)_pin_nanosleep;     (void)_pin_time;
    (void)_pin_gmtime;        (void)_pin_gmtime_r;
    (void)_pin_localtime;     (void)_pin_localtime_r;
    (void)_pin_mktime;        (void)_pin_asctime;       (void)_pin_asctime_r;
    (void)_pin_ctime;         (void)_pin_ctime_r;
    (void)_pin_strftime;      (void)_pin_tzset;
    (void)_struct_fields;
    (void)_pin_tzname; (void)_pin_timezone; (void)_pin_daylight;
}
```

- [ ] **Step 4: Delete the placeholder**

Run: `git rm tests/posix/.gitkeep`

- [ ] **Step 5: Run `make posix-check`**

Run: `make posix-check`

Expected outcomes (note both — interpret reality against this list):

- **Likely PASS** on `_pin_gettimeofday` and `struct timespec` checks since PR #96 fixed those.
- **Possible FAIL** on `clock_gettime` LHS type `clockid_t` — our libc currently declares this as `int clock_gettime(int clockid, ...)`, not `clockid_t`. The pin will fail compile with "initialization from incompatible pointer type."
- **Possible FAIL** on `mktime`'s LHS `struct tm *` constness or signature.
- **Possible FAIL** on `_pin_tzname` if `tzname[]` is missing from our header.

Record exactly which pins fail and which pass. This output is data for the audit doc later.

- [ ] **Step 6: Commit**

```bash
git add tests/posix/time.c
git commit -m "test(posix): canary conformance test for <time.h>

Pins every POSIX function our libc declares in <time.h> to its SUSv5
prototype via function-pointer assignment, plus struct-field references
for struct tm and struct timespec. First test file in the harness;
establishes the per-header pattern."
```

---

## Task 3: Harness sanity — deliberate-fail walkthrough (no commit)

Confirms the diagnostic shape when a pin fails, so subsequent tasks know what
"good FAIL" output looks like. Do NOT commit any change made in this task.

**Files:**
- Modify (temporarily): `tests/posix/time.c`

- [ ] **Step 1: Inject a deliberate failure into the canary**

Edit `tests/posix/time.c` and change the line:
```c
static time_t (*_pin_time)(time_t *) = time;
```
to:
```c
static int (*_pin_time)(time_t *) = time;   /* WRONG: POSIX returns time_t */
```

- [ ] **Step 2: Run `make posix-check`**

Run: `make posix-check`

Expected output contains a line like:
```
FAIL  tests/posix/time.c
    tests/posix/time.c: error: initialization of '...' from incompatible pointer type ...
```
And the rule exits non-zero.

- [ ] **Step 3: Confirm the diagnostic points at the injected line**

Read the error output. Find the gcc error whose file:line matches the line you
edited in Step 1. If that error appears in the harness output, identifies the
broken pin by name, and other test files are unaffected, the harness is
working correctly. (Other errors from pre-existing divergences in `time.c` may
also be present; that is expected and not a harness problem.)

- [ ] **Step 4: Revert the file**

Run: `git checkout -- tests/posix/time.c`

- [ ] **Step 5: Confirm canary passes again**

Run: `make posix-check`. Expected: `PASS  tests/posix/time.c` (modulo any
real divergences that were already there before this task).

- [ ] **Step 6: No commit. Task done.**

---

## Tasks 4 through 22 — header-by-header authoring

Each of the next 19 tasks creates exactly one test file at
`tests/posix/<header>.c`, following Task 2's pattern. The body of every such
task is the same shape; the variable content per task is **(a)** the POSIX
reference file, **(b)** our libc header file, and **(c)** the function-pointer
pin list derived by intersecting the two.

To keep this plan tractable, the per-task steps are identical and shown once
below; each task then names its specific files and any header-specific notes.

### Per-header task step template

```
- [ ] Step 1: Read POSIX surface
  Run: grep -E "^(int|long|ssize_t|size_t|void|char|pid_t|off_t|time_t|mode_t|uid_t|gid_t|clock_t|clockid_t|sigset_t|sig_atomic_t|FILE|struct|extern|wchar_t|wint_t|jmp_buf|sigjmp_buf|nfds_t|fd_set|mqd_t)" docs/susv5-html/basedefs/<reference>.html
  Record the function list and exact prototype text.

- [ ] Step 2: Read our libc surface
  Run: grep -E "^(int|long|ssize_t|size_t|void|char|pid_t|off_t|time_t|mode_t|uid_t|gid_t|clock_t|clockid_t|sigset_t|sig_atomic_t|FILE|struct|extern|wchar_t|wint_t|jmp_buf|sigjmp_buf|nfds_t|fd_set|mqd_t)" libc/include/<our-header>
  Note which POSIX functions we declare, which non-POSIX names we declare,
  and which POSIX functions we omit.

- [ ] Step 3: Author tests/posix/<header>.c
  Apply the pattern from "Canonical Test Pattern" above. The manifest comment
  lists audited / excluded-as-extension / excluded-as-unimplemented. One
  prototype pin per audited function. Struct-field block lists POSIX-required
  members for every struct type the header introduces.

- [ ] Step 4: Run make posix-check
  Run: make posix-check
  Record per-pin PASS/FAIL. A failed pin = a divergence the audit doc will
  list. A failed pin does NOT block this task — the test file's purpose is to
  surface divergences, not to be green.

- [ ] Step 5: Commit
  git add tests/posix/<header>.c
  git commit -m "test(posix): conformance pins for <<header>>"
```

The 19 header tasks follow.

---

### Task 4: `tests/posix/sys_time.c`

**Files:**
- Create: `tests/posix/sys_time.c`

**POSIX reference:** `docs/susv5-html/basedefs/sys_time.h.html`
**Our header:** `libc/include/sys/time.h` (currently a shim that delegates to `<time.h>` — expect divergences).

**Header-specific notes:**
- POSIX assigns `gettimeofday`, `utimes`, `futimes`, `lutimes`, `getitimer`, `setitimer`, `select` to this header. Our shim likely exposes only `gettimeofday` after PR #96.
- Audit `struct timeval` field references here (POSIX requires this header to expose it). If our shim's `#include <time.h>` makes the struct visible transitively, that's a known POSIX-compliant practice but document the dependency in the manifest.
- `struct itimerval` and `struct timezone` are mandated by this header — pin field references for both.

Apply per-header step template.

```bash
git commit -m "test(posix): conformance pins for <sys/time.h>"
```

---

### Task 5: `tests/posix/sys_times.c`

**Files:**
- Create: `tests/posix/sys_times.c`

**POSIX reference:** `docs/susv5-html/basedefs/sys_times.h.html`
**Our header:** `libc/include/sys/times.h`

**Header-specific notes:**
- Single POSIX function: `clock_t times(struct tms *)`. Plus field references for `struct tms` (`tms_utime`, `tms_stime`, `tms_cutime`, `tms_cstime`).
- If we don't implement `times` at all, the test file should be empty except for the manifest noting it as unimplemented.

Apply per-header step template.

```bash
git commit -m "test(posix): conformance pins for <sys/times.h>"
```

---

### Task 6: `tests/posix/unistd.c`

**Files:**
- Create: `tests/posix/unistd.c`

**POSIX reference:** `docs/susv5-html/basedefs/unistd.h.html`
**Our header:** `libc/include/unistd.h`

**Header-specific notes:**
- Large surface (~50 POSIX functions). Many divergences expected based on initial inspection: our header declares `long write(int, const void *, long)` where POSIX requires `ssize_t write(int, const void *, size_t)`. Similar for `read`, `lseek`, `pread`, `pwrite`. `fork` returns `int` not `pid_t`. `getpid` returns `int` not `pid_t`. `wait` is in `sys/wait.h` per POSIX, not `unistd.h`.
- Manifest must list: audited (`read`, `write`, `close`, `open`, `lseek`, `fork`, `getpid`, `getppid`, `chdir`, `getcwd`, `mkdir`, `unlink`, `pipe`, `execv`, `execve`, `execvp`, `dup`, `dup2`, `isatty`, `getuid`, `geteuid`, `getgid`, `getegid`, `getpgrp`, `setpgid`, `setsid`, `link`, `symlink`, `readlink`, `rmdir`, `access`, `ftruncate`, `truncate`, `sync`, `fsync`, `_exit`, `usleep`, `sleep`, ...).
- Excluded non-POSIX: anything specific to our kernel (`sleep_ms`, `getdents64`, etc.).

Apply per-header step template.

```bash
git commit -m "test(posix): conformance pins for <unistd.h>"
```

---

### Task 7: `tests/posix/sys_wait.c`

**Files:** Create `tests/posix/sys_wait.c`
**POSIX reference:** `docs/susv5-html/basedefs/sys_wait.h.html`
**Our header:** `libc/include/sys/wait.h`

**Notes:** POSIX functions: `wait`, `waitpid`, `waitid`. Plus the macros `WIFEXITED`, `WEXITSTATUS`, `WIFSIGNALED`, `WTERMSIG`, `WIFSTOPPED`, `WSTOPSIG`, `WIFCONTINUED`, `WNOHANG`, `WUNTRACED`, `WCONTINUED`. Macros can't be function-pin'd; instead reference them in a `_macro_checks(void)` function that uses each in an `if` or assigns to an `int`:

```c
static void _macro_checks(int s) {
    int x = 0;
    x |= WIFEXITED(s);
    x |= WEXITSTATUS(s);
    x |= WIFSIGNALED(s);
    x |= WTERMSIG(s);
    x |= WIFSTOPPED(s);
    x |= WSTOPSIG(s);
    x |= WIFCONTINUED(s);
    x |= WNOHANG;
    x |= WUNTRACED;
    x |= WCONTINUED;
    (void)x;
}
```

Apply per-header step template plus the macro block.

```bash
git commit -m "test(posix): conformance pins for <sys/wait.h>"
```

---

### Task 8: `tests/posix/sys_utsname.c`

**Files:** Create `tests/posix/sys_utsname.c`
**POSIX reference:** `docs/susv5-html/basedefs/sys_utsname.h.html`
**Our header:** `libc/include/sys/utsname.h`

**Notes:** One function `uname(struct utsname *)`. Struct fields: `sysname`, `nodename`, `release`, `version`, `machine` (all char arrays, sizes implementation-defined).

Apply per-header step template.

```bash
git commit -m "test(posix): conformance pins for <sys/utsname.h>"
```

---

### Task 9: `tests/posix/signal.c`

**Files:** Create `tests/posix/signal.c`
**POSIX reference:** `docs/susv5-html/basedefs/signal.h.html`
**Our header:** `libc/include/signal.h`

**Notes:** Large surface. POSIX functions include `kill`, `killpg`, `raise`, `sigaction`, `sigprocmask`, `sigpending`, `sigsuspend`, `sigwait`, `sigaltstack`, `pthread_kill` (skip — no pthreads), `pthread_sigmask` (skip), plus the legacy `signal()` (the convoluted `void (*signal(int, void(*)(int)))(int)`).

The `signal()` prototype is tricky to pin. Use this form:
```c
typedef void (*_posix_sighandler_t)(int);
static _posix_sighandler_t (*_pin_signal)(int, _posix_sighandler_t) = signal;
```

Plus pins for the SIG_* constants (`SIG_DFL`, `SIG_IGN`, `SIG_ERR`) as static pointers and reference of `struct sigaction` fields (`sa_handler`, `sa_sigaction`, `sa_mask`, `sa_flags`, `sa_restorer` — note `sa_restorer` is non-POSIX, exclude), `stack_t` fields (`ss_sp`, `ss_size`, `ss_flags`), `siginfo_t` fields (`si_signo`, `si_code`, `si_errno`, `si_pid`, `si_uid`, `si_addr`, `si_status`, `si_value`).

Apply per-header step template.

```bash
git commit -m "test(posix): conformance pins for <signal.h>"
```

---

### Task 10: `tests/posix/sched.c`

**Files:** Create `tests/posix/sched.c`
**POSIX reference:** `docs/susv5-html/basedefs/sched.h.html`
**Our header:** `libc/include/sched.h`

**Notes:** Common POSIX functions: `sched_yield`, `sched_get_priority_min`, `sched_get_priority_max`, `sched_getscheduler`, `sched_setscheduler`, `sched_getparam`, `sched_setparam`, `sched_rr_get_interval`. Struct `sched_param` with field `sched_priority`. Many SBUnix libcs only expose `sched_yield`; manifest the rest as unimplemented.

Apply per-header step template.

```bash
git commit -m "test(posix): conformance pins for <sched.h>"
```

---

### Task 11: `tests/posix/fcntl.c`

**Files:** Create `tests/posix/fcntl.c`
**POSIX reference:** `docs/susv5-html/basedefs/fcntl.h.html`
**Our header:** `libc/include/fcntl.h`

**Notes:** POSIX functions: `creat`, `fcntl`, `open`, `openat`, `posix_fadvise`, `posix_fallocate`. Plus `struct flock` fields (`l_type`, `l_whence`, `l_start`, `l_len`, `l_pid`). The `open()` LHS pin must use `mode_t` for the optional third argument — represent it as `int (*)(const char *, int, ...)` since variadic and POSIX uses `mode_t` as the documented third arg.

Macros to reference: `O_RDONLY`, `O_WRONLY`, `O_RDWR`, `O_CREAT`, `O_EXCL`, `O_TRUNC`, `O_APPEND`, `O_NOCTTY`, `O_NONBLOCK`, `O_CLOEXEC`, `F_DUPFD`, `F_GETFD`, `F_SETFD`, `F_GETFL`, `F_SETFL`, `F_GETLK`, `F_SETLK`, `F_SETLKW`, `AT_FDCWD`, `AT_SYMLINK_NOFOLLOW`.

Apply per-header step template plus a `_macro_checks(void)` referencing each constant.

```bash
git commit -m "test(posix): conformance pins for <fcntl.h>"
```

---

### Task 12: `tests/posix/sys_stat.c`

**Files:** Create `tests/posix/sys_stat.c`
**POSIX reference:** `docs/susv5-html/basedefs/sys_stat.h.html`
**Our header:** `libc/include/sys/stat.h`

**Notes:** POSIX functions: `stat`, `fstat`, `lstat`, `fstatat`, `chmod`, `fchmod`, `fchmodat`, `mkdir`, `mkdirat`, `mkfifo`, `mkfifoat`, `mknod`, `mknodat`, `umask`, `utimensat`, `futimens`.

`struct stat` required fields: `st_dev`, `st_ino`, `st_mode`, `st_nlink`, `st_uid`, `st_gid`, `st_rdev`, `st_size`, `st_atim` (struct timespec), `st_mtim`, `st_ctim`, `st_blksize`, `st_blocks`.

Macros: `S_IFMT`, `S_IFREG`, `S_IFDIR`, `S_IFCHR`, `S_IFBLK`, `S_IFIFO`, `S_IFLNK`, `S_IFSOCK`, `S_ISREG`, `S_ISDIR`, ..., `S_IRWXU`, `S_IRUSR`, `S_IWUSR`, `S_IXUSR`, and corresponding G/O groups.

Apply per-header step template.

```bash
git commit -m "test(posix): conformance pins for <sys/stat.h>"
```

---

### Task 13: `tests/posix/sys_uio.c`

**Files:** Create `tests/posix/sys_uio.c`
**POSIX reference:** `docs/susv5-html/basedefs/sys_uio.h.html`
**Our header:** `libc/include/sys/uio.h`

**Notes:** Functions: `readv`, `writev`, `preadv`, `pwritev`. `struct iovec` fields: `iov_base` (`void *`), `iov_len` (`size_t`).

Apply per-header step template.

```bash
git commit -m "test(posix): conformance pins for <sys/uio.h>"
```

---

### Task 14: `tests/posix/dirent.c`

**Files:** Create `tests/posix/dirent.c`
**POSIX reference:** `docs/susv5-html/basedefs/dirent.h.html`
**Our header:** `libc/include/dirent.h`

**Notes:** Functions: `opendir`, `fdopendir`, `closedir`, `readdir`, `readdir_r` (deprecated in newer POSIX — manifest accordingly), `rewinddir`, `seekdir`, `telldir`, `dirfd`, `alphasort`, `scandir`.

`struct dirent` POSIX-required field: only `d_name`. Many implementations expose `d_ino`, `d_off`, `d_reclen`, `d_type` but only `d_ino` is on some POSIX revisions and `d_name` is the only mandated one. Pin only `d_name` and note in manifest which extras we expose.

Apply per-header step template.

```bash
git commit -m "test(posix): conformance pins for <dirent.h>"
```

---

### Task 15: `tests/posix/sys_mman.c`

**Files:** Create `tests/posix/sys_mman.c`
**POSIX reference:** `docs/susv5-html/basedefs/sys_mman.h.html`
**Our header:** `libc/include/sys/mman.h`

**Notes:** Functions: `mmap`, `munmap`, `mprotect`, `msync`, `mlock`, `munlock`, `mlockall`, `munlockall`, `posix_madvise`, `shm_open`, `shm_unlink`. Macros: `PROT_READ`, `PROT_WRITE`, `PROT_EXEC`, `PROT_NONE`, `MAP_SHARED`, `MAP_PRIVATE`, `MAP_FIXED`, `MAP_ANON` (POSIX 2024), `MS_ASYNC`, `MS_SYNC`, `MS_INVALIDATE`, `MAP_FAILED`.

Apply per-header step template.

```bash
git commit -m "test(posix): conformance pins for <sys/mman.h>"
```

---

### Task 16: `tests/posix/sys_resource.c`

**Files:** Create `tests/posix/sys_resource.c`
**POSIX reference:** `docs/susv5-html/basedefs/sys_resource.h.html`
**Our header:** `libc/include/sys/resource.h`

**Notes:** Functions: `getrlimit`, `setrlimit`, `getrusage`, `getpriority`, `setpriority`. `struct rlimit` fields: `rlim_cur` (`rlim_t`), `rlim_max` (`rlim_t`). `struct rusage` fields: at minimum `ru_utime` (`struct timeval`), `ru_stime`. Macros: `RLIMIT_CORE`, `RLIMIT_CPU`, `RLIMIT_DATA`, `RLIMIT_FSIZE`, `RLIMIT_NOFILE`, `RLIMIT_STACK`, `RLIMIT_AS`, `RLIM_INFINITY`, `RLIM_SAVED_CUR`, `RLIM_SAVED_MAX`, `PRIO_PROCESS`, `PRIO_PGRP`, `PRIO_USER`, `RUSAGE_SELF`, `RUSAGE_CHILDREN`.

Apply per-header step template.

```bash
git commit -m "test(posix): conformance pins for <sys/resource.h>"
```

---

### Task 17: `tests/posix/sys_select.c`

**Files:** Create `tests/posix/sys_select.c`
**POSIX reference:** `docs/susv5-html/basedefs/sys_select.h.html`
**Our header:** `libc/include/sys/select.h`

**Notes:** Functions: `pselect`, `select`. Type `fd_set` (opaque). Macros: `FD_CLR`, `FD_ISSET`, `FD_SET`, `FD_ZERO`, `FD_SETSIZE`. The macros take an `fd_set *` argument — exercise each in a `_macro_checks(int fd, fd_set *s)` function.

Apply per-header step template.

```bash
git commit -m "test(posix): conformance pins for <sys/select.h>"
```

---

### Task 18: `tests/posix/poll.c`

**Files:** Create `tests/posix/poll.c`
**POSIX reference:** `docs/susv5-html/basedefs/poll.h.html`
**Our header:** `libc/include/poll.h`

**Notes:** Functions: `poll`, `ppoll`. `struct pollfd` fields: `fd` (`int`), `events` (`short`), `revents` (`short`). Type `nfds_t`. Macros: `POLLIN`, `POLLPRI`, `POLLOUT`, `POLLRDHUP`, `POLLERR`, `POLLHUP`, `POLLNVAL`, `POLLRDNORM`, `POLLRDBAND`, `POLLWRNORM`, `POLLWRBAND`.

Apply per-header step template.

```bash
git commit -m "test(posix): conformance pins for <poll.h>"
```

---

### Task 19: `tests/posix/termios.c`

**Files:** Create `tests/posix/termios.c`
**POSIX reference:** `docs/susv5-html/basedefs/termios.h.html`
**Our header:** `libc/include/termios.h`

**Notes:** Functions: `tcgetattr`, `tcsetattr`, `tcsendbreak`, `tcdrain`, `tcflush`, `tcflow`, `cfgetispeed`, `cfgetospeed`, `cfsetispeed`, `cfsetospeed`, `cfmakeraw` (BSD extension — exclude), `tcgetpgrp`, `tcsetpgrp`, `tcgetsid`. `struct termios` fields: `c_iflag`, `c_oflag`, `c_cflag`, `c_lflag`, `c_cc[]`. Types: `tcflag_t`, `speed_t`, `cc_t`. Many macros: `TCSANOW`, `TCSADRAIN`, `TCSAFLUSH`, `VEOF`, `VEOL`, `VERASE`, `VINTR`, `VKILL`, `VMIN`, `VTIME`, `BRKINT`, `ICRNL`, `IGNBRK`, ..., `OPOST`, `ONLCR`, ..., `CS8`, `CREAD`, ..., `ECHO`, `ICANON`, `ISIG`, `B0`, `B50`, ..., `B115200`.

Apply per-header step template.

```bash
git commit -m "test(posix): conformance pins for <termios.h>"
```

---

### Task 20: `tests/posix/stdio.c`

**Files:** Create `tests/posix/stdio.c`
**POSIX reference:** `docs/susv5-html/basedefs/stdio.h.html`
**Our header:** `libc/include/stdio.h`

**Notes:** Large surface. Functions: `fopen`, `freopen`, `fdopen`, `fclose`, `fflush`, `fread`, `fwrite`, `fgetc`, `fgets`, `fputc`, `fputs`, `getc`, `getchar`, `putc`, `putchar`, `puts`, `ungetc`, `fseek`, `ftell`, `fseeko`, `ftello`, `rewind`, `fgetpos`, `fsetpos`, `feof`, `ferror`, `clearerr`, `fileno`, `setbuf`, `setvbuf`, `remove`, `rename`, `tmpfile`, `tmpnam`, `mktemp` (deprecated — exclude), `printf`, `fprintf`, `sprintf`, `snprintf`, `vprintf`, `vfprintf`, `vsprintf`, `vsnprintf`, `scanf`, `fscanf`, `sscanf`, `vscanf`, `vfscanf`, `vsscanf`, `getline`, `getdelim`, `perror`, `popen` (XSI — pin if we have it), `pclose`, `dprintf`, `vdprintf`.

`FILE` is opaque. `fpos_t` opaque. Macros: `BUFSIZ`, `EOF`, `FILENAME_MAX`, `FOPEN_MAX`, `L_tmpnam`, `SEEK_SET`, `SEEK_CUR`, `SEEK_END`, `TMP_MAX`, `_IOFBF`, `_IOLBF`, `_IONBF`, `NULL`. Global `FILE *` pins for `stdin`, `stdout`, `stderr`.

Variadic functions: pin by direct address comparison — `static int (*_pin)(const char *, ...) = printf;` is correct C.

Apply per-header step template.

```bash
git commit -m "test(posix): conformance pins for <stdio.h>"
```

---

### Task 21: `tests/posix/stdlib.c`

**Files:** Create `tests/posix/stdlib.c`
**POSIX reference:** `docs/susv5-html/basedefs/stdlib.h.html`
**Our header:** `libc/include/stdlib.h`

**Notes:** Functions: `malloc`, `calloc`, `realloc`, `free`, `aligned_alloc`, `posix_memalign`, `exit`, `_Exit`, `abort`, `atexit`, `at_quick_exit` (skip if missing), `quick_exit`, `getenv`, `setenv`, `unsetenv`, `putenv`, `system`, `mkstemp`, `mkdtemp`, `realpath`, `atoi`, `atol`, `atoll`, `atof`, `strtol`, `strtoll`, `strtoul`, `strtoull`, `strtod`, `strtof`, `strtold`, `qsort`, `bsearch`, `rand`, `srand`, `random`, `srandom`, `abs`, `labs`, `llabs`, `div`, `ldiv`, `lldiv`.

Types: `div_t` (with `quot`, `rem`), `ldiv_t`, `lldiv_t`, `size_t`, `wchar_t`. Macros: `EXIT_FAILURE`, `EXIT_SUCCESS`, `RAND_MAX`, `MB_CUR_MAX`, `NULL`.

Apply per-header step template.

```bash
git commit -m "test(posix): conformance pins for <stdlib.h>"
```

---

### Task 22: `tests/posix/string.c`

**Files:** Create `tests/posix/string.c`
**POSIX reference:** `docs/susv5-html/basedefs/string.h.html`
**Our header:** `libc/include/string.h`

**Notes:** Functions: `memccpy`, `memchr`, `memcmp`, `memcpy`, `memmove`, `memset`, `stpcpy`, `stpncpy`, `strcat`, `strchr`, `strcmp`, `strcoll`, `strcpy`, `strcspn`, `strdup`, `strerror`, `strerror_r`, `strlen`, `strncat`, `strncmp`, `strncpy`, `strndup`, `strnlen`, `strpbrk`, `strrchr`, `strsignal`, `strspn`, `strstr`, `strtok`, `strtok_r`, `strxfrm`. Type `size_t`, `locale_t` (skip if missing).

Apply per-header step template.

```bash
git commit -m "test(posix): conformance pins for <string.h>"
```

---

## Task 23: Batch trivials — `strings`, `ctype`, `setjmp`, `fnmatch`, `glob`, `libgen`, `inttypes`, `syslog`, `assert`, `errno`, `stdarg`, `stdbool`, `stddef`, `stdint`, `math`, `pwd`, `grp`, `sys_types`

These headers are small or have small POSIX surfaces. One file each, batched in one task. Each follows the same per-header step template.

**Files (each created):**
- `tests/posix/strings.c` — `strcasecmp`, `strncasecmp`, `ffs`, `index`, `rindex`.
- `tests/posix/ctype.c` — `isalnum`, `isalpha`, `isascii`, `isblank`, `iscntrl`, `isdigit`, `isgraph`, `islower`, `isprint`, `ispunct`, `isspace`, `isupper`, `isxdigit`, `tolower`, `toupper`.
- `tests/posix/setjmp.c` — `setjmp`, `longjmp`, `sigsetjmp`, `siglongjmp`. Types `jmp_buf`, `sigjmp_buf`.
- `tests/posix/fnmatch.c` — `fnmatch`. Macros `FNM_PATHNAME`, `FNM_PERIOD`, `FNM_NOMATCH`, `FNM_NOESCAPE`.
- `tests/posix/glob.c` — `glob`, `globfree`. `struct glob_t` fields `gl_pathc`, `gl_pathv`, `gl_offs`. Macros `GLOB_ERR`, `GLOB_MARK`, `GLOB_NOSORT`, `GLOB_NOCHECK`, `GLOB_DOOFFS`, `GLOB_APPEND`, `GLOB_NOMATCH`, `GLOB_ABORTED`, `GLOB_NOSPACE`.
- `tests/posix/libgen.c` — `basename`, `dirname`.
- `tests/posix/inttypes.c` — `imaxabs`, `imaxdiv`, `strtoimax`, `strtoumax`, `wcstoimax`, `wcstoumax`. Type `intmax_t`, `uintmax_t`, `imaxdiv_t` (fields `quot`, `rem`). Macros: skip the giant `PRI*`/`SCN*` family (low value; manifest as deferred).
- `tests/posix/syslog.c` — `openlog`, `closelog`, `syslog`, `setlogmask`. Macros `LOG_EMERG`, `LOG_ALERT`, `LOG_CRIT`, `LOG_ERR`, `LOG_WARNING`, `LOG_NOTICE`, `LOG_INFO`, `LOG_DEBUG`, `LOG_USER`, etc.
- `tests/posix/assert.c` — manifest-only: `assert` is a macro and `static_assert` is a keyword in C11. Note in manifest that `assert(1)` and `assert(0)` cannot be tested at compile time without runtime. Provide an empty test body and a comment explaining no compile-time pins are applicable.
- `tests/posix/errno.c` — pins for the global `int *errno` accessor (POSIX requires `errno` to be a modifiable lvalue; pin `errno` as referenceable). Macro references for every standard errno value POSIX requires (`E2BIG`, `EACCES`, `EADDRINUSE`, ..., `EXDEV`) — full list in basedefs file.
- `tests/posix/stdarg.c` — pin `va_list` typedef visibility. Macros `va_start`, `va_end`, `va_arg`, `va_copy` (require a function with varargs to exercise them).
- `tests/posix/stdbool.c` — macros `bool`, `true`, `false`, `__bool_true_false_are_defined`. Reference each in a function.
- `tests/posix/stddef.c` — types `size_t`, `ptrdiff_t`, `wchar_t`, `max_align_t`. Macros `NULL`, `offsetof`.
- `tests/posix/stdint.c` — types `int8_t`, `int16_t`, `int32_t`, `int64_t`, `uint8_t`–`uint64_t`, `int_least*_t`, `int_fast*_t`, `intmax_t`, `intptr_t`, plus the matching INT*_MIN/MAX macros. Pin a variable of each type.
- `tests/posix/math.c` — Math is large (`acos`, `asin`, ..., `sqrt`, `pow`, etc.). Audit only what our libc declares. Many SBUnix libcs ship a partial math.h — manifest the rest as unimplemented.
- `tests/posix/pwd.c` — `getpwnam`, `getpwuid`, `getpwnam_r`, `getpwuid_r`, `getpwent`, `setpwent`, `endpwent`. `struct passwd` fields `pw_name`, `pw_uid`, `pw_gid`, `pw_dir`, `pw_shell`, `pw_passwd`, `pw_gecos`.
- `tests/posix/grp.c` — `getgrnam`, `getgrgid`, `getgrnam_r`, `getgrgid_r`, `getgrent`, `setgrent`, `endgrent`. `struct group` fields `gr_name`, `gr_gid`, `gr_mem`, `gr_passwd`.
- `tests/posix/sys_types.c` — Header is types-only; no function pins. Reference every POSIX typedef in a `_typedef_checks` function: `pid_t`, `uid_t`, `gid_t`, `mode_t`, `off_t`, `size_t`, `ssize_t`, `time_t`, `clock_t`, `clockid_t`, `dev_t`, `ino_t`, `nlink_t`, `blkcnt_t`, `blksize_t`, `id_t`, `suseconds_t`. For each, declare a local variable of that type.

- [ ] **Step 1: Read all 18 basedefs reference pages**

Run: `for h in strings ctype setjmp fnmatch glob libgen inttypes syslog assert errno stdarg stdbool stddef stdint math pwd grp sys_types; do echo "=== $h ==="; grep -E "^(int|long|ssize_t|size_t|void|char|pid_t|off_t|time_t|mode_t|uid_t|gid_t|clock_t|clockid_t|struct|extern|wchar_t|intmax_t|jmp_buf|sigjmp_buf)" docs/susv5-html/basedefs/$h.h.html | head -40; done`

- [ ] **Step 2: For each header, repeat the per-header step template**

(Read our libc header, author the test file, run `make posix-check` per file, observe PASS/FAIL.)

- [ ] **Step 3: Commit each batch of 4-5 files together**

```bash
git add tests/posix/strings.c tests/posix/ctype.c tests/posix/setjmp.c tests/posix/fnmatch.c
git commit -m "test(posix): conformance pins for strings/ctype/setjmp/fnmatch"

git add tests/posix/glob.c tests/posix/libgen.c tests/posix/inttypes.c tests/posix/syslog.c
git commit -m "test(posix): conformance pins for glob/libgen/inttypes/syslog"

git add tests/posix/assert.c tests/posix/errno.c tests/posix/stdarg.c tests/posix/stdbool.c tests/posix/stddef.c tests/posix/stdint.c
git commit -m "test(posix): conformance pins for assert/errno/stdarg/stdbool/stddef/stdint"

git add tests/posix/math.c tests/posix/pwd.c tests/posix/grp.c tests/posix/sys_types.c
git commit -m "test(posix): conformance pins for math/pwd/grp/sys_types"
```

---

## Task 24: Full harness run + machine-readable report

**Files:**
- Create: `build/posix-check.log` (gitignored — artifact, not source)
- Modify: `.gitignore`

- [ ] **Step 1: Add log artifact to `.gitignore`**

```diff
+ build/posix-check.log
```

- [ ] **Step 2: Run the full harness and capture both stdout and stderr**

Run: `make posix-check > build/posix-check.log 2>&1 || true`

The `|| true` is intentional — we expect non-zero exit while divergences are unfixed.

- [ ] **Step 3: Tally counts**

Run: `grep -c "^PASS " build/posix-check.log; grep -c "^FAIL " build/posix-check.log`

Record the two numbers. They become the audit doc's headline.

- [ ] **Step 4: Extract a per-file failure summary**

Run:
```bash
awk '/^FAIL/{f=$2; next} /^    .*error:/{print f"\t"$0}' build/posix-check.log > build/posix-check.failures.txt
wc -l build/posix-check.failures.txt
```

Each line: `<test file path>\t<gcc error>`. This is the raw input for the audit doc.

- [ ] **Step 5: Commit the gitignore change only**

```bash
git add .gitignore
git commit -m "chore: ignore posix-check log artifact"
```

---

## Task 25: Audit doc — skeleton + clean-header entries

**Files:**
- Create: `docs/posix-audit-2026-05.md`

- [ ] **Step 1: Create `docs/posix-audit-2026-05.md` with the document header and per-header skeleton**

```markdown
# POSIX Conformance Audit — 2026-05

Generated from `make posix-check` output run on commit `<HEAD-SHA>`.
Reference: `docs/susv5-html/basedefs/<header>.h.html` (IEEE Std 1003.1-2024).

## Headline

- Total tests: <N>
- PASS: <N>
- FAIL: <N>

## How to read this audit

Each section corresponds to one `tests/posix/<header>.c` test file. A "clean"
header has no divergences and is recorded as a single line. A header with
divergences gets a table:

| Function | Current (libc) | POSIX (SUSv5) | Class | Severity | Fix sketch |

Class is one of: `missing-struct`, `wrong-return-type`, `wrong-param-type`,
`extra-param`, `missing-param`, `wrong-header-location`, `missing-field`,
`missing-macro`, `missing-typedef`.
Severity is one of: `build-breaker`, `latent`, `cosmetic`.

## Per-header sections

### `<assert.h>`

(filled in Task 26)

### `<ctype.h>`

(filled in Task 26)

[... continue for every header in the harness, in alphabetical order:
   assert, ctype, dirent, errno, fcntl, fnmatch, glob, grp, inttypes, libgen,
   math, poll, pwd, sched, setjmp, signal, stdarg, stdbool, stddef, stdint,
   stdio, stdlib, string, strings, sys/mman, sys/resource, sys/select, sys/stat,
   sys/time, sys/times, sys/types, sys/uio, sys/utsname, sys/wait, syslog,
   termios, time, unistd
   each as a level-3 heading.]
```

- [ ] **Step 2: For every header file that recorded zero failures in `build/posix-check.log`, replace its `(filled in Task 26)` placeholder with a one-line clean entry**

Format per clean entry:
```markdown
### `<time.h>`

Clean. All N audited functions match POSIX prototypes; all required struct
fields present.
```

(Substitute `N` with the count from the test file's manifest.)

- [ ] **Step 3: Fill in the headline numbers**

Read the counts you tallied in Task 24, Step 3, and replace `<N>` in the
Headline section.

- [ ] **Step 4: Commit**

```bash
git add docs/posix-audit-2026-05.md
git commit -m "docs(posix): audit doc skeleton + clean-header entries"
```

---

## Task 26: Audit doc — fill divergence entries

**Files:**
- Modify: `docs/posix-audit-2026-05.md`

- [ ] **Step 1: For each FAIL line in `build/posix-check.log`, walk the gcc errors and write the divergence row**

For each failed pin, the gcc error already tells you what diverges:
- `initialization from incompatible pointer type` → wrong return type or wrong param type. Diff the LHS (POSIX) from the symbol's declared type to classify.
- `'struct foo' has no member named 'bar'` → `missing-field`.
- `'POSIX_MACRO' undeclared` → `missing-macro`.
- `unknown type name 'pid_t'` → `missing-typedef`.

- [ ] **Step 2: For each divergence, write the row in the per-header section's table**

Example row:
```markdown
| `gettimeofday` | `int gettimeofday(struct timeval *, void *)` | `int gettimeofday(struct timeval *, struct timezone *)` | wrong-param-type | build-breaker | Add `struct timezone` definition to header (already done in PR #96 — verify) |
```

Fix sketches should be one short sentence describing the minimal change. They
are guidance for Phase 2 plans, not implementation prescriptions.

- [ ] **Step 3: For each header section that ends up with divergences, add a short summary sentence above the table noting the count and most severe class**

Example:
```markdown
### `<unistd.h>`

7 divergences. Most are `wrong-return-type` (we declare `long write(...)` where
POSIX requires `ssize_t`) and `wrong-param-type` (we use `long` for sizes and
offsets where POSIX uses `size_t` / `off_t`). All build-breakers for portable
user code.

| Function | ... |
```

- [ ] **Step 4: Spot-check the audit doc against `docs/susv5-html/basedefs/` for two non-trivial headers**

Pick `unistd.h` and `signal.h`. Open `docs/susv5-html/basedefs/unistd.h.html`
and `docs/susv5-html/basedefs/signal.h.html`. For each, confirm that every
POSIX prototype listed is either represented in the audit's "matched" count or
appears in a divergence row. Add any missed entries.

- [ ] **Step 5: Commit**

```bash
git add docs/posix-audit-2026-05.md
git commit -m "docs(posix): fill divergence entries from harness run

Audit catalog with per-header divergence tables, classification, severity,
and fix sketches. Becomes the input for Phase 2 fix plans."
```

---

## Task 27: Close out Phase 1

**Files:** none modified

- [ ] **Step 1: Re-run the harness on a clean checkout to confirm reproducibility**

Run: `git stash && make posix-check > /tmp/repro.log 2>&1; git stash pop`

Diff `/tmp/repro.log` against `build/posix-check.log` (sans dates) — they
should report identical PASS/FAIL counts. If they don't, the harness has
hidden state and must be fixed before Phase 1 closes.

- [ ] **Step 2: Confirm `make` still succeeds**

Run: `make clean && make`. Expected: kernel.elf builds, no harness side
effects on the default target.

- [ ] **Step 3: Push the branch**

```bash
git push -u origin feature/posix-complience
```

Phase 1 deliverable is complete: `tests/posix/*.c` (~37 files),
`make posix-check` target, `docs/posix-audit-2026-05.md`. Phase 2 plans
consume the audit doc to drive per-header fixes.

---

## Notes on running this plan

1. Tasks 4–22 are independent after Task 3 lands. If running under
   `subagent-driven-development`, dispatch them in parallel waves of 2–3 at a
   time (more risks merge conflicts in the Makefile and audit doc skeleton if
   later tasks accidentally touch them).
2. Each header task should take 20–40 minutes for a human implementer once the
   pattern from Task 2 is internalized.
3. The harness is intentionally permissive about expected failures during
   Phase 1. A "passing" Phase 1 = tests authored, harness reproducible, audit
   doc complete — NOT a green `make posix-check`. A green harness is Phase 2's
   exit criterion.
