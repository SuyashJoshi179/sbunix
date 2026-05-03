# BusyBox Port Plan

Goal: Run BusyBox on SBUnix linked against in-tree `build/libc.a` (path B). Drives libc completeness — that is the project's main aim.

Strategy: minimal applet set first, expand once stable. Build BB statically, link in-tree libc, drop into tarfs.

## TL;DR — current state

**Branch**: `feat/busybox-port-libc`. Now includes merge of `develop` (#45 job control). User handles `git push` to origin.

**What works**:
```
BusyBox v1.36.1 (2026-05-03 02:18:46 UTC) built-in shell (ash)
# ls /
dev bin etc data proc
# echo hello
hello
# mkdir /data/tt
# cd data
# ls
. .. trunc.txt subdir tt
```

ash boots, runs synchronous fork+exec+wait, lists tarfs + sbfs, runs `mkdir`, `echo`, `cd`, `pwd`. Symlinks `/bin/{echo,cat,pwd,sh,true,false}` → `busybox` resolved by tarfs and dispatched by argv[0].

**Active bug** (Stage 9 triage):
- *(none blocking — cat-argv FIXED, bg-jobs/`wait` FIXED via /dev/null + real sigsuspend.)*
- Minor cosmetic: first input char after each `Starting /bin/sh` is dropped (`cat` parsed as `at`, `echo` parsed as `cho`). Subsequent commands fine. Looks like UART/console startup race during fork+exec of /bin/sh — not affecting correctness of any applet, deferred.

**Confirmed working via triage**:
- pipes (1-, 2-, 5-stage), `>` redirect, `<` redirect, here-doc
- `&` background, `wait`, multiple parallel bg jobs
- variable expansion, `$?`, `for`/`if`/`while`, command substitution, subshells
- `sh -c`, fork+exec+wait of external applets

## Decisions locked in

| Item | Choice | Notes |
|------|--------|-------|
| Toolchain path | **B** — in-tree libc | Drives libc completeness (project goal) |
| Regex applets | **Skip** | grep/sed/awk/expr dropped; no `regex.h` needed |
| Time funcs | **Deferred** | Not needed for sh; revisit before `ls -l`/`date` |
| Job control | **Lie-stubs** | `setpgid`/`setsid`/etc. silently succeed in libc; revisit only if Ctrl-Z needed |
| BB version | **1.36.1** | Vendored at `third_party/busybox/` |
| BB sourcing | **Vendored** | One-time bulk add. PR-bloat issue — see "PR Hygiene" below |
| Init | **Custom `bin/init/init.c`** | Spawns `/bin/sh` after running test suite |
| Config | **`allnoconfig` + seed** | Started with `defconfig` (875 toggles); pivoted to `allnoconfig` + `third_party/busybox/configs/sbunix_min.config` for minimal pressure |
| Applet seed | sh, echo, cat, pwd, true, false | Expand applet-by-applet as triage progresses |

## Quick build/run reference

**Full from clean**:
```bash
make clean
make busybox-minconfig                 # gen .config from allnoconfig + seed
cd third_party/busybox && \
  make CC=$PWD/../../tools/sbunix-cc.sh CROSS_COMPILE=riscv64-unknown-elf-
cd ../.. && make                       # builds kernel, copies BB into rootfs/bin
                                       # creates symlinks, regen tarball
```

**Run interactively**:
```bash
make qemu
# Ctrl-A X to exit
```

**Run scripted (recommended for triage)**:
```bash
printf 'echo hi\nls /\nexit\n' | tools/qemu-script.sh 60
```

**Inspect BB binary**:
```bash
riscv64-unknown-elf-readelf -h third_party/busybox/busybox
riscv64-unknown-elf-objdump -d third_party/busybox/busybox_unstripped > /tmp/bb_dis.txt
# locate function at PC X:
awk '/^[0-9a-f]+ <.*>:/{f=$0} /^   X:/{print f; exit}' /tmp/bb_dis.txt
```

**Regen kernel after BB rebuild** (forces tarfs.o regen):
```bash
rm -f build/tarfs.o build/rootfs.tar && make
```

## Stage 0 audit — DONE

### libc.a state
299 text symbols at audit time (2026-05-02). Now larger: scanf, glob, fnmatch, bb_compat additions.

### Kernel syscalls present (custom numbering)
```
1=exit  2=write  3=exec  4=open  5=read  6=close  7=wait  8=getpid  9=fork
10=spawn  11=getppid  12=yield  13=sleep  14=dup  15=dup2  16=lseek
17=fstat  18=getdents64  19=chdir  20=getcwd  21=mkdir  22=unlink
23=pipe  24=execv  25=link  26=rename  27=sigsuspend
70=sbrk  71=mmap  72=munmap
80=clock_gettime  81=gettimeofday  82=nanosleep
90=kill  91=sigaction  92=sigprocmask  93=sigreturn  94=pause
95-99=setpgid/getpgid/getpgrp/setsid/getsid (#45)
100-105=uid/gid  106=wait4 (#45)  110=ioctl  111=meminfo  112=readlink  113=lstat
```
Total: 48 cases.

### Kernel gaps (BusyBox impact)

**Critical (Stage 7 reactive — surfaces when exercised)**:
- ~~`waitpid` with WNOHANG~~ — **DONE via develop merge (#45)**. Real `wait4` syscall (106) with WUNTRACED/WCONTINUED/WNOHANG; libc waitpid routes through it.
- `ppoll` — ash interactive `read`, pipe-driven applets. Currently libc returns `-ENOSYS`. Not yet hit by triage.
- ~~`ftruncate`~~ — `>` redirect verified to work via existing `O_TRUNC` path; no syscall needed for current applets.
- ~~`sigsuspend`~~ — **DONE**. Real `sys_sigsuspend` (27) with atomic mask install. Pause-fallback was hanging BB `wait` builtin; fixed.
- `/dev/null` — **DONE**. Devfs entry added; required by ash background-job stdin redirect.

**Done via develop merge (#45)**:
- `setpgid`/`getpgid`/`getpgrp`/`setsid`/`getsid` — real syscalls 95-99; libc lie-stubs replaced.
- `kill(-pgid)` — pgrp-targeted signal delivery.
- `tcsetpgrp`/`tcgetpgrp` via existing `ioctl(TIOCSPGRP/TIOCGPGRP)`.
- SIGTSTP/SIGTTIN/SIGTTOU/SIGCONT default actions; PROC_STOPPED scheduler integration.

**Optional (lies suffice for now)**:
- `symlink` — libc ENOSYS. Needed for `ln -s` runtime; not for BB applet install (those are in tarball).

**Defer entirely**:
- `*at` family — libc AT_FDCWD shim works.
- `mknod`/`mkfifo`/`statfs`/`mprotect`/`sysinfo`/`prlimit`/`chmod`/`chown`/`uname` — libc lies/no-ops fine.

### Filesystem state
- tarfs: now supports typeflag '2' symlinks (Stage 6). Has `readlink` op + DT_LNK in getdents.
- procfs: only uptime/meminfo/version/cpuinfo. `/proc/<pid>/` not present. `ps`/`pidof`/`top` need it; can ship without.
- sbfs: read-write, mounted at `/data`. 6 KiB/file cap (12 direct blocks) — fine for triage but real workloads hit it.

## Strategy: data-driven reactive kernel work

Principle: **minimal-not-hacky**. Real impls with narrow contracts, no silent lies. Avoid speculative kernel infra before runtime proves need.

Approach: complete guaranteed libc/fs gaps first, attempt BB build, let link-errors and runtime ENOSYS log drive remaining work.

## Stage list

| # | Stage | State | Notes |
|---|-------|-------|-------|
| 0 | Kernel syscall + libc gap audit | **DONE** | This doc |
| 1 | Easy libc string/path additions | **DONE** | `libc/string_ext.c` + `libc/path.c`. strsep, strchrnul, memrchr, mempcpy, stpcpy, stpncpy, strcasestr, strverscmp, realpath (lexical only), mkstemp, mkdtemp |
| 2 | Libc stdio scanf family | **DONE** | `libc/scanf.c` + `libc/printf.c` edits. getdelim, sscanf/fscanf/scanf + v* variants, asprintf/vasprintf, fseeko/ftello. Real `ungetc` (1-char pushback). `vsnprintf` count-only fix. **No `%f` scanf** (no float infra). |
| 5 | Libc glob/fnmatch | **DONE** | `libc/fnmatch.c` + `libc/glob.c`. POSIX shell wildcards. No GLOB_BRACE, no GLOB_TILDE |
| 6 | tarfs symlink support | **DONE** | typeflag '2' → I_LNK inode, readlink op, DT_LNK in getdents. namei + sys_readlink already symlink-aware |
| 8 | Build BusyBox against libc.a | **DONE** | BB 1.36.1 vendored. Wrapper, stub headers, allnoconfig+seed config. Boots into ash prompt. |
| 9 | Runtime triage | **IN PROGRESS** | cat-argv FIXED. Merged develop (#45 job-control). bg jobs + `wait` builtin work after `/dev/null` + real `sys_sigsuspend(27)`. Pipes (5-stage), redirects, `sh -c`, `$?`, command-sub all clean. Next: enable more applets, exercise `read` builtin, drive ppoll. |
| 7 | Reactive kernel additions | partial | Done: wait4(106) via develop, setpgid/getpgid/getpgrp/setsid/getsid (95-99) via develop, sigsuspend(27), /dev/null. Pending: ppoll. |
| 3 | Libc time funcs | **DEFERRED** | Add only if `ls -l`/`date` matter |

## Branch / commit tree

`feat/busybox-port-libc` (off `develop`):

```
docs(plan): mark Stage 8 done — BusyBox ash running           d900d03
feat(init,build): wire BusyBox into rootfs + qemu test runner da6631d
fix(libc): make getcwd POSIX-compliant (returns char *)       7efb254
feat(libc): declare bb_compat helpers in headers              753c52e
feat(libc,build): scaffold libc + tooling for BB build        f29aaba
feat(libc): add bb_compat + stub network/alloca headers       a4c29df
docs(plan): record Stage 1/2/5/6 completion + Stage 8         9b25b95
feat(libc): add stub headers for BusyBox compatibility        b980de9
chore: vendor BusyBox 1.36.1 + SBUnix CC wrapper              08733e4 ← BIG
docs: add BusyBox port plan + stage tracker                   433ec74
feat(tarfs): support tar typeflag '2' symbolic links          22db6a4
feat(libc): add fnmatch + glob for shell pattern matching     f8ebbfa
feat(libc): add scanf family + stdio input helpers            776baef
feat(libc): add string/path extensions for BusyBox port       792c00b
```

## Files added/modified by this work

### Libc — new files
- `libc/string_ext.c` — strsep, strchrnul, memrchr, mempcpy, stpcpy, stpncpy, strcasestr, strverscmp
- `libc/path.c` — realpath (lexical), mkstemp, mkdtemp
- `libc/scanf.c` — sscanf/fscanf/scanf + v* via abstract source/unget
- `libc/glob.c` — pattern walker over opendir+fnmatch
- `libc/fnmatch.c` — POSIX shell wildcard matcher
- `libc/bb_compat.c` — fputs_unlocked + family, dprintf/vdprintf, strsignal, poll/ppoll/sigsuspend stubs

### Libc — modified
- `libc/printf.c` — added `unget` to FILE struct, real ungetc, fread drains pushback, fseek invalidates, asprintf/vasprintf/getdelim/fseeko/ftello, `vsnprintf` count-only mode
- `libc/syscall.c` — POSIX `getcwd` (returns char *, allocates if buf==NULL); variadic `open`
- `libc/include/*.h` — large pile of additions (see below)

### Libc — new headers
- `alloca.h`, `byteswap.h`, `endian.h`, `paths.h`, `poll.h`, `sched.h`
- `sys/select.h`, `sys/socket.h`, `sys/sysmacros.h`, `sys/uio.h`, `sys/un.h`, `sys/param.h`
- `net/if.h`, `arpa/inet.h`, `netinet/in.h`
- `glob.h`, `fnmatch.h`
- `features.h`, `malloc.h`

### Libc — header additions to existing files
- `string.h` — memrchr, mempcpy, strsep, strchrnul, stpcpy, stpncpy, strcasestr, strverscmp, strsignal
- `stdlib.h` — realpath, mkstemp, mkdtemp, alloca macro
- `stdio.h` — getdelim, scanf family, asprintf/vasprintf, dprintf/vdprintf, *_unlocked, fseeko/ftello (off_t)
- `signal.h` — SA_*, extra signals (SIGTSTP/TTIN/TTOU/URG/PROF/WINCH/IO/SYS), siginfo_t, stack_t, sigsuspend decl
- `termios.h` — full B50..B4000000 baud constants
- `netdb.h` — AI_*, NI_* getaddrinfo flags, getnameinfo decl
- `errno.h` — socket-related errnos
- `fcntl.h`, `unistd.h` — variadic `open` decl
- `sys/stat.h` — st_rdev field
- `time.h` — `struct tm`
- `unistd.h` — `getopt.h` include, `getcwd` returns `char *`

### Kernel
- `kernel/fs/tarfs.c` — typeflag '2' symlink support; `readlink` op; DT_LNK in getdents
- `kernel/include/inode.h` — DT_LNK constant

### Build / tooling
- `Makefile` — `busybox-minconfig` target, BB install hook in `build/tarfs.o` rule (copies binary + creates applet symlinks before tarball)
- `tools/sbunix-cc.sh` — CC wrapper (rv64 freestanding flags, links libc.a + crt.S, filters -lm, handles -r partial-link mode, places crt0 first + libc in --start-group)
- `tools/busybox-minconfig.sh` — `make allnoconfig` + apply seed file
- `tools/qemu-script.sh` — pipe stdin to qemu serial with timeout
- `third_party/busybox/configs/sbunix_min.config` — minimal applet seed
- `bin/init/init.c` — fixed `execv("/bin/sh", argv)` (was passing NULL)
- `bin/argvdump/argvdump.c` — argv inspector for triage
- `bin/copyio_test`, `bin/copyio_fuzz_test`, `bin/getcwd_test` — adapted to new POSIX getcwd return type

### Vendored
- `third_party/busybox/` — entire BB 1.36.1 source tree (~2800 files, 360k LOC). Source-of-bloat in PR.

## Stage 9 — fixed

### cat-argv duplicate (resolved)

**Root cause**: BB `GETOPT_RESET()` (in `include/libbb.h:1373`) sets `optind = 0` (glibc convention: "reset state, skip argv[0]"). Our libc `getopt`/`getopt_long` saw `optind == 0`, read `argv[0]` (program name), it didn't begin with `-`, returned `-1` with `optind` unchanged at `0`. BB then did `argc -= optind` (no-op) and applets did `argv += optind` (no-op), leaving `argv[0]` (the program name) as the first "positional" filename → cat tried to open `"cat"`.

**Fix**: `libc/getopt.c` — both `getopt` and `getopt_long` now treat `optind == 0` as a reset request and bump it to `1` (glibc init behavior).

**Verification**: `cat /data/trunc.txt` now prints just `BB` (file content), no `cat: can't open 'cat'` line.

### Seed config relocation
After Stage 8 BB-as-submodule conversion, `third_party/busybox/configs/sbunix_min.config` was wiped (lives inside submodule). Moved to `tools/busybox-min.config`; `tools/busybox-minconfig.sh` updated.

## Stage 9 — current investigation

### Symptom
```
# cat /data/trunc.txt
cat: can't open 'cat': No such file or directory
BB
# 
```

`cat: can't open 'cat'` = error from BB cat applet. cat opens TWO things: "cat" (fails) then the actual file (succeeds, contents printed).

### Hypothesis tree

1. **Most likely**: BB getopt32 returns optind=0 instead of 1 → `argv += optind` is no-op → cat sees `argv[0] = "cat"` (program name) as a filename to read. Then iterates to argv[1] = real file.
   - Verify: run `bin/argvdump/argvdump.c foo bar` from BB sh. If `argc=3` and argv printed correctly, kernel exec is OK. If `argc=2` or argv wrong, kernel/libc bug.
   - If kernel OK: investigate `libc/getopt.c` `getopt()` and `getopt_long()`. Note `getopt32` (BB's wrapper) calls our `getopt_long` with `argc = string_array_len(argv+1) + 1`.
   - Suspect spot: when first non-option is at `argv[optind]`, our getopt returns -1 but does not advance optind (correct POSIX), so `argv += optind` shifts past program name. With initial optind=1, argv += 1 puts argv[0] = real file. So expected behavior is correct — yet output shows it isn't. Need data.

2. **Second most likely**: `bin/cat` symlink resolution causes argv duplication somewhere in setup_user_stack or namei.

3. **Less likely**: Stack frame layout off. crt.S does `ld a0, 0(sp); addi a1, sp, 8; call main`. Kernel `setup_user_stack` puts argc at sp+0, argv pointers from sp+8. Verify with argvdump.

### Active leads (file:line)

- `kernel/syscall.c:737-796` — `setup_user_stack`. Builds [argc][argv0...][NULL][NULL_envp]. Strings copied to bottom of page.
- `libc/getopt.c` — small, easy to single-step mentally. `getopt_long` line 70-97.
- `third_party/busybox/libbb/getopt32.c:529-590` — BB's getopt wrapper. Calls `getopt_long`. Sets optind, then `argc -= optind` and returns.
- `third_party/busybox/coreutils/cat.c:181` — `argv += optind;` — this is where the shift happens. If optind=0 here, argv keeps pointing at argv[0]=program name.

### Next steps for fresh session

1. Build kernel + BB if not current:
   ```bash
   cd third_party/busybox && \
     make CC=$PWD/../../tools/sbunix-cc.sh CROSS_COMPILE=riscv64-unknown-elf-
   cd ../.. && rm -f build/tarfs.o build/rootfs.tar && make
   ```
2. Run argvdump:
   ```bash
   printf '/bin/argvdump foo bar baz\nexit\n' | tools/qemu-script.sh 30
   ```
3. Confirm argc/argv look right at the kernel↔userland boundary.
4. If kernel OK, add a printf in BB cat.c after `argv += optind;` to dump optind + argv[0]. Rebuild BB. Run.
5. If optind wrong, walk through `libc/getopt.c` `getopt_long` for cat's invocation: applet_opts = `"u"` + n/b features, no long opts. argv[1] = `/data/trunc.txt` does not start with `-` → first call returns -1 with optind unchanged at 1. So `argc -= optind` in getopt32 sets argc=1, then `argv += optind` in cat shifts to real filename. Should work.
6. If can't reproduce or hypothesis fails, instrument with `kprintf` in `setup_user_stack` to dump argv strings as they're laid out.

## Stage 9 — known/expected next failures

After cat-argv bug fixed, expect runtime to hit:
1. **`poll`/`ppoll` ENOSYS** — when ash uses `read` builtin or `wait`. Stub in `bb_compat.c` returns -1/ENOSYS.
2. **`waitpid` options dropped** — bg jobs / `wait <pid>`. libc fakes via blocking `wait()`.
3. **`sigsuspend` race** — shell signal handling under load. libc uses pause() fallback.
4. **Time funcs** — `ls -l`, `date`, `touch -d`. Stage 3 is deferred — decide approach when hit.
5. **`vsnprintf` format gaps** — `%5d`, `%-10s`, `%.2f`, `%02d`. BB uses heavily. Will cause garbled output, not crashes.

## Stage 7 (when triggered)

Implementation order recommendation when Stage 9 forces these:
1. **ftruncate** — smallest. SUBSET (len=0 only). Reuse existing `inode_ops->truncate(ip)`. Add `SYS_ftruncate` (proposed nr 27). Non-zero len → -EINVAL.
2. **sigsuspend** — small. New `SYS_sigsuspend` (95). Atomic mask-swap+pause. Mirror existing `sys_pause`. Update `libc/bb_compat.c` to use real syscall.
3. **waitpid** — medium. `SYS_waitpid` (28). Args (pid, *status, options). Support pid==-1, pid>0, WNOHANG. WUNTRACED → -EINVAL. Replace fake in `libc/syscall.c:202-209`.
4. **ppoll** — biggest. `SYS_ppoll` (120). Add `int (*poll)(struct inode *, int events)` to `inode_ops`. Implement for UART (existing wakers in `kernel/drivers/uart.c`), pipe (`kernel/pipe.c`), regular files (always-ready default). Update `libc/bb_compat.c` poll/ppoll to use real syscall.

## Open risks (still applicable)

- `vsnprintf` may lack `%5d`, `%-10s`, `%.2f`, `%02d`, positional `%1$s` — BB uses some. Garbled output, not crashes. See `libc/printf.c:54-102` (`do_format`).
- `malloc` quality under BB heap pressure (BB's `mallopt(M_TRIM_THRESHOLD)` is no-op in our libc).
- User stack size for sh recursion — single 4 KiB page. May need expansion for nested commands.
- BB binary size (75 KB now; grows with applets) vs tarfs limits — tarfs OK, sbfs 6 KiB cap blocks `/data`.
- `/etc/passwd`, `/etc/group` need to exist for `pwd_grp.c` callers — currently rootfs/etc/ has `rc` only. Populate before applets like `id`, `whoami`, `login` are enabled.
- ash uses `bb_busybox_exec_path = "/proc/self/exe"` — falls through to PATH search when /proc/self/exe missing. Slight inefficiency but works.

## PR hygiene

Current PR is **400k LOC / 2800 files** because we vendored BB source wholesale (`third_party/busybox/` is one commit, `08733e4`). Reviewers see all of it.

Options to fix:
1. **Submodule** (recommended): replace `third_party/busybox/` with `.gitmodules` pointing at upstream BB 1.36.1 tag. PR shrinks to ~30 files. Build needs `git submodule update --init`.
2. **Makefile fetch**: add `tools/fetch-busybox.sh` that downloads + extracts + applies any patches. Drop vendor dir. PR shrinks; build needs net first time.
3. **Split PR**: keep vendor commit isolated; reviewer ignores it. Same total files but obvious which commit is bulk.

Recommend (1) — simplest to land + future BB updates are one submodule bump.

## Test plan (per stage)

Pattern as stages land:
- in-kernel selftest if applicable
- `printf 'cmds' | tools/qemu-script.sh 30` for end-to-end
- triage syscall-default-case kprintf log when adding kernel features

## Status — paste-fresh checklist

- Branch: `feat/busybox-port-libc`
- Last working state: BB ash boots, `ls`/`echo`/`mkdir`/`cd`/`pwd` work
- cat-argv bug FIXED (libc/getopt.c optind=0 reset). Next triage: exercise `read` builtin, pipes, `wait`, `>` redirect, `ls -l`, basic scripts to surface poll/waitpid/sigsuspend/ftruncate/time/vsnprintf gaps in that order.
- Test command: `printf 'echo hi\nexit\n' | tools/qemu-script.sh 300` (init test suite + sh take ~250s)
- After fresh session start, read this file then read commits in branch with `git log --oneline develop..HEAD`
