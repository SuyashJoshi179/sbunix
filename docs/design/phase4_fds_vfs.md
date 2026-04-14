# Phase 4 — File Descriptors, VFS, Mount Table, Console Device

## 1. Goal

Give user space a real I/O substrate: per-process file descriptors
pointing at refcounted `struct file`s, a VFS layer with a mount
table, tarfs with proper directory inodes, and `/dev/console` as the
first device-backed inode so `read(0,...)` finally comes from
interactive UART input instead of nowhere.

This is the structural phase every later phase leans on. No shell
yet, no writable disk yet — just the plumbing those need to exist on
top of. Four logical sub-PRs (4a, 4b, 4c, 4d) landing in order.

## 2. Preconditions

- Phase 3 tail (timer preempt, fault-kill, getppid/yield/sleep, leak
  hygiene).
- Existing tarfs reader at `kernel/fs/tarfs.c` that does a path
  lookup across the embedded archive.
- `create_user_pgtable` / `free_user_pgtable` / `uvmcopy` working for
  user address spaces.
- `sys_write` in `kernel/syscall.c` that currently bypasses FDs and
  writes directly to UART.

## 3. Concepts & data structures

### 3.1 `struct file`

Refcounted handle held by the file-descriptor table. One per open()
call; `dup`/`fork` bump the refcount, `close` decrements.

```c
// kernel/include/file.h
typedef enum {
    FD_NONE = 0,
    FD_INODE,       // backed by a VFS inode (regular file, dir, device)
    FD_PIPE,        // Phase 6
} file_type_t;

struct file {
    file_type_t  type;
    int          refcnt;
    uint8_t      readable;
    uint8_t      writable;
    uint64_t     off;         // byte offset; unused for pipes
    struct inode *ip;         // FD_INODE
    struct pipe  *pipe;       // FD_PIPE (Phase 6)
};
```

Global file table is a fixed-size array (`NFILE = 64` is fine). File
allocation is "scan for type==FD_NONE." All access under IRQs-off.

Helpers:

```c
struct file *filealloc(void);                 // refcnt=1
struct file *filedup(struct file *f);         // refcnt++
void         fileclose(struct file *f);       // refcnt--, free on 0
int          fileread(struct file *f, void *dst, uint64_t n);
int          filewrite(struct file *f, const void *src, uint64_t n);
int          filestat(struct file *f, struct stat *st);
int          fileseek(struct file *f, int64_t off, int whence);
```

### 3.2 `struct inode` and inode ops

Every VFS object (regular file, directory, device) is a `struct
inode`. The concrete filesystem owns the inode lifetime but exposes
uniform ops.

```c
// kernel/include/inode.h
struct inode;
struct inode_ops {
    int (*read)    (struct inode *, uint64_t off, void *buf, uint64_t n);
    int (*write)   (struct inode *, uint64_t off, const void *buf, uint64_t n);
    int (*stat)    (struct inode *, struct stat *);
    int (*lookup)  (struct inode *dir, const char *name, struct inode **out);
    int (*getdents)(struct inode *dir, uint64_t off, void *buf, uint64_t n,
                    uint64_t *out_next_off);
    int (*create)  (struct inode *dir, const char *name, int mode,
                    struct inode **out);
    int (*unlink)  (struct inode *dir, const char *name);
    int (*mkdir)   (struct inode *dir, const char *name, int mode);
    int (*rmdir)   (struct inode *dir, const char *name);
    int (*rename)  (struct inode *olddir, const char *oldname,
                    struct inode *newdir, const char *newname);
};

#define I_REG  1
#define I_DIR  2
#define I_CHR  3   // character device (console)

struct inode {
    int               type;
    uint32_t          mode;       // POSIX-style
    uint32_t          uid, gid;   // stubs, always 0 in Phase 4
    uint64_t          size;
    uint64_t          mtime;
    uint32_t          nlink;
    int               refcnt;
    const struct inode_ops *ops;
    void             *fs_data;    // fs-private (e.g. tarfs block ptr)
    struct inode     *mount_child;// if non-null, a mount is rooted here
};
```

