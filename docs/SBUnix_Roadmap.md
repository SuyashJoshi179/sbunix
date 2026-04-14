# SBUnix — Completion Roadmap

High-level plan from current state (end of Phase D: fork / exec / wait on
tarfs) through a usable RISC-V teaching kernel that runs our own shell,
MicroPython as a mid-project milestone, and has an architecture that
doesn't preclude a later BusyBox bring-up.

Low-level design for each subsystem lives in per-phase design docs that
will be written at the start of each phase, not up-front.

---

## 0. Snapshot of What Exists

| Area | Status |
|---|---|
| Boot + OpenSBI + `start.S` | Done |
| `printk` / UART polling TX | Done |
| Physical allocator (`pmem.c`) | Done |
| Sv39 paging, kernel higher-half map | Done |
| User page tables, `create_user_pgtable`, `map_stack`, `uvmcopy` | Done |
| Trap entry / S-mode `ecall` dispatch | Done |
| PCB (dynamic linked list), round-robin sched, `swtch`, kernel threads | Done |
| Timer interrupts plumbed (not yet preempting user) | Partial |
| tarfs (read-only, baked into `kernel.elf` via `objcopy`) | Done |
| ELF loader for static user binaries | Done |
| Syscalls: `exit, write, getpid, exec, fork, wait` | Done |
| libc (ours, grown in-tree): `printf`, `exit`, syscall wrappers | Done |
| User binaries: `init`, `echo`, `fork_test`, `pid_test`, `write_test`, `addrspace_test`, `multi_fork_test` | Done |
| `make thirdparty` hook for future BusyBox (empty directory, contract only) | Staked out |
| QEMU machine already attaches `virtio-blk`, `virtio-gpu`, `virtio-net` | Hardware waiting for drivers |

---

## 1. Guiding Principles

1. **Every user-visible feature lands as a syscall first, then a libc
   wrapper, then a user binary that exercises it.** No kernel-internal
   shortcuts that bypass the syscall boundary.
2. **One subsystem, one merge.** Each phase below is PR-sized. Cross-
   cutting refactors get their own prep-PR.
3. **Tests are non-optional.** Every phase adds at least one kernel
   selftest and one user-space test binary.
4. **Grow our own libc in-tree.** Not porting musl. Scope is "enough for
   our own binaries + MicroPython-minimal + eventually BusyBox, in that
   order." New headers land when a user program demands them.
5. **MicroPython-minimal is the shipping milestone.** BusyBox is the
   north star — architecture must not preclude it, but no phase is gated
   on getting it to link.
6. **No POSIX fidelity beyond what a current user matters.** Document
   deviations as we go.
7. **Fail loud in kernel, negative-errno in user space.** No silent
   corruption; no panic on bad user input.
8. **Single-CPU, big lock = interrupts off.** Revisit only on a concrete
   race.

---

## 2. Phase Map

```
Phase 3 tail ── finish user-space plumbing (preempt, segv-kill, idle)
      │
Phase 4  ── FDs, VFS with mount table, tarfs-with-directories
      │        (/ and /bin on tarfs)
      │
Phase 5  ── VirtIO-blk + sbfs + mount at /data (first writable FS)
      │
Phase 6  ── Shell (our own), pipes, redirection, console line discipline
      │
Phase 7  ── User memory: VMA refactor, sbrk, malloc, mmap-anon, COW fork
      │
Phase 7.5 ── MicroPython milestone
      │        setjmp/longjmp, %f printf, errno.h, stat, clock_gettime,
      │        frozen-module micropython build → "we ran a Python script"
      │
Phase 8  ── Signals (minimal), time, termios, uid/gid stubs
      │        just enough to make our shell feel real
      │
Phase 9  ── Limits, resource cleanup, robustness, leak tests
      │
Phase 10 ── BusyBox aspirational:
             iterative libc growth driven by busybox-static link errors.
             No deadline, no scope commitment. The goal is to make the
             first applet link; everything beyond that is bonus.
```

Phases 3 tail through 9 are mandatory for "complete." Phase 10 is
explicitly open-ended.

---

## 3. Phase 3 Tail — Close Out User-Space Substrate

Small items that unblock everything after:

- **Preempt user on timer.** Trap path calls `yield()` when the fault
  comes from U-mode. Selftest: two user loops both make progress without
  cooperating.
- **User fault → kill, not panic.** `scause ∈ {12,13,15}` with `SPP == U`
  goes to `proc_exit_current(-SEGV)`. Integer status only; no signals
  yet.
