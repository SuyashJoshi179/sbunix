# Audit Items 9a / 9b / 14d Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the three remaining audit-driven items — `O_CREAT|O_EXCL` error semantics, tarfs `getdents` emitting `.`/`..`, and `O_CLOEXEC` + a real `SYS_fcntl` (`F_GETFD`/`F_SETFD`).

**Architecture:** Three independent changes, each on its own feature branch off `develop`, each test-first. 9a reuses an existing candidate's code applied TDD-style. 9b mirrors tmpfs's cursor-based `getdents`. 14d adds a per-process `cloexec_mask` bitmap to `struct pcb`, honored by `open`/`dup`/`dup2`/`close`/`exec`, plus a new `SYS_fcntl` syscall and a libc rewire.

**Tech Stack:** C, RISC-V 64 freestanding kernel, QEMU `virt`, in-tree libc, GNU make.

---

## Reference facts (verified against `develop` HEAD `e052884`)

- **Build:** `make` builds `build/kernel.elf`. The Makefile has **no header dependency tracking** — after editing any `kernel/include/*.h`, you **must** `make clean && make`, or stale `.o` files cause phantom struct-corruption panics.
- **Boot + capture:** `printf ' \nexit\n' | timeout 240 make -s qemu > /tmp/run.log 2>&1`. The leading space before `\n` absorbs QEMU's first-char eat. QEMU serial output has `\r` line endings — grep with `-a`.
- **Check results:** `grep -aE "<test name>|init: [0-9]+/|FAIL|panic" /tmp/run.log`. Baseline on `develop`: `init: 113/113`, kernel selftest all PASS, no panic.
- **New user binary:** create `bin/<name>/<name>.c`; the Makefile wildcard builds it to `/bin/<name>` in the running OS automatically. Add test binaries to the test list in `bin/init/init.c` so the kernel runs them at boot. A helper that is `execv`'d by a test (not run standalone) is **not** added to `init.c`.
- **Commits:** no `Co-Authored-By` / Claude attribution. Never commit to `develop`.

---

## Task 0: Confirm clean baseline

**Files:** none (verification only)

- [ ] **Step 1: Confirm on develop with a clean tree**

Run: `git branch --show-current && git status --short`
Expected: branch is `develop` (or switch with `git checkout develop`). Untracked session files (`.claude/`, `HANDOFF.md`, `bin/*_probe/`, etc.) are fine — there must be no *modified tracked* files. If there are, stop and resolve before continuing.

- [ ] **Step 2: Build and boot baseline**

Run:
```bash
make clean > /dev/null && make > /tmp/build.log 2>&1 && echo BUILD_OK
printf ' \nexit\n' | timeout 240 make -s qemu > /tmp/run.log 2>&1
grep -aE "init: [0-9]+/|FAIL|panic" /tmp/run.log
```
Expected: `BUILD_OK`, `init: 113/113 tests passed`, no `FAIL`, no `panic`.

---

# Task Group A — Item 9a: O_EXCL (branch `feature/open-o-excl`)

`open(path, O_CREAT|O_EXCL)` on an existing file currently falls through and reopens the file instead of failing with `EEXIST`. The fix and its test already exist as candidate commit `2590cc5`; this group applies that content test-first onto a fresh branch off current `develop`.

## Task A1: Watch the test fail on `develop`

**Files:**
- Create (working tree only, discarded after): `bin/o_excl_test/o_excl_test.c`
- Modify (working tree only, discarded after): `bin/init/init.c`

- [ ] **Step 1: Create the test binary**

Create `bin/o_excl_test/o_excl_test.c`:
```c
// o_excl_test: open(O_CREAT|O_EXCL) on an existing file must fail with EEXIST.
//
// POSIX: if O_CREAT and O_EXCL are set, open() shall fail with [EEXIST] when
// the named file exists. Without O_EXCL, the same call shall reopen it.
//
// /tmp is tmpfs (writable, in-RAM) so the test is self-contained.
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

static int pass_cnt = 0, fail_cnt = 0;

static void chk(int cond, const char *msg) {
    if (cond) { printf("[o_excl_test] PASS %s\n", msg); pass_cnt++; }
    else      { printf("[o_excl_test] FAIL %s\n", msg); fail_cnt++; }
}

int main(void) {
    const char *path = "/tmp/o_excl_test.tmp";
    (void)unlink(path);  /* best-effort cleanup from a prior run */

    /* 1. Fresh create with O_EXCL: must succeed. */
    errno = 0;
    int fd = open(path, O_RDWR | O_CREAT | O_EXCL);
    chk(fd >= 0, "O_CREAT|O_EXCL on new file succeeds");
    if (fd >= 0) close(fd);

    /* 2. Re-create with O_EXCL on the now-existing file: must fail with EEXIST. */
    errno = 0;
    int rc = open(path, O_RDWR | O_CREAT | O_EXCL);
    chk(rc == -1 && errno == EEXIST,
        "O_CREAT|O_EXCL on existing file -> -1/EEXIST");
    if (rc >= 0) close(rc);

    /* 3. O_CREAT without O_EXCL on existing file: must reopen. */
    errno = 0;
    fd = open(path, O_RDWR | O_CREAT);
    chk(fd >= 0, "O_CREAT (no EXCL) on existing file reopens");
    if (fd >= 0) close(fd);

    (void)unlink(path);

    if (fail_cnt == 0)
        printf("o_excl_test: PASS (%d tests)\n", pass_cnt);
    else
        printf("o_excl_test: FAIL (%d/%d failed)\n", fail_cnt,
               pass_cnt + fail_cnt);
    return fail_cnt ? 1 : 0;
}
```

