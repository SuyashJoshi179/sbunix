# procfs + VFS Symlinks Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Linux-style `/proc` pseudo-filesystem and the VFS symlink machinery it needs (`I_LNK`, `readlink`, `lstat`, `namei` follow).

**Architecture:** Two phases land in order. Phase P1 adds symlink support to the existing VFS (new inode type, new `readlink` op on `inode_ops`, `namei` symlink-following loop, `readlink`/`lstat` syscalls, libc wrappers, and a self-loop fixture in devfs). Phase P2 adds `kernel/fs/procfs.c` modeled on `devfs.c`, mounts it at `/proc`, exposes static system files plus dynamic per-pid directories, and `/proc/self` as an `I_LNK` whose target is computed from `current_proc()->pid` per call. Per-pid inodes come from a fixed pool released through `inode_ops->release`; pid-recycle aliasing is prevented by a `pcb->generation` counter.

**Tech Stack:** RISC-V64 freestanding C kernel, `riscv64-unknown-elf-gcc`, in-tree libc, QEMU virt machine, tar-backed initrd built from `rootfs/`.

**Spec:** [`docs/superpowers/specs/2026-04-30-procfs-and-symlinks-design.md`](../specs/2026-04-30-procfs-and-symlinks-design.md)

**Build/test loop for every task that touches code:**

```bash
make                                  # full build, must succeed
make qemu                             # boots; init runs the test array
# Look for the new test name in the init output:
#   "init: 'XXX' exited with status N"
# Status 0 = pass.
```

QEMU exits when the shell exits; press `Ctrl-A x` to detach if it does not.

---

## File Structure

### Phase P1 — VFS symlinks

| File | Action | Responsibility |
|------|--------|----------------|
| `kernel/include/inode.h` | Modify | Add `I_LNK`; add `readlink` to `inode_ops`. |
| `kernel/include/stat.h` | Modify | Add `S_IFLNK`, `S_ISLNK`. |
| `kernel/include/errno.h` | Modify | Add `ELOOP`. |
| `kernel/include/syscall.h` | Modify | Add `SYS_readlink = 112`, `SYS_lstat = 113`. |
| `kernel/include/vfs.h` | Modify | Declare `lnamei`. |
| `kernel/fs/vfs.c` | Modify | Refactor `namei` into `namei_flags` + symlink loop; add `lnamei`. |
| `kernel/fs/devfs.c` | Modify | Add `/dev/loop` self-loop fixture. |
| `kernel/syscall.c` | Modify | Add `SYS_readlink` and `SYS_lstat` cases. |
| `libc/include/sys/stat.h` | Modify | Add `S_IFLNK`, `S_ISLNK`. |
| `libc/include/unistd.h` | Modify | Declare `lstat`. |
| `libc/syscall.c` | Modify | Replace `readlink` stub with real syscall; add `lstat`. |
| `bin/symlink_test/symlink_test.c` | Create | User test for symlink behavior. |
| `bin/init/init.c` | Modify | Add `/bin/symlink_test` to test array. |

### Phase P2 — procfs

| File | Action | Responsibility |
|------|--------|----------------|
| `kernel/include/proc.h` | Modify | Add `generation` field to `struct pcb`; declare `proc_find_by_pid`. |
| `kernel/proc.c` | Modify | Bump `generation` in `alloc_proc`; implement `proc_find_by_pid`. |
| `kernel/include/procfs.h` | Create | `procfs_init()` declaration. |
| `kernel/fs/procfs.c` | Create | All procfs logic: inode pool, lookups, getdents, file content formatters, `/proc/self` symlink. |
| `kernel/kernel.c` | Modify | Call `procfs_init()` after `devfs_init()`. |
| `rootfs/proc/.keep` | Create | Mount-point directory in tarfs. |
| `bin/proc_test/proc_test.c` | Create | User test for procfs. |
| `bin/ps/ps.c` | Create | Minimal `ps` consumer. |
| `bin/init/init.c` | Modify | Add `/bin/proc_test` and `/bin/ps` to test array. |

---

# Phase P1 — VFS Symlinks

---

### Task 1: Add I_LNK / S_IFLNK / S_ISLNK / ELOOP constants

**Files:**
- Modify: `kernel/include/inode.h`
- Modify: `kernel/include/stat.h`
- Modify: `kernel/include/errno.h`
- Modify: `libc/include/sys/stat.h`

- [ ] **Step 1: Add `I_LNK` to kernel inode.h**

In `kernel/include/inode.h`, after the existing `#define I_CHR  3` line (around line 31), add:

```c
#define I_LNK  4
```

- [ ] **Step 2: Add `S_IFLNK` + `S_ISLNK` to kernel stat.h**

In `kernel/include/stat.h`, after `#define S_IFCHR  0020000`, add:

```c
#define S_IFLNK  0120000
```

After the existing `S_ISCHR` macro, add:

```c
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
```

- [ ] **Step 3: Add `ELOOP` to errno.h**

In `kernel/include/errno.h`, after `#define ENOTSUP    95`, add:

```c
#define ELOOP        40
```

- [ ] **Step 4: Mirror S_IFLNK + S_ISLNK in libc**

In `libc/include/sys/stat.h`, after `S_IFIFO`/`S_IFCHR` lines, add:

```c
#define S_IFLNK  0120000
```

After `S_ISFIFO`, add:

```c
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
```

- [ ] **Step 5: Mirror `ELOOP` in libc errno**

Locate `libc/errno.c` and the matching libc errno header (search for `ENOSYS`). Add `ELOOP 40` next to it. If `libc/errno.c` only defines values, add `#define ELOOP 40` to whichever header userspace consumes (`libc/include/errno.h`).

- [ ] **Step 6: Build**

Run: `make`
Expected: builds cleanly. No callers reference these new symbols yet.

- [ ] **Step 7: Commit**

```bash
git add kernel/include/inode.h kernel/include/stat.h kernel/include/errno.h \
        libc/include/sys/stat.h libc/include/errno.h libc/errno.c
git commit -m "vfs: add I_LNK, S_IFLNK, S_ISLNK, ELOOP"
```

---

### Task 2: Extend `inode_ops` with `readlink`

**Files:**
- Modify: `kernel/include/inode.h`

- [ ] **Step 1: Add `readlink` to inode_ops**

In `kernel/include/inode.h`, inside `struct inode_ops { ... }`, after the existing `void (*release)` line, add:

```c
    /* Read the target of a symbolic link into `buf`. Returns the number
     * of bytes written (no NUL terminator), or -errno. NULL for non-link
     * filesystems; namei treats that as -EINVAL. */
    int  (*readlink)(struct inode *ip, char *buf, uint64_t n);
```

- [ ] **Step 2: Build**

Run: `make`
Expected: builds cleanly. Existing filesystem op tables use designated initializers, so the new field defaults to NULL.

- [ ] **Step 3: Commit**

```bash
git add kernel/include/inode.h
git commit -m "vfs: add readlink op to inode_ops"
```

---

### Task 3: Add `/dev/loop` self-loop symlink fixture

**Files:**
- Modify: `kernel/fs/devfs.c`

- [ ] **Step 1: Add the loop inode + ops in `devfs.c`**

Inside `kernel/fs/devfs.c`, before the `devfs_init` function, add:

```c
/* ----------------------------------------------------------------
 * /dev/loop — self-referential symlink, used by the symlink test
 * to exercise ELOOP detection. readlink returns "/dev/loop", so
 * resolution loops until namei hits SYMLINK_MAX.
 * ---------------------------------------------------------------- */
static int loop_readlink(struct inode *ip, char *buf, uint64_t n) {
    (void)ip;
    static const char target[] = "/dev/loop";
    int len = (int)sizeof(target) - 1;  /* 9, no NUL */
    int copy = (int)n < len ? (int)n : len;
    for (int i = 0; i < copy; i++) buf[i] = target[i];
    return copy;
}

static int loop_stat(struct inode *ip, struct stat *st) {
    st->st_dev   = 2;
    st->st_ino   = (uint64_t)(uintptr_t)ip;
    st->st_mode  = ip->mode;
    st->st_nlink = 1;
    st->st_uid   = st->st_gid = 0;
    st->st_size  = 9;       /* strlen("/dev/loop") */
    st->st_atime = st->st_mtime = st->st_ctime = 0;
    return 0;
}

static const struct inode_ops loop_ops = {
    .stat     = loop_stat,
    .readlink = loop_readlink,
};

static struct inode loop_inode;
```

- [ ] **Step 2: Initialize `loop_inode` in `devfs_init`**

In `devfs_init`, before the `mount_fs("/dev", &devroot_inode);` call, add:

```c
loop_inode.type        = I_LNK;
loop_inode.mode        = S_IFLNK | 0777;
loop_inode.uid         = 0;
loop_inode.gid         = 0;
loop_inode.size        = 9;
loop_inode.mtime       = 0;
loop_inode.nlink       = 1;
loop_inode.refcnt      = 1;
loop_inode.ops         = &loop_ops;
loop_inode.fs_data     = 0;
loop_inode.mount_child = 0;
loop_inode.mount_parent = 0;
```

- [ ] **Step 3: Make `devroot_lookup` resolve "loop"**