- **Trivial syscalls:** `getppid`, `sleep(ms)`, `yield`. Unblocks tests.
- **Idle/reap hygiene.** Reaping a child from `init` actually frees
  kstack + user pagetable + PCB page. Kernel selftest: fork/exec/wait
  1000× and check `pmem` free-count is invariant.

Exit: `multi_fork_test` runs under preemption, bad derefs kill only the
faulting process, page accounting is tight.

### Gotchas

- **Don't free the kstack you're running on.** When reaping a zombie,
  the frees must happen from the *parent's* (or init's) kernel stack
  after `swtch` has already left the zombie. Mark zombie in `exit`,
  actually free in `wait`/reaper. Same rule for the user pagetable and
  PCB page.
- **`sepc` on user fault.** Before jumping to `proc_exit_current`, save
  the faulting `sepc`/`stval`/`scause` *off* the trapframe if you want
  to log them — the scheduler will clobber `sstatus.SPP` on the way
  out. Kill the process; don't try to resume.
- **Preempt re-entry.** The timer IRQ path must check `SPP == U` before
  calling `yield()`. Preempting a kernel thread mid-critical-section is
  fine only if you're genuinely interrupts-off in those sections; if
  you ever re-enable SIE in kernel, revisit.
- **`sleep(ms)` must not spin with IRQs off.** Block on a wait-queue or
  at minimum `wfi` with IRQs enabled, otherwise the timer that would
  wake you never fires.
- **Trap-from-U stval quirks.** Instruction page fault (scause 12) sets
  `stval` to the faulting PC, but misaligned/illegal may not — don't
  assume `stval` is always the bad address.
- **`sstatus.SPIE` vs `SIE`.** On `sret` the hw restores SIE from SPIE.
  If you forget to set SPIE=1 when building the first user trapframe,
  user runs with interrupts masked and never gets preempted. Classic.
- **Idle hart.** After killing the last runnable proc, the scheduler
  must `wfi` with IRQs enabled, otherwise you deadlock at the first
  "nothing to run" moment in the leak test.

---

## 4. Phase 4 — File Descriptors, VFS, Mount Table, Tarfs Directories

This is the structural phase the rest of user-space leans on. Four sub-PRs.

### 4a. Per-process file descriptor table

- `struct file { type, refcnt, readable, writable, off, inode|pipe|dev }`.
- Per-PCB `ofile[NOFILE]`, start with `NOFILE = 16`.
- Syscalls: `open, close, read, write, dup, dup2, lseek, fstat,
  getdents64`.
- `sys_write` drops the UART shortcut; fd 1/2 become `struct file`s
  backed by the console device.
- `fork` bumps refcnts, `exec` keeps the table, `exit` closes all fds.

### 4b. VFS with mount table

- `struct inode` with ops vtable: `read, write, stat, lookup,
  getdents, create, unlink, mkdir, rmdir, rename`. Unsupported ops
  return `-ENOSYS`.
- **Mount table**: a small array of `{path, fs_root_inode, fs_ops}`.
  Path resolution walks the tree and hops mounts when it crosses a
  mount-point inode. Absolute paths only in v1; `.`/`..` as a follow-up.
- Per-process `cwd` inode; `sys_chdir`, `sys_getcwd`.

### 4c. Tarfs grows real directories