- [ ] **Step 2: Register the test in init**

In `bin/init/init.c`, find the line `"/bin/open_read_test",` in the test-list array and add a new line immediately after it:
```c
        "/bin/o_excl_test",
```

- [ ] **Step 3: Build, boot, watch it fail**

Run:
```bash
make > /tmp/build.log 2>&1 && echo BUILD_OK
printf ' \nexit\n' | timeout 240 make -s qemu > /tmp/run.log 2>&1
grep -aE "o_excl_test" /tmp/run.log
```
Expected: `BUILD_OK`; `o_excl_test` reports `FAIL` on `O_CREAT|O_EXCL on existing file -> -1/EEXIST` and the summary line `o_excl_test: FAIL (1/3 failed)`. (Cases 1 and 3 pass; case 2 fails because the kernel reopens instead of returning `EEXIST`.)

- [ ] **Step 4: Discard the working-tree changes**

Run: `git checkout bin/init/init.c && rm -rf bin/o_excl_test`
Expected: `git status --short` shows no modified tracked files again.

## Task A2: Apply the fix on a fresh branch

**Files:**
- Create: `bin/o_excl_test/o_excl_test.c`
- Modify: `bin/init/init.c`
- Modify: `kernel/syscall.c` (`sys_open`, the `namei`-result chain ~line 252)

- [ ] **Step 1: Create the branch off current develop**

`feature/open-o-excl` already exists, based on an old `develop`. Reset it to current `develop` (the only commit on it, `2590cc5`, is reproduced verbatim below, so nothing is lost):
```bash
git checkout develop
git checkout -B feature/open-o-excl develop
```
Expected: `Switched to and reset branch 'feature/open-o-excl'`.

- [ ] **Step 2: Re-create the test binary**

Create `bin/o_excl_test/o_excl_test.c` with the exact content from Task A1 Step 1.

- [ ] **Step 3: Register the test in init**

In `bin/init/init.c`, add `"/bin/o_excl_test",` immediately after `"/bin/open_read_test",` (same as Task A1 Step 2).

- [ ] **Step 4: Add the kernel fix**

In `kernel/syscall.c`, in `sys_open`, locate this existing block (the `namei`-result chain):
```c
        int crc = parent->ops->create(parent, leaf, &ip);
        inode_put(parent);
        if (crc < 0) return crc;
    } else if (rc < 0) {
        return rc;
    }
```
Add a third arm so it becomes:
```c
        int crc = parent->ops->create(parent, leaf, &ip);
        inode_put(parent);
        if (crc < 0) return crc;
    } else if (rc < 0) {
        return rc;
    } else if ((flags & 0100) && (flags & 0200) /* O_CREAT|O_EXCL */) {
        /* POSIX: with both O_CREAT and O_EXCL, an existing target is a
         * hard error — don't reopen, don't truncate. */
        inode_put(ip);
        return -EEXIST;
    }
```

- [ ] **Step 5: Build, boot, watch it pass**

Run:
```bash
make > /tmp/build.log 2>&1 && echo BUILD_OK
printf ' \nexit\n' | timeout 240 make -s qemu > /tmp/run.log 2>&1
grep -aE "o_excl_test|init: [0-9]+/|FAIL|panic" /tmp/run.log
```
Expected: `BUILD_OK`; `o_excl_test: PASS (3 tests)`; `init: 114/114 tests passed`; no `FAIL`, no `panic`.

- [ ] **Step 6: Commit**

```bash
git add bin/o_excl_test/o_excl_test.c bin/init/init.c kernel/syscall.c
git commit -m "$(cat <<'EOF'
fix(open): honor O_CREAT|O_EXCL on an existing target

sys_open's create path was guarded only on namei returning -ENOENT plus
O_CREAT; when namei succeeded (file exists) the code fell through to
filealloc regardless of O_EXCL. POSIX requires open() to fail with
EEXIST when both O_CREAT and O_EXCL are set and the named file already
exists. Add a third arm to the namei-result chain: when the inode
resolved and the caller requested O_CREAT|O_EXCL, drop the reference and
return -EEXIST.

bin/o_excl_test covers fresh-create-with-EXCL, recreate-with-EXCL (must
fail EEXIST), and recreate-without-EXCL (must reopen).
EOF
)"
```
Expected: commit succeeds; `git log --oneline -1` shows the new commit.

---

# Task Group B — Item 9b: tarfs `getdents` emits `.` / `..` (branch `feature/tarfs-getdents-dots`)

`tarfs_getdents` (kernel/fs/tarfs.c) walks only the directory's `children` list and never emits `.` or `..`; tmpfs, sbfs, and procfs all do. Fix by mirroring `tmpfs_op_getdents`'s cursor scheme.

## Task B1: Write the failing test