In `devfs.c`, inside `devroot_lookup`, after the existing `if (streq(name, "console")) { ... }` block and before the trailing `return -ENOENT;`, add:

```c
if (streq(name, "loop")) {
    *out = inode_get(&loop_inode);
    return 0;
}
```

- [ ] **Step 4: Emit "loop" from `devroot_getdents`**

The existing implementation only emits `console`. Replace the body of `devroot_getdents` with a small two-entry walker:

```c
static int devroot_getdents(struct inode *dir, uint64_t off, void *buf,
                             uint64_t n, uint64_t *out_next) {
    (void)dir;

    static const struct {
        const char *name;
        struct inode *ino;
        uint8_t d_type;
    } ents[] = {
        { "console", &console_inode, DT_CHR },
        { "loop",    &loop_inode,    DT_UNKNOWN },  /* DT_LNK not defined */
    };
    int nent = (int)(sizeof(ents) / sizeof(ents[0]));

    if (off >= (uint64_t)nent) { if (out_next) *out_next = off; return 0; }

    int i = (int)off;
    int namelen = 0;
    while (ents[i].name[namelen]) namelen++;
    namelen++;                                       /* include NUL */
    int reclen = (DIRENT64_FIXED_LEN + namelen + 7) & ~7;
    if ((uint64_t)reclen > n) { if (out_next) *out_next = off; return 0; }

    struct dirent64 *de = (struct dirent64 *)buf;
    de->d_ino    = (uint64_t)(uintptr_t)ents[i].ino;
    de->d_off    = off + 1;
    de->d_reclen = (uint16_t)reclen;
    de->d_type   = ents[i].d_type;
    for (int j = 0; j < namelen; j++) de->d_name[j] = ents[i].name[j];

    if (out_next) *out_next = off + 1;
    return reclen;
}
```

- [ ] **Step 5: Build**

Run: `make`
Expected: builds cleanly.

- [ ] **Step 6: Commit**

```bash
git add kernel/fs/devfs.c
git commit -m "devfs: add /dev/loop self-symlink fixture"
```

---

### Task 4: Refactor `namei` into `namei_flags` + add `lnamei`

**Files:**
- Modify: `kernel/include/vfs.h`
- Modify: `kernel/fs/vfs.c`

- [ ] **Step 1: Declare `lnamei` in vfs.h**

In `kernel/include/vfs.h`, after the existing `int namei(const char *path, struct inode **out);`, add:

```c
/* Like namei, but does not follow a trailing symlink component. */
int lnamei(const char *path, struct inode **out);
```

- [ ] **Step 2: Add a static `namei_flags` helper, route both wrappers through it**

In `kernel/fs/vfs.c`, replace the existing `namei` definition (lines 59–148) with:

```c
static int namei_flags(const char *path, struct inode **out, int nofollow);

int namei(const char *path, struct inode **out) {
    return namei_flags(path, out, 0);
}

int lnamei(const char *path, struct inode **out) {
    return namei_flags(path, out, 1);
}

static int namei_flags(const char *path, struct inode **out, int nofollow) {
    if (!path || !path[0]) return -ENOENT;

    int pathlen = 0;
    while (path[pathlen]) pathlen++;
    if (pathlen >= 256) return -ENAMETOOLONG;

    char buf[256];
    for (int i = 0; i <= pathlen; i++) buf[i] = path[i];

    struct inode *cur;
    char *p;

    if (buf[0] == '/') {
        if (nmounts == 0) return -ENOENT;
        cur = mounts[0].root;
        p   = buf + 1;
    } else {
        struct pcb *proc = current_proc();
        if (!proc || !proc->cwd) return -ENOENT;
        cur = proc->cwd;
        p   = buf;
    }

    cur = inode_get(cur);

    while (*p) {
        while (cur->mount_child) {
            struct inode *mc = inode_get(cur->mount_child);
            inode_put(cur);
            cur = mc;
        }
        while (*p == '/') p++;
        if (!*p) break;

        char *start = p;
        while (*p && *p != '/') p++;
        char saved = *p;
        *p = '\0';

        if (cur->type != I_DIR) {
            inode_put(cur);
            return -ENOTDIR;
        }

        if (start[0] == '.' && start[1] == '.' && (p - start) == 2 &&
            cur->mount_parent) {
            struct inode *mp = inode_get(cur->mount_parent);
            inode_put(cur);
            cur = mp;
        }

        struct inode *next = 0;
        int rc = cur->ops->lookup(cur, start, &next);
        *p = saved;
        if (rc < 0) { inode_put(cur); return rc; }

        inode_put(cur);
        cur = next;

        while (cur->mount_child) {
            struct inode *mc = inode_get(cur->mount_child);
            inode_put(cur);
            cur = mc;
        }
    }

    while (cur->mount_child) {
        struct inode *mc = inode_get(cur->mount_child);
        inode_put(cur);
        cur = mc;
    }

    *out = cur;
    return 0;
}
```

This is byte-for-byte the previous `namei` body, just renamed and behind two wrappers. No symlink logic yet — that comes in Task 5. The `nofollow` parameter is currently ignored on purpose; Task 5 will use it.

- [ ] **Step 3: Build**

Run: `make`
Expected: builds cleanly. All existing tests still resolve via `namei`.

- [ ] **Step 4: Commit**

```bash
git add kernel/include/vfs.h kernel/fs/vfs.c
git commit -m "vfs: route namei through namei_flags, add lnamei"
```

---

### Task 5: Implement symlink following in `namei_flags`

**Files:**
- Modify: `kernel/fs/vfs.c`

- [ ] **Step 1: Add the `SYMLINK_MAX` constant**

At the top of `kernel/fs/vfs.c`, after the `#define NMOUNT 8` line, add:

```c
#define SYMLINK_MAX 8
```

- [ ] **Step 2: Add symlink following inside the resolution loop**

Inside `namei_flags`, locate the block immediately after `cur = next;` (after `lookup` succeeds and any mount overlay is crossed). Replace the trailing inner `while (cur->mount_child) ...` block with:

```c
        /* Cross any mount on the newly resolved inode. */
        while (cur->mount_child) {
            struct inode *mc = inode_get(cur->mount_child);
            inode_put(cur);
            cur = mc;
        }

        /* Symlink following. We follow when:
         *   - the resolved inode is a symlink, AND
         *   - this is not the final component, OR nofollow == 0.
         * Track hops to detect loops. */
        int is_final = (*p == '\0');
        if (cur->type == I_LNK && !(is_final && nofollow)) {
            if (!cur->ops->readlink) { inode_put(cur); return -EINVAL; }
            if (++hops > SYMLINK_MAX) { inode_put(cur); return -ELOOP; }

            char target[256];
            int tlen = cur->ops->readlink(cur, target, sizeof(target) - 1);
            if (tlen < 0) { inode_put(cur); return tlen; }
            target[tlen] = '\0';
            inode_put(cur);

            /* Build the new path: target + remaining (whatever is after p). */
            int remlen = 0; while (p[remlen]) remlen++;
            int total = tlen + (remlen ? 1 + remlen : 0);
            if (total >= (int)sizeof(buf)) return -ENAMETOOLONG;

            char merged[256];
            int mi = 0;
            for (int i = 0; i < tlen; i++) merged[mi++] = target[i];
            if (remlen) {
                merged[mi++] = '/';
                for (int i = 0; i < remlen; i++) merged[mi++] = p[i];
            }
            merged[mi] = '\0';
            for (int i = 0; i <= mi; i++) buf[i] = merged[i];

            /* Restart the walk. Absolute target → from root mount;
             * relative target → from where the symlink lived (current
             * directory of the resolution; we approximate by restarting
             * from the parent of the link, which is the caller's cwd or
             * the previous component's parent. Simplification: restart
             * from root for absolute, from cwd for relative — same
             * convention `namei` uses for absolute vs relative paths). */
            if (buf[0] == '/') {
                cur = inode_get(mounts[0].root);
                p   = buf + 1;
            } else {
                struct pcb *proc = current_proc();
                if (!proc || !proc->cwd) return -ENOENT;
                cur = inode_get(proc->cwd);
                p   = buf;
            }
            continue;
        }
```

Also add `int hops = 0;` immediately above the outer `while (*p)` loop so the counter exists.

- [ ] **Step 3: Build**

Run: `make`
Expected: builds cleanly.

- [ ] **Step 4: Quick boot smoke**

Run: `make qemu`
Expected: existing tests in `init.c` still pass (no symlinks resolved on existing paths, so behavior is unchanged). Detach with `Ctrl-A x`.

- [ ] **Step 5: Commit**

```bash
git add kernel/fs/vfs.c
git commit -m "vfs: follow symlinks in namei with ELOOP cap"
```

---

### Task 6: Add `SYS_readlink` syscall

**Files:**
- Modify: `kernel/include/syscall.h`
- Modify: `kernel/syscall.c`

- [ ] **Step 1: Allocate the syscall number**

In `kernel/include/syscall.h`, after `#define SYS_meminfo  111`, add:

```c
#define SYS_readlink     112    // (const char *path, char *buf, size_t n)
#define SYS_lstat        113    // (const char *path, struct stat *st)
```

