# Phase 6 — Shell, pipes, redirection

**Status:** design ready, not yet implemented
**Depends on:** Phase 3 (fork/exec/wait), Phase 4 (fd table, dup2, `/dev/console`), Phase 5 (writable `/data` for history)
**Enables:** Phase 7 (real workloads that stress the allocator), Phase 10 (BusyBox aspirational relies on the shell)

---

## 1. Goal

Give SBUnix a real interactive shell: `sh` reads a line from `/dev/console`, parses it, forks, runs external commands from `/bin`, supports `|`, `<`, `>`, `>>`, and a handful of built-ins (`cd`, `pwd`, `exit`, `export`). It is the first phase that produces a usable REPL — after this phase a human at the QEMU console can type `ls /bin` and see output.

The shell itself is not the hard part. The hard part is the two pieces the kernel must grow to support it:
1. A real `sys_pipe(int fd[2])` that produces a kernel pipe object plumbed through the fd table.
2. A console line discipline (`ICANON`, backspace, `^C`, line buffering) that turns raw UART bytes into lines.

Redirection is entirely a user-space concern once we have `dup2` (Phase 4) and `pipe` (this phase). The shell does `fork → child opens/pipes/dup2's fds → exec`, exactly as on Unix. We commit to the Unix model because every later phase (shell scripting, BusyBox, pipelines under MicroPython) assumes it.

---

## 2. Preconditions

- `fork`, `exec`, `wait`, `exit` from Phase 3 work for ordinary user processes.
- `dup`, `dup2`, `open`, `close`, `read`, `write`, `lseek`, `getcwd`, `chdir` from Phase 4 work and return negative errnos.
- `/dev/console` is open as fds 0/1/2 for the first user process; child processes inherit them across fork/exec.
- `/data` is mounted read-write (Phase 5), so `~/.sh_history` has somewhere to live.
- libc has `printf`, `fprintf(stderr, ...)`, `strtok`, `strcmp`, `strdup`, `malloc`, `free`. If any of these are missing they will surface as undefined symbols during the shell build — that is intentional, it drives libc growth.