**Files:**
- Create: `bin/tarfs_dots_test/tarfs_dots_test.c`
- Modify: `bin/init/init.c`

- [ ] **Step 1: Create the branch**

```bash
git checkout develop
git checkout -b feature/tarfs-getdents-dots
```
Expected: `Switched to a new branch 'feature/tarfs-getdents-dots'`.

- [ ] **Step 2: Create the test binary**

`/bin` is a pure tarfs directory. `readdir` in libc passes `getdents64` results through unfiltered, so this test reflects the kernel's tarfs behavior directly.

Create `bin/tarfs_dots_test/tarfs_dots_test.c`:
```c
// tarfs_dots_test: a tarfs directory must expose "." and ".." via getdents.
//
// /bin is served entirely by tarfs. libc readdir() forwards raw getdents64
// records, so a missing "."/".." here means the kernel's tarfs_getdents
// omitted them.
#include <dirent.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    int pass = 0, fail = 0;

    DIR *d = opendir("/bin");
    if (!d) {
        printf("[tarfs_dots_test] FAIL opendir(/bin)\n");
        printf("tarfs_dots_test: FAIL (1/1 failed)\n");
        return 1;
    }

    int saw_dot = 0, saw_dotdot = 0, total = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        total++;
        if (strcmp(e->d_name, ".")  == 0) saw_dot++;
        if (strcmp(e->d_name, "..") == 0) saw_dotdot++;
    }
    closedir(d);

    if (saw_dot == 1) { printf("[tarfs_dots_test] PASS '.' present exactly once\n"); pass++; }
    else { printf("[tarfs_dots_test] FAIL '.' count=%d\n", saw_dot); fail++; }

    if (saw_dotdot == 1) { printf("[tarfs_dots_test] PASS '..' present exactly once\n"); pass++; }
    else { printf("[tarfs_dots_test] FAIL '..' count=%d\n", saw_dotdot); fail++; }

    if (fail == 0)
        printf("tarfs_dots_test: PASS (%d entries scanned, %d checks)\n", total, pass);
    else
        printf("tarfs_dots_test: FAIL (%d/%d failed)\n", fail, pass + fail);
    return fail ? 1 : 0;
}
```

- [ ] **Step 3: Register the test in init**

In `bin/init/init.c`, add `"/bin/tarfs_dots_test",` to the test-list array, immediately after the existing `"/bin/getdents_test",` line.

- [ ] **Step 4: Build, boot, watch it fail**

Run:
```bash
make > /tmp/build.log 2>&1 && echo BUILD_OK
printf ' \nexit\n' | timeout 240 make -s qemu > /tmp/run.log 2>&1
grep -aE "tarfs_dots_test" /tmp/run.log
```
Expected: `BUILD_OK`; `tarfs_dots_test` reports `FAIL '.' count=0` and `FAIL '..' count=0`, summary `tarfs_dots_test: FAIL (2/2 failed)`.

- [ ] **Step 5: Commit the test**

```bash
git add bin/tarfs_dots_test/tarfs_dots_test.c bin/init/init.c
git commit -m "$(cat <<'EOF'
test(tarfs): getdents on a tarfs directory should emit . and ..

Adds bin/tarfs_dots_test, which scans /bin (pure tarfs) and asserts both
"." and ".." appear exactly once. Fails against the current
tarfs_getdents, which walks only the children list.
EOF
)"
```

## Task B2: Make the test pass

**Files:**
- Modify: `kernel/fs/tarfs.c` — replace the body of `tarfs_getdents`

- [ ] **Step 1: Rewrite `tarfs_getdents`**

