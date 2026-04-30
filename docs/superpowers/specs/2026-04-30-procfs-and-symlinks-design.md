# procfs and VFS Symlinks — Design

**Status:** Draft for implementation
**Date:** 2026-04-30
**Scope:** Two sub-PRs, landed in order: P1 (VFS symlinks) → P2 (procfs).

## 1. Goals

Add a Linux-style `/proc` pseudo-filesystem so userspace tools (`ps`, `uptime`,
`cat /proc/self/status`) can introspect the running system. POSIX does not
mandate `/proc` itself, but the entries follow Linux conventions closely enough
that standard parsers work.

`/proc/self` is exposed as a real symlink, so we add VFS symlink support as a
prerequisite. Symlinks are useful beyond procfs and are part of the OS roadmap
regardless.

## 2. Non-goals

- Writable procfs (`/proc/sys`, sysctl). All entries are read-only.
- `/proc/<pid>/fd/`, `maps`, `mem`, `exe`, `cwd`. Listed in §8 as future work.
- `O_NOFOLLOW` flag on `open`. Defer until a concrete need appears.
- Symlink creation from userspace (`symlink(2)`). Kernel-internal only for now.
- Multi-hart cpuinfo. Single processor entry until SMP lands.

## 3. Sub-PRs

### P1 — VFS symlink support (~250 LOC, lands first)

Adds the `I_LNK` inode type, `readlink` and `lstat` syscalls, and
symlink-following inside `namei`. Independently testable via a single fixture
symlink in `devfs`.

### P2 — procfs (~500 LOC + `bin/ps`)

Adds `kernel/fs/procfs.c`, mounts `/proc`, exposes the entries listed in §5.
Consumes the symlink machinery from P1 for `/proc/self`.

## 4. P1 — VFS symlinks

### 4.1 Inode type and ops

Extend `kernel/include/inode.h`:

```c
#define I_LNK 3

struct inode_ops {
    /* existing fields ... */
    int  (*readlink)(struct inode *ip, char *buf, uint64_t n);
    void (*release)(struct inode *ip);   /* optional, called when refcnt hits 0 */
};
```

Extend `kernel/include/stat.h`:

```c
#define S_IFLNK  0120000
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
```

`inode_put` calls `ip->ops->release(ip)` when refcnt drops to zero, if the hook
is non-NULL. Existing filesystems (sbfs, tarfs, devfs) leave `release` NULL and
keep their current static-inode behavior.

### 4.2 Path resolution

`namei` in `kernel/fs/vfs.c` is split:

```c
int namei(const char *path, struct inode **out);   /* follows symlinks */
int lnamei(const char *path, struct inode **out);  /* does not follow last */
```

Both share an internal `namei_flags(path, out, nofollow)` that walks the path
component-by-component. After each component resolves to an inode, if the inode
is `I_LNK` and either it is not the final component or `nofollow == 0`, the
walker:

1. Increments a hop counter. If `hops > 8`, returns `-ELOOP`.
2. Calls `ip->ops->readlink` into a 256-byte target buffer.
3. Splices the target in front of the remaining path inside the working buffer.
   If the target is absolute, restart from the root mount; otherwise continue
   from the current directory's parent.
4. Drops the symlink inode and continues the loop.

`SYMLINK_MAX = 8` matches Linux's historical limit and is enough for any sane
chain. The 256-byte target cap matches `PATH_MAX` already used by `namei`.

### 4.3 Syscalls

Two new syscall numbers (allocate next free slots in `syscall.h`):

| Name        | Args                                  | Returns               |
|-------------|---------------------------------------|-----------------------|
| `readlink`  | `const char *path, char *buf, size_t n` | bytes written, or errno |
| `lstat`     | `const char *path, struct stat *st`   | 0 or errno            |

`sys_readlink` resolves the path via `lnamei`. If the inode is not a symlink,
returns `-EINVAL`. Otherwise calls `ops->readlink` into a kernel buffer and
`copyout`s up to `n` bytes. No NUL terminator is written (matches POSIX).

`sys_lstat` resolves via `lnamei` and calls `ops->stat`.

### 4.4 errno

Add `ELOOP` to `kernel/include/errno.h` if not already present.

### 4.5 libc

Add thin wrappers in `lib/libc/`:

```c
ssize_t readlink(const char *path, char *buf, size_t n);
int     lstat(const char *path, struct stat *st);
```

### 4.6 Test fixture and `bin/symlink_test`

`devfs` gains one fixture: an `I_LNK` inode at `/dev/loop` whose `readlink`
returns `"/dev/loop"`. This exists solely as a synthetic ELOOP target so the
symlink test does not depend on procfs.

`bin/symlink_test` covers:

- `readlink("/dev/loop", ...)` returns the literal target.
- `open("/dev/loop", ...)` returns `-ELOOP`.
- `readlink` on a non-symlink (e.g. `/dev/console`) returns `-EINVAL`.
- `readlink` on a missing path returns `-ENOENT`.
- Short buffer: target is N bytes, caller supplies N-1 → returns N-1, no NUL.
- `lstat("/dev/loop")` reports `S_ISLNK`; `stat` on the same path returns
  `-ELOOP` (since the link points at itself).

## 5. P2 — procfs

### 5.1 Layout

```
/proc/
├── self                  symlink → /proc/<caller_pid>
├── uptime                "<seconds>.<centiseconds>\n"
├── meminfo               "MemTotal: %lu kB\nMemFree: %lu kB\n"
├── version               "SBUnix <git-rev> riscv64\n"
├── cpuinfo               "processor: 0\nisa: rv64imafdc\n"
└── <pid>/                one directory per live process
    ├── status            multi-line: Name, State, Pid, PPid, Uid, Gid, VmSize
    ├── cmdline           NUL-separated argv (currently just the exec path + '\0')
    └── stat              compact one-liner, Linux-format prefix
```

All files are read-only. `write` returns `-EROFS`. Each file fits comfortably
in one page; `read` builds the full content into a stack buffer per call and
serves the requested slice.

### 5.2 Inode model

procfs inodes carry a tag rather than a backing-store record. In
`kernel/fs/procfs.c`:

```c
enum proc_kind {
    PK_ROOT, PK_SELF,
    PK_UPTIME, PK_MEMINFO, PK_VERSION, PK_CPUINFO,
    PK_PIDDIR, PK_STATUS, PK_CMDLINE, PK_STAT,
};

struct proc_node {
    struct inode    ino;
    enum proc_kind  kind;
    int             pid;          /* 0 for non-pid-bound nodes */
    uint64_t        generation;   /* snapshot of pcb->generation at lookup */
};

#define PROC_INODE_POOL 64
static struct proc_node pool[PROC_INODE_POOL];
static struct proc_node *freelist;
```

Static entries (`PK_ROOT`, `PK_SELF`, and the five system files) are six
permanent singletons with `refcnt = 1` for the life of the kernel.

Dynamic entries (`PK_PIDDIR`, `PK_STATUS`, `PK_CMDLINE`, `PK_STAT`) are
allocated from the pool on `lookup` and returned to the pool by an
`ops->release` hook when their refcnt reaches zero. Pool exhaustion makes
`lookup` return `-ENOMEM`.

Sixty-four slots covers roughly twelve simultaneously-walked pids, each with
four inodes. `ps` walks pids one at a time, so this is generous.

### 5.3 Pid validity and recycling

Add a `uint64_t generation` field to `struct pcb`, bumped in `proc_alloc`. The
procfs `lookup` records `pcb->generation` at the moment the inode is allocated.
On every read, procfs re-resolves the pcb by pid (via a new
`proc_find_by_pid(int)` helper) and compares generations. If the pcb is gone
or the generation differs, the read returns `-ESRCH`.

This prevents the classic pid-recycle hazard where `ps` opens
`/proc/123/status`, process 123 exits, a new process reuses pid 123, and the
read silently returns the new process's data.

### 5.4 Listing pids in /proc

`procfs_root_getdents` walks `ptable` under the proc lock and emits one
`d_type = DT_DIR` entry per live pcb (name = decimal pid), plus the static
names. Iteration is keyed by ptable index, not by inode pointer, so the listing
is consistent for the duration of the locked walk.

### 5.5 /proc/self

`PK_SELF` is a singleton inode of type `I_LNK`. Its `readlink` formats
`"/proc/%d"` using `current_proc()->pid`. Different callers see different
targets, but the inode itself is shared — `readlink` is per-call, so this is
correct.

### 5.6 Mount point

`/proc` must exist as a directory in the tarfs root image so `mount_fs("/proc",
&proc_root_inode)` can resolve it. Add an empty `proc/.keep` file to the tar
build inputs. `kernel.c` calls `procfs_init` after `devfs_init`, in the same
ordering pattern.

### 5.7 File content formats

`status` example for pid 7:

```
Name:   sh
State:  S
Pid:    7
PPid:   1
Uid:    0
Gid:    0
VmSize: 132 kB
```

`cmdline`: bytes are the NUL-separated argv. Until the kernel preserves full
argv on exec, this is just the exec path followed by a single `'\0'`. Format
matches Linux, so updating to real argv later is additive.

`stat` (one line, Linux-style prefix only):

```
7 (sh) S 1 0 0 0 0 0
```

`uptime`: `<integer_secs>.<centiseconds>\n`, both derived from
`timer_ticks` at 100 Hz.

`meminfo`: `MemTotal:` from `pmem_total()`, `MemFree:` from `pmem_free()`,
both in kilobytes.