- [ ] **Step 2: Add the dispatch case for `SYS_readlink`**

In `kernel/syscall.c`, inside `syscall_dispatch`, add a new case alongside other path-based syscall cases (e.g. near `SYS_fstat`):

```c
        case SYS_readlink: {
            const char *upath = (const char *)trapframe[TF_A0];
            char       *ubuf  = (char *)trapframe[TF_A1];
            uint64_t    n     = trapframe[TF_A2];

            char kpath[256];
            int  klen = 0;
            for (klen = 0; klen < 255; klen++) {
                char c;
                if (copyin(&c, upath + klen, 1) < 0) return -EFAULT;
                kpath[klen] = c;
                if (!c) break;
            }
            kpath[255] = '\0';

            struct inode *ip;
            int rc = lnamei(kpath, &ip);
            if (rc < 0) return rc;

            if (ip->type != I_LNK || !ip->ops->readlink) {
                inode_put(ip);
                return -EINVAL;
            }

            char kbuf[256];
            uint64_t cap = n < sizeof(kbuf) ? n : sizeof(kbuf);
            int got = ip->ops->readlink(ip, kbuf, cap);
            inode_put(ip);
            if (got < 0) return got;

            if (got > 0 && copyout(ubuf, kbuf, (uint64_t)got) < 0) return -EFAULT;
            return got;
        }
```

- [ ] **Step 3: Build**

Run: `make`
Expected: builds cleanly.

- [ ] **Step 4: Commit**

```bash
git add kernel/include/syscall.h kernel/syscall.c
git commit -m "syscall: add SYS_readlink (no follow, copyout)"
```

---

### Task 7: Add `SYS_lstat` syscall

**Files:**
- Modify: `kernel/syscall.c`

- [ ] **Step 1: Add `SYS_lstat` case**

Right after the `SYS_readlink` case, add:

```c
        case SYS_lstat: {
            const char    *upath = (const char *)trapframe[TF_A0];
            struct stat   *ust   = (struct stat *)trapframe[TF_A1];

            char kpath[256];
            int  klen;
            for (klen = 0; klen < 255; klen++) {
                char c;
                if (copyin(&c, upath + klen, 1) < 0) return -EFAULT;
                kpath[klen] = c;
                if (!c) break;
            }
            kpath[255] = '\0';

            struct inode *ip;
            int rc = lnamei(kpath, &ip);
            if (rc < 0) return rc;

            struct stat kst;
            rc = ip->ops->stat(ip, &kst);
            inode_put(ip);
            if (rc < 0) return rc;

            if (copyout(ust, &kst, sizeof(kst)) < 0) return -EFAULT;
            return 0;
        }
```

- [ ] **Step 2: Build**

Run: `make`
Expected: builds cleanly.

- [ ] **Step 3: Commit**

```bash
git add kernel/syscall.c
git commit -m "syscall: add SYS_lstat (path stat without symlink follow)"
```

---

### Task 8: Wire up libc `readlink` and `lstat`

**Files:**
- Modify: `libc/include/unistd.h`
- Modify: `libc/syscall.c`

- [ ] **Step 1: Declare `lstat`**

In `libc/include/unistd.h`, near the existing `fstat` declaration, add:

```c
int   lstat(const char *path, struct stat *st);
```

- [ ] **Step 2: Replace the readlink stub with the real syscall**

In `libc/syscall.c`, find the existing stub:

```c
long readlink(const char *path, char *buf, long n) {
    (void)path; (void)buf; (void)n;
    return -EINVAL;
}
```

Replace with:

```c
long readlink(const char *path, char *buf, long n) {
    return (long)__syscall3(SYS_readlink,
                            (long)path, (long)buf, n);
}
```

If `libc/syscall.c` does not already define `__syscall3` directly but uses an inline helper (e.g. a `syscall(num, ...)` wrapper), follow that file's existing convention by reading neighboring entries (look at how `read` or `open` is implemented). Mirror exactly.

- [ ] **Step 3: Add `lstat` next to `fstat`**

Below the existing `fstat` definition in `libc/syscall.c`:

```c
int lstat(const char *path, struct stat *st) {
    return (int)__syscall2(SYS_lstat, (long)path, (long)st);
}
```

Again, match the calling convention used by `fstat` in the same file.

- [ ] **Step 4: Build**

Run: `make`
Expected: builds cleanly. All `bin/*` rebuild and link.

- [ ] **Step 5: Commit**

```bash
git add libc/include/unistd.h libc/syscall.c
git commit -m "libc: real readlink + lstat syscalls"
```

---

### Task 9: Write `bin/symlink_test`

**Files:**
- Create: `bin/symlink_test/symlink_test.c`

- [ ] **Step 1: Write the test program**

Create `bin/symlink_test/symlink_test.c`:

```c
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails = 0;
static void check(int cond, const char *name) {
    if (cond) printf("[symlink_test] PASS  %s\n", name);
    else    { printf("[symlink_test] FAIL  %s\n", name); fails++; }
}

int main(void) {
    printf("=== symlink_test ===\n");

    /* readlink on /dev/loop returns the literal target. */
    char buf[64] = {0};
    long n = readlink("/dev/loop", buf, sizeof(buf) - 1);
    check(n == 9, "readlink length == 9");
    if (n > 0) buf[n] = 0;
    check(strcmp(buf, "/dev/loop") == 0, "readlink target string");

    /* readlink on a non-symlink returns -EINVAL. */
    long r = readlink("/dev/console", buf, sizeof(buf));
    check(r == -EINVAL, "readlink on non-symlink → EINVAL");

    /* readlink on a missing path returns -ENOENT. */
    r = readlink("/dev/no_such_thing", buf, sizeof(buf));
    check(r == -ENOENT, "readlink on missing path → ENOENT");

    /* Buffer truncation: target is 9 bytes, ask for 4 → 4 bytes, no NUL. */
    char tbuf[16];
    for (int i = 0; i < (int)sizeof(tbuf); i++) tbuf[i] = (char)0xAA;
    r = readlink("/dev/loop", tbuf, 4);
    check(r == 4, "truncated readlink returns 4");
    check(tbuf[4] == (char)0xAA, "truncated readlink writes no NUL");

    /* lstat reports S_IFLNK on the link itself. */
    struct stat st;
    int rc = lstat("/dev/loop", &st);
    check(rc == 0, "lstat /dev/loop succeeds");
    check(S_ISLNK(st.st_mode), "lstat /dev/loop is S_ISLNK");

    /* open follows the symlink → ELOOP because target is itself. */
    int fd = open("/dev/loop", 0);
    check(fd == -ELOOP, "open(/dev/loop) → ELOOP");

    /* lstat on a non-link still works (regression check). */
    rc = lstat("/dev/console", &st);
    check(rc == 0, "lstat /dev/console succeeds");
    check(S_ISCHR(st.st_mode), "lstat /dev/console is S_ISCHR");

    printf("=== symlink_test: %d failures ===\n", fails);
    return fails;
}
```

- [ ] **Step 2: Build**

Run: `make`
Expected: `build/rootfs/bin/symlink_test` is produced. The build globs `bin/*/*.c`, so no Makefile change is needed.

- [ ] **Step 3: Commit**

```bash
git add bin/symlink_test/symlink_test.c
git commit -m "bin/symlink_test: cover readlink, lstat, ELOOP"
```

---

### Task 10: Wire `symlink_test` into init and run

**Files:**
- Modify: `bin/init/init.c`

- [ ] **Step 1: Add the test to the init array**

In `bin/init/init.c`, locate the `tests[]` array. Add `"/bin/symlink_test"` near the other Phase 7/8 tests (e.g. right after `"/bin/stack_overflow_test"`):

```c
        "/bin/stack_overflow_test",
        "/bin/symlink_test",
        "/bin/header_test",
```

- [ ] **Step 2: Build and boot**

Run: `make qemu`
Expected output includes:

```
[symlink_test] PASS  readlink length == 9
[symlink_test] PASS  readlink target string
[symlink_test] PASS  readlink on non-symlink → EINVAL
[symlink_test] PASS  readlink on missing path → ENOENT
[symlink_test] PASS  truncated readlink returns 4
[symlink_test] PASS  truncated readlink writes no NUL
[symlink_test] PASS  lstat /dev/loop succeeds
[symlink_test] PASS  lstat /dev/loop is S_ISLNK
[symlink_test] PASS  open(/dev/loop) → ELOOP
[symlink_test] PASS  lstat /dev/console succeeds
[symlink_test] PASS  lstat /dev/console is S_ISCHR
=== symlink_test: 0 failures ===
init: 'symlink_test' exited with status 0
```

Detach with `Ctrl-A x`. If any check fails, debug before committing.

- [ ] **Step 3: Commit**

```bash
git add bin/init/init.c
git commit -m "init: run symlink_test"
```

---

**Phase P1 done.** Open the PR for symlinks at this point. Optional: rebase, push, request review.

---

# Phase P2 — procfs

---

### Task 11: Add `pcb->generation` and `proc_find_by_pid`

**Files:**
- Modify: `kernel/include/proc.h`
- Modify: `kernel/proc.c`