In `kernel/fs/tarfs.c`, replace the entire existing `tarfs_getdents` function:
```c
static int tarfs_getdents(struct inode *dir, uint64_t off, void *buf,
                           uint64_t n, uint64_t *out_next) {
    struct tarfs_ino_data *d = dir->fs_data;

    // Walk to the 'off'-th child.
    struct tarfs_child *c = d->children;
    uint64_t idx = 0;
    while (c && idx < off) { c = c->next; idx++; }

    uint64_t written  = 0;
    uint64_t next_off = off;

    while (c) {
        int namelen = 0;
        while (c->name[namelen]) namelen++;
        namelen++;  // include null terminator

        int reclen = (DIRENT64_FIXED_LEN + namelen + 7) & ~7;
        if (written + (uint64_t)reclen > n) break;

        struct dirent64 *de = (struct dirent64 *)((char *)buf + written);
        de->d_ino    = (uint64_t)(uintptr_t)c->ino;
        de->d_off    = next_off + 1;
        de->d_reclen = (uint16_t)reclen;
        de->d_type   = (c->ino->type == I_DIR) ? DT_DIR :
                       (c->ino->type == I_CHR) ? DT_CHR :
                       (c->ino->type == I_LNK) ? DT_LNK : DT_REG;
        for (int i = 0; i < namelen; i++) de->d_name[i] = c->name[i];

        written  += (uint64_t)reclen;
        next_off++;
        c = c->next;
    }

    if (out_next) *out_next = next_off;
    return (int)written;
}
```
with this version:
```c
static int tarfs_getdents(struct inode *dir, uint64_t off, void *buf,
                           uint64_t n, uint64_t *out_next) {
    struct tarfs_ino_data *d = dir->fs_data;

    uint64_t written = 0;
    uint64_t cursor  = off;

    /* Emit one dirent if it fits; otherwise stop. `cursor` is an opaque
     * stream position: 0 = ".", 1 = "..", 2+ = children[cursor - 2]. */
    #define EMIT(name_str, ino_ptr, dtype) do {                          \
        int namelen = 0;                                                  \
        while ((name_str)[namelen]) namelen++;                            \
        namelen++;                                                        \
        int reclen = (DIRENT64_FIXED_LEN + namelen + 7) & ~7;             \
        if (written + (uint64_t)reclen > n) goto done;                    \
        struct dirent64 *de = (struct dirent64 *)((char *)buf + written); \
        de->d_ino    = (uint64_t)(uintptr_t)(ino_ptr);                    \
        de->d_off    = cursor + 1;                                        \
        de->d_reclen = (uint16_t)reclen;                                  \
        de->d_type   = (dtype);                                           \
        for (int i = 0; i < namelen; i++) de->d_name[i] = (name_str)[i];  \
        written += (uint64_t)reclen;                                      \
        cursor++;                                                         \
    } while (0)

    /* Mirror tarfs_lookup: ".." of the root resolves to the root itself. */
    if (cursor == 0) EMIT(".",  dir,                         DT_DIR);
    if (cursor == 1) EMIT("..", d->parent ? d->parent : dir, DT_DIR);

    /* Walk to children[cursor - 2]. */
    uint64_t skip = (cursor >= 2) ? cursor - 2 : 0;
    struct tarfs_child *c = d->children;
    while (c && skip > 0) { c = c->next; skip--; }

    while (c) {
        EMIT(c->name, c->ino,
             (c->ino->type == I_DIR) ? DT_DIR :
             (c->ino->type == I_CHR) ? DT_CHR :
             (c->ino->type == I_LNK) ? DT_LNK : DT_REG);
        c = c->next;
    }

done:
    if (out_next) *out_next = cursor;
    return (int)written;

    #undef EMIT
}
```

- [ ] **Step 2: Build, boot, watch it pass**

Run:
```bash
make > /tmp/build.log 2>&1 && echo BUILD_OK
printf ' \nexit\n' | timeout 240 make -s qemu > /tmp/run.log 2>&1
grep -aE "tarfs_dots_test|getdents_test|init: [0-9]+/|FAIL|panic" /tmp/run.log
```
Expected: `BUILD_OK`; `tarfs_dots_test: PASS`; `getdents_test` still PASS (no regression); `init: 114/114 tests passed`; no `FAIL`, no `panic`.

- [ ] **Step 3: Commit**

```bash
git add kernel/fs/tarfs.c
git commit -m "$(cat <<'EOF'
feat(tarfs): emit . and .. from getdents

tarfs_getdents walked only the children list, so a tarfs directory
exposed no "." or ".." entries — unlike tmpfs, sbfs, and procfs. Switch
to the same opaque-cursor scheme tmpfs uses: cursor 0 = ".", 1 = "..",
2+ = children[cursor - 2]. ".." of the tarfs root resolves to the root
itself, matching tarfs_lookup.
EOF
)"
```

---

# Task Group C — Item 14d: O_CLOEXEC + SYS_fcntl (branch `feature/o-cloexec`)

The kernel ignores `O_CLOEXEC` and there is no `fcntl` syscall (`fcntl` is a libc stub that fakes `F_GETFD`/`F_SETFD`). Add a per-process `cloexec_mask` bitmap to `struct pcb`, honored by `open`/`close`/`dup`/`dup2`/`exec`, plus a real `SYS_fcntl` for `F_GETFD`/`F_SETFD`, and rewire the libc stub.

**Build note:** Task C2 edits `kernel/include/proc.h`. From that point on, **every build in this group must be `make clean && make`** — the Makefile has no header dependency tracking and a stale build produces phantom panics.

## Task C1: Write the failing tests

**Files:**
- Create: `bin/cloexec_helper/cloexec_helper.c`
- Create: `bin/cloexec_test/cloexec_test.c`
- Modify: `bin/init/init.c` (register `cloexec_test` only — **not** the helper)

- [ ] **Step 1: Create the branch**

`feature/o-cloexec` already exists but has no commits beyond `develop`, so resetting it loses nothing:
```bash
git checkout develop
git checkout -B feature/o-cloexec develop
```

- [ ] **Step 2: Create the helper binary**

Create `bin/cloexec_helper/cloexec_helper.c`:
```c
// cloexec_helper <cloexec_fd> <kept_fd>
//
// Exec'd by cloexec_test after fork. Verifies that the descriptor opened
// with O_CLOEXEC was closed across exec, while the plain descriptor
// survived. Exits 0 only when both hold. Not a standalone test — not
// listed in init.c.
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("[cloexec_helper] FAIL bad argc=%d\n", argc);
        return 2;
    }
    int cl = atoi(argv[1]);   // expected CLOSED (opened with O_CLOEXEC)
    int kp = atoi(argv[2]);   // expected OPEN   (plain descriptor)

    int cl_state = fcntl(cl, F_GETFD);   // < 0  => fd is closed
    int kp_state = fcntl(kp, F_GETFD);   // >= 0 => fd is open

    if (cl_state < 0 && kp_state >= 0) {
        printf("[cloexec_helper] PASS cloexec fd closed, plain fd kept\n");
        return 0;
    }
    printf("[cloexec_helper] FAIL cl_state=%d kp_state=%d\n",
           cl_state, kp_state);
    return 1;
}
```

