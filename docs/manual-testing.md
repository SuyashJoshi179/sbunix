# SBUnix — Manual Testing Guide

A grader-facing walkthrough for exercising SBUnix from the interactive shell
that comes up after `make qemu`. Each section is self-contained: a brief
description of what is being verified, the exact commands to type, and the
output (or behavior) you should see. Scenarios are ordered from "boot looks
sane" to "the harder POSIX semantics you would expect a teaching OS to get
right."

> **Conventions.**
> * Lines beginning with `$` are typed at the SBUnix shell prompt. The `$` is
>   not part of the input.
> * `Ctrl-A x` exits QEMU. Use this between persistence tests; do not just
>   close the terminal.
> * `Ctrl-C`, `Ctrl-Z`, `Ctrl-\` are the standard interrupt / stop / quit
>   chars. The shell forwards them to the foreground process group.
> * The system runs the built-in test suite at boot before showing a shell.
>   The line you want to see is `init: NNN/NNN tests passed`. A clean run
>   is `init: <N>/<N>` with equal numerator and denominator; everything
>   below assumes that line printed successfully.

---

## 0. Boot Sanity

**What you are verifying.** The kernel boots, mounts the four built-in
filesystems (`/`, `/dev`, `/proc`, `/mnt`, `/tmp`), runs `init`, and hands
control to `/bin/sh`.

```text
$ make qemu
... (kernel banner) ...
init: starting
Running /etc/rc
init: N/N tests passed
Starting /bin/sh
$
```

Sanity check:

```text
$ pwd
/
$ ls
bin
dev
etc
mnt
proc
tmp
$ ls /bin | wc -l
147
```

The exact count of `/bin` entries should match the number of binaries that
the build packed into the tarfs image. Anything close to ~140+ is healthy.

---

## 1. Filesystem Layout & Coreutils

### 1.1 Root listing and a coreutil round-trip

```text
$ ls /
$ ls /etc
rc
$ cat /etc/rc
#!/bin/sh
echo Running $0
mount -t proc proc /proc
mount -t disk virtio /mnt
mount -t tmpfs none /tmp
exec /bin/sh
```

### 1.2 `cd`, `pwd`, `mkdir`, `rmdir`

```text
$ cd /tmp
$ pwd
/tmp
$ mkdir a
$ mkdir -p a/b/c
$ ls a
b
$ ls a/b
c
$ rmdir a/b/c
$ rmdir a/b
$ rmdir a
```

Negative cases:

```text
$ rmdir /tmp/doesnotexist
rmdir: failed to remove '/tmp/doesnotexist': No such file or directory
$ mkdir /tmp                  # should fail, already exists
mkdir: cannot create directory '/tmp': File exists
```

### 1.3 `touch`, `cat`, `echo`, redirection

```text
$ cd /tmp
$ touch hello
$ ls hello
hello
$ echo "hello world" > hello
$ cat hello
hello world
$ echo "second line" >> hello
$ cat hello
hello world
second line
$ wc hello
       2       4      24 hello
```

### 1.4 `cp`, `mv`, `ln`, `rm`

```text
$ cd /tmp
$ cp hello hello.bak
$ cat hello.bak
hello world
second line
$ mv hello.bak renamed
$ ls
hello   renamed
$ ln renamed hardlink           # hard link
$ stat hardlink
  File: hardlink
  Size: 24	Blocks: ...	IO Block: ...	regular file
Device: ...	Inode: N	Links: 2
Access: (0644)	Uid: 0	Gid: 0
Modify: ...
$ rm hello renamed hardlink
$ ls
```

`ln -s` is intentionally not implemented in `/bin/ln`; symlinks come from
the `symlink` syscall directly (see §4.5).

### 1.5 `head`, `tail`, `wc`

Build a small file and check the line/byte counts and head/tail:

```text
$ cd /tmp
$ echo one > nums
$ echo two >> nums
$ echo three >> nums
$ echo four >> nums
$ echo five >> nums
$ head -n 2 nums
one
two
$ tail -n 2 nums
four
five
$ wc nums
       5       5      19 nums