- [ ] **Step 1: Add the field to `struct pcb`**

In `kernel/include/proc.h`, inside `struct pcb`, immediately after `int pid;`, add:

```c
    uint64_t       generation;      // bumped on each alloc_proc; pid-recycle guard
```

- [ ] **Step 2: Declare `proc_find_by_pid`**

In `kernel/include/proc.h`, near the other `struct pcb *` accessors, add:

```c
struct pcb *proc_find_by_pid(int pid);
```

- [ ] **Step 3: Bump the counter and implement the lookup**

In `kernel/proc.c`, locate `alloc_proc`. After the function clears or initializes the slot's fields and assigns the new pid, add the bump (use a static monotonic counter so different recycled pids always differ):

```c
    static uint64_t generation_seq = 0;
    p->generation = ++generation_seq;
```

In the same file, add the lookup helper near `current_proc`:

```c
struct pcb *proc_find_by_pid(int pid) {
    for (struct pcb *p = proc_list_head(); p; p = p->next) {
        if (p->pid == pid && p->state != PROC_UNUSED) return p;
    }
    return 0;
}
```

If `proc.c` does not expose iteration, add the helper as a friend of the static `ptable[]` array using whatever existing iteration pattern already lives there (look at `proc_wakeup` or `current_proc` for the pattern).

- [ ] **Step 4: Build**

Run: `make`
Expected: builds cleanly.

- [ ] **Step 5: Commit**

```bash
git add kernel/include/proc.h kernel/proc.c
git commit -m "proc: add generation counter + proc_find_by_pid"
```

---

### Task 12: Create `/proc` mount point in tarfs

**Files:**
- Create: `rootfs/proc/.keep`

- [ ] **Step 1: Create the directory marker**

```bash
mkdir -p rootfs/proc
: > rootfs/proc/.keep
```

The build's `tar cf build/rootfs.tar -C build/rootfs .` step picks up the directory because `rsync`/`cp -a` carries the empty subdir.

- [ ] **Step 2: Build and confirm `/proc` is in the tarfs**

```bash
make
tar tf build/rootfs.tar | grep '^./proc/'
```

Expected: `./proc/` (and `./proc/.keep`) in the listing.

- [ ] **Step 3: Commit**

```bash
git add rootfs/proc/.keep
git commit -m "rootfs: add empty /proc mount point"
```

---

### Task 13: Stub `procfs.c` with root-only mount

**Files:**
- Create: `kernel/include/procfs.h`
- Create: `kernel/fs/procfs.c`
- Modify: `kernel/kernel.c`

- [ ] **Step 1: Create `kernel/include/procfs.h`**

```c
#pragma once
void procfs_init(void);
```

- [ ] **Step 2: Create `kernel/fs/procfs.c` with the bare root**

```c
#include <inode.h>
#include <stat.h>
#include <errno.h>
#include <vfs.h>
#include <vmem.h>
#include <string.h>
#include <printk.h>
#include <procfs.h>

static int proc_root_read(struct inode *ip, uint64_t off, void *buf,
                           uint64_t n) {
    (void)ip; (void)off; (void)buf; (void)n;
    return -EISDIR;
}
static int proc_root_write(struct inode *ip, uint64_t off, const void *buf,
                            uint64_t n) {
    (void)ip; (void)off; (void)buf; (void)n;
    return -EROFS;
}
static int proc_root_stat(struct inode *ip, struct stat *st) {
    st->st_dev   = 3;
    st->st_ino   = (uint64_t)(uintptr_t)ip;
    st->st_mode  = ip->mode;
    st->st_nlink = 1;
    st->st_uid = st->st_gid = 0;
    st->st_size = 0;
    st->st_atime = st->st_mtime = st->st_ctime = 0;
    return 0;
}
static int proc_root_lookup(struct inode *dir, const char *name,
                             struct inode **out) {
    (void)dir;
    if (name[0] == '.' && name[1] == '\0') { *out = inode_get(dir); return 0; }
    if (name[0] == '.' && name[1] == '.' && name[2] == '\0') {
        *out = inode_get(dir); return 0;
    }
    /* No entries yet; filled in later tasks. */
    return -ENOENT;
}
static int proc_root_getdents(struct inode *dir, uint64_t off, void *buf,
                               uint64_t n, uint64_t *out_next) {
    (void)dir; (void)buf; (void)n;
    if (out_next) *out_next = off;
    return 0;
}

static const struct inode_ops proc_root_ops = {
    .read     = proc_root_read,
    .write    = proc_root_write,
    .stat     = proc_root_stat,
    .lookup   = proc_root_lookup,
    .getdents = proc_root_getdents,
};

static struct inode proc_root_inode;

void procfs_init(void) {
    proc_root_inode.type        = I_DIR;
    proc_root_inode.mode        = S_IFDIR | 0555;
    proc_root_inode.uid         = 0;
    proc_root_inode.gid         = 0;
    proc_root_inode.size        = 0;
    proc_root_inode.mtime       = 0;
    proc_root_inode.nlink       = 1;
    proc_root_inode.refcnt      = 1;
    proc_root_inode.ops         = &proc_root_ops;
    proc_root_inode.fs_data     = 0;
    proc_root_inode.mount_child = 0;
    proc_root_inode.mount_parent = 0;

    mount_fs("/proc", &proc_root_inode);
    printk("procfs: mounted /proc\n");
}
```

- [ ] **Step 3: Call `procfs_init` from `kernel.c`**

In `kernel/kernel.c`, add `#include <procfs.h>` near the other `#include <devfs.h>`/equivalent. After the `devfs_init();` call, add:

```c
    procfs_init();
```

- [ ] **Step 4: Build and boot**

Run: `make qemu`
Expected: `procfs: mounted /proc` in boot output. All existing tests still pass.

- [ ] **Step 5: Commit**

```bash
git add kernel/include/procfs.h kernel/fs/procfs.c kernel/kernel.c
git commit -m "procfs: mount empty /proc"
```

---

### Task 14: Add static system files (`uptime`, `meminfo`, `version`, `cpuinfo`)

**Files:**
- Modify: `kernel/fs/procfs.c`

- [ ] **Step 1: Add a small string-builder helper at the top of `procfs.c`**

Before `proc_root_read`, add:

```c
/* Tiny formatter for procfs files. Each producer fills `out` with up to
 * `cap` bytes and returns the byte count. The caller's read() then
 * services off..off+n from this snapshot. */
typedef int (*proc_producer)(char *out, int cap);

static int produce_static(proc_producer prod, uint64_t off, void *buf,
                           uint64_t n) {
    char tmp[512];
    int  total = prod(tmp, (int)sizeof(tmp));
    if (off >= (uint64_t)total) return 0;
    int copy = total - (int)off;
    if ((int)n < copy) copy = (int)n;
    char *dst = (char *)buf;
    for (int i = 0; i < copy; i++) dst[i] = tmp[(int)off + i];
    return copy;
}

/* Minimal unsigned-decimal formatter — places digits in `out`, returns count.
 * Caller ensures cap >= 21. */
static int u64_to_dec(char *out, int cap, uint64_t v) {
    char tmp[24];
    int  i = 0;
    if (!v) tmp[i++] = '0';
    else while (v) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    if (i > cap) i = cap;
    for (int j = 0; j < i; j++) out[j] = tmp[i - 1 - j];
    return i;
}
```

- [ ] **Step 2: Add the four producer functions**

```c
extern uint64_t timer_ticks;            /* declared in kernel/timer.h */
extern uint64_t pmem_total_pages(void); /* declared in kernel/pmem.h, see note */
extern uint64_t pmem_free_pages(void);

#ifndef SBUNIX_VERSION
#define SBUNIX_VERSION "unknown"
#endif

static int prod_uptime(char *out, int cap) {
    uint64_t centi = timer_ticks;        /* 100 Hz → centiseconds */
    uint64_t secs  = centi / 100;
    uint64_t cs    = centi % 100;
    int n = 0;
    n += u64_to_dec(out + n, cap - n, secs);
    if (n < cap) out[n++] = '.';
    if (cs < 10 && n < cap) out[n++] = '0';
    n += u64_to_dec(out + n, cap - n, cs);
    if (n < cap) out[n++] = '\n';
    return n;
}

static int prod_meminfo(char *out, int cap) {
    static const char k1[] = "MemTotal: ";
    static const char k2[] = " kB\nMemFree: ";
    static const char k3[] = " kB\n";
    uint64_t total_kb = pmem_total_pages() * 4;
    uint64_t free_kb  = pmem_free_pages()  * 4;
    int n = 0;
    for (int i = 0; i < (int)sizeof(k1) - 1 && n < cap; i++) out[n++] = k1[i];
    n += u64_to_dec(out + n, cap - n, total_kb);
    for (int i = 0; i < (int)sizeof(k2) - 1 && n < cap; i++) out[n++] = k2[i];
    n += u64_to_dec(out + n, cap - n, free_kb);
    for (int i = 0; i < (int)sizeof(k3) - 1 && n < cap; i++) out[n++] = k3[i];
    return n;
}

static int prod_version(char *out, int cap) {
    static const char s[] = "SBUnix " SBUNIX_VERSION " riscv64\n";
    int n = (int)sizeof(s) - 1;
    if (n > cap) n = cap;
    for (int i = 0; i < n; i++) out[i] = s[i];
    return n;
}

static int prod_cpuinfo(char *out, int cap) {
    static const char s[] = "processor: 0\nisa: rv64imafdc\n";
    int n = (int)sizeof(s) - 1;
    if (n > cap) n = cap;
    for (int i = 0; i < n; i++) out[i] = s[i];
    return n;
}
```