**Out of scope for this phase:**
- Job control (`&`, `fg`, `bg`, `^Z`). Requires process groups, which require signals (Phase 8).
- Command substitution (`` ` `` or `$(...)`). Needs a second pipe round-trip and a subshell, punted.
- Globbing beyond a trivial `*` expansion (and even that is a §9 decision).
- Environment variable expansion beyond `$VAR` lookup against a flat env array.
- History search (`^R`), tab completion. Phase 10 territory.

---

## 3. Concepts & data structures

### 3.1 Pipe object

A pipe is a bounded-buffer producer/consumer queue. The kernel allocates one `struct pipe` per successful `sys_pipe` call and wires two `struct file`s at it — one read end, one write end. Closing both ends frees the pipe.

```c
#define PIPESIZE 512

struct pipe {
    char     data[PIPESIZE];
    uint32_t nread;        // bytes consumed
    uint32_t nwrite;       // bytes produced
    int      readopen;     // write() returns EPIPE when 0
    int      writeopen;    // read() returns 0 (EOF) when 0
};
```

`write` blocks (via `proc_sleep`) when the buffer is full until `read` drains it. `read` blocks when the buffer is empty *and* `writeopen != 0`; when `writeopen == 0` and the buffer is empty, `read` returns 0 (EOF).

Both ends use `struct file_ops` hooks just like any other file:
- `pipe_read`, `pipe_write`, `pipe_close`.
- `.stat` returns `S_ISFIFO(mode)`.
- `.lseek` returns `-ESPIPE`.
- `.ioctl` returns `-ENOTTY`.

Because `struct file` already carries the read/write disposition, we reuse it rather than introducing a `pipe_fd` subtype. The only new thing is the allocator that produces two `struct file *`s sharing the same `struct pipe *`.

### 3.2 Console line discipline

Today `/dev/console` read returns whatever bytes UART buffered. A shell needs **lines**: read blocks until `\n` (or `^D` → EOF) and returns exactly one line, with backspace handling and `^C` that interrupts the current read.

Minimal line discipline state (one global instance — single console):

```c
struct tty {
    char    linebuf[256];
    int     head;          // index where next input char lands
    int     readp;         // index the reader has consumed to
    int     line_ready;    // incremented each time a '\n' lands
    int     canon;         // ICANON on/off (Phase 6: always on)
    int     echo;          // ECHO on/off
    struct pcb *waiter;    // sleeping reader
};
```

Input path:
1. UART IRQ → `uart_intr()` → one byte.
2. `tty_input(c)`: handle backspace (erase last char, echo `\b \b` if `echo`), `^D` (force line_ready even on empty input → EOF), `^C` (wake waiter with `-EINTR`; full signals come in Phase 8, this phase fakes it by returning -EINTR on the next read), otherwise append + echo.
3. When `c == '\n'`: push `\n` into linebuf, increment `line_ready`, wake `waiter`.

Read path:
1. `console_read` sleeps on the tty until `line_ready > 0`.
2. Copies bytes from `linebuf[readp..head]` into user buffer up to the first `\n` or requested `n`.
3. Advances `readp`, decrements `line_ready`, returns bytes copied.

The whole thing is a ~120-line change to `kernel/console.c`. It is deliberately not `termios`-complete — Phase 8 adds `tcgetattr`/`tcsetattr`.

### 3.3 Shell parser

Single file `bin/sh/sh.c`, target binary `/bin/sh`. Grammar:

```
pipeline  := cmd ('|' cmd)*
cmd       := word+ redirect*
redirect  := '<' word | '>' word | '>>' word
word      := any run of non-whitespace non-metachar chars, or a quoted string
```

No `&`, no `;` (use separate lines), no `&&`/`||`, no subshells. The parser is a hand-written recursive descent with a tokenizer that understands `"..."`, `\x` escapes, and the metachars `|<>`. Output is an AST of `struct cmd` nodes.

```c
enum { CMD_EXEC, CMD_PIPE, CMD_REDIR };
struct cmd { int type; };
struct execcmd  { int type; char *argv[MAXARGS]; };
struct pipecmd  { int type; struct cmd *left, *right; };
struct rediroff { int type; struct cmd *cmd; int fd; int mode; char *file; };
```

### 3.4 Built-ins

A built-in executes in the shell's own process (no fork), so it can mutate state like cwd. The exception is if it appears in a pipeline — then it is executed in a forked child so the redirection plumbing works, and any state mutation is lost. That mirrors Bash behaviour and avoids special-casing pipes.

Built-ins:
- `cd [path]` — calls `chdir`. `cd` with no arg goes to `/` (we do not have `$HOME` yet).
- `pwd` — `getcwd` + print.
- `exit [n]` — `sys_exit(n)`.
- `export KEY=VAL` — appends to a flat `env[]` array the shell maintains.
- `help` (optional) — prints a built-in list. Useful for first-run bring-up.

### 3.5 History (very small)

`~/.sh_history` is `/data/sh_history`. The shell:
- On startup, opens it, reads at most the last 32 lines into an in-memory ring.
- On every accepted line, appends to the file and rotates the in-memory ring.
- Exposes history via arrow-up/arrow-down in line editing (§3.6).

This is the first user-visible test that Phase 5's writable filesystem actually works. If `/data` is broken, the shell still runs but logs a one-line warning and disables history.

### 3.6 Line editing (minimal)

In Phase 6 the shell reads lines via `read(0, ...)` after the kernel's canonical-mode line discipline has done backspace handling. That is enough for a usable REPL.

Arrow keys, history scroll, and tab completion require raw-mode reads which require `tcsetattr(ICANON off)` which requires termios which is Phase 8. **So arrow-up does nothing in Phase 6 unless we bolt on a minimal raw-mode toggle here.** See §9.

---

## 4. File-by-file changes

### 4a — `sys_pipe` and the pipe kernel object

New:
- `kernel/pipe.c`, `kernel/include/pipe.h`
  - `int pipe_alloc(struct file **rf, struct file **wf)` — allocates pipe + two files.
  - `pipe_read`, `pipe_write`, `pipe_close` (called from `fileclose` when `f->type == FD_PIPE`).
- `kernel/include/file.h`: add `FD_PIPE` to the file type enum, add `struct pipe *pipe` to the union.

Modified:
- `kernel/sys_file.c`: new `sys_pipe(int __user *fds)` — allocates two fd slots atomically (rollback both if the `copyout` of the fds array fails).
- `kernel/syscall.c`: register syscall number (proposal: `SYS_pipe = 60`).
- `user/libc/unistd.h`, `user/libc/unistd.c`: `int pipe(int fds[2]);`

### 4b — Console line discipline

Modified:
- `kernel/console.c` — add the `struct tty` state, `tty_input` helper, rewrite `console_read` to wait on `line_ready`.
- `kernel/uart.c` — `uart_intr` calls `tty_input` instead of dumping raw bytes into a ring.

**Careful with echo ordering:** echo happens from IRQ context on UART. If the user holds a key down while a long `write` is in flight, the echo can interleave with program output. That is acceptable — Phase 8's `stty -echo` is the long-term fix — but do not try to buffer echoes, it leads to latency the user notices immediately.

### 4c — Shell binary

New:
- `bin/sh/sh.c` (~600 lines)
- `bin/sh/Makefile` entry in the Phase 6 `bin/` tree
- `bin/sh/builtins.c` — split out built-in table for readability
- `bin/sh/parser.c` — tokenizer + parser
- `bin/sh/history.c` — history file I/O

Modified:
- `kernel/proc.c`: `sched_init` no longer spawns every test binary by hand. Instead it spawns one process: `/bin/init`. `init` is a small binary (`bin/init/init.c`) that:
  - Opens `/dev/console` as 0/1/2 (redundant — kernel already did — but safe).
  - Runs the test binaries in order if booted with `-append test`.
  - Otherwise `exec`s `/bin/sh`.
  - Always `wait(0)`s in a loop to reap orphaned zombies.

This is a significant scheduler cleanup that falls out naturally here: once we have a shell, the "kernel spawns 18 test binaries directly" pattern goes away and the kernel only knows about init.

### 4d — Libc additions

- `int pipe(int[2])` wrapper.
- `char *getenv(const char *name)` — reads the process's auxv/envp set at exec time. If auxv isn't implemented yet, walks a flat array the kernel writes onto the new stack at exec.
- `int execvp(const char *file, char *const argv[])` — `/bin/` prefix search.

**Whichever of these is missing at first compile is a good thing.** Do not pre-build the libc surface. Add each symbol only when the linker complains.

---

## 5. Key flows

### 5.1 `ls | wc -l`

1. Shell parses into a `pipecmd{ left=execcmd("ls"), right=execcmd("wc -l") }`.
2. Shell calls `pipe(fds)` → `fds[0]` is read end, `fds[1]` is write end.
3. `fork()` left child:
   - `dup2(fds[1], 1)` → stdout becomes the pipe write end.
   - `close(fds[0]); close(fds[1]);` → drop originals.
   - `execvp("ls", argv)` → loads `/bin/ls`.
4. `fork()` right child:
   - `dup2(fds[0], 0)` → stdin becomes the pipe read end.
   - `close(fds[0]); close(fds[1]);`
   - `execvp("wc", argv)`.
5. Shell (parent) closes both `fds[0]` and `fds[1]` in itself (crucial — otherwise `wc` never sees EOF).
6. Shell `wait`s for each child in order.

The failure to close pipe ends in the shell is the single most common pipeline bug. Phase 6 tests explicitly check this.

### 5.2 `echo hi > /data/out`

1. Parser produces `rediroff{ fd=1, mode=O_WRONLY|O_CREAT|O_TRUNC, file="/data/out" }` wrapping `execcmd("echo hi")`.
2. Shell forks. Child:
   - `open("/data/out", O_WRONLY|O_CREAT|O_TRUNC)` → newfd.
   - `dup2(newfd, 1); close(newfd);`
   - `execvp("echo", ["echo", "hi"])`.
3. Parent waits.

`>>` differs only in the open flags (`O_APPEND` replaces `O_TRUNC`). Phase 6 is the first time `O_APPEND` actually matters — Phase 4's fd tests can exercise it with a stub but the shell is the real consumer.

### 5.3 Built-in `cd` in a pipeline vs alone

- `cd /data` alone: shell does *not* fork, just calls `chdir("/data")` directly. `pwd` afterwards sees the change.
- `cd /data | echo ok`: the `cd` runs in the forked child. The child's `chdir` mutates only the child, which exits. The parent's cwd is unchanged. This matches Bash and is intentional — if we tried to run built-ins in the parent even in pipelines we would corrupt the shell's own state.

### 5.4 Interactive `^C` while a child is running

1. User types `^C` mid-line.
2. `tty_input` sees `0x03`, sets `line_ready = 1` with a sentinel flag, wakes the shell's `read`.
3. `console_read` notices the sentinel, returns `-EINTR` and clears the linebuf.
4. Shell catches `-EINTR`, prints a fresh prompt.

If a child is running, the `^C` goes to the shell because the shell is the one blocked in `wait`, not the child. Until Phase 8 gives us process groups, `^C` cannot kill the child. The shell displays "(hit ^C — no process groups yet, child continues)" as a breadcrumb. Annoying, but honest.

---

## 6. Syscall ABI & error codes

| Syscall | Num | Args                       | Returns                                   |
|---------|-----|----------------------------|-------------------------------------------|
| `pipe`  | 60  | `int fds[2]`               | 0, `-EFAULT`, `-EMFILE`, `-ENOMEM`        |

`read`, `write`, `close` pick up pipe-aware returns:
- `read(pipe_read_end)` on empty pipe with writer closed → `0` (EOF, not error).
- `write(pipe_write_end)` with reader closed → `-EPIPE`.
- `lseek(any_pipe)` → `-ESPIPE`.
- `fstat(pipe)` → succeeds with `S_ISFIFO(st.st_mode)`.

Console read errors:
- `-EINTR` when `^C` interrupts an in-flight canonical read.

No other syscalls are introduced this phase.

---

## 7. Test plan

### 7.1 Unit tests (`kernel/selftest.c`)

| ID   | Test                                                                 | What it proves |
|------|----------------------------------------------------------------------|----------------|
| U-1  | `pipe_alloc` + write 10 bytes + read 10 bytes                        | Basic bounded buffer |
| U-2  | Write 600 bytes into 512-byte pipe, reader drains in chunks of 100   | Writer blocks at full, wakes on reader progress |
| U-3  | Close read end, write 1 byte                                         | Returns `-EPIPE`, not panic |
| U-4  | Close write end, read remaining buffer, then one more read           | Drains, then returns 0 |
| U-5  | `pipe_alloc` 100×, close both ends each time                         | No pipe object leak |
| U-6  | Feed `tty_input` "abc\b\bX\n"                                        | Canonical read returns "aX\n" |
| U-7  | Feed `tty_input` "^D" on an empty line                               | Read returns 0 (EOF) |
| U-8  | Feed `tty_input` "hello\x03"                                         | Read returns `-EINTR`, linebuf cleared |

### 7.2 End-to-end tests (`bin/`)

| ID   | Binary / scenario                                                                 |
|------|-----------------------------------------------------------------------------------|
| E-1  | `pipe_basic_test`: `pipe(fds); write(fds[1], "hi", 2); read(fds[0], buf, 2);`     |
| E-2  | `pipe_fork_test`: fork, child writes, parent reads 100 lines → each matches       |
| E-3  | `pipe_epipe_test`: reader closes, writer sees `-EPIPE`                            |
| E-4  | `pipe_eof_test`: writer closes, reader sees `0`                                   |
| E-5  | `sh_cmd_test`: drive the shell in "non-interactive" mode (`sh -c "ls /bin"`), compare output |
| E-6  | `sh_pipe_test`: `sh -c "echo abc | wc -c"` → output is "4\n"                      |
| E-7  | `sh_redir_test`: `sh -c "echo hi > /data/tt && cat /data/tt"` → "hi\n"            |
| E-8  | `sh_append_test`: two `echo ... >>` into the same file, `cat` shows both lines    |
| E-9  | `sh_builtin_test`: `cd /bin; pwd` → "/bin"                                        |
| E-10 | `sh_exit_test`: `exit 7`, `wait()` in parent sees status 7                        |
| E-11 | `init_reap_test`: orphaned child (parent dies first) is reaped by init not leaked |

### 7.3 Limit / stress tests

| ID   | Binary                       | Boundary                                                                                           |
|------|------------------------------|----------------------------------------------------------------------------------------------------|
| L-1  | `pipe_full_test`             | Write until pipe is full, confirm writer blocks (not spins); second thread drains, writer resumes  |
| L-2  | `pipe_many_test`             | Allocate `NFILE/2` pipes, close them all, allocate again — no global file leak                     |
| L-3  | `pipe_interleave_test`       | 4 children in a pipeline `a | b | c | d`, 10 KB through — output matches sum of all writes        |
| L-4  | `sh_deep_pipe_test`          | `sh -c "a | b | c | d | e | f"` — 6-stage pipeline survives; the limit here is the shell's own `MAXPIPES` which must be ≥ 8 |
| L-5  | `sh_long_line_test`          | 255-char command line (buffer-full boundary), 256th char must be rejected cleanly                  |
| L-6  | `sh_heavy_redir_test`        | `> /data/f` in a loop 200×, watching `/data` free-inode count — no leak                            |
| L-7  | `tty_fast_input_test`        | Shove 10k chars into the line discipline faster than the reader drains — no lost chars, no overrun |
| L-8  | `sh_many_children_test`      | Script that launches 16 sequential `/bin/true` — no zombie accumulation (init-reaps) |
| L-9  | `sh_orphan_storm_test`       | Shell forks 10 children in a row, each exits before shell `wait`s — all are reaped, none become double-fork zombies |
| L-10 | `pipe_write_reader_close_race`| Writer about to `write`, reader closes between syscalls — writer sees `-EPIPE`, no panic          |

L-1 and L-7 will catch the most common Phase 6 bug: missing `wakeup` on the pipe or tty after a state change. If either spins or hangs, the scheduler loop and the `proc_sleep`/`proc_wakeup` wiring are wrong.

### 7.4 Test gate

- All U-*, E-*, L-* pass.
- Running `make qemu` with `-append test` executes the full Phase 3+4+5+6 suite under the new `init`-spawned model.
- Running `make qemu` without that flag drops to a usable `sh>` prompt. A human can type `ls /bin`, see the output, and `exit` cleanly.

---

## 8. Gotchas

- **Pipe wakeups must happen inside the spinlock that protects `nread`/`nwrite`.** If you drop the lock before wakeup you get a lost-wakeup race: reader sleeps *after* writer woke it, and nobody ever wakes it again. This is the canonical "toy OS pipe hangs after N iterations" bug. L-1 hunts for exactly this.
- **`fork()` must duplicate `struct file` reference counts, not the `struct file`.** A pipe fd dup'd into a child is the *same* pipe end — closing the parent's copy must not tear down the pipe. Phase 3 fork already does this for regular files; verify pipe paths are no different.
- **The shell must close both pipe ends in itself** after forking both children. If it leaks the write end, `wc` never sees EOF and the pipeline hangs forever. Every pipeline test (`E-6`, `L-3`, `L-4`) must exercise this.
- **Canonical mode + `^D` = EOF**, but only when the line is empty. `^D` mid-line must flush what was typed without adding a newline. Document this in `console_read`.
- **Exec clobbers the file table only when `FD_CLOEXEC` is set.** We are not implementing `FD_CLOEXEC` until Phase 8. Document it in §9; until then, a shell pipeline that fails to close an fd before `exec` will leak it into the child. This is actually fine because the shell already explicitly `close`s its copies, but third-party programs (Phase 10 BusyBox) may assume CLOEXEC.
- **`init` must `wait(0)` in a tight loop.** Otherwise orphaned children become zombies forever. The easy bug: `wait(&status)` with `status` on a stack address that got clobbered by the loop body. Use a dedicated variable.
- **`sh` running on `/dev/console` can race with kernel `printk`.** Every test line ends up interleaved with kernel log spam unless we either (a) gate `printk` behind a "boot only" flag or (b) give the shell an acceptance that the first screen will be ugly. Decide in §9.
- **Pipe reader blocking forever because writer got `-EPIPE` first.** The sequence is: writer's first `write` fills the pipe, writer's second `write` returns `-EPIPE` because reader closed. Writer exits. But the reader's copy of the read-end fd is *still open* somewhere else (leaked). Result: the second reader process will sit forever. Fix: aggressively close pipe fds in the shell before every `wait`.
- **`read(0, buf, 1)` in canonical mode blocks until a full line.** This is surprising to code that expects one-char-at-a-time. BusyBox (Phase 10) will break on this. Document clearly; raw mode fix is Phase 8.

---

## 9. Open questions / decisions punted

1. **Raw mode this phase or defer to Phase 8?** A minimal `ioctl(TIOCRAW, 1)` is ~50 LoC and would let us implement arrow-key history. **Lean:** implement a trivial `stty -icanon` ioctl but keep `termios` proper for Phase 8.
2. **Globbing.** Even `*` expansion needs `readdir`, which Phase 4 already has. But it also needs a "match this pattern" library routine. **Lean:** skip globbing in Phase 6; let each program handle its own arguments. Shell passes words literally.
3. **`printk` interleaving with shell output.** Options: (a) ignore, (b) add a `quiet_printk` runtime flag set by `init` once it `exec`s `sh`, (c) gate `printk` on a compile-time `DEBUG` switch. **Lean:** (b), toggleable with a `/proc/printk_level` hack later.
4. **How does `exec` receive `argv` and `envp`?** Phase 3's exec copied `argv` onto the new stack. Does env live there too, or in a separate auxv? **Lean:** same stack layout as Linux `execve`: argc, argv[], 0, envp[], 0, auxv[]. Implement env this phase; auxv is Phase 7 territory.
5. **Built-in vs external for `echo`, `true`, `false`.** Bash makes them built-in for speed. Phase 6 keeps them external `/bin/echo`, `/bin/true`, `/bin/false` — simpler test surface, forces `exec` paths to work.
6. **`~/.sh_history` as a single line-append file: no rotation, no locking.** Fine until two shells run simultaneously. Phase 6 has exactly one shell. Punt multi-shell history to Phase 10.

---

## 10. Exit criteria

- [ ] `sys_pipe` returns two fds sharing one `struct pipe`; both ends close cleanly.
- [ ] Pipe reads block when empty, writes block when full, close-end gives EOF/EPIPE.
- [ ] Console line discipline: backspace, `^D`, `^C`, newline handling all covered by U-6/7/8.
- [ ] `/bin/init` is the only kernel-spawned process; it reparents orphans and reaps zombies.
- [ ] `/bin/sh` parses pipelines and redirections, runs them, reports exit status.
- [ ] Built-ins `cd`, `pwd`, `exit`, `export` work standalone; in pipelines they run in a forked child.
- [ ] History written to `/data/sh_history` on each command, read back on next boot.
- [ ] All §7 tests pass.
- [ ] Running `make qemu` without test flags drops into a `sh>` prompt a human can use.
- [ ] Selftest count grows by at least 30 assertions over Phase 5.
- [ ] `docs/design/README.md` updated to mark Phase 6 shipped.