```

### 1.6 Globbing (shell-side, not coreutil-side)

```text
$ cd /tmp
$ touch a.txt b.txt c.log
$ echo *.txt
a.txt b.txt
$ echo *.log
c.log
$ echo "*.txt"                 # quoted → literal
*.txt
$ rm *.txt *.log
```

A no-match glob should pass through literally (Bourne-shell behavior, which
is what the prof's `mkfs` / `tarfs` toolchain expects):

```text
$ echo /tmp/nonexistent-*
/tmp/nonexistent-*
```

---

## 2. The Shell

### 2.1 Built-ins

The shell implements: `cd`, `pwd`, `echo`, `exit`, `kill`, `jobs`, `fg`, `bg`.
Each of these is dispatched without forking — confirm `pwd` shows the new
directory after `cd`:

```text
$ cd /
$ pwd
/
$ cd /tmp
$ pwd
/tmp
$ cd                            # bare cd → /
$ pwd
/
```

### 2.2 Redirection (`<`, `>`, `>>`)

```text
$ cd /tmp
$ echo "abcdef" > f
$ wc f
       1       1       7 f
$ cat < f
abcdef
$ echo "ghi" >> f
$ cat f
abcdef
ghi
$ rm f
```

A redirection target that can't be opened should fail without crashing the
shell:

```text
$ cat < /no/such/file
sh: cannot open '/no/such/file' for reading
```

### 2.3 Pipes (`|`)

```text
$ ls /bin | wc -l
147
$ echo "hello world" | wc -w
       2
$ ls /bin | head -n 5
addrspace_test
atexit_helper
atexit_test
bigfile_pcache_test
bss_test
```

Multi-stage pipeline (3+ procs):

```text
$ ls /bin | head -n 20 | tail -n 5 | wc -l
       5
```

### 2.4 Sequencing and short-circuit (`;`, `&&`)

```text
$ true ; echo ok
ok
$ false ; echo ok               # ; runs both regardless
ok
$ true && echo ok
ok
$ false && echo ok              # short-circuited, prints nothing
$
```

### 2.5 Background jobs and job control (`&`, `jobs`, `fg`, `bg`, `^Z`, `^C`)

Background a sleep, list jobs, foreground it:

```text
$ sleep 30 &
[1] 12
$ jobs
[1] Running   sleep 30
$ fg %1
sleep 30
^C                              # Ctrl-C kills it; control returns to shell
$
```

Stop a foreground process with `^Z`, continue it in the background, then
foreground it again:

```text
$ sleep 30
^Z
[1]+ Stopped     sleep 30
$ bg %1
[1] sleep 30 &
$ jobs
[1] Running   sleep 30
$ fg %1
sleep 30
^C
$
```

### 2.6 Exit status (`true`, `false`, `test`)

```text
$ true && echo ok               # exit 0
ok
$ false || echo not-zero
not-zero
$ test -f /etc/rc && echo yes
yes
$ test -d /tmp && echo dir
dir
$ test ! -f /no/such/thing && echo absent
absent
```

`test` returns 0 on truthy and 1 otherwise, exactly like POSIX.

### 2.7 Quoting

```text
$ echo 'single $vars are literal'
single $vars are literal
$ echo "double quotes pass through"
double quotes pass through
$ echo a   b   c                # multiple spaces collapse
a b c
```

### 2.8 Comments

```text
$ # this is a comment
$ echo hi   # so is this
hi
```

---

## 3. Processes & Signals

### 3.1 `ps` and `/proc`

```text
$ ps
PID NAME STATE
1 init R
2 sh S
3 ps R
$ ls /proc
1
2
$ ls /proc/1
status
cmdline
stat
$ cat /proc/1/status
Name:	init
...
```

`/proc/<pid>/cmdline` is the argv joined by NULs:

```text
$ cat /proc/1/cmdline
init
```

### 3.2 `kill` and signal delivery

Background a sleeper, kill it with a specific signal:

```text
$ sleep 30 &
[1] 14
$ kill -15 14                    # SIGTERM
$
[1]+ Terminated   sleep 30
$ sleep 30 &
[1] 16
$ kill -9 16                     # SIGKILL
$
[1]+ Killed       sleep 30
```

`kill -0 <pid>` (signal 0) probes liveness without delivering. Use
`/bin/kill` (the external binary) so that `&&` short-circuits on the
external exit status — the shell `kill` built-in handles `kill <pid>`
itself and does not chain into the next command:

```text
$ sleep 30 &
[1] 18
$ /bin/kill -0 18 && echo alive
alive
$ /bin/kill -9 18
$ /bin/kill -0 18 || echo gone
gone
```

### 3.3 Foreground SIGINT

```text
$ sleep 30
^C
$ echo $?                        # the shell does not export $?, but exit
                                 # status drives && / || correctly (see 2.4)