- [ ] **Step 3: Create the test binary**

Create `bin/cloexec_test/cloexec_test.c`:
```c
// cloexec_test: O_CLOEXEC, fcntl(F_GETFD/F_SETFD), and exec-closes-cloexec.
//
//  1. open(O_CLOEXEC) sets FD_CLOEXEC; fcntl(F_GETFD) reports it.
//  2. plain open() leaves it clear; fcntl(F_SETFD) sets/clears it.
//  3. after fork+exec, the FD_CLOEXEC descriptor is closed in the new
//     image while a plain descriptor survives (checked by cloexec_helper).
//
// /tmp is tmpfs (writable) so the test is self-contained.
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

static int pass_cnt = 0, fail_cnt = 0;
static void chk(int cond, const char *msg) {
    if (cond) { printf("[cloexec_test] PASS %s\n", msg); pass_cnt++; }
    else      { printf("[cloexec_test] FAIL %s\n", msg); fail_cnt++; }
}

int main(void) {
    const char *pa = "/tmp/cloexec_a.tmp";
    const char *pb = "/tmp/cloexec_b.tmp";
    (void)unlink(pa);
    (void)unlink(pb);

    /* 1. O_CLOEXEC sets the descriptor flag. */
    int fd_cl = open(pa, O_RDWR | O_CREAT | O_CLOEXEC);
    chk(fd_cl >= 0, "open(O_CLOEXEC) succeeds");
    chk(fd_cl >= 0 && fcntl(fd_cl, F_GETFD) == FD_CLOEXEC,
        "F_GETFD reports FD_CLOEXEC after O_CLOEXEC open");

    /* 2. Plain open leaves it clear; F_SETFD toggles it. */
    int fd_kp = open(pb, O_RDWR | O_CREAT);
    chk(fd_kp >= 0, "open() without O_CLOEXEC succeeds");
    chk(fd_kp >= 0 && fcntl(fd_kp, F_GETFD) == 0,
        "F_GETFD reports 0 for a plain fd");
    chk(fcntl(fd_kp, F_SETFD, FD_CLOEXEC) == 0 &&
        fcntl(fd_kp, F_GETFD) == FD_CLOEXEC,
        "F_SETFD sets FD_CLOEXEC");
    chk(fcntl(fd_kp, F_SETFD, 0) == 0 &&
        fcntl(fd_kp, F_GETFD) == 0,
        "F_SETFD clears FD_CLOEXEC");

    /* 3. exec closes the O_CLOEXEC fd, keeps the plain one. fd_kp was
     *    left non-cloexec by step 2. */
    char acl[16], akp[16];
    snprintf(acl, sizeof(acl), "%d", fd_cl);
    snprintf(akp, sizeof(akp), "%d", fd_kp);
    int pid = fork();
    if (pid == 0) {
        char *av[] = { "cloexec_helper", acl, akp, 0 };
        execv("/bin/cloexec_helper", av);
        _exit(127);   /* exec failed */
    }
    chk(pid > 0, "fork before exec");
    int status = 0;
    int w = wait(&status);
    chk(w == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0,
        "exec'd helper confirms cloexec fd closed, plain fd kept");

    (void)unlink(pa);
    (void)unlink(pb);

    if (fail_cnt == 0)
        printf("cloexec_test: PASS (%d tests)\n", pass_cnt);
    else
        printf("cloexec_test: FAIL (%d/%d failed)\n", fail_cnt,
               pass_cnt + fail_cnt);
    return fail_cnt ? 1 : 0;
}
```

- [ ] **Step 4: Register the test in init**

In `bin/init/init.c`, add `"/bin/cloexec_test",` to the test-list array, immediately after the existing `"/bin/dup_test",` line. Do **not** add `cloexec_helper`.

- [ ] **Step 5: Build, boot, watch it fail**

Run:
```bash
make > /tmp/build.log 2>&1 && echo BUILD_OK
printf ' \nexit\n' | timeout 240 make -s qemu > /tmp/run.log 2>&1
grep -aE "cloexec" /tmp/run.log
```
Expected: `BUILD_OK` (the test compiles — `fcntl`/`F_GETFD`/`O_CLOEXEC` already exist in libc/headers). `cloexec_test` reports multiple `FAIL`s: `F_GETFD reports FD_CLOEXEC...` fails (libc stub returns 0), and `exec'd helper confirms...` fails (kernel ignores O_CLOEXEC, so the fd survives exec).

- [ ] **Step 6: Commit the tests**