Every fs op returns 0 on success or negative errno on failure.
Ops a given fs doesn't support set the slot to a stub that returns
`-ENOSYS`.

`mount_child` is the mount-point tie-in for §3.4.

### 3.3 `struct stat`

ABI struct — picked once, frozen forever. Declared in
`libc/include/sys/stat.h` and mirrored exactly in
`kernel/include/stat.h`.

```c
struct stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_size;
    uint64_t st_atime;
    uint64_t st_mtime;
    uint64_t st_ctime;
};

#define S_IFMT   0170000
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
```

Fits in 64 bytes. Kernel and libc include the same header; avoid
drift by symlinking or by sharing via a `common/` subdir.

### 3.4 Per-process FD table

Added to `struct pcb`:

```c
#define NOFILE 16

struct pcb {
    ...
    struct file *ofile[NOFILE];
    struct inode *cwd;       // current working directory inode (refcounted)
    ...
};
```

- `fork` duplicates each non-null slot via `filedup`.
- `exec` keeps the table (close-on-exec deferred to Phase 9).
- `exit` closes every slot via `fileclose`.

### 3.5 Mount table

Simple fixed-size array:

```c
#define NMOUNT 8

struct mount {
    const char  *path;        // mount point, e.g. "/", "/dev", "/data"
    struct inode *mounted_root;
};

static struct mount mounts[NMOUNT];
int mount_fs(const char *path, struct inode *root);  // returns 0 or -errno
```

Root is always `mounts[0]`, path "/". Later phases add entries for
`/dev` (synthetic) and `/data` (sbfs).

### 3.6 Path resolution

Absolute and relative paths. Algorithm:

```
namei(path):
    if path starts with '/': cur = mounts[0].mounted_root
    else: cur = current->cwd
    for each component in path:
        if cur is not a directory: return -ENOTDIR
        if cur->mount_child: cur = cur->mount_child
        if component is ".": continue
        if component is "..": cur = parent_of(cur)   // Phase 4b deferral?
        let next: cur->ops->lookup(cur, component, &next)
        if error: return error
        cur = next
    return cur
```

"parent_of" across mount points is the annoying case. Simplification:
`..` support lands in Phase 4b *only for same-fs*. Cross-mount `..`
is documented as "returns the mount point itself" (Linux behavior is
"go above the mount," which needs a parent pointer on every inode —
too much for this phase).

### 3.7 Tarfs v2 — real directories

Current `tarfs.c` only supports "find by full path." Phase 4c rewrites
it to expose an inode tree.

Two-pass construction at kernel boot (done once, in `tarfs_init()`,
called from `kernel.c` after `vmem_init`):

1. **Pass 1**: scan headers, build a list of entries
   `{ path, typeflag, size, data_ptr, mode, mtime }`.
2. **Pass 2**: synthesize directory inodes for every path prefix
   that doesn't have an explicit entry. Build a tree: root dir
   inode → children by name.

Data lives in memory allocated from a slab-style pool (`static
struct inode tarfs_inodes[TARFS_MAX_INODES]` — 512 is plenty).
tarfs inodes carry a pointer to the in-ELF data blob (regular
files) or a child-list pointer (directories).

Mode bits come from `parse_octal(h->mode, 8)`. `mtime` from
`h->mtime`. Tarfs stays read-only: `create`, `unlink`, `mkdir`,
`rmdir`, `write` all return `-EROFS`.

### 3.8 Console inode

Synthetic inode for `/dev/console`, exposed before `/dev` is a real
filesystem. For Phase 4d we stake out a tiny "devfs" namespace: one
hardcoded entry, `console`. Full `/dev` mount is Phase 10 stretch.

Shape:

- `type = I_CHR`, `mode = S_IFCHR | 0666`.
- `ops->read` blocks on the UART RX ring buffer in canonical mode
  (line-buffered, echo, backspace).
- `ops->write` calls `write_char` for each byte.
- `ops->stat` returns size=0, nlink=1.
- Other ops: `-ENOSYS` or `-EPERM` as appropriate.

### 3.9 UART RX ring