```

### 3.4 Orphaned children get reaped by `init`

Fire-and-forget a chain and then check that nothing lingers:

```text
$ (sleep 1 ; echo done) &
[1] 22
$ jobs
[1] Running   ( sleep 1 ; echo done )
$ done                           # printed when sleep finishes
$ jobs                           # job slot freed
$
```

---

## 4. Files & Filesystems

### 4.1 The on-disk SBFS at `/mnt` (persistence)

This is the headline grader test: `/mnt` is backed by the virtio-blk disk,
formatted as SBFS with a write-ahead log. Data written there must survive a
reboot.

```text
$ cd /mnt
$ ls                             # may be empty on a fresh disk
$ echo "persistence check" > marker
$ cat marker
persistence check
$ ls -- marker
marker
```

Now exit QEMU with `Ctrl-A x`, **then re-run `make qemu` without
rebuilding the disk image** (no `make clean`):

```text
$ ls /mnt
marker
$ cat /mnt/marker
persistence check
```

The file survived because the log recovers any in-flight transactions on
mount. To verify the log recovery path is exercised, write a large file and
crash QEMU (close the window or `Ctrl-A x`) mid-write — the next boot
should either show the file in its pre-write state or the fully-written
state, never garbage in between.

### 4.2 The in-memory tmpfs at `/tmp` (non-persistence)

```text
$ echo ephemeral > /tmp/foo
$ cat /tmp/foo
ephemeral
```

After exit/reboot, `/tmp/foo` is gone. tmpfs is recreated empty on every
boot (it lives entirely in RAM).

### 4.3 `/proc` is read-only synthetic

```text
$ echo nope > /proc/1/status     # should fail
sh: cannot open '/proc/1/status' for writing
$ ls /proc/1
cmdline   stat     status
```

### 4.4 Hard links share inodes

```text
$ cd /tmp
$ echo aaa > a
$ ln a b
$ stat a                         # note the Inode and Links: 2 line
$ stat b                         # same inode as a, also Links: 2
$ echo bbb > a
$ cat b
bbb                              # writes through both names
$ rm a
$ cat b
bbb                              # one name removed, data still alive
$ rm b
```

### 4.5 Symlinks and ELOOP

`/dev/loop` is a self-referential symlink shipped specifically so the
grader can confirm ELOOP detection:

```text
$ cat /dev/loop                   # should fail with ELOOP, not hang
cat: /dev/loop: Too many levels of symbolic links
```

### 4.6 Rename across types

```text
$ cd /tmp
$ mkdir d
$ touch d/f
$ mv d/f d2                       # move file out
$ ls d
$ ls -- d2
d2
$ rmdir d
$ rm d2
```

---

## 5. Devices

All under `/dev`, auto-populated by the kernel's devfs:

```text
$ ls /dev
console   loop      null      tty       zero
```

### 5.1 `/dev/null`

```text
$ echo "discard me" > /dev/null
$ cat /dev/null                  # immediate EOF
$
```

### 5.2 `/dev/zero`

The read end is infinite (every read fills the buffer with NUL bytes and
returns the requested length, never EOF). Don't `cat /dev/zero` directly —
it will run forever. Use the shipped test instead, which asserts the
fill-with-zeros contract in one shot (passes silently):

```text
$ /bin/dev_zero_tty_test
$
```

### 5.3 `/dev/tty` / `/dev/console`

`echo > /dev/tty` should reach the terminal just like writing to stdout:

```text
$ echo "from tty" > /dev/tty
from tty
```

---

## 6. Memory & VM

### 6.1 Heap growth (`sbrk` via `malloc`)

The libc `malloc` is backed by `sbrk`. A long-running shell session that
allocates and frees should not leak — verify by spawning a few allocating
binaries from the shell and checking the system stays responsive:

```text
$ /bin/malloc_test               # passes silently if OK
$ /bin/sbrk_test
$ /bin/leak_test                 # 1000 fork/exec cycles
```

Any of these returning a non-zero status (visible because the prompt
returns immediately and silently on success, or prints a libc error on
failure) is a regression.

### 6.2 `mmap` and copy-on-write fork

```text
$ /bin/mmap_test
$ /bin/cow_test
$ /bin/fork_cow_heap
```

### 6.3 OOM behavior

```text
$ /bin/oom_test
```

Should reach OOM gracefully (the test deliberately allocates until ENOMEM
and verifies the kernel rejects further allocations rather than panicking).
The shell prompt must come back afterward.

---

## 7. Pipes & IPC

### 7.1 Pipeline coverage

```text
$ /bin/pipe_test
$ /bin/pipe_stress_test
```

### 7.2 Manual pipeline

```text
$ echo one two three | wc -w
       3