If `pmem_total_pages` / `pmem_free_pages` do not exist by these exact names, search `kernel/include/pmem.h` for the equivalents and adjust the `extern` declarations and call sites. Use `meminfo()` (the syscall handler's underlying function) as a fallback for free pages.

- [ ] **Step 3: Add per-file inode_ops + inode structs**

Right after the producers, add:

```c
static int file_uptime_read(struct inode *ip, uint64_t off, void *buf, uint64_t n) {
    (void)ip; return produce_static(prod_uptime, off, buf, n);
}
static int file_meminfo_read(struct inode *ip, uint64_t off, void *buf, uint64_t n) {
    (void)ip; return produce_static(prod_meminfo, off, buf, n);
}
static int file_version_read(struct inode *ip, uint64_t off, void *buf, uint64_t n) {
    (void)ip; return produce_static(prod_version, off, buf, n);
}
static int file_cpuinfo_read(struct inode *ip, uint64_t off, void *buf, uint64_t n) {
    (void)ip; return produce_static(prod_cpuinfo, off, buf, n);
}

static int file_write_rofs(struct inode *ip, uint64_t off, const void *buf,
                            uint64_t n) {
    (void)ip; (void)off; (void)buf; (void)n; return -EROFS;
}
static int file_stat_generic(struct inode *ip, struct stat *st) {
    st->st_dev = 3; st->st_ino = (uint64_t)(uintptr_t)ip;
    st->st_mode = ip->mode; st->st_nlink = 1;
    st->st_uid = st->st_gid = 0; st->st_size = 0;
    st->st_atime = st->st_mtime = st->st_ctime = 0;
    return 0;
}
static int file_lookup_rofs(struct inode *dir, const char *name,
                             struct inode **out) {
    (void)dir; (void)name; (void)out; return -ENOTDIR;
}
static int file_getdents_rofs(struct inode *dir, uint64_t off, void *buf,
                               uint64_t n, uint64_t *out_next) {
    (void)dir; (void)off; (void)buf; (void)n; (void)out_next;
    return -ENOTDIR;
}

#define DEFINE_STATIC_FILE(NAME, READFN)                                       \
    static const struct inode_ops NAME##_ops = {                               \
        .read = READFN, .write = file_write_rofs,                              \
        .stat = file_stat_generic, .lookup = file_lookup_rofs,                 \
        .getdents = file_getdents_rofs,                                        \
    };                                                                         \
    static struct inode NAME##_inode

DEFINE_STATIC_FILE(uptime,  file_uptime_read);
DEFINE_STATIC_FILE(meminfo, file_meminfo_read);
DEFINE_STATIC_FILE(version, file_version_read);
DEFINE_STATIC_FILE(cpuinfo, file_cpuinfo_read);
```

- [ ] **Step 4: Initialize the four file inodes in `procfs_init`**

Inside `procfs_init`, before `mount_fs("/proc", ...)`, add:

```c
    static const struct {
        struct inode      *ip;
        const struct inode_ops *ops;
    } static_files[] = {
        { &uptime_inode,  &uptime_ops  },
        { &meminfo_inode, &meminfo_ops },
        { &version_inode, &version_ops },
        { &cpuinfo_inode, &cpuinfo_ops },
    };
    for (int i = 0; i < (int)(sizeof(static_files)/sizeof(static_files[0])); i++) {
        struct inode *ip = static_files[i].ip;
        ip->type        = I_REG;
        ip->mode        = S_IFREG | 0444;
        ip->uid = ip->gid = 0;
        ip->size = ip->mtime = 0;
        ip->nlink = 1;
        ip->refcnt = 1;
        ip->ops = static_files[i].ops;
        ip->fs_data = 0;
        ip->mount_child = ip->mount_parent = 0;
    }
```

- [ ] **Step 5: Resolve them in `proc_root_lookup` and emit them in `proc_root_getdents`**

Replace `proc_root_lookup` body's "no entries yet" comment + return with:

```c
    static const struct { const char *name; struct inode *ip; } statics[] = {
        { "uptime",  &uptime_inode  },
        { "meminfo", &meminfo_inode },
        { "version", &version_inode },
        { "cpuinfo", &cpuinfo_inode },
    };
    for (int i = 0; i < 4; i++) {
        const char *s = statics[i].name;
        int j = 0;
        while (s[j] && name[j] && s[j] == name[j]) j++;
        if (s[j] == 0 && name[j] == 0) {
            *out = inode_get(statics[i].ip); return 0;
        }
    }
    return -ENOENT;
```

Replace `proc_root_getdents` body with a static-only emitter:

```c
    static const struct { const char *name; struct inode *ip; uint8_t dt; } ents[] = {
        { "uptime",  &uptime_inode,  DT_REG },
        { "meminfo", &meminfo_inode, DT_REG },
        { "version", &version_inode, DT_REG },
        { "cpuinfo", &cpuinfo_inode, DT_REG },
    };
    int nent = 4;
    if (off >= (uint64_t)nent) { if (out_next) *out_next = off; return 0; }

    int i = (int)off;
    int namelen = 0;
    while (ents[i].name[namelen]) namelen++;
    namelen++;
    int reclen = (DIRENT64_FIXED_LEN + namelen + 7) & ~7;
    if ((uint64_t)reclen > n) { if (out_next) *out_next = off; return 0; }

    struct dirent64 *de = (struct dirent64 *)buf;
    de->d_ino = (uint64_t)(uintptr_t)ents[i].ip;
    de->d_off = off + 1;
    de->d_reclen = (uint16_t)reclen;
    de->d_type = ents[i].dt;
    for (int j = 0; j < namelen; j++) de->d_name[j] = ents[i].name[j];

    if (out_next) *out_next = off + 1;
    return reclen;
```

Note `DT_REG` is already declared in `kernel/include/inode.h` (value 8).

- [ ] **Step 6: Build + boot smoke**

Run: `make qemu`
Expected: existing tests still pass. From shell, `cat /proc/uptime` prints a number, `cat /proc/meminfo` prints `MemTotal: ...`. Detach.

- [ ] **Step 7: Commit**

```bash
git add kernel/fs/procfs.c
git commit -m "procfs: uptime, meminfo, version, cpuinfo"
```

---

### Task 15: Add per-pid directories (lookup + getdents)

**Files:**
- Modify: `kernel/fs/procfs.c`
- Modify: `kernel/include/proc.h` (if iteration helper missing)

This task introduces the inode pool and the dynamic per-pid dir resolution. Per-pid files come in Task 16.

- [ ] **Step 1: Add the inode pool + tag struct**

Near the top of `kernel/fs/procfs.c`, after `#include`s, add:

```c
#include <proc.h>

enum proc_kind {
    PK_PIDDIR = 1,
    PK_STATUS, PK_CMDLINE, PK_STAT,
    PK_SELF,
};

struct proc_node {
    struct inode    ino;
    enum proc_kind  kind;
    int             pid;
    uint64_t        generation;
    int             in_use;
};

#define PROC_INODE_POOL 64
static struct proc_node pool[PROC_INODE_POOL];

static struct proc_node *pool_alloc(void) {
    for (int i = 0; i < PROC_INODE_POOL; i++) {
        if (!pool[i].in_use) {
            struct proc_node *pn = &pool[i];
            for (uint64_t b = 0; b < sizeof(*pn); b++) ((char *)pn)[b] = 0;
            pn->in_use = 1;
            return pn;
        }
    }
    return 0;
}

static void pool_free(struct proc_node *pn) {
    pn->in_use = 0;
}
```

- [ ] **Step 2: Forward-declare ops for per-pid dirs (filled in Task 16)**

```c
static int piddir_read(struct inode *ip, uint64_t off, void *buf, uint64_t n);
static int piddir_lookup(struct inode *dir, const char *name, struct inode **out);
static int piddir_getdents(struct inode *dir, uint64_t off, void *buf,
                            uint64_t n, uint64_t *out_next);
static int piddir_stat(struct inode *ip, struct stat *st);
static void piddir_release(struct inode *ip);

static const struct inode_ops piddir_ops = {
    .read     = piddir_read,
    .write    = file_write_rofs,
    .stat     = piddir_stat,
    .lookup   = piddir_lookup,
    .getdents = piddir_getdents,
    .release  = piddir_release,
};
```

- [ ] **Step 3: Implement parse-decimal + populate-piddir helpers**

```c
static int parse_pid(const char *s, int *out_pid) {
    if (!s || !s[0]) return -1;
    int v = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        v = v * 10 + (s[i] - '0');
        if (v < 0) return -1;     /* overflow */
    }
    *out_pid = v;
    return 0;
}

static struct proc_node *piddir_make(int pid, struct pcb *pcb) {
    struct proc_node *pn = pool_alloc();
    if (!pn) return 0;
    pn->kind       = PK_PIDDIR;
    pn->pid        = pid;
    pn->generation = pcb->generation;

    pn->ino.type        = I_DIR;
    pn->ino.mode        = S_IFDIR | 0555;
    pn->ino.uid = pn->ino.gid = 0;
    pn->ino.size = pn->ino.mtime = 0;
    pn->ino.nlink = 1;
    pn->ino.refcnt = 1;
    pn->ino.ops = &piddir_ops;
    pn->ino.fs_data = pn;
    pn->ino.mount_child = pn->ino.mount_parent = 0;
    return pn;
}

static int piddir_stat(struct inode *ip, struct stat *st) {
    return file_stat_generic(ip, st);
}

static void piddir_release(struct inode *ip) {
    struct proc_node *pn = (struct proc_node *)ip->fs_data;
    if (pn) pool_free(pn);
}

/* Stubs filled in Task 16; today they only need to exist to satisfy the ops table. */
static int piddir_read(struct inode *ip, uint64_t off, void *buf, uint64_t n) {
    (void)ip; (void)off; (void)buf; (void)n; return -EISDIR;
}
static int piddir_lookup(struct inode *dir, const char *name, struct inode **out) {
    (void)dir; (void)name; (void)out; return -ENOENT;
}
static int piddir_getdents(struct inode *dir, uint64_t off, void *buf,
                            uint64_t n, uint64_t *out_next) {
    (void)dir; (void)buf; (void)n;
    if (out_next) *out_next = off;
    return 0;
}
```

- [ ] **Step 4: Resolve pid components in `proc_root_lookup`**

In `proc_root_lookup`, after the `for (int i = 0; i < 4; i++)` static-files block and before `return -ENOENT;`, add:

```c
    int pid;
    if (parse_pid(name, &pid) == 0) {
        struct pcb *pcb = proc_find_by_pid(pid);
        if (!pcb) return -ENOENT;
        struct proc_node *pn = piddir_make(pid, pcb);
        if (!pn) return -ENOMEM;
        *out = &pn->ino;
        return 0;
    }
```

- [ ] **Step 5: Emit pid entries in `proc_root_getdents`**

Rewrite `proc_root_getdents` to walk static entries first, then live pcbs:

```c
static int proc_root_getdents(struct inode *dir, uint64_t off, void *buf,
                               uint64_t n, uint64_t *out_next) {
    (void)dir;
    static const struct { const char *name; struct inode *ip; uint8_t dt; } stat_ents[] = {
        { "uptime",  &uptime_inode,  DT_REG },
        { "meminfo", &meminfo_inode, DT_REG },
        { "version", &version_inode, DT_REG },
        { "cpuinfo", &cpuinfo_inode, DT_REG },
    };
    int nstat = 4;

    /* off in [0, nstat) → static entry; off >= nstat → pcb index = off - nstat. */
    if (off < (uint64_t)nstat) {
        int i = (int)off;
        int namelen = 0;
        while (stat_ents[i].name[namelen]) namelen++;
        namelen++;
        int reclen = (DIRENT64_FIXED_LEN + namelen + 7) & ~7;
        if ((uint64_t)reclen > n) { if (out_next) *out_next = off; return 0; }

        struct dirent64 *de = (struct dirent64 *)buf;
        de->d_ino = (uint64_t)(uintptr_t)stat_ents[i].ip;
        de->d_off = off + 1;
        de->d_reclen = (uint16_t)reclen;
        de->d_type = stat_ents[i].dt;
        for (int j = 0; j < namelen; j++) de->d_name[j] = stat_ents[i].name[j];
        if (out_next) *out_next = off + 1;
        return reclen;
    }

    int idx = (int)(off - (uint64_t)nstat);
    int seen = 0;
    for (struct pcb *p = proc_list_head(); p; p = p->next) {
        if (p->state == PROC_UNUSED) continue;
        if (seen == idx) {
            char name[16]; int nlen = u64_to_dec(name, (int)sizeof(name) - 1, (uint64_t)p->pid);
            name[nlen++] = '\0';
            int reclen = (DIRENT64_FIXED_LEN + nlen + 7) & ~7;
            if ((uint64_t)reclen > n) { if (out_next) *out_next = off; return 0; }

            struct dirent64 *de = (struct dirent64 *)buf;
            de->d_ino = (uint64_t)(uintptr_t)p;
            de->d_off = off + 1;
            de->d_reclen = (uint16_t)reclen;
            de->d_type = DT_DIR;
            for (int j = 0; j < nlen; j++) de->d_name[j] = name[j];
            if (out_next) *out_next = off + 1;
            return reclen;
        }
        seen++;
    }
    if (out_next) *out_next = off;
    return 0;
}
```

- [ ] **Step 6: Add `/`-resolution self-test smoke**

Run: `make qemu`. From the shell, run: `ls /proc`. Expected: `cpuinfo  meminfo  uptime  version  1  <other-pids>`.

- [ ] **Step 7: Commit**

```bash
git add kernel/fs/procfs.c
git commit -m "procfs: per-pid directory enumeration"
```

---

### Task 16: Add per-pid files `status`, `cmdline`, `stat`

**Files:**
- Modify: `kernel/fs/procfs.c`

- [ ] **Step 1: Add a per-pid file inode**

Near the per-pid dir helpers, add:

```c
static int piddir_file_read(struct inode *ip, uint64_t off, void *buf, uint64_t n);
static int piddir_file_stat(struct inode *ip, struct stat *st) {
    return file_stat_generic(ip, st);
}
static void piddir_file_release(struct inode *ip) {
    struct proc_node *pn = (struct proc_node *)ip->fs_data;
    if (pn) pool_free(pn);
}

static const struct inode_ops piddir_file_ops = {
    .read     = piddir_file_read,
    .write    = file_write_rofs,
    .stat     = piddir_file_stat,
    .lookup   = file_lookup_rofs,
    .getdents = file_getdents_rofs,
    .release  = piddir_file_release,
};

static struct proc_node *piddir_file_make(int pid, uint64_t gen,
                                          enum proc_kind kind) {
    struct proc_node *pn = pool_alloc();
    if (!pn) return 0;
    pn->kind = kind; pn->pid = pid; pn->generation = gen;
    pn->ino.type = I_REG;
    pn->ino.mode = S_IFREG | 0444;
    pn->ino.uid = pn->ino.gid = 0;
    pn->ino.size = pn->ino.mtime = 0;
    pn->ino.nlink = 1; pn->ino.refcnt = 1;
    pn->ino.ops = &piddir_file_ops;
    pn->ino.fs_data = pn;
    pn->ino.mount_child = pn->ino.mount_parent = 0;
    return pn;
}
```

- [ ] **Step 2: Implement the three producers (operating on `pcb`, with generation check)**

```c
static const char *state_letter(proc_state_t s) {
    switch (s) {
        case PROC_RUNNING:  return "R";
        case PROC_READY:    return "R";
        case PROC_SLEEPING: return "S";
        case PROC_ZOMBIE:   return "Z";
        default:            return "?";
    }
}

static int prod_status(char *out, int cap, struct pcb *p) {
    /* Name: <something> for now is the cwd-derived name or a placeholder.
     * argv0 is not preserved across exec yet → use "proc" as a stable
     * fallback. Update once proc.c gains a name field. */
    static const char k_name[] = "Name:\tproc\nState:\t";
    static const char k_pid[]  = "\nPid:\t";
    static const char k_ppid[] = "\nPPid:\t";
    static const char k_uid[]  = "\nUid:\t0\nGid:\t0\nVmSize:\t0 kB\n";

    int n = 0;
    for (int i = 0; i < (int)sizeof(k_name) - 1 && n < cap; i++) out[n++] = k_name[i];
    const char *st = state_letter(p->state);
    while (*st && n < cap) out[n++] = *st++;
    for (int i = 0; i < (int)sizeof(k_pid) - 1 && n < cap; i++) out[n++] = k_pid[i];
    n += u64_to_dec(out + n, cap - n, (uint64_t)p->pid);
    for (int i = 0; i < (int)sizeof(k_ppid) - 1 && n < cap; i++) out[n++] = k_ppid[i];
    n += u64_to_dec(out + n, cap - n, (uint64_t)p->parent_pid);
    for (int i = 0; i < (int)sizeof(k_uid) - 1 && n < cap; i++) out[n++] = k_uid[i];
    return n;
}

static int prod_cmdline(char *out, int cap, struct pcb *p) {
    /* argv0 is not stored on pcb yet; emit "proc" + NUL as placeholder.
     * Future task can swap to p->exe_path. */
    (void)p;
    static const char s[] = "proc";
    int n = 0;
    for (int i = 0; i < (int)sizeof(s) - 1 && n < cap; i++) out[n++] = s[i];
    if (n < cap) out[n++] = '\0';
    return n;
}

static int prod_stat(char *out, int cap, struct pcb *p) {
    /* Compact Linux-style: pid (comm) state ppid pgrp session tty flags
     * Truncated to fields we can supply. */
    int n = 0;
    n += u64_to_dec(out + n, cap - n, (uint64_t)p->pid);
    static const char a[] = " (proc) ";
    for (int i = 0; i < (int)sizeof(a) - 1 && n < cap; i++) out[n++] = a[i];
    const char *st = state_letter(p->state);
    while (*st && n < cap) out[n++] = *st++;
    if (n < cap) out[n++] = ' ';
    n += u64_to_dec(out + n, cap - n, (uint64_t)p->parent_pid);
    static const char b[] = " 0 0 0 0 0\n";
    for (int i = 0; i < (int)sizeof(b) - 1 && n < cap; i++) out[n++] = b[i];
    return n;
}
```

- [ ] **Step 3: Implement `piddir_file_read`**

```c
static int piddir_file_read(struct inode *ip, uint64_t off, void *buf,
                             uint64_t n) {
    struct proc_node *pn = (struct proc_node *)ip->fs_data;
    if (!pn) return -EIO;
    struct pcb *p = proc_find_by_pid(pn->pid);
    if (!p || p->generation != pn->generation) return -ESRCH;

    char tmp[512];
    int  total;
    switch (pn->kind) {
        case PK_STATUS:  total = prod_status(tmp, sizeof(tmp), p); break;
        case PK_CMDLINE: total = prod_cmdline(tmp, sizeof(tmp), p); break;
        case PK_STAT:    total = prod_stat(tmp, sizeof(tmp), p); break;
        default:         return -EIO;
    }
    if (off >= (uint64_t)total) return 0;
    int copy = total - (int)off;
    if ((int)n < copy) copy = (int)n;
    char *dst = (char *)buf;
    for (int i = 0; i < copy; i++) dst[i] = tmp[(int)off + i];
    return copy;
}
```

- [ ] **Step 4: Resolve files in `piddir_lookup` and emit them in `piddir_getdents`**

Replace the stub `piddir_lookup`:

```c
static int piddir_lookup(struct inode *dir, const char *name,
                          struct inode **out) {
    struct proc_node *pn = (struct proc_node *)dir->fs_data;
    if (!pn) return -EIO;
    struct pcb *p = proc_find_by_pid(pn->pid);
    if (!p || p->generation != pn->generation) return -ENOENT;

    if (name[0] == '.' && name[1] == '\0') { *out = inode_get(dir); return 0; }
    if (name[0] == '.' && name[1] == '.' && name[2] == '\0') {
        *out = inode_get(&proc_root_inode); return 0;
    }

    static const struct { const char *name; enum proc_kind k; } files[] = {
        { "status",  PK_STATUS  },
        { "cmdline", PK_CMDLINE },
        { "stat",    PK_STAT    },
    };
    for (int i = 0; i < 3; i++) {
        const char *s = files[i].name;
        int j = 0;
        while (s[j] && name[j] && s[j] == name[j]) j++;
        if (s[j] == 0 && name[j] == 0) {
            struct proc_node *fn = piddir_file_make(pn->pid, pn->generation,
                                                     files[i].k);
            if (!fn) return -ENOMEM;
            *out = &fn->ino;
            return 0;
        }
    }
    return -ENOENT;
}
```

Replace the stub `piddir_getdents`:

```c
static int piddir_getdents(struct inode *dir, uint64_t off, void *buf,
                            uint64_t n, uint64_t *out_next) {
    (void)dir;
    static const struct { const char *name; uint8_t dt; } files[] = {
        { "status",  DT_REG },
        { "cmdline", DT_REG },
        { "stat",    DT_REG },
    };
    int nfiles = 3;
    if (off >= (uint64_t)nfiles) { if (out_next) *out_next = off; return 0; }

    int i = (int)off;
    int namelen = 0;
    while (files[i].name[namelen]) namelen++;
    namelen++;
    int reclen = (DIRENT64_FIXED_LEN + namelen + 7) & ~7;
    if ((uint64_t)reclen > n) { if (out_next) *out_next = off; return 0; }

    struct dirent64 *de = (struct dirent64 *)buf;
    de->d_ino = 0;
    de->d_off = off + 1;
    de->d_reclen = (uint16_t)reclen;
    de->d_type = files[i].dt;
    for (int j = 0; j < namelen; j++) de->d_name[j] = files[i].name[j];
    if (out_next) *out_next = off + 1;
    return reclen;
}
```

- [ ] **Step 5: Build + boot smoke**

Run: `make qemu`. From the shell:

```
cat /proc/1/status        # prints Name:, State:, Pid: 1, PPid: 0, Uid:, Gid:, VmSize:
cat /proc/1/cmdline       # prints "proc"
cat /proc/1/stat          # one line, state letter present
```

- [ ] **Step 6: Commit**

```bash
git add kernel/fs/procfs.c
git commit -m "procfs: per-pid status/cmdline/stat"
```

---

### Task 17: Add `/proc/self` symlink

**Files:**
- Modify: `kernel/fs/procfs.c`

- [ ] **Step 1: Add `self` inode + readlink**

Near the static files block, add:

```c
static int self_readlink(struct inode *ip, char *buf, uint64_t n) {
    (void)ip;
    struct pcb *p = current_proc();
    if (!p) return -EIO;
    char tmp[24];
    int  i = 0;
    static const char prefix[] = "/proc/";
    for (int k = 0; k < (int)sizeof(prefix) - 1; k++) tmp[i++] = prefix[k];
    i += u64_to_dec(tmp + i, (int)sizeof(tmp) - i, (uint64_t)p->pid);
    int copy = i < (int)n ? i : (int)n;
    for (int k = 0; k < copy; k++) buf[k] = tmp[k];
    return copy;
}

static int self_stat(struct inode *ip, struct stat *st) {
    file_stat_generic(ip, st);
    st->st_size = 8;     /* approx; not used */
    return 0;
}

static const struct inode_ops self_ops = {
    .stat     = self_stat,
    .readlink = self_readlink,
};

static struct inode self_inode;
```

- [ ] **Step 2: Initialize in `procfs_init`**

Inside `procfs_init`, before the mount call:

```c
    self_inode.type        = I_LNK;
    self_inode.mode        = S_IFLNK | 0777;
    self_inode.uid = self_inode.gid = 0;
    self_inode.size = 8;
    self_inode.mtime = 0;
    self_inode.nlink = 1;
    self_inode.refcnt = 1;
    self_inode.ops = &self_ops;
    self_inode.fs_data = 0;
    self_inode.mount_child = self_inode.mount_parent = 0;
```

- [ ] **Step 3: Resolve `self` in `proc_root_lookup` and emit in `proc_root_getdents`**

In `proc_root_lookup`, alongside the static files (or as an early branch), add:

```c
    if (name[0] == 's' && name[1] == 'e' && name[2] == 'l' &&
        name[3] == 'f' && name[4] == 0) {
        *out = inode_get(&self_inode); return 0;
    }
```

In `proc_root_getdents`, extend `stat_ents` to include `{ "self", &self_inode, DT_UNKNOWN }`. Bump `nstat` to 5.

- [ ] **Step 4: Build + boot smoke**

Run: `make qemu`. From the shell: `readlink /proc/self` should print `/proc/<sh_pid>`. (This requires a `readlink(1)` binary; if absent, defer the manual check to the test program in Task 18.)

- [ ] **Step 5: Commit**

```bash
git add kernel/fs/procfs.c
git commit -m "procfs: /proc/self symlink"
```

---

### Task 18: Write `bin/proc_test`

**Files:**
- Create: `bin/proc_test/proc_test.c`

- [ ] **Step 1: Write the test**

```c
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails = 0;
static void check(int cond, const char *name) {
    if (cond) printf("[proc_test] PASS  %s\n", name);
    else    { printf("[proc_test] FAIL  %s\n", name); fails++; }
}

static int read_all(const char *path, char *buf, int cap) {
    int fd = open(path, 0);
    if (fd < 0) return fd;
    int total = 0;
    while (total < cap - 1) {
        int r = read(fd, buf + total, cap - 1 - total);
        if (r <= 0) break;
        total += r;
    }
    close(fd);
    buf[total] = 0;
    return total;
}

static int contains(const char *hay, const char *needle) {
    int hlen = (int)strlen(hay), nlen = (int)strlen(needle);
    for (int i = 0; i + nlen <= hlen; i++) {
        int j = 0;
        while (j < nlen && hay[i + j] == needle[j]) j++;
        if (j == nlen) return 1;
    }
    return 0;
}

int main(void) {
    printf("=== proc_test ===\n");

    /* /proc/uptime is non-empty + has a dot. */
    char buf[1024];
    int n = read_all("/proc/uptime", buf, sizeof(buf));
    check(n > 0, "/proc/uptime non-empty");
    check(contains(buf, "."),  "/proc/uptime has '.'");

    /* /proc/meminfo contains MemFree:. */
    n = read_all("/proc/meminfo", buf, sizeof(buf));
    check(n > 0,                       "/proc/meminfo non-empty");
    check(contains(buf, "MemFree:"),   "/proc/meminfo has MemFree:");

    /* /proc/version starts with SBUnix. */
    n = read_all("/proc/version", buf, sizeof(buf));
    check(n > 0 && contains(buf, "SBUnix"), "/proc/version has SBUnix");

    /* /proc/<self_pid>/status has correct Pid line. */
    int my = getpid();
    char path[64];
    int pl = 0;
    static const char p1[] = "/proc/";
    for (int i = 0; i < (int)sizeof(p1) - 1; i++) path[pl++] = p1[i];
    {
        int v = my; char tmp[16]; int ti = 0;
        if (!v) tmp[ti++] = '0';
        else while (v) { tmp[ti++] = (char)('0' + v % 10); v /= 10; }
        for (int j = ti - 1; j >= 0; j--) path[pl++] = tmp[j];
    }
    static const char p2[] = "/status";
    for (int i = 0; i < (int)sizeof(p2) - 1; i++) path[pl++] = p2[i];
    path[pl] = 0;

    n = read_all(path, buf, sizeof(buf));
    check(n > 0, "open+read /proc/<self>/status");

    /* Build "Pid:\t<my>" needle. */
    char need[24]; int ni = 0;
    static const char pn[] = "Pid:\t";
    for (int i = 0; i < (int)sizeof(pn) - 1; i++) need[ni++] = pn[i];
    int v = my; char tmp[16]; int ti = 0;
    if (!v) tmp[ti++] = '0';
    else while (v) { tmp[ti++] = (char)('0' + v % 10); v /= 10; }
    for (int j = ti - 1; j >= 0; j--) need[ni++] = tmp[j];
    need[ni] = 0;
    check(contains(buf, need), "status has correct Pid:");

    /* readlink /proc/self == /proc/<my>. */
    char rbuf[64];
    long rl = readlink("/proc/self", rbuf, sizeof(rbuf) - 1);
    check(rl > 0, "readlink /proc/self");
    if (rl > 0) rbuf[rl] = 0;
    char want[32]; int wi = 0;
    for (int i = 0; i < (int)sizeof(p1) - 1; i++) want[wi++] = p1[i];
    int vv = my; char tt[16]; int tn = 0;
    if (!vv) tt[tn++] = '0';
    else while (vv) { tt[tn++] = (char)('0' + vv % 10); vv /= 10; }
    for (int j = tn - 1; j >= 0; j--) want[wi++] = tt[j];
    want[wi] = 0;
    check(strcmp(rbuf, want) == 0, "readlink /proc/self matches /proc/<pid>");

    /* Bogus pid → ENOENT. */
    int fd = open("/proc/999999/status", 0);
    check(fd == -ENOENT, "open /proc/999999/status → ENOENT");

    /* Leak check: 100 open/close cycles on /proc/<self>/status. */
    long mem_before = meminfo();
    for (int i = 0; i < 100; i++) {
        int f = open(path, 0);
        if (f >= 0) close(f);
    }
    long mem_after = meminfo();
    check(mem_before == mem_after, "no procfs leak after 100 opens");

    printf("=== proc_test: %d failures ===\n", fails);
    return fails;
}
```

- [ ] **Step 2: Build**

Run: `make`
Expected: `build/rootfs/bin/proc_test` produced.

- [ ] **Step 3: Commit**

```bash
git add bin/proc_test/proc_test.c
git commit -m "bin/proc_test: cover static + per-pid + self symlink + leaks"
```

---

### Task 19: Write `bin/ps`

**Files:**
- Create: `bin/ps/ps.c`

- [ ] **Step 1: Write `ps`**

```c
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Parse "<key>:\t<value>\n" — returns the value or NULL. */
static const char *find_field(const char *blob, const char *key, char *out, int cap) {
    int klen = (int)strlen(key);
    for (const char *p = blob; *p; p++) {
        int j = 0;
        while (j < klen && p[j] == key[j]) j++;
        if (j == klen && p[j] == ':') {
            const char *v = p + j + 1;
            while (*v == ' ' || *v == '\t') v++;
            int n = 0;
            while (v[n] && v[n] != '\n' && n < cap - 1) { out[n] = v[n]; n++; }
            out[n] = 0;
            return out;
        }
    }
    return 0;
}

static int read_all(const char *path, char *buf, int cap) {
    int fd = open(path, 0);
    if (fd < 0) return fd;
    int total = 0;
    while (total < cap - 1) {
        int r = read(fd, buf + total, cap - 1 - total);
        if (r <= 0) break;
        total += r;
    }
    close(fd);
    buf[total] = 0;
    return total;
}

int main(void) {
    int dfd = open("/proc", 0);
    if (dfd < 0) { printf("ps: open /proc: %d\n", dfd); return 1; }

    printf("  PID S NAME\n");

    char dirbuf[2048];
    long off = 0;
    int  bytes;
    while ((bytes = getdents64(dfd, dirbuf, sizeof(dirbuf))) > 0) {
        int p = 0;
        while (p < bytes) {
            struct dirent64 *de = (struct dirent64 *)(dirbuf + p);
            const char *nm = de->d_name;
            int is_pid = nm[0] >= '0' && nm[0] <= '9';
            if (is_pid) {
                char path[64];
                int  pi = 0;
                static const char p1[] = "/proc/";
                for (int i = 0; i < (int)sizeof(p1) - 1; i++) path[pi++] = p1[i];
                int j = 0;
                while (nm[j]) path[pi++] = nm[j++];
                static const char p2[] = "/status";
                for (int i = 0; i < (int)sizeof(p2) - 1; i++) path[pi++] = p2[i];
                path[pi] = 0;

                char buf[512];
                int  n = read_all(path, buf, sizeof(buf));
                if (n > 0) {
                    char name[32], state[8], pid[16];
                    find_field(buf, "Name", name, sizeof(name));
                    find_field(buf, "State", state, sizeof(state));
                    find_field(buf, "Pid", pid, sizeof(pid));
                    printf("%5s %s %s\n", pid, state, name);
                }
            }
            p += de->d_reclen;
        }
    }
    close(dfd);
    return 0;
}
```

- [ ] **Step 2: Build**

Run: `make`
Expected: `build/rootfs/bin/ps` produced.

- [ ] **Step 3: Commit**

```bash
git add bin/ps/ps.c
git commit -m "bin/ps: list pid/state/name from /proc"
```

---

### Task 20: Wire `proc_test` and `ps` into init, run end-to-end

**Files:**
- Modify: `bin/init/init.c`

- [ ] **Step 1: Add to the init array**

In `bin/init/init.c`, near the other Phase 7/8 entries, add:

```c
        "/bin/proc_test",
        "/bin/ps",
```

- [ ] **Step 2: Build and boot**

Run: `make qemu`
Expected:

```
[proc_test] PASS  /proc/uptime non-empty
[proc_test] PASS  /proc/uptime has '.'
[proc_test] PASS  /proc/meminfo non-empty
[proc_test] PASS  /proc/meminfo has MemFree:
[proc_test] PASS  /proc/version has SBUnix
[proc_test] PASS  open+read /proc/<self>/status
[proc_test] PASS  status has correct Pid:
[proc_test] PASS  readlink /proc/self
[proc_test] PASS  readlink /proc/self matches /proc/<pid>
[proc_test] PASS  open /proc/999999/status → ENOENT
[proc_test] PASS  no procfs leak after 100 opens
=== proc_test: 0 failures ===
init: 'proc_test' exited with status 0
  PID S NAME
    1 S proc
   ...
init: 'ps' exited with status 0
```

If any check fails or `ps` prints nothing, debug before committing.

- [ ] **Step 3: Commit**

```bash
git add bin/init/init.c
git commit -m "init: run proc_test and ps"
```

---

## Self-Review

**Spec coverage:** every spec section maps to a task — §4 (P1 mechanism) → Tasks 1, 2, 4, 5; §4.3 syscalls → Tasks 6, 7; §4.5 libc → Task 8; §4.6 fixture + tests → Tasks 3, 9, 10. §5 (procfs) → Tasks 11–17, with §5.5 (`/proc/self`) explicitly in Task 17. §5.8 (`bin/ps`) → Task 19. §5.9 (`bin/proc_test`) → Task 18. §9 exit criteria reflected in Task 20's expected output.

**Placeholder scan:** no TBDs or "implement later" markers. The few "if your tree differs, look at neighbors" notes (libc syscall convention, pmem helper names) are followed by concrete fallback instructions.

**Type consistency:** `proc_node`, `proc_kind`, and `pool_alloc/pool_free` are defined once (Task 15) and reused (Tasks 16, 17). `piddir_ops` and `piddir_file_ops` are the only two ops tables for dynamic procfs entries. `parse_pid`, `u64_to_dec`, `produce_static`, and `file_stat_generic` are each defined once and called consistently downstream.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-04-30-procfs-and-symlinks.md`. Two execution options:

**1. Subagent-Driven (recommended)** — Dispatch a fresh subagent per task with two-stage review between tasks. Lowest risk for kernel-level changes.

**2. Inline Execution** — Execute tasks here using `superpowers:executing-plans` with checkpoints after each phase boundary (P1 done, P2 done).

A third route is available since you asked: dispatch each task to **`codex:codex-rescue`** through the Agent tool. That keeps you in control of review while Codex does the keyboarding.

Which approach?