```bash
git add bin/cloexec_helper/cloexec_helper.c bin/cloexec_test/cloexec_test.c bin/init/init.c
git commit -m "$(cat <<'EOF'
test(cloexec): O_CLOEXEC, fcntl(F_GETFD/F_SETFD), exec-closes-cloexec-fds

bin/cloexec_test exercises O_CLOEXEC on open, F_GETFD/F_SETFD round-trip,
and (via bin/cloexec_helper, exec'd after fork) that an O_CLOEXEC fd is
closed across exec while a plain fd survives. Fails today: the libc fcntl
stub fakes F_GETFD, and the kernel ignores O_CLOEXEC entirely.
EOF
)"
```

## Task C2: Add the `cloexec_mask` field and plumb fork/alloc

**Files:**
- Modify: `kernel/include/proc.h` (`struct pcb`)
- Modify: `kernel/proc.c` (`alloc_proc`, `proc_fork_current`)

- [ ] **Step 1: Add the field to `struct pcb`**

In `kernel/include/proc.h`, locate:
```c
    // File descriptor table (Phase 4).
    struct file   *ofile[NOFILE];   // open files; null = free slot
    struct inode  *cwd;             // current working directory (refcounted)
```
and insert the `cloexec_mask` line between `ofile` and `cwd`:
```c
    // File descriptor table (Phase 4).
    struct file   *ofile[NOFILE];   // open files; null = free slot
    uint64_t       cloexec_mask;    // bit fd set => FD_CLOEXEC; NOFILE==64
    struct inode  *cwd;             // current working directory (refcounted)
```

- [ ] **Step 2: Initialize it in `alloc_proc`**

In `kernel/proc.c`, in `alloc_proc`, locate:
```c
    p->brk_start  = 0;
    p->next       = 0;
```
and insert the init line between them:
```c
    p->brk_start  = 0;
    p->cloexec_mask = 0;
    p->next       = 0;
```

- [ ] **Step 3: Copy it across fork**

In `kernel/proc.c`, in `proc_fork_current`, locate:
```c
    // Duplicate open file descriptors into the child.
    for (int fd = 0; fd < NOFILE; fd++) {
        if (parent->ofile[fd])
            child->ofile[fd] = filedup(parent->ofile[fd]);
    }
```
and add the mask copy immediately after the loop:
```c
    // Duplicate open file descriptors into the child.
    for (int fd = 0; fd < NOFILE; fd++) {
        if (parent->ofile[fd])
            child->ofile[fd] = filedup(parent->ofile[fd]);
    }
    /* POSIX: fork preserves FD_CLOEXEC on inherited descriptors. */
    child->cloexec_mask = parent->cloexec_mask;
```

## Task C3: Honor the mask in open/close/dup/dup2/exec

**Files:**
- Modify: `kernel/syscall.c` (`sys_open`, `sys_close`, `sys_dup`, `sys_dup2`, `do_exec`)

- [ ] **Step 1: Set the bit on `open(O_CLOEXEC)`**

In `kernel/syscall.c`, in `sys_open`, locate the end of the function:
```c
    int fd = alloc_fd(p, f);
    if (fd < 0) { fileclose(f); return fd; }
    return fd;
}
```
and add the O_CLOEXEC handling:
```c
    int fd = alloc_fd(p, f);
    if (fd < 0) { fileclose(f); return fd; }
    if (flags & 02000000 /* O_CLOEXEC */)
        p->cloexec_mask |= (1ULL << fd);
    return fd;
}
```

- [ ] **Step 2: Clear the bit on `close`**

In `kernel/syscall.c`, in `sys_close`, locate:
```c
    fileclose(p->ofile[fd]);
    p->ofile[fd] = 0;
    return 0;
```
and add the clear:
```c
    fileclose(p->ofile[fd]);
    p->ofile[fd] = 0;
    p->cloexec_mask &= ~(1ULL << fd);
    return 0;
```

- [ ] **Step 3: Clear the bit on `dup`**

