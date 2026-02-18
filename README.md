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

## Development

The kernel currently boots and prints "Booting SBUnix". As we progress through the course, we'll implement:
- System calls
- Process management
- Memory management
- File systems
- Shell
- ...

## Troubleshooting

If you encounter issues in the dev container, try:
```bash
make clean && make
```