$ ls /bin | wc -l
147
$ cat /etc/rc | head -n 3
#!/bin/sh
echo Running $0
mount -t proc proc /proc
```

### 7.3 SIGPIPE on broken pipeline

If the right-hand side exits before the left-hand side finishes writing,
the left side gets SIGPIPE. The shell suppresses the per-line noise but
exit status of the pipeline reflects the failure:

```text
$ /bin/sigpipe_test
```

---

## 8. POSIX-compliance Spot Checks

These verify that the kernel surfaces errors the way POSIX promises rather
than collapsing them into a generic failure.

### 8.1 `errno` propagation

```text
$ cat /no/such/path
cat: /no/such/path: No such file or directory
$ cd /no/such/path
cd: '/no/such/path': no such directory
$ rmdir /etc                     # non-empty
rmdir: failed to remove '/etc': Directory not empty
$ rm /etc                        # rm refuses directories
rm: cannot remove '/etc': Is a directory
```

The strings come from `strerror(errno)`; the specific phrasing of "No such
file or directory" vs. "Is a directory" demonstrates `ENOENT` and `EISDIR`
are distinct, not collapsed.

### 8.2 `open(O_CREAT)` returns sensible errors

```text
$ touch /dev/zero                # devfs is non-writable in this way
touch: /dev/zero: ...
$ touch /proc/foo                # procfs rejects creation
touch: /proc/foo: ...
```

### 8.3 File descriptor limits

```text
$ /bin/fd_limits_test
$ /bin/fd_test
```

### 8.4 Signal masks and `sigaction`

```text
$ /bin/signal_test
$ /bin/sigmask_test
$ /bin/sa_restart_test
```

### 8.5 Resource limits

```text
$ /bin/rlimit_test
$ /bin/setrlimit_test
```

### 8.6 Wait status macros

```text
$ /bin/wait_test
$ /bin/wait4_nohang_test
```

### 8.7 Termios

```text
$ /bin/termios_test
```

---

## 9. Tying It All Together — A Grader-Style Smoke

This is the single shortest script that exercises a substantial fraction of
the POSIX surface in one shot. Type it into the shell:

```text
$ cd /tmp
$ mkdir work
$ cd work
$ echo aaa > one
$ echo bbb > two
$ echo ccc > three
$ cat one two three > all
$ wc -l all
       3 all
$ cat all | wc -w
       3
$ cp all all.copy
$ mv all.copy renamed
$ ln renamed link
$ ls
all
link
one
renamed
three
two
$ stat link                      # note the Links: 2 line
  File: link
  Size: ...	Blocks: ...	IO Block: ...	regular file