- Parse tar headers for **mode bits**, **mtime**, and **directory
  entries** (the tar format already carries all of this — we just
  haven't been reading it).
- Directory inodes expose `getdents`; `stat` returns populated `mode`
  and `mtime`.
- Tarfs mounted at `/` (and therefore `/bin`, `/etc`, etc. fall out
  naturally). Still read-only; writes return `-EROFS`.

### 4d. Console as a device

- `/dev/console` inode with `read` (UART RX, interrupt-driven; this is
  where RX interrupts finally land) and `write` (existing UART TX).
- `init` opens it three times → fds 0,1,2 before `exec`.
- `/dev` is a synthetic VFS directory, not a real mount yet. Just
  enough to give the console a path name.

Exit: `ls /bin`, `cat /etc/rc`, `stat /bin/echo` all work via the VFS
from a user program. Console RX reads line-buffered lines via
`read(0, ...)`.

### Gotchas

- **Refcount `struct file`, not fd slots.** `dup`/`dup2`/`fork` all
  bump the same refcount; the slot is just an index. Freeing on last
  close is the only correct time.
- **`dup2(fd, fd)` is a no-op**, not "close then reopen". Check before
  touching the target.
- **Fork must `filedup` each open file** under the ftable lock, not
  just memcpy the array — otherwise `close` in one proc tanks the
  refcount of a file the other still uses.
- **`exec` keeps fds, does not reset stdio.** Close-on-exec doesn't
  exist yet; when we add `O_CLOEXEC`, honor it here or forever chase
  ghosts in the shell.
- **Path resolution budget.** Cap path depth (e.g. 40 components) and
  symlink depth (when we have them) — kernel stack blowouts here are
  silent and ugly. Also reject empty components except the leading `/`.
- **Mount crossing.** When the lookup lands on an inode that *is* a
  mount point, swap to the mounted fs's root inode before continuing.
  Going the other way (`..` out of a mount) is the bit everyone gets
  wrong — defer until explicitly needed.
- **Tar parsing.** Tar uses 512-byte blocks; file data is padded to a
  block boundary; long-name extensions (`L` type) exist and will bite
  you the first time a path > 100 chars shows up. Reject unknown typeflags
  loudly rather than silently skipping.
- **Tar doesn't always carry directory entries.** Many archives list
  only files; you may have to *synthesize* directory inodes from file
  path prefixes at mount time.
- **`getdents64` is resumable.** The user-supplied offset must be a
  stable cookie you can restart from — don't use "index into a list
  that changes if entries are added."
- **`stat` struct layout is ABI.** Pick our layout once, freeze it in
  `sys/stat.h`, and don't silently reorder fields later.
- **Console RX ring is shared with an ISR.** Access under
  interrupts-off, not a spinlock-only discipline — we're single-hart
  and IRQs are our only real concurrency.
- **Canonical mode line buffer.** Echo, backspace, and line-commit all
  happen in the tty layer, not in user space. Ctrl-D at start of line
  returns 0 bytes (EOF); in the middle of a line it commits the line
  without the `\n`. Easy to get wrong.
- **`init` opens /dev/console ×3 *before* `exec`.** If you open after
  exec, the new program's fd 0/1/2 are whatever garbage was in the
  table. Equally: don't let the fd table get wiped on exec.

---

## 5. Phase 5 — VirtIO-blk + sbfs + Read-Write Mount

First real writable filesystem. Mounted at `/data`.

- **VirtIO-blk driver.** MMIO, single virtqueue, polling first. Backing
  disk is `build/disk.img` created by `tools/mkfs`. QEMU already
  attaches the device — just write the driver.
- **sbfs v1 (simple block fs).** Superblock + inode table + block bitmap
  + **direct blocks only** in v1. No journal, no indirect blocks.
  Directory blocks are arrays of `{inum, name}`. Modes + mtimes stored
  per inode.
- **`tools/mkfs` extends** to format sbfs on the disk image at build
  time, populate a few seed directories (`/data/home`, `/data/tmp`), and
  write them.
- **Mount** at `/data` during kernel init. Path resolution across the
  mount boundary works (Phase 4b already supports this).
- Syscalls that finally do something interesting: `sys_write` to a real
  file, `sys_mkdir`, `sys_unlink`.
- **Buffer cache** is deferred to Phase 10. Direct disk I/O for now.
- **Crash consistency is not a goal.** Document it — if QEMU dies
  mid-write, expect corruption. No fsck.

Exit: `echo hello > /data/x; cat /data/x` works end-to-end from a user
program (not yet from a shell — that's Phase 6). Reboots preserve
`/data` contents.

### Gotchas

- **Legacy vs modern VirtIO.** QEMU's `virtio-blk-mmio` defaults
  differ by machine; our `virt` machine gives you modern (v1.0).
  Feature-negotiate `VIRTIO_F_VERSION_1` explicitly or you'll chase
  layout differences for a day. Fail loudly on unsupported features.
- **DMA uses physical addresses; the kernel lives in the higher half.**
  Every descriptor ring entry must hold the *physical* address of its
  buffer, not the kernel virtual one. A `vtop()` helper (or reusing
  the direct-map offset) is mandatory; forgetting this is the #1
  virtio bug.
- **Queue memory must be contiguous and page-aligned.** Allocate with
  `page_alloc`, not via a generic allocator. Queue size is negotiated
  — don't hardcode 256.
- **Polling vs IRQ.** Start with polling on `used` ring index; wiring
  the PLIC for virtio IRQs is a separate task and can be deferred.
- **Memory barriers matter even on 1 CPU.** Device sees memory via
  DMA, not via the coherent CPU path — use `__sync_synchronize()` /
  `fence rw,rw` around descriptor publish and before kicking.
- **sbfs size cap.** Direct-blocks-only means a hard per-file cap
  (e.g. 12 * BSIZE = 48 KiB). Enforce with `-EFBIG` on every write
  that would cross it, not just at `open`.
- **Block 0 is the superblock, not inode 0.** Don't hand out block 0
  from the allocator. Reserve explicit ranges for super / inode-table
  / bitmap / data, and bake them into `mkfs` and the kernel side in
  *one place* (a shared header).
- **`mkfs` is the FS's mini-me.** Every layout change has to be made
  in `tools/mkfs` and the kernel at once. Pull shared structs into a
  header both include, even though one is host-side.
- **bio / block cache (even the dumb version).** Outstanding writes
  to the same block must serialize. The simplest correct thing: one
  global "block in flight" flag per in-core buffer, not a clever
  queue.
- **Crash truncation is real** even without a crash: if your write
  path extends the file length *before* the data block is on disk, a
  subsequent read sees garbage. Order: allocate block → write data →
  update inode → write inode. Document this and move on.
- **Path cross-mount for `/data`.** The FD returned must remember
  which fs it belongs to; `close` dispatches via the inode's ops, not
  a global switch on path.

---

## 6. Phase 6 — Shell, Pipes, Redirection

Our own `sh`, not BusyBox.

- `sh` user binary: prompt, tokenize, `fork`/`exec`, `wait`. Built-ins:
  `cd`, `pwd`, `exit`, `export` (stubbed; env support is trivial).
  External resolution: hardcoded `/bin` first, then `$PATH`.
- Line editing: backspace, newline, Ctrl-D (EOF). **Ctrl-C deferred to
  Phase 8** (needs signals); stub as "does nothing" for now.
- `sys_pipe(int fd[2])` + pipe file type. Buffer is a single page
  ring. Shell `|` composition.
- Redirection `<`, `>`, `>>` via `dup2` before `exec`.
- Globbing `*` shell-side, no kernel help.
- **Explicitly punted:** background jobs (`&`), job control, ptys.

Exit: `ls /bin | grep echo > /data/out; cat /data/out` works from an
interactive shell session.

### Gotchas

- **Parent must close both pipe ends after fork+dup2.** The classic
  hang: `ls | grep echo` where `grep` never sees EOF because the
  shell kept the write end open. Close in *every* process that
  shouldn't have it, including the shell itself.
- **Pipe write semantics.** Writing to a pipe with no readers must
  return `-EPIPE` (later `SIGPIPE`). A pipe that's full blocks; keep
  the ring a single page and make reads/writes partial-allowed.
- **Redirection order is left-to-right** in POSIX: `cmd > f 2>&1` and
  `cmd 2>&1 > f` mean different things. Parse & apply in order before
  `exec`.
- **`dup2` before `exec`, not after.** Once you've execed, the child's
  address space is gone and so is your chance to set up stdio.
- **Globbing is shell-side**, which means the shell needs
  `opendir`/`readdir` in libc — plumb them before Phase 6 or you'll
  bounce back.
- **Line editing reentrancy.** Don't call `printf` from inside the
  line-buffer handler if `printf` itself uses `write` which goes back
  through the tty — infinite recursion is easy here.
- **Built-ins vs external.** `cd`, `exit`, `export` *must* run in the
  shell process (not a forked child), otherwise they no-op silently.
- **Ctrl-C stub.** Until Phase 8, SIGINT doesn't exist. Make the
  shell's `read` on the tty return `-EINTR` on a console-level break
  byte so the prompt can be redrawn — or just live without it and
  document the limitation.

---

## 7. Phase 7 — User Memory: VMAs, sbrk, malloc, mmap, COW

Four sub-PRs, ordered so each builds cleanly on the last.

### 7a. Per-process VMA list (refactor PR)

- `struct vma { start, end, prot, flags, backing }` list per PCB.
- Replace ad-hoc `user_entry`/`user_sp`/hardcoded ranges in `uvmcopy`
  and `free_user_pgtable` with VMA iteration.
- Page-fault path consults the VMA list.
- **No user-visible change.** Ships before 7b so nothing downstream has
  to care about the old layout.

### 7b. sbrk + libc malloc

- `VMA_HEAP` type, `sys_sbrk(n)` extends it by n bytes. Go **lazy**:
  allocate pages on first touch, not at `sbrk` time. Exercises the
  fault path.
- libc `malloc`/`free`/`realloc`: simplest viable allocator (first-fit
  free list, coalesce on free). Not fast, not fancy — good enough for
  MicroPython and our binaries.

### 7c. mmap anonymous + COW fork

- `sys_mmap(addr, len, prot, flags, fd, off)` with `MAP_ANONYMOUS |
  MAP_PRIVATE` only. File-backed mmap is Phase 10.
- **COW fork:** instead of eager copy, clear `PTE_W` in both parent and
  child, mark pages COW in the VMA. Fault handler duplicates the page
  and restores write on COW fault.
- `pmem` grows a per-page refcount table so COW frees correctly.

### 7d. munmap + stack auto-grow

- `sys_munmap` tears down a VMA range, frees backing by refcount.
- Stack VMA auto-grows on faults just below `user_sp`, up to a static
  cap (`MAX_STACK`, e.g. 1 MiB).

Exit: a user program can `malloc(1 << 20)`, touch it, fork, have the
child write pages without affecting the parent, exit, and leave zero
leaked physical pages.

### Gotchas

- **VMA refactor (7a) must preserve behavior exactly.** Ship it alone,
  with the existing tests passing unchanged, before touching COW.
  Resist the urge to add features in the same PR.
- **Distinguishing lazy-anon vs COW in the fault handler.** Both look
  like "store fault on a present-or-not page." Encode the state in
  PTE bits + VMA flags (`VMA_HEAP` + `!PTE_V` = zero-fill; `PTE_V &&
  !PTE_W && VMA flag COW` = copy). Draw the truth table before you
  write the code.
- **Parent's own PTEs must lose `PTE_W` on COW fork.** If you only
  mark the child's, the parent keeps writing *into the shared page*
  and the child silently sees the changes. `sfence.vma` after.
- **COW refcount table.** A `uint16_t[num_phys_pages]` (or similar)
  indexed by PFN. Increment when cloning a PTE, decrement when
  unmapping/freeing. Freeing only happens at refcount == 0. Set
  refcount = 1 at `page_alloc`, not at first clone.
- **Atomicity on 1 hart = IRQs off.** No `__atomic_*` needed, but *do*
  push_off/pop_off around refcount ops or a timer in the middle will
  bite you the week you add SMP.
- **Lazy sbrk pages must be zero.** `page_alloc` does not zero by
  default in this project — check, and add a `page_alloc_zero` if
  missing. Non-zero pages leak kernel memory to user space, which is
  both a bug and a security hole.
- **Stack auto-grow needs a guard.** One unmapped page below the
  stack VMA, and a `MAX_STACK` cap. Without the guard, a runaway
  recursion silently eats the heap.
- **`malloc` alignment.** Return 8-byte aligned (16 is safer for
  future float/vector). Header-before-block layout is fine; keep the
  header size a multiple of the alignment.
- **`munmap` of a partial VMA splits it.** Either implement the split
  or reject non-boundary munmap with `-EINVAL`. Don't pretend it
  worked.

---

## 7.5 — MicroPython-Minimal Milestone

Proof that the memory and FS subsystems are solid. Small, focused.

- **Libc additions:** `setjmp.S` (RISC-V callee-save save/restore),
  `longjmp`, a richer `errno.h`, `%f` in `printf` (soft-float),
  `clock_gettime(CLOCK_MONOTONIC)` stub backed by our tick counter,
  `string.h` staples (`memcpy`/`memmove`/`memset`/`strcmp`/`strlen` —
  add whatever isn't there yet).
- **Stat support in libc:** a minimal `sys/stat.h` + the `stat` wrapper.
- **Build glue:** a `thirdparty/micropython/` port that builds
  MicroPython's "minimal" configuration against our libc, with **frozen
  modules** so import doesn't need filesystem machinery, and drops the
  resulting binary into `build/rootfs/bin/micropython` so tarfs picks
  it up.
- **Demo:** run a Python script passed on the command line from our
  shell: `micropython /data/hello.py` → prints "hello from python",
  `print(sum(range(100)))`, etc.

Exit: MicroPython runs a user-provided script end-to-end, uses
`malloc` under the hood, does float arithmetic, reads a file from
`/data`, and cleanly exits.

### Gotchas

- **`setjmp`/`longjmp` must save *all* callee-saved GPRs** on RV64:
  `s0..s11`, `sp`, `ra`. If using hardware float, also `fs0..fs11` —
  otherwise skip and compile micropython with soft-float
  (`MICROPY_FLOAT_IMPL=MICROPY_FLOAT_IMPL_FLOAT` + soft-float ABI).
- **`%f` in printf is a scope trap.** A working soft-float `%f` is
  real work; fine to start with `%e` or fixed precision and document
  the deviation.
- **MicroPython heap sizing.** Configure `MICROPY_GC_HEAP_SIZE`
  explicitly (e.g. 256 KiB) rather than letting it grow via `sbrk` on
  first run — easier to debug OOM.
- **Frozen modules avoid the FS entirely** for the stdlib. Build via
  `mpy-cross` on the host; the result goes into a generated C file
  linked into the binary. Otherwise you're debugging module import
  while also debugging `open`/`read`.
- **`__errno` vs `errno`.** MicroPython (and many ports) expect
  `errno` to be an lvalue — provide `int *__errno_location(void)`
  returning a per-process slot even though we're single-threaded.
- **`clock_gettime` stub.** Back it with our tick counter; document
  resolution as ticks. MicroPython's `time.time()` will be coarse
  but functional.
- **Stack check symbols.** If anything is compiled with
  `-fstack-protector*`, you need `__stack_chk_guard` and
  `__stack_chk_fail` — easiest fix is to disable the flag in the
  port Makefile.
- **crt0 must pass argc/argv** the way MicroPython expects. Our init
  path currently hands off a bare entry; make sure the user-entry
  shim sets `a0=argc, a1=argv` before jumping to `main`.

---

## 8. Phase 8 — Signals, Time, Termios, uid/gid Stubs

Minimal versions of each. Scoped to "make our own shell feel real,"
*not* to "make BusyBox sh happy" — that would blow up scope and we
agreed BusyBox is aspirational.

- **Signals (minimal):** per-PCB pending + blocked masks, handler
  table. Delivery on return-to-user. Signals implemented:
  `SIGINT` (from console Ctrl-C), `SIGCHLD` (to parent on exit),
  `SIGSEGV` (on fault), `SIGPIPE` (broken-pipe write), `SIGTERM`,
  `SIGKILL`. Syscalls: `kill`, `sigaction`, `sigprocmask`,
  `sigreturn`. Signal trampoline in libc. **No real-time signals,
  no `sigtimedwait`, no job control.**
- **Time:** `clock_gettime(CLOCK_MONOTONIC | CLOCK_REALTIME)` (REALTIME
  = MONOTONIC + epoch 0), `gettimeofday`, `nanosleep`. All backed by
  our existing tick counter.
- **Termios (minimal):** a single termios struct on `/dev/console`.
  `TCGETS`/`TCSETS` ioctls. Line discipline honors `ICANON`, `ECHO`,
  `ISIG` (the last one makes Ctrl-C actually raise `SIGINT`).
  `TIOCGWINSZ` returns a hardcoded 80×24.
- **uid/gid stubs:** `getuid`/`geteuid`/`getgid`/`getegid` all return
  0. `setuid`/`setgid` are no-ops. Files are always 0:0. Not
  security; just making the syscalls exist.

Exit: Ctrl-C kills the foreground child of our shell and the shell
survives. `sleep 2 &&  echo done` works via our `sh`. `date` (a tiny
user binary we write) prints the uptime.

### Gotchas

- **Signal delivery on return-to-user, not anywhere else.** Single
  check at the top of the user-ret path: pending & ~blocked → deliver
  one signal. Do not deliver from inside random syscalls.
- **Signal trampoline runs in user mode.** Kernel builds a fake frame
  on the user stack containing saved regs + signum, sets `sepc` to
  the handler, `sret`s. The handler returns into a libc trampoline
  that calls `sys_sigreturn` — the kernel restores the original frame
  there. Picking a fixed user-stack trampoline address is easier than
  a per-proc one.
- **`SIGKILL`/`SIGSTOP` cannot be caught or blocked.** Enforce in
  `sigaction`/`sigprocmask`. Many bugs begin by forgetting this.
- **Restarting syscalls.** Decide once: do interrupted blocking
  syscalls return `-EINTR` or restart? Go with `-EINTR` (simpler,
  matches BSD `SA_RESTART=0`) and document.
- **Async-signal-safety.** Our libc `printf` uses a buffer; calling
  it from a signal handler will corrupt state. Document "only
  `write`/`_exit` are safe" even though we can't enforce it.
- **Termios `ISIG` is where Ctrl-C becomes `SIGINT`.** It's the tty
  layer that raises the signal against the foreground pgrp — and we
  don't have process groups. Cheat: the tty knows which pid is
  "foreground" because the shell set it via an ioctl or the console
  tracks "last forked child of init-of-shell-tree." Document the
  cheat.
- **`clock_gettime(CLOCK_REALTIME)` with epoch = 0** will confuse
  anything that expects post-1970 timestamps. Fine for our shell;
  note it for MicroPython tests.

---

## 9. Phase 9 — Limits, Cleanup, Robustness

- **Resource limits:** per-process caps on fds, VMA count, total
  mapped pages. Enforce in syscalls with `-EMFILE` / `-ENOMEM`.
- **Unified process teardown:** single `proc_destroy(p)` that closes
  fds, frees VMAs, frees pagetable, frees kstack, frees PCB. Called
  from exit + signal-kill + fault-kill. No duplicated teardown.
- **Zombie reaping:** `init` loops on `wait(NULL)`; orphaned children
  reparent to pid 1.
- **OOM behavior:** `page_alloc` failure in `fork`/`exec`/`sbrk` →
  `-ENOMEM`, never panic.
- **`copyin`/`copyout` helpers:** walk the user page table explicitly,
  replacing the ad-hoc `SUM=1 + < KVMEM_OFFSET` checks. Kernel never
  faults on a bad user pointer.
- **Selftest harness:** `sys_selftest(n)` runs numbered tests, prints
  pass/fail, drives CI.
- **Leak test:** 1000× fork/exec/wait loop + pmem free-count check as
  an automated test, not a one-shot selftest.

Exit: full test suite runs clean, no panics, predictable error codes
on every failure path.

### Gotchas

- **`copyin`/`copyout` must re-walk per page.** Crossing a page
  boundary where one page is present and the next is not is the
  common bug. Check `PTE_U`, `PTE_R`/`PTE_W` on *every* page, not
  just the first.
- **Kernel must never fault on user pointers.** The `SUM` bit lets
  you touch user memory but doesn't let you survive an unmapped
  page. The helper has to walk the pgtable explicitly and return
  `-EFAULT` before dereferencing.
- **`proc_destroy` ordering.** Remove from scheduler → close fds →
  free VMAs → free user pgtable → free kstack → free PCB page. Any
  other order either frees-in-use or leaks.
- **Reparenting race.** `exit` and `fork` both touch the parent
  pointer. Walk the proc list with IRQs off when reparenting to
  init, and *then* wake init if it's sleeping in `wait`.
- **OOM in fork is the messy one.** Half-built child must be fully
  torn down on any intermediate failure — this is where leak bugs
  hide. Write the teardown as the *only* exit path from `fork`.
- **Selftest numbering is an ABI** the moment CI depends on it.
  Append-only, never renumber.
- **Leak test tolerance.** Free-page count must be *exactly* equal
  before/after, not "close enough." Anything else is a slow leak.

---

## 10. Phase 10 — BusyBox Aspirational (Open-Ended)

North star, no deadline. The `make thirdparty` hook in our Makefile
already stages the build contract: `thirdparty/Makefile` builds
BusyBox-static against `build/libc.a`, drops the binary into
`build/rootfs/bin/`, and the kernel picks it up via tarfs on the next
`make`. What's missing is the content of that directory.

Workflow when we eventually tackle it:

1. Pick a BusyBox version (1.36.x is fine) and an `allnoconfig`-based
   minimal applet set. Start with applets that need the least:
   `true`, `false`, `echo`, `cat`, `pwd`, `sleep`, `env`.
2. Try to link. Read the undefined-symbol list from `ld`.
3. Implement that batch in our libc — each undefined symbol is a
   concrete, small task. Repeat until the first applet links.
4. Run it. File the first bug. Repeat.
5. When the basics work, grow the applet set: `ls`, `mkdir`, `rm`,
   `mv`, `cp`, `wc`, `grep`.
6. BusyBox `sh` is its own project — needs signals + termios + job
   control. Phase 8 gives us signals and termios; job control is a
   new scope item. Probably don't bother — our own shell is good
   enough, and BusyBox's other applets run fine under it.

**Non-goals of Phase 10:** full BusyBox defconfig, every applet, shared
libraries, a dynamic linker, pthreads, locale support, IPv4/IPv6,
anything network. If we get ~15 applets linking and running under our
shell, that's a success.

### Gotchas

- **Static link only.** No dynamic linker, no `ld.so`. Build flags
  must force `-static`, and the libc archive must be complete enough
  that `ld` doesn't pull in host symbols.
- **crt0 is our problem.** BusyBox `main()` expects argc/argv/envp on
  the C stack; our crt0 must set those up from the user frame the
  kernel built. envp can be an empty vector pointer.
- **Undefined-symbol workflow is the plan.** Link, read errors,
  implement, relink. Don't try to anticipate the list.
- **Stack protector / FORTIFY_SOURCE / sanitizers.** Disable in the
  BusyBox config, not by stubbing out the runtime symbols.
- **`__libc_start_main` vs direct `main`.** BusyBox doesn't need
  glibc's wrapper if we provide a simple crt0 that calls `main`
  directly and calls `_exit` on return.
- **AT_* auxv.** Most applets don't need it; if one does, provide a
  minimal auxv with `AT_NULL` only and grow on demand.
- **`errno` as a macro.** Many libcs define `errno` as
  `(*__errno_location())`. Match that contract from day one — retrofitting
  is painful.

### Other stretch items (lower priority than the above)

1. **`/proc`** — synthetic VFS, `/proc/<pid>/status`, `/proc/meminfo`.
2. **`/dev` as a real mount** — `/dev/null`, `/dev/zero`, etc.
3. **File-backed `mmap`** with demand paging.
4. **Disk buffer cache** in front of VirtIO-blk, write-back, LRU.
5. **Environment variables** threaded through `exec`.
6. **Indirect blocks in sbfs** so files can exceed ~48 KiB.

---

## 11. Cross-Cutting Design Notes

- **Syscall ABI:** `a7 = number`, `a0..a5 = args`, return in `a0`.
  Negative return = negated errno. Document errnos in
  `libc/include/errno.h` as we need them.
- **Error reporting:** kernel functions return `int` (0 or `-errno`).
  Panic only on kernel invariant violations, never on user input.
- **Locking:** single big lock = interrupts-off in kernel critical
  sections. `push_off`/`pop_off` helpers. Revisit only on a concrete
  race.
- **Memory ownership:** every allocation has exactly one owner, stated
  at the allocation site. COW page refcounts are the one exception.
- **Naming:** `sys_foo` = kernel impl, `foo` = libc wrapper, `SYS_foo`
  = ABI number. Kept in lockstep in `kernel/include/syscall.h` and
  `libc/syscall.c`.
- **Libc growth policy:** add a header/function when a shipping user
  binary demands it. Don't speculatively populate `stdlib.h` with
  things nothing calls.
- **Build system:** user binaries under `bin/<name>/` are auto-added
  to the tarfs by the existing Makefile rule. Third-party binaries
  (MicroPython, eventually BusyBox) land under `build/rootfs/bin/`
  via their own `thirdparty/` subdir.

---

## 12. Test Plan (accumulates across phases)

| Test | Added in | What it proves |
|---|---|---|
| `pid_test`, `fork_test`, `multi_fork_test` | Phase 3 | existing |
| `preempt_test` | 3 tail | timer preempts user loops |
| `segv_test` | 3 tail | bad deref kills process, not kernel |
| `fd_test` | 4a | open/read/close/dup |
| `getdents_test` | 4b/4c | directory iteration on tarfs |
| `console_test` | 4d | stdin from UART RX |
| `disk_rw_test` | 5 | sbfs write/read + persist across reboot |
| manual `sh` exercises | 6 | pipes, redirection |
| `malloc_test` | 7b | sbrk + libc malloc |
| `cow_test` | 7c | COW semantics + refcounts |
| `micropython hello.py` | 7.5 | **milestone** |
| `signal_test` | 8 | Ctrl-C, SIGCHLD, SIGSEGV |
| `oom_test` | 9 | graceful ENOMEM |
| `leak_test` | 9 | 1000× fork/exec/wait invariant pmem |
| BusyBox first-applet link | 10 | north star |

Each test is a user binary plus an entry in the selftest harness.

---

## 13. Risks & Sequencing Rationale

- **VFS + mount table before the shell.** The shell needs real fds,
  cwd, and path resolution that hops mounts. Doing the shell first
  would bake in a temporary interface we'd rip out.
- **VMA refactor (7a) before COW (7c).** COW on top of the current
  ad-hoc user map tracking would be fragile. One boring refactor PR
  avoids a messy one.
- **Signals after the shell, not before.** The shell can boot without
  real Ctrl-C; doing signals first would mean writing the shell
  against unproven signal plumbing. Cheaper to add signals once the
  shell exists and tell us exactly what it needs.
- **MicroPython before signals.** MicroPython-minimal doesn't need
  signals (Python's `KeyboardInterrupt` support can be stubbed). Phase
  7.5 is genuinely independent of Phase 8, and running it earlier gets
  us a visible milestone sooner.
- **sbfs on VirtIO, not a RAM disk.** The Makefile already attaches
  `virtio-blk-pci` and `tools/mkfs` already builds a `disk.img`. The
  contract is staked out — use it.
- **BusyBox never blocks a phase.** If Phase 10 turns out to need 30
  libc functions we don't feel like writing, the project still ships
  at Phase 9 and runs our own shell + MicroPython.
- **No SMP.** Single-hart keeps locking trivial. SMP would eat the
  entire remaining budget; not worth it.

---

## 14. How This Doc Is Used

- Each phase gets its own design doc under
  `docs/phase<N>_<topic>.md` that expands sub-PRs into concrete
  file-level changes. Written *at the start of the phase*, not now.
- This doc gets updated when a phase lands — check the box, note
  deviations, move slipped items to the next phase explicitly rather
  than silently.
- If a phase takes more than ~1.5× its intuitive budget, stop and
  reread §1 before adding scope.
