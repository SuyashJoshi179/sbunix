# BusyBox Port Plan

Goal: Run BusyBox on SBUnix linked against in-tree `build/libc.a` (path B). Drives libc completeness — that is the project's main aim.

Strategy: minimal applet set first, expand once stable. Build BB statically, link in-tree libc, drop into tarfs.

## Decisions

| Item | Choice | Notes |
|------|--------|-------|
| Toolchain path | **B** — in-tree libc | Forces real libc work vs musl shortcut |
| Regex applets | **Skip** | Drop grep/sed/awk/expr first cut |
| Time funcs | **Deferred** | Decide before `ls -l`/`date` matter |
| Job control | **Open** | Lie-stubs may suffice; revisit if Ctrl-Z needed |
| BB version | **TBD** | Pin 1.36.1 vs 1.37 |
| BB sourcing | **TBD** | Vendored `third_party/busybox/` vs Makefile fetch |
| Applet list | **TBD signoff** | Proposal: sh, echo, cat, ls, pwd, mkdir, rm, rmdir, mv, cp, ln, true, false, env, kill, sleep, head, tail, wc, dd, touch, test, [ |
| Init | **TBD** | Keep `/init` or `busybox init` |

## Stage 0 audit — DONE

### libc.a state
299 text symbols. `nm` output captured 2026-05-02.

### Kernel syscalls present (custom numbering)
```
1=exit  2=write  3=exec  4=open  5=read  6=close  7=wait  8=getpid  9=fork
10=spawn  11=getppid  12=yield  13=sleep  14=dup  15=dup2  16=lseek
17=fstat  18=getdents64  19=chdir  20=getcwd  21=mkdir  22=unlink
23=pipe  24=execv  25=link  26=rename
70=sbrk  71=mmap  72=munmap
80=clock_gettime  81=gettimeofday  82=nanosleep
90=kill  91=sigaction  92=sigprocmask  93=sigreturn  94=pause
100-105=uid/gid  110=ioctl  111=meminfo  112=readlink  113=lstat
```
Total: 48 cases.

### Kernel gaps (BusyBox impact)

**Critical (must add):**
- `waitpid` with WNOHANG — currently libc fakes via `wait()`, options ignored. Sh polling broken.
- `ppoll` — covers poll/select via libc emulation. Sh interactive read, pipe-driven applets.
- `ftruncate` — `: > file`, `>` redirect.
- `sigsuspend` — sh signal wait.

**Optional (real impl vs lie):**
- `setpgid`/`getpgid`/`setsid`/`getsid` — currently libc lies. Real impl needed only for full job control.
- `symlink` — currently libc ENOSYS. Needed for `ln -s`, BB applet symlink install.

**Defer / lies suffice:**
- `*at` family — libc AT_FDCWD shim works.
- `mknod`/`mkfifo`/`statfs`/`mprotect`/`sysinfo`/`prlimit` — uncommon or lies OK.
- `chmod`/`chown` — libc no-ops fine.
- `uname` — libc fixed-string fine.

### Libc gaps (no kernel work)

**Hard misses needing real impl:**
- stdio input: `getline`, `getdelim`, `sscanf`, `fscanf`, `scanf`, `vsscanf`, `asprintf`, `vasprintf`, `fseeko`, `ftello`
- string: `strsep`, `strchrnul`, `memrchr`, `mempcpy`, `stpcpy`, `stpncpy`, `strcasestr`, `strverscmp`
- path: `realpath`, `mkstemp`, `mkdtemp`
- time: `localtime`, `gmtime`, `mktime`, `strftime`, `asctime`, `ctime`, `difftime`, `tzset`
- io mux: `poll`, `ppoll`, `select`, `pselect` (depends on kernel ppoll)
- pattern: `glob`, `fnmatch`
- proc: `wait3`, `wait4`, `waitid`
- misc: `strsignal`, `setlocale` (stub OK)

**Headers absent:**
`poll.h`, `sys/select.h`, `sys/uio.h`, `sched.h`, `glob.h`, `fnmatch.h`, `regex.h`, `locale.h`, `langinfo.h`, `wchar.h` (stub).

### Filesystem gaps
- tarfs skips typeflag '2' (symlinks) at `kernel/fs/tarfs.c:370`. BusyBox install layout is N symlinks → busybox.
- procfs has only uptime/meminfo/version/cpuinfo. No `/proc/<pid>/`. Can ship without; only `ps`/`pidof` need it.

## Strategy: data-driven reactive kernel work

Principle: **minimal-not-hacky**. Real impls with narrow contracts, no silent lies. Avoid speculative kernel infra before runtime proves need.

Approach: complete guaranteed libc/fs gaps first, attempt BB build, let link-errors and runtime ENOSYS log drive remaining work.

## Stage list (revised)

| # | Stage | State | Notes |
|---|-------|-------|-------|
| 0 | Kernel syscall + libc gap audit | **DONE** | This doc |
| 1 | Easy libc string/path additions | **DONE** | `libc/string_ext.c` + `libc/path.c`. Added: strsep, strchrnul, memrchr, mempcpy, stpcpy, stpncpy, strcasestr, strverscmp, realpath (lexical only), mkstemp, mkdtemp |
| 2 | Libc stdio scanf family | **DONE** | `libc/scanf.c` + extensions to `libc/printf.c`. Added: getdelim, sscanf, fscanf, scanf, vsscanf, vfscanf, vscanf, asprintf, vasprintf, fseeko, ftello. Real ungetc (1-char pushback). vsnprintf count-only fix. No %f scanf (no float). |
| 5 | Libc glob/fnmatch | **DONE** | `libc/fnmatch.c` + `libc/glob.c`. POSIX shell wildcards. No GLOB_BRACE, no GLOB_TILDE (no $HOME) |
| 6 | tarfs symlink support | **DONE** | typeflag '2' → I_LNK inode, readlink op, DT_LNK in getdents. namei + sys_readlink already symlink-aware |
| 8 | Build BusyBox against libc.a | **IN PROGRESS** | BB 1.36.1 vendored at `third_party/busybox/`. Wrapper `tools/sbunix-cc.sh` set up. defconfig generated. First build attempts surface header gaps — adding stubs as encountered. Iterations ongoing. |
| 7 | Reactive kernel additions | pending | Only what Stage 8/9 prove needed: candidates waitpid, ppoll, ftruncate, sigsuspend |
| 9 | Runtime triage | pending | Iterate on failures |
| 3 | Libc time funcs | **DEFERRED** | Decision pending — only if `ls -l`/`date` matter |

## Open risks (not yet scoped)

- `vsnprintf` may lack `%a`, `%ls`, `%lc`, positional `%1$s` — BB uses some.
- `malloc` quality under BB heap pressure.
- User stack size for sh recursion.
- BB binary size (1-2 MB) vs tarfs limits — tarfs OK, sbfs 6 KiB cap blocks `/data`.
- crt.S entry compat with BB's `_start` expectations.
- `/etc/passwd`, `/etc/group` need to exist for pwd_grp.c — populate rootfs.

## Test plan (per stage)

TBD. Each stage gets a "verify" entry as it lands. Pattern:
- in-kernel selftest if applicable
- `make qemu` + invoke from shell
- triage syscall-default-case kprintf log

## Status

- Created: 2026-05-02
- Last updated: 2026-05-02
- Current stage: Stages 1, 2, 5, 6 done. Stage 8 in iterative phase (header gaps).
- BB build setup: `cd third_party/busybox && make defconfig && make CC=$REPO/tools/sbunix-cc.sh CROSS_COMPILE=riscv64-unknown-elf-`
- Stubs added so far: byteswap, endian, paths, poll, sched, sys/select, sys/socket, sys/sysmacros, sys/uio
- Stuck on: `sys/param.h` (next iteration). After headers, expect 30-50 link-time libc symbol gaps + kernel ENOSYS during runtime.
- Reorder rationale: deferred all kernel work to reactive Stage 7. fork+exec+wait synchronous already works via libc waitpid fake; only proven runtime ENOSYS triggers kernel additions.