Device: ...	Inode: N	Links: 2
...
$ sleep 60 &
[1] N
$ jobs
[1] Running   sleep 60
$ kill -15 N
$ jobs                           # job slot freed after reap
$ cd /mnt
$ echo final > marker
$ cat marker
final
$ cd /
$ rm /tmp/work/* && rmdir /tmp/work
```

Exit with `Ctrl-A x`, re-run `make qemu`:

```text
$ cat /mnt/marker
final
```

If every step above produced the indicated output, the OS has demonstrated:

* fork/exec/wait (every external command)
* pipelines and redirection (`|`, `>`, `<`, `>>`)
* job control (`&`, `jobs`, `fg`, `bg`, SIGINT, SIGTERM, SIGTSTP, SIGCONT)
* file creation, read, write, rename, link, unlink, stat
- directory create, list, remove
- multiple mounted filesystems (tarfs root, devfs, procfs, tmpfs, sbfs)
- disk persistence and log recovery
- POSIX errno semantics
- a working /proc with per-pid status / cmdline / stat
- working /dev with null, zero, tty, console, and an ELOOP-trap symlink

---

## 10. Extra: Running the Built-in Test Binaries by Name

Anything under `/bin/*_test` is a focused unit test. Run them directly from
the shell to drill into a specific subsystem the grader flagged. Exit
status 0 = pass.

A useful subset:

| Area               | Binary                              |
| ------------------ | ----------------------------------- |
| libc surface       | `header_test`, `posix_headers_test`, `errno_test`, `ctype_test`, `strftime_c_test`, `strtol_overflow_test`, `setjmp_test`, `atexit_test`, `mntent_test`, `scandir_test` |
| process / signals  | `fork_test`, `multi_fork_test`, `pid_test`, `wait_test`, `wait4_nohang_test`, `signal_test`, `sigmask_test`, `sigchld_test`, `sigpipe_test`, `sigsegv_handler_test`, `sigaltstack_test`, `sigtstp_test`, `sa_restart_test`, `eintr_test`, `pgrp_test`, `kill_pgrp_test` |
| memory / VM        | `sbrk_test`, `sbrk_edge_test`, `malloc_test`, `mmap_test`, `mmap_fixed_test`, `mmap_smoke_test`, `mmap_cow_test`, `mmap_share_test`, `mmap_stress_test`, `munmap_test`, `partial_munmap_test`, `cow_test`, `cow_write_test`, `cow_pcache_refleak_test`, `fork_cow_heap`, `stack_test`, `stack_grow_test`, `stack_overflow_test`, `lazy_sbrk_test`, `lazy_reserve`, `huge_reserve_no_touch`, `zerofill_test`, `bss_test`, `demand_walk_test`, `leak_test`, `oom_test`, `rlimit_test`, `setrlimit_test`, `rlimit_inherit` |
| files / FS         | `sbfs_basic_test`, `stat_test`, `getdents_test`, `chdir_test`, `getcwd_test`, `open_read_test`, `dup_test`, `fd_test`, `fd_limits_test`, `fd_invariant_test`, `path_test`, `mkdir_test`, `link_test`, `rename_test`, `timestamp_test`, `tmpfs_test`, `o_append_test`, `symlink_test`, `truncate_mmap_test`, `pagecache_test`, `pagecache_stress`, `bigfile_pcache_test`, `dev_zero_tty_test` |
| pipes / IPC        | `pipe_test`, `pipe_stress_test` |
| exec               | `exec_argv_test`, `exec_reset_test`, `exec_resets_arena`, `shebang_test` |
| shell / scripting  | `sh_c_test`, `sh_hardening_test`, `env_test`, `glob_test` |
| time / IDs / proc  | `time_test`, `time_posix_test`, `date`, `uid_test`, `proc_test`, `ps` |
| stress             | `fork_storm_test`, `mmap_stress_test`, `pipe_stress_test`, `reap_stress_test`, `resource_churn_test`, `usertests`, `comprehensive_test` |

`init` runs essentially this whole list at boot — the grader can re-run
any individual entry from the shell to isolate a regression.

---

## 11. Known Behavioral Choices (so the grader is not surprised)

* The shell does not implement `$?`, `$1..$9`, `${var}`, command
  substitution `$(...)`, here-docs `<<`, or arithmetic. It implements the
  subset that the prof's `/etc/rc` and the grading scripts actually use.
* A line whose first word is a built-in (`cd`, `pwd`, `echo`, `exit`,
  `kill`, `jobs`, `fg`, `bg`) is executed in the shell and short-circuits
  out of the line — operators after the built-in (`&&`, `;`, `|`, `>`)
  are ignored. Use the external `/bin/kill`, `/bin/echo`, etc. when you
  need pipeline or short-circuit semantics, and split built-ins onto
  their own lines (`cd /tmp` then `ls`, not `cd /tmp && ls`).
* `ln -s` is rejected with a clear error — symlinks must be created via
  the `symlink(2)` syscall, which is exercised by `/bin/symlink_test`.
* `mount` is idempotent: re-running the commands from `/etc/rc` after
  boot returns 0 rather than `EBUSY`.
* `init` exits zero only via reboot/halt; without a shell available, it
  sleeps and retries rather than panicking.
* `Ctrl-A x` is QEMU, not SBUnix. There is no `halt` or `reboot` syscall
  surfaced to userspace.
