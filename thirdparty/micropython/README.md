# thirdparty/micropython/

Placeholder for the MicroPython port. The roadmap §7.5 milestone runs
`micropython /data/hello.py` from our shell.

This directory intentionally contains no source yet — adding a `Makefile`
here makes the port active, per the contract in `../README.md`.

## Expected build flow when populated

1. **Source.** Fetch upstream MicroPython at a pinned tag (recommended:
   download-on-build into `upstream/`, do not vendor).
2. **mpy-cross.** Build for the build host: `make -C upstream/mpy-cross`.
   Produces the bytecode compiler used to freeze stdlib modules.
3. **Port.** Configure as a custom port (modeled on `ports/embed` or
   `ports/minimal`) with `mpconfigport.h` set to:
   - `MICROPY_FLOAT_IMPL`: `MICROPY_FLOAT_IMPL_NONE`. SBUnix is soft-float
     and `libc/math.h` ships only declarations.
   - `MICROPY_USE_INTERNAL_ERRNO`: 0. Defer to our `__errno_location()`
     in `libc/include/errno.h`.
   - `MICROPY_GC_HEAP_SIZE`: ~256 KiB statically. Avoids early `sbrk`
     dependence; debuggable OOM.
   - Disable `-fstack-protector` in the port flags. SBUnix has no
     `__stack_chk_guard` symbol and we don't want the runtime stub.
4. **Build flags.** Match `Makefile:9` of the repo root:
   `-march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany
   -ffreestanding -fno-builtin -nostdlib -nostdinc`.
5. **Link.** `$(ROOTDIR)/build/libc/crt.S.o` first, then MicroPython
   objects + frozen-modules.o, then `$(ROOTDIR)/build/libc.a`. Pass
   `-Wl,--gc-sections` per the existing user-binary rule.
6. **Install.** Drop the final ELF at `$(ROOTFS)/micropython` so the
   next top-level `make` rebuilds tarfs and exposes it at
   `/bin/micropython` inside the running kernel.

## Entry point contract

`main.c` reads `argv[1]` as the script path. If absent, exit non-zero.
Otherwise:

```c
mp_init();
mp_lexer_t *lex = mp_lexer_new_from_file(argv[1]);
qstr src = qstr_from_str(argv[1]);
mp_parse_tree_t pt = mp_parse(lex, MP_PARSE_FILE_INPUT);
mp_obj_t mod = mp_compile(&pt, src, false);
mp_call_function_0(mod);
mp_deinit();
```

Wrap in `nlr_buf_t` so unhandled Python exceptions exit cleanly via
`_exit(1)` rather than dropping out the bottom of `main`.

## What this PR has already provided

The libc surface required for the build to *link*:

- `string.h`: `memcmp`, `memchr`, `strcat`/`strncat`, `strdup`/`strndup`,
  `strstr`, `strpbrk`, `strspn`/`strcspn`, `strtok`/`strtok_r`,
  `strerror`, `strnlen`.
- `errno.h`: `__errno_location()` + `errno` macro; full POSIX errno
  spread (`EAGAIN`, `ERANGE`, `EDOM`, `EILSEQ`, `EBUSY`, `ENXIO`,
  `EXDEV`, `ENODEV`, `ELOOP`, `E2BIG`, `ENOTEMPTY`, …).
- `setjmp.h` + `libc/setjmp.S`: RV64 callee-save save/restore.
- `ctype.h` + `libc/ctype.c`: every standard char-class predicate +
  `tolower`/`toupper`.
- `assert.h` + `__assert_fail` in `libc/stdlib.c`.
- `limits.h`, `stdbool.h`, `inttypes.h`, `strings.h`, `math.h` (decls).
- `sys/wait.h` with the SBUnix-convention `WIFEXITED`/`WEXITSTATUS`/
  `WIFSIGNALED`/`WTERMSIG` macros.
- `unistd.h`: `_exit`, `isatty`, `access`, `readlink`.
- `stdlib.h`: `strtoll`/`strtoull`, `strtod`/`strtof` (stubs until FP
  lands), `div`/`ldiv`/`lldiv`, `setenv`/`getenv`/`unsetenv`/`putenv`,
  `mblen`/`mbtowc`/`wctomb`/`mbstowcs`/`wcstombs`.
- `stdio.h`: `ungetc`, `setbuf`/`setvbuf`, `tmpnam`.

The compile gate at `bin/headers_compile_gate/` references every symbol
above; if the gate links, MicroPython's link step will not blow up on a
missing libc declaration. Linking *will* still surface anything the port
genuinely needs that we haven't shipped — that's the iterative loop
