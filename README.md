# SBUnix

A RISC-V operating system developed for the CSE506 (Operating Systems) course by Professor Mike Ferdman.

## Prerequisites

- Local Setup: Docker and Visual Studio Code with the Dev Containers extension
- Course Containers: RISC-V toolchain (`riscv64-unknown-elf-gcc`, `qemu-system-riscv64`, `riscv64-unknown-elf-gdb`) included already by prof.

## Setup

### Local development: Using Dev Containers

1. Install [VS Code](https://code.visualstudio.com/) and the [Dev Containers extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers)
2. Install [Docker](https://www.docker.com/get-started)
3. Clone this repository:
   ```bash
   git clone https://github.com/SuyashJoshi179/sbunix.git
   cd sbunix
   ```
4. Open in VS Code:
   ```bash
   code .
   ```
5. When prompted, click "Reopen in Container" (or run: `Dev Containers: Reopen in Container` from the command palette)
6. Wait for the container to build and install dependencies
7. Once ready, you can start building!

The dev container automatically installs all required tools:
- `riscv64-unknown-elf-gcc` (RISC-V cross-compiler)
- `qemu-system-riscv64` (RISC-V emulator)
- `gdb-multiarch` (debugger) (Note that the course containers use `riscv64-unknown-elf-gdb` as debugger, we are using `gdb-multiarch` - a multi-architecture debugger, as it's the one provided by the Ubuntu package manager, need to check if it makes any difference)

## Project Structure

```
sbunix/
├── kernel/                # RISC-V64 kernel
│   ├── start.S            # Bootstrap (entry point)
│   ├── kernel.c           # Boot/init sequence
│   ├── trap.[cS]          # Trap/syscall entry
│   ├── proc.c             # Processes, scheduler
│   ├── exec.c             # ELF loader + shebang
│   ├── vmem.c             # Page tables, COW fork
│   ├── vma.c              # VMA tree, mmap/munmap
│   ├── pmem.c             # Physical frame allocator
│   ├── page_cache.c       # Unified file-backed page cache
│   ├── pipe.c, signal.c   # IPC + POSIX signals
│   ├── syscall.c          # Syscall dispatch (60+ syscalls)
│   ├── fs/                # tarfs (root), devfs, procfs, tmpfs, sbfs (disk)
│   ├── drivers/           # uart, plic, pci, virtio-blk, rtc
│   └── include/           # Kernel headers
├── libc/                  # Freestanding POSIX C library
│                          # stdio, stdlib, string, signal, termios,
│                          # time, fcntl, sys/* — ~290 functions
├── bin/                   # User-space programs (auto-discovered: bin/X/*.c)
│                          # init, sh, coreutils (cat/cp/ls/mkdir/...),
│                          # plus test binaries
├── rootfs/                # Embedded root filesystem
│   ├── etc/rc             # Boot script (mounts /proc, /mnt, /tmp)
│   ├── mnt/               # Mount point for sbfs disk
│   ├── proc/              # Mount point for procfs
│   └── tmp/               # Mount point for tmpfs
├── thirdparty/            # External test harness (grader-injected)
└── tools/                 # Build utilities
    └── mkfs.c             # sbfs disk-image creator
```

The kernel supports lp64 (no F/D extensions, soft-float) and runs on
`qemu-system-riscv64 -machine virt`. The on-disk filesystem (sbfs) uses
an xv6-style write-ahead log for crash safety.

## Building

Build the kernel:
```bash
make
```

Clean build artifacts:
```bash
make clean
```

## Running

Run the OS in QEMU:
```bash
make qemu
```

This will:
- Start QEMU with a RISC-V virtual machine
- Load and boot your kernel
- Display output in the terminal

**Exit QEMU:** Press `Ctrl+A` then `X`

## Features

- **Boot + low-level**: OpenSBI handoff, trap/syscall dispatch, PLIC, UART
  console, Goldfish RTC, timer interrupts and preemption.
- **Process management**: fork, exec (with shebang), wait/wait4, setsid,
  setpgid, job control (foreground/background pgroups via TIOCSPGRP),
  signals (sigaction, sigmask, sigaltstack, default handlers), rlimit.
- **Memory management**: paged virtual memory with COW fork, demand-paged
  anonymous and file-backed mmap, sbrk, mprotect-implicit RW/RO, lazy
  page allocation, page cache shared between read() and mmap().
- **File systems**: VFS layer with mount table, tarfs root (from embedded
  tarball), devfs (/dev/console, null, zero, tty, loop), procfs
  (status/cmdline/stat, meminfo, uptime), tmpfs, sbfs (on-disk, journaled).
- **IPC**: pipes (anonymous and pipeline), SIGPIPE on broken-pipe writes.
- **Userspace**: minimal POSIX C library, an init that runs `/etc/rc` and
  respawns `/bin/sh`, a Bourne-style shell with pipes, redirection
  (`<`, `>`, `>>`), backgrounding (`&`), short-circuit (`&&`, `;`),
  globbing, job control, and POSIX `exec` builtin, plus the standard
  coreutils.

## Troubleshooting

If you encounter issues in the dev container, try:
```bash
make clean && make
```
