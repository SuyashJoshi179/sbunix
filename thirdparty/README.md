# thirdparty/

Build glue for external programs that link against SBUnix's in-tree libc
and run on the SBUnix kernel. Currently empty by design — a fresh
checkout has no third-party ports vendored. `make thirdparty` from the
repo root succeeds either way: the dispatcher iterates subdirectories
that contain a `Makefile`, and skips ones (like `micropython/`) that
only carry a `README.md`.

## Contract

Each port lives in its own subdirectory: `thirdparty/<name>/`.

A port directory becomes active when it contains a `Makefile` with at
least an `install` target. The top-level `thirdparty/Makefile` invokes
`$(MAKE) -C <name> install ROOTDIR=<repo root> ROOTFS=<repo>/build/rootfs/bin`.

Each port's `Makefile` must:

1. **Build with the project's RISC-V flags.** Match `Makefile:9` exactly:
   `-march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany
   -ffreestanding -fno-builtin -nostdlib -nostdinc`. Soft-float ABI; do
   not enable `f`/`d` extensions.
2. **Use only the in-tree libc.** Compile with `-isystem
   $(ROOTDIR)/libc/include`. Link with `$(ROOTDIR)/build/libc/crt.S.o`
   first, then your objects, then `$(ROOTDIR)/build/libc.a`.
3. **Drop the final ELF into `$(ROOTFS)/<binary-name>`.** The next
   `make` from the repo root rebuilds tarfs so the binary appears at
   `/bin/<binary-name>` inside the running kernel.
4. **Provide a `clean` target.** Must remove anything it produced under
   its own subdirectory; must not touch `$(ROOTFS)`.

## Available libc surface

The libc API a port can rely on is defined entirely by `libc/include/`.
Highlights as of this PR:

- C89/C99 headers: `stddef.h`, `stdint.h`, `stdarg.h`, `stdio.h`,
  `stdlib.h`, `string.h`, `ctype.h`, `assert.h`, `limits.h`, `stdbool.h`,
  `inttypes.h`, `setjmp.h`, `errno.h`, `time.h`, `signal.h`, `math.h`
  (declarations only — no soft-float libm yet).
- POSIX headers: `unistd.h`, `fcntl.h`, `dirent.h`, `termios.h`,
  `strings.h`, `sys/types.h`, `sys/stat.h`, `sys/wait.h`, `sys/mman.h`,
  `sys/ioctl.h`, `sys/time.h`.
- `errno` is a writable lvalue via `__errno_location()`. SBUnix's syscall
  wrappers themselves do not set `errno` — they return negative-errno
  directly. A port that wants POSIX-style `if (rc == -1) perror(...)`
  must wrap its uses or set `errno` itself from the negative return.
- `setjmp.h` saves the RV64 callee-saves: `ra`, `sp`, `s0..s11`. No FP
  state is saved; the build is soft-float.
- Wait status uses a non-POSIX packing: `0..127` is a normal exit code,
  `128 + signum` is a signal exit. The macros in `sys/wait.h` decode
  this directly.

## Adding a port (worked example: micropython)

The roadmap §7.5 milestone is `micropython /data/hello.py`. The expected
shape, when added:

```
thirdparty/micropython/
├── Makefile          # builds mpy-cross on host, then the embed/minimal
│                     # port for riscv64-unknown-elf, links against
│                     # ../../build/libc.a, installs to $(ROOTFS)/micropython
├── README.md         # this directory's notes
├── mpconfigport.h    # MICROPY_FLOAT_IMPL_NONE to start; configure
│                     # MICROPY_USE_INTERNAL_ERRNO to defer to our libc.
├── mphalport.h       # types + decls
├── mphalport.c       # mp_hal_stdout_tx_strn → write(1, ...);
│                     # mp_hal_delay_ms → sleep_ms; etc.
└── main.c            # argv-aware entry: argv[1] is the script path.
                      # call mp_lexer_new_from_file + mp_compile + run.
```

Source can be staged any way: a git submodule, a tarball downloaded by
the port's Makefile, or vendored in-tree. Recommendation: download in
build, so the repo stays small.