New in `kernel/drivers/uart.c`:

```c
#define UART_RX_BUFSZ 512

static struct {
    char     buf[UART_RX_BUFSZ];
    uint32_t head, tail;          // reader head, writer tail
} urx;

// Called from the UART IRQ path.
void uart_rx_isr(char c);

// Called from kernel thread context; blocks on wait queue if empty.
int  uart_rx_get(char *out);
```

IRQ wiring: the QEMU `virt` machine routes UART0 to PLIC hart 0
context 1 (S-mode). Need `plic_init` + `plic_claim`/`plic_complete`.
Small enough to live in `kernel/drivers/plic.c`.

PLIC register layout (QEMU virt):

| Offset | Name |
|---|---|
| `0x0c000000 + 4*i` | Source priority for IRQ `i` |
| `0x0c002000` | Pending bits |
| `0x0c002080` | Enable bits for hart 0 M-mode (we don't use) |
| `0x0c002100` | **Enable bits for hart 0 S-mode** — write `1 << UART_IRQ` here |
| `0x0c201000` | Hart 0 M-mode threshold/claim |
| `0x0c201004` | Hart 0 M-mode claim |
| `0x0c202000` | Hart 0 S-mode threshold |
| `0x0c202004` | **Hart 0 S-mode claim/complete** |

UART IRQ number on `virt` is **10** (check against the device tree;
`my_devicetree.dtb` is in the repo).

Enable the UART side: write `1` to UART `IER` (offset 1) bit 0 to
enable received-data-available interrupt.

### 3.10 Canonical line discipline

Stashed inside `uart_rx_get` (simpler than a separate layer for
Phase 4):

- Maintains an in-kernel "current line" buffer plus a ring of
  committed lines.
- On RX char: echo it via `write_char`; on `\r` or `\n` commit the
  line (append `\n`, move to ring).
- On backspace (`0x7f` or `\b`): drop last char from the current
  line, echo `\b \b`.
- On Ctrl-D (`0x04`) at column 0: commit an empty line with EOF
  marker (returns 0 bytes from `read`).
- `uart_rx_get` blocks until the ring has a complete line, then
  returns bytes from the oldest line.

ICANON/ECHO are *always on* in Phase 4. Phase 8 adds termios.

## 4. File-by-file changes

### 4a. Per-process file descriptor table

**New files**

- `kernel/include/file.h` — `struct file`, prototypes.
- `kernel/include/inode.h` — forward decl (full def in Phase 4b).
- `kernel/file.c` — file table, `filealloc`/`filedup`/`fileclose`,
  read/write/stat dispatchers.

**Modified**

- `kernel/include/proc.h` — add `ofile[NOFILE]`, `cwd`.
- `kernel/proc.c`
  - `alloc_proc`: zero ofile table.
  - `proc_fork_current`: `filedup` each open file into the child
    table. `cwd` bumped too.
  - `proc_exit_current`: walk `ofile`, `fileclose` each non-null
    slot. Drop ref on `cwd`.
- `kernel/syscall.c`
  - `sys_open(path, flags, mode)` — resolve via namei, allocate
    file, put in lowest free fd slot.
  - `sys_close(fd)` — fileclose, clear slot.
  - `sys_read(fd, buf, n)` — fileread dispatch.
  - `sys_write(fd, buf, n)` — fileread, **delete the UART
    shortcut**.
  - `sys_dup(fd)` — smallest free slot, filedup.
  - `sys_dup2(oldfd, newfd)` — if oldfd == newfd → noop return
    newfd; else fileclose newfd slot, filedup old into newfd.
  - `sys_lseek(fd, off, whence)` — fileseek.
  - `sys_fstat(fd, *st)` — filestat.
  - `sys_getdents64(fd, buf, n)` — dispatch to inode getdents.
  - `sys_chdir(path)` — namei + replace cwd.
  - `sys_getcwd(buf, n)` — walks parent pointers if available; else
    stub to "/" plus current stored path (see open question §9).

**Syscall numbers (append-only)**

```c
#define SYS_open       14
#define SYS_close      15
#define SYS_read       16
#define SYS_dup        17
#define SYS_dup2       18
#define SYS_lseek      19
#define SYS_fstat      20
#define SYS_getdents64 21
#define SYS_chdir      22
#define SYS_getcwd     23
```

Note: the existing `SYS_open` (4), `SYS_read` (5), `SYS_close` (6)
slots in `kernel/include/syscall.h` are **reserved but unused**.
Use them instead of 14/15/16 so the numbering stays compact. Pick
one scheme and stick to it.

Decision: reuse 4/5/6. Add only open, close, read, dup, dup2, lseek,
fstat, getdents64, chdir, getcwd to the list.

### 4b. VFS with mount table

**New files**

- `kernel/include/stat.h` and `libc/include/sys/stat.h` — mirror
  structs and `S_IFMT` macros.
- `kernel/fs/vfs.c` — `namei`, `mount_fs`, path walk.
- `kernel/fs/mount.c` — mount table.
- `kernel/include/vfs.h` — function prototypes.

**Modified**

- `kernel/include/inode.h` — full definition (see §3.2).
- `kernel/file.c` — file ops forward to inode ops.

**Key function**

```c
int namei(const char *path, struct inode **out);
// Returns 0 on success, -errno on failure.  Refcount on *out bumped.
// Caller must inode_put(*out) when done.

struct inode *inode_get(struct inode *ip);   // refcount++
void          inode_put(struct inode *ip);   // refcount--, free via fs cb
```

### 4c. Tarfs directories

**New files**

- `kernel/fs/tarfs_inode.c` — inode ops for tarfs.

**Modified**

- `kernel/fs/tarfs.c` — add `tarfs_init()` which builds the inode
  tree and calls `mount_fs("/", tarfs_root)`. Old `tarfs_find` stays
  as a fallback used by exec (until exec is converted to go through
  the VFS — do that inside this sub-PR).
- `kernel/include/tarfs.h` — add `tarfs_init`, `tarfs_root`.
- `kernel/kernel.c` — call `tarfs_init()` after `vmem_init()`,
  before `trap_init()`.
- `kernel/exec.c` — `proc_spawn` uses `namei(path)` +
  `inode->ops->read` into a buffer, then calls `load_user_elf` on
  the buffer. **Remove direct `tarfs_find` dependency** from the
  spawn path.

**Getdents record format**

```c
struct dirent64 {
    uint64_t d_ino;
    uint64_t d_off;       // cookie for next getdents call
    uint16_t d_reclen;    // total record size
    uint8_t  d_type;      // DT_REG, DT_DIR, DT_CHR
    char     d_name[];    // null-terminated
};
```

Matches Linux `struct dirent64`. Records are variable-length,
padded to 8-byte alignment. `d_off` is an opaque cookie: for tarfs
it's "index into the directory's child list" — stable because the
tree is built once at boot and never mutated.

### 4d. Console device

**New files**

- `kernel/fs/devfs.c` — synthetic root dir containing `console`.
  `mount_fs("/dev", devfs_root)` at kernel init.
- `kernel/drivers/plic.c`, `kernel/include/drivers/plic.h` — PLIC
  init/claim/complete.

**Modified**

- `kernel/drivers/uart.c` — add RX ring, `uart_rx_isr`,
  `uart_rx_get`, canonical line discipline. Enable RDA interrupt in
  `uart_init`.
- `kernel/include/drivers/uart.h` — prototypes.
- `kernel/trap.c` — `case 9` (S-mode external interrupt): claim via
  PLIC, dispatch to `uart_rx_isr`, complete.
- `kernel/kernel.c` — `plic_init()` + `tarfs_init()` + `devfs_init()`
  + mount `/` and `/dev` before starting the scheduler.
- `bin/init/init.c` — open `/dev/console` three times before exec of
  the first real program (this sets up fd 0/1/2). Exec target
  currently doesn't exist — defer to Phase 6 shell. For Phase 4d,
  init just spawns existing tests and does a toy `read(0, …)` loop.

**Console inode ops**

```c
static int console_read(struct inode *ip, uint64_t off, void *buf,
                        uint64_t n) {
    (void)ip; (void)off;  // no seek for char devices
    char *p = buf;
    for (uint64_t i = 0; i < n; i++) {
        if (uart_rx_get(&p[i]) < 0) return i;   // EOF
    }
    return (int)n;
}

static int console_write(struct inode *ip, uint64_t off, const void *buf,
                         uint64_t n) {
    (void)ip; (void)off;
    const char *p = buf;
    for (uint64_t i = 0; i < n; i++) write_char(p[i]);
    return (int)n;
}
```

## 5. Key flows

### 5.1 `sys_read` end-to-end

```
user ecall(SYS_read, fd, buf, n)
  → trap_handler
  → syscall_dispatch
  → sys_read(fd, buf, n)
  → validate fd, grab current->ofile[fd] under IRQs-off
  → fileread(f, buf, n)
      f->type == FD_INODE
      f->ip->ops->read(f->ip, f->off, buf, n)
        console inode: uart_rx_get loop
        tarfs inode: memcpy from baked-in blob at f->off
      on success: f->off += ret
  → return bytes read
```

### 5.2 Path walk across a mount boundary

```
namei("/dev/console")
  cur = mounts[0].root     // tarfs root
  component "dev"
    tarfs synthesized a /dev dir entry? No — devfs is mounted.
    After lookup, the "dev" inode cur has mount_child == devfs_root.
    Swap: cur = devfs_root
  component "console"
    cur = devfs_root
    cur->ops->lookup(cur, "console", &console_inode)
    cur = console_inode
  return console_inode
```

To wire a mount point: on `mount_fs("/dev", devfs_root)`, walk the
existing tree to `/dev` and set `dev_inode->mount_child =
devfs_root`. If `/dev` doesn't exist in tarfs, tarfs_init must
pre-create an empty directory stub for it. Cleaner: `mount_fs` just
appends to the mount table and the path walker checks the mount
table at every step. Pick one; the "mount_child" hook is faster,
the "scan mount table" is simpler.

**Decision:** use the "mount_child" hook. The mount call is rare
(handful at boot), and scanning the mount table on every path
component is slower. tarfs_init pre-creates stubs for `/dev`, `/bin`,
`/etc`, `/data` (the known mount points).

### 5.3 Fork's effect on the fd table

```c
int proc_fork_current(void) {
    ...
    for (int fd = 0; fd < NOFILE; fd++) {
        if (parent->ofile[fd])
            child->ofile[fd] = filedup(parent->ofile[fd]);
    }
    if (parent->cwd) child->cwd = inode_get(parent->cwd);
    ...
}
```

`filedup` bumps refcount atomically (IRQs off).

### 5.4 Exit closes FDs

```c
void proc_exit_current(int status) {
    for (int fd = 0; fd < NOFILE; fd++) {
        if (current->ofile[fd]) {
            fileclose(current->ofile[fd]);
            current->ofile[fd] = 0;
        }
    }
    if (current->cwd) { inode_put(current->cwd); current->cwd = 0; }
    ...
    proc_exit_current's existing body follows.
}
```

### 5.5 UART RX interrupt path

```
UART raises IRQ 10 on PLIC
  → external interrupt delivered to S-mode
  → trap.S saves frame
  → trap_handler: is_interrupt=1, cause_code=9
    → plic_claim() returns IRQ number
    → switch on IRQ, dispatch to uart_rx_isr
      → uart_rx_isr reads RBR, pushes char into ring,
        updates line discipline, wakes any blocked reader
    → plic_complete(IRQ)
  → trap.S exits
```

Waker: a global wait-queue for the UART. Readers parked on it go via
`proc_sleep(current)` with `wake_tick=0`. ISR walks the proc list
and wakes. For Phase 4 with one reader at a time, a single "blocked
reader pid" slot is enough — no general wait-queue primitive yet.

## 6. Syscall ABI & error codes

| # | Name | args | returns |
|---|---|---|---|
| 4 | `open` | `const char *path, int flags, int mode` | fd or -errno |
| 5 | `read` | `int fd, void *buf, uint64_t n` | bytes or -errno |
| 6 | `close` | `int fd` | 0 or -errno |
| 14 | `dup` | `int fd` | fd or -errno |
| 15 | `dup2` | `int oldfd, int newfd` | fd or -errno |
| 16 | `lseek` | `int fd, int64_t off, int whence` | new off or -errno |
| 17 | `fstat` | `int fd, struct stat *` | 0 or -errno |
| 18 | `getdents64` | `int fd, void *buf, uint64_t n` | bytes or -errno |
| 19 | `chdir` | `const char *path` | 0 or -errno |
| 20 | `getcwd` | `char *buf, uint64_t n` | len or -errno |

Errnos introduced: `EBADF 9`, `ENOENT 2`, `ENOTDIR 20`, `EISDIR 21`,
`ENOMEM 12`, `EFAULT 14`, `EMFILE 24`, `ENFILE 23`, `EINVAL 22`,
`EROFS 30`, `ENOSYS 38`, `ENOTSUP 95`. Declare once in
`libc/include/errno.h` and `kernel/include/errno.h`.

## 7. Test plan

New user binaries under `bin/`:

- `fd_test` — open `/etc/rc` (seed file), read, assert contents.
- `dup_test` — open, dup, dup2 over an existing fd, close, verify
  refcounts via observable behavior.
- `getdents_test` — open `/bin`, getdents, assert each known binary
  appears exactly once.
- `stat_test` — stat `/bin/echo`, assert mode has S_IFREG set and
  size > 0.
- `console_test` — read one line from fd 0, echo it back to fd 1.
  Manual interactive test.
- `chdir_test` — chdir to `/bin`, open a relative path, read, assert.

New kernel selftests in `selftest.c`:

- `test_namei`: absolute paths to `/`, `/bin`, `/bin/init`,
  `/doesnt_exist` (-ENOENT), `/bin/init/oops` (-ENOTDIR).
- `test_mount_crossing`: resolve `/dev/console`, assert inode type
  == I_CHR.
- `test_tarfs_inode_tree`: post-init, walk all `/bin` children and
  count; must match known `bin/*` count.

Add a new `rootfs/etc/rc` with a known magic string so `fd_test` can
verify.

## 8. Gotchas

- **Reusing `SYS_open` slot 4** — it existed as a number but was
  never implemented. If any user binary has already been compiled
  assuming slot 4 means something else, rebuild from clean. Double-
  check `libc/syscall.c` before wiring.
- **The file table lock is IRQs-off, not a spinlock.** Single-hart
  + IRQs off is our only concurrency. If any alloc/free path is
  called with IRQs already off, nesting push_off/pop_off is fine;
  if you acquire the same pretend-lock twice from the same path,
  still fine. But: the ISR-driven UART path means `fileread` on
  the console inode *must not* be holding the file-table lock
  when it parks waiting for input.
- **Path length cap.** Limit to 256 bytes. Reject longer paths with
  `-ENAMETOOLONG`. Enforced in `namei` before any fs ops.
- **Component length cap.** 100 bytes (tar's `name` field is 100).
  Reject longer. Tar-`prefix` support for long paths: defer — note
  as a known tar format limitation.
- **getdents cookie stability.** For tarfs this is easy (static
  tree). For devfs, also easy (one entry). When sbfs arrives in
  Phase 5 the cookie must be a stable directory offset, not a
  list index.
- **`namei` and refcounts.** Every `lookup` that returns a new
  inode takes an internal reference; the walker must `inode_put`
  the previous `cur` once it has `next`. Miss this and inodes leak
  slowly.
- **`exec` path resolution.** `sys_exec` must use `namei`, not
  `tarfs_find` directly, so mounted filesystems are in the search
  path. But tarfs stays as the source of truth for `/bin` until
  Phase 5 adds sbfs.
- **Mode bits from tar are untrusted.** Mask with `0777` and OR in
  type bits from `typeflag`. Don't pass arbitrary tar mode straight
  to `st_mode`.
- **Console `read` blocking semantics.** Returns immediately with
  bytes if input is available. Blocks until at least one line
  exists if not. Returns 0 on EOF (Ctrl-D at column 0). Never
  returns partial lines unless the user's `n` is smaller than the
  line — in which case the rest stays in the ring.
- **Console `read` during reparenting.** If the reader dies mid-
  block, the ISR waker must notice the proc is no longer
  PROC_SLEEPING and not dereference it. Make the "blocked reader"
  slot a weak reference (pid + state check), not a raw pointer.
- **The UART device is in the kernel's upper-half map only.** Post-
  Phase 3 vmem_init removed the identity mapping. `uart.c` uses
  `UART + mem_offset` which resolves to the high-half address. Any
  ISR code reading RBR must respect this.
- **PLIC priority**: set threshold 0 and source priority 1 for UART.
  Forgetting priority means the interrupt is "pending" but never
  claimed.
- **`init` must open /dev/console *before* the eventual exec of
  shell.** If you defer this to the shell itself, the shell has no
  stdio to printf its prompt. Phase 6 should not have to think
  about fd 0/1/2 setup — init owns that contract.
- **Don't call `sys_read` from a kernel thread.** Kernel threads
  don't have an ofile table. Direct `uart_rx_get` for anything
  kernel-side.
- **Duplicate `struct stat` in kernel vs libc.** The layout MUST
  match byte-for-byte. Prefer a shared header under `common/` that
  both build trees include. If that's infeasible with the current
  Makefile layout, add a CI-style assertion in `selftest.c` that
  `sizeof(struct stat) == 64` (or whatever the agreed number is).
- **`cwd` refcount on fork**. Easy to forget. Test: fork a child,
  child chdirs elsewhere, parent's cwd is unchanged.

## 9. Open questions / decisions punted to later

- **`..` across mount points.** Phase 4b implements `..` within a
  single fs only. Going "above" a mount returns the mount root
  (i.e., `/dev/..` is `/dev`). Linux behavior is to pop through the
  mount, which requires back-pointers. Add when a user actually
  cares.
- **Symbolic links.** Not supported. tar's `2` typeflag is ignored
  (logged and skipped). Revisit if BusyBox needs them.
- **`open` flags.** Phase 4 supports `O_RDONLY`, `O_WRONLY`,
  `O_RDWR`, `O_CREAT` (ENOSYS for tarfs, works for sbfs in Phase 5),
  `O_APPEND`. `O_EXCL`, `O_TRUNC`, `O_NONBLOCK`, `O_CLOEXEC` all
  deferred.
- **`getcwd` without parent pointers.** Two options: store the path
  string on the PCB (`char cwd_path[256]`) and update it on chdir;
  or walk back via a parent-inode pointer we maintain. Simpler:
  store the string. Normalizes on every chdir (resolves `..`).
- **NOFILE = 16 vs 32.** 16 is fine for Phase 4. Bump to 32 when
  the shell needs more (Phase 6 will tell us).
- **Device major/minor numbers.** Not tracked. `st_rdev` always 0.
  Add when a user demands.
- **`poll`/`select`.** Deferred. Console read is blocking; pipes
  (Phase 6) are blocking. Good enough for our shell.

## 10. Exit criteria

- [ ] User binary can `open("/bin/echo")`, `fstat`, `read` the ELF
      header bytes, `close`.
- [ ] User binary can `open("/bin")`, `getdents64`, list every
      entry we know exists.
- [ ] `stat /bin/echo` returns size > 0 and `S_IFREG` bit set.
- [ ] User binary can `chdir("/bin")`, `open("echo")` (relative),
      read it.
- [ ] `/dev/console` reads one line from typed input, including
      backspace editing, including Ctrl-D EOF.
- [ ] `write(1, ...)` still works (via FD 1 = console, not the old
      UART shortcut).
- [ ] Every kernel selftest passes, including the new namei / tree
      tests.
- [ ] The page free-count invariant from Phase 3 tail still holds
      across 1000× spawn cycles (nothing regressed).
- [ ] Docs: phase doc updated, roadmap §2 and §4 checked off.
