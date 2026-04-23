# SBUnix

A minimal RISC-V operating system with a kernel, libc, init, and interactive shell.

## Prerequisites

- `riscv64-unknown-elf-gcc`
- `riscv64-unknown-elf-ld`
- `riscv64-unknown-elf-objcopy`
- `qemu-system-riscv64`
- `gcc` (host compiler for build utilities)

## Setup

```bash
git clone https://github.com/SuyashJoshi179/sbunix.git
cd sbunix
```

## Project Structure

```
sbunix/
├── kernel/          # OS kernel code
│   ├── start.S      # Bootstrap assembly
│   ├── kernel.c     # Main kernel implementation
│   ├── printk.c     # Kernel printing functions
│   └── kernel.ld    # Linker script
├── libc/            # Minimal C library
│   ├── crt.S        # C runtime startup
│   ├── printf.c     # Standard I/O functions
│   └── exit.c       # Exit implementation
├── bin/             # User-space programs
│   └── echo/        # Echo command
├── rootfs/          # Root filesystem
│   └── etc/rc       # Startup script
└── tools/           # Build utilities
   └── mkfs.c       # Disk image creator
```

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

## Troubleshooting

If you encounter build issues, try:
```bash
make clean && make
```