`version`: build-time string, formatted as `"SBUnix <rev> riscv64\n"` where
`<rev>` comes from a Makefile-injected define. If unavailable, use
`"SBUnix unknown riscv64\n"`.

`cpuinfo`: literal `"processor: 0\nisa: rv64imafdc\n"`.

### 5.8 `bin/ps`

Minimal `ps`: open `/proc`, getdents, for each pid entry open
`/proc/<pid>/status`, parse the `Name:`, `State:`, and `Pid:` lines, print one
line per process. Errors on individual pids are skipped (race with exit).

### 5.9 `bin/proc_test`

- `getdents("/proc")` lists `self`, `uptime`, `meminfo`, `version`, `cpuinfo`,
  and at least one decimal pid.
- `open("/proc/<self_pid>/status")` reads back a buffer whose `Pid:` line
  matches `getpid()`.
- `read("/proc/<self_pid>/cmdline")` first NUL-terminated chunk equals
  `argv[0]` (currently the exec path).
- `readlink("/proc/self")` returns `/proc/<getpid()>`.
- `open("/proc/999999/status")` returns `-ENOENT`.
- `uptime` second token (after the `.`) parses as 0–99.
- `meminfo` contains the literal substring `"MemFree:"`.
- Stale-pid race: parent forks, child sends its pid via pipe and exits, parent
  waits, then `open("/proc/<dead_pid>/status")` returns `-ENOENT` or `-ESRCH`.
- Leak check: 100 sequential `open`/`close` cycles on `/proc/<self>/status`
  leave `meminfo()` unchanged.

`bin/proc_test` and `bin/ps` are added to the `init.c` test array.

## 6. Build and integration

- New files: `kernel/fs/procfs.c`, `kernel/include/procfs.h`,
  `bin/symlink_test/symlink_test.c`, `bin/proc_test/proc_test.c`,
  `bin/ps/ps.c`.
- Modified files: `kernel/fs/vfs.c`, `kernel/fs/devfs.c`,
  `kernel/include/inode.h`, `kernel/include/stat.h`,
  `kernel/include/errno.h`, `kernel/include/proc.h`,
  `kernel/include/syscall.h`, `kernel/syscall.c`, `kernel/proc.c`,
  `kernel/kernel.c`, `lib/libc/*` (readlink + lstat wrappers),
  `bin/init/init.c`, the kernel `Makefile`, the userland `Makefile`, and the
  tarfs build manifest (add `proc/.keep`).
- `kernel.c` initialization order: `tarfs_init` → `devfs_init` →
  `procfs_init`.

## 7. Risks and mitigations

- **Pid recycle aliasing:** addressed by `pcb->generation` in §5.3.
- **Pool exhaustion under heavy `/proc` walkers:** `-ENOMEM` is a clean failure
  mode; consumers retry. 64 slots is sized for a single-shell system. If a
  future tool walks all pids in parallel, raise the constant.
- **Symlink loops:** `SYMLINK_MAX = 8` cap returns `-ELOOP`. Tested against
  the self-loop fixture.
- **`namei` buffer overflow during target splicing:** the working buffer is
  256 bytes and the target buffer is 256 bytes; concatenation is bounded and
  truncates to `-ENAMETOOLONG`.
- **Concurrent ptable mutation during getdents:** ptable walk runs under the
  existing proc lock; no new locking discipline.

## 8. Future work (tier C, not in this design)

- `/proc/<pid>/fd/N` — needs reverse lookup from FD table to inode/path; FDs
  do not currently store the open path.
- `/proc/<pid>/maps` — straightforward; walk `pcb->vma_list` and format.
- `/proc/<pid>/exe` — needs `pcb->exe_path` set in `do_execve`.
- `/proc/<pid>/cwd` — needs inode-to-path reverse mapping; defer until inodes
  cache their canonical name.
- `/proc/loadavg`, `/proc/stat` system-wide — needs runqueue counters.
- `/proc/mounts` — iterate `mounts[]` in `vfs.c`.
- Writable `/proc/sys/...` — full sysctl tree; large surface, defer
  indefinitely.
- `O_NOFOLLOW`, `AT_SYMLINK_NOFOLLOW`, `symlink(2)`, `symlinkat(2)` —
  add when a real consumer appears.

## 9. Exit criteria

- `bin/symlink_test` and `bin/proc_test` pass under `init`.
- `bin/ps` lists all live processes with correct names and states.
- `cat /proc/uptime`, `cat /proc/meminfo`, `cat /proc/version`,
  `cat /proc/cpuinfo` print sensible content from the shell.
- `readlink /proc/self` from the shell prints the shell's pid.
- 1000-iteration leak loop on `/proc/<self>/status` open/close shows no
  `meminfo()` drift.
- All existing tests in `init.c` still pass.