In `kernel/syscall.c`, in `sys_dup`, locate:
```c
    int newfd = alloc_fd(p, f);
    if (newfd < 0) { fileclose(f); return newfd; }
    return newfd;
```
and add the clear (POSIX: a dup'd fd has FD_CLOEXEC clear):
```c
    int newfd = alloc_fd(p, f);
    if (newfd < 0) { fileclose(f); return newfd; }
    p->cloexec_mask &= ~(1ULL << newfd);
    return newfd;
```

- [ ] **Step 4: Clear the bit on `dup2`**

In `kernel/syscall.c`, in `sys_dup2`, locate:
```c
    if (p->ofile[newfd]) fileclose(p->ofile[newfd]);
    p->ofile[newfd] = filedup(p->ofile[oldfd]);
    return newfd;
```
and add the clear:
```c
    if (p->ofile[newfd]) fileclose(p->ofile[newfd]);
    p->ofile[newfd] = filedup(p->ofile[oldfd]);
    p->cloexec_mask &= ~(1ULL << newfd);
    return newfd;
```
(The `oldfd == newfd` early-return path above is left untouched, which is correct — `dup2(fd, fd)` is a no-op.)

- [ ] **Step 5: Close cloexec fds in `do_exec`**

In `kernel/syscall.c`, in `do_exec`, locate (in the post-`write_satp` commit block, near the on-exec resets):
```c
    p->in_sighandler   = 0;
    p->delivering_segv = 0;
    p->alarm_tick      = 0;        /* POSIX: pending alarm cleared on exec */
    p->did_exec        = 1;
```
and add the cloexec close loop immediately after:
```c
    p->in_sighandler   = 0;
    p->delivering_segv = 0;
    p->alarm_tick      = 0;        /* POSIX: pending alarm cleared on exec */
    p->did_exec        = 1;
    /* POSIX: descriptors marked FD_CLOEXEC are closed across a successful
     * exec. This runs only after the point of no return, so a failed exec
     * leaves the fd table intact. */
    for (int fd = 0; fd < NOFILE; fd++) {
        if ((p->cloexec_mask & (1ULL << fd)) && p->ofile[fd]) {
            fileclose(p->ofile[fd]);
            p->ofile[fd] = 0;
        }
    }
    p->cloexec_mask = 0;
```

## Task C4: Add the `SYS_fcntl` syscall

**Files:**
- Modify: `kernel/include/syscall.h` (new syscall number)
- Modify: `libc/include/sys/syscall.h` (matching number)
- Modify: `kernel/syscall.c` (new `sys_fcntl`, new dispatch case)

- [ ] **Step 1: Add the kernel syscall number**

In `kernel/include/syscall.h`, locate the line `#define SYS_setrlimit     118` and add after it:
```c

// Filesystem: per-descriptor flags
#define SYS_fcntl          119  // (fd, cmd, arg) — F_GETFD / F_SETFD only
```

- [ ] **Step 2: Add the matching libc syscall number**

In `libc/include/sys/syscall.h`, locate the line `#define SYS_setrlimit    118` and add after it:
```c
#define SYS_fcntl        119
```

- [ ] **Step 3: Implement `sys_fcntl`**

In `kernel/syscall.c`, add this function immediately after `sys_dup2` (before `sys_lseek`):
```c
// ---------------------------------------------------------------------------
// sys_fcntl — only the per-descriptor flag commands are kernel-backed.
// F_GETFD / F_SETFD read and write the FD_CLOEXEC bit in cloexec_mask.
// All other fcntl commands (F_DUPFD, F_GETFL/F_SETFL, locks) are handled
// in libc and never reach here.
// ---------------------------------------------------------------------------
static int64_t sys_fcntl(int fd, int cmd, uint64_t arg) {
    struct pcb *p = current_proc();
    if (!p || fd < 0 || fd >= NOFILE || !p->ofile[fd]) return -EBADF;
    switch (cmd) {
    case 1: /* F_GETFD */
        return (p->cloexec_mask & (1ULL << fd)) ? 1 /* FD_CLOEXEC */ : 0;
    case 2: /* F_SETFD */
        if (arg & 1 /* FD_CLOEXEC */) p->cloexec_mask |=  (1ULL << fd);
        else                          p->cloexec_mask &= ~(1ULL << fd);
        return 0;
    default:
        return -EINVAL;
    }
}
```

- [ ] **Step 4: Wire the dispatch case**

In `kernel/syscall.c`, in the syscall dispatch `switch`, locate the `case SYS_dup2:` block:
```c
        case SYS_dup2:
            return sys_dup2((int)(int64_t)trapframe[TF_A0],
                            (int)(int64_t)trapframe[TF_A1]);
```
and add a new case immediately after it:
```c
        case SYS_fcntl:
            return sys_fcntl((int)(int64_t)trapframe[TF_A0],
                             (int)(int64_t)trapframe[TF_A1],
                             trapframe[TF_A2]);
```

## Task C5: Rewire the libc `fcntl` stub

**Files:**
- Modify: `libc/sys_stubs.c` (`fcntl`, includes)

- [ ] **Step 1: Add the syscall.h include**

In `libc/sys_stubs.c`, the include block currently starts:
```c
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
```
Add `#include <sys/syscall.h>` so `SYS_fcntl` and the generic `syscall()` are visible:
```c
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
```

- [ ] **Step 2: Route F_GETFD/F_SETFD to the kernel and fix F_DUPFD_CLOEXEC**

In `libc/sys_stubs.c`, replace the entire `fcntl` function:
```c
/* fcntl: SBUnix has no F_GETLK/F_SETLK or per-fd flag storage. We service
 * F_DUPFD via dup, return success/zero for the descriptor-flag queries. */
int fcntl(int fd, int cmd, ...) {
    va_list ap; va_start(ap, cmd);
    int r = -1;
    switch (cmd) {
    case F_DUPFD:
    case F_DUPFD_CLOEXEC: {
        int minfd = va_arg(ap, int);
        r = dup_to_minfd(fd, minfd);
        break;
    }
    case F_GETFD:
    case F_GETFL:
        r = 0;
        break;
    case F_SETFD:
    case F_SETFL:
        (void)va_arg(ap, int);
        r = 0;
        break;
    case F_GETLK:
    case F_SETLK:
    case F_SETLKW:
        errno = ENOSYS;
        r = -1;
        break;
    default:
        errno = EINVAL;
        r = -1;
    }
    va_end(ap);
    return r;
}
```
with:
```c
/* fcntl: F_GETFD/F_SETFD are kernel-backed (per-descriptor FD_CLOEXEC).
 * F_DUPFD is serviced via dup; F_DUPFD_CLOEXEC additionally sets the
 * cloexec flag on the new descriptor. F_GETFL/F_SETFL have no per-fd
 * flag storage in the kernel and keep their permissive stub behavior;
 * advisory locks report ENOSYS. */
int fcntl(int fd, int cmd, ...) {
    va_list ap; va_start(ap, cmd);
    int r = -1;
    switch (cmd) {
    case F_DUPFD: {
        int minfd = va_arg(ap, int);
        r = dup_to_minfd(fd, minfd);
        break;
    }
    case F_DUPFD_CLOEXEC: {
        int minfd = va_arg(ap, int);
        r = dup_to_minfd(fd, minfd);
        if (r >= 0) (void)fcntl(r, F_SETFD, FD_CLOEXEC);
        break;
    }
    case F_GETFD:
        r = (int)syscall(SYS_fcntl, fd, F_GETFD);
        break;
    case F_SETFD: {
        int arg = va_arg(ap, int);
        r = (int)syscall(SYS_fcntl, fd, F_SETFD, arg);
        break;
    }
    case F_GETFL:
        r = 0;
        break;
    case F_SETFL:
        (void)va_arg(ap, int);
        r = 0;
        break;
    case F_GETLK:
    case F_SETLK:
    case F_SETLKW:
        errno = ENOSYS;
        r = -1;
        break;
    default:
        errno = EINVAL;
        r = -1;
    }
    va_end(ap);
    return r;
}
```

- [ ] **Step 3: Compile-check the kernel + libc**

Run: `make clean > /dev/null && make > /tmp/build.log 2>&1 && echo BUILD_OK || tail -30 /tmp/build.log`
Expected: `BUILD_OK`. If it fails, fix the compile error before continuing — do not proceed to the boot test with a broken build.

## Task C6: Integration test and commit

**Files:** none (verification + commit)

- [ ] **Step 1: Boot and verify**

Run:
```bash
printf ' \nexit\n' | timeout 240 make -s qemu > /tmp/run.log 2>&1
grep -aE "cloexec|dup_test|init: [0-9]+/|FAIL|panic|SELFTEST.*FAIL" /tmp/run.log
```
Expected:
- `cloexec_test: PASS (8 tests)` and `[cloexec_helper] PASS ...`
- `dup_test` still PASS (no regression — `dup`/`dup2`/`close` were touched)
- `init: 114/114 tests passed`
- kernel selftest all PASS (no `SELFTEST` `FAIL`), no `panic` — in particular the `leak: spawn/free 1000x` selftest must pass, confirming the `struct pcb` change is clean under a `make clean` build.

If the `leak: spawn/free` selftest panics with `timer: bad pcb ptr`, the build was stale — re-run `make clean && make` and re-test.

- [ ] **Step 2: Commit the implementation**

```bash
git add kernel/include/proc.h kernel/proc.c kernel/syscall.c \
        kernel/include/syscall.h libc/include/sys/syscall.h libc/sys_stubs.c
git commit -m "$(cat <<'EOF'
feat: O_CLOEXEC support and a kernel-backed SYS_fcntl

Add a per-process cloexec_mask bitmap to struct pcb (one bit per fd,
NOFILE==64 fits a single uint64_t). open() sets the bit on O_CLOEXEC;
close()/dup()/dup2() clear it for the affected descriptor; fork() copies
the mask; a successful do_exec() closes every fd whose bit is set, then
clears the mask.

Add SYS_fcntl (119) implementing F_GETFD/F_SETFD against the mask; other
fcntl commands are rejected at the kernel and stay libc-handled. Rewire
the libc fcntl stub: F_GETFD/F_SETFD now hit the kernel, and
F_DUPFD_CLOEXEC sets cloexec on the duplicated fd. F_GETFL/F_SETFL and
advisory locks keep their existing stub behavior.
EOF
)"
```

---

# Task Group D — Pull requests

The three branches are independent and can be PR'd in any order.

## Task D1: Push branches and open PRs

**Files:** none

- [ ] **Step 1: Confirm with the user before pushing**

Pushing branches and opening PRs are externally-visible actions. Before running anything in this task, confirm with the user that they want all three PRs opened now (vs. reviewing branches locally first).

- [ ] **Step 2: Push and open a PR per branch**

For each of `feature/open-o-excl`, `feature/tarfs-getdents-dots`, `feature/o-cloexec`:
```bash
git checkout <branch>
git push -u origin <branch>
gh pr create --base develop --title "<title>" --body "<body>"
```
Suggested titles:
- `feature/open-o-excl` → `fix(open): honor O_CREAT|O_EXCL on an existing target`
- `feature/tarfs-getdents-dots` → `feat(tarfs): emit . and .. from getdents`
- `feature/o-cloexec` → `feat: O_CLOEXEC support and a kernel-backed SYS_fcntl`

Each PR body should summarize the change and reference the corresponding `merged_audit_report.md` item (9a / 9b / 14d).

---

## Notes on scope

- Item 9c (PROT_WRITE-only `mmap`) is intentionally excluded — `sys_mmap` already rejects W-without-R with a documented RISC-V rationale; treated as closed-as-intended.
- `F_GETFL`/`F_SETFL` real backing and advisory file locks are out of scope; only the cloexec-related `fcntl` commands get kernel support.
