#!/bin/sh
# Compiler wrapper for building user-space programs (BusyBox, ports) against
# the SBUnix in-tree libc. Produces freestanding rv64 binaries linked with
# build/libc.a and libc/crt.S.o.
#
# Usage: invoked as $CC by foreign build systems. Equivalent to invoking
# riscv64-unknown-elf-gcc with the right includes/libs and no host headers.
set -e

REPO="$(cd "$(dirname "$0")/.." && pwd)"
GCC="riscv64-unknown-elf-gcc"

CFLAGS="
  -nostdinc
  -isystem $REPO/libc/include
  -isystem $($GCC -print-file-name=include)
  -ffreestanding -fno-builtin -nostdlib
  -mcmodel=medany
  -march=rv64imac_zicsr_zifencei
  -mabi=lp64
  -fno-stack-protector
  -fno-pic
  -fno-pie
"

# Detect link mode by scanning args for compile-only flags or .o-output.
mode=link
for a in "$@"; do
    case "$a" in
        -c|-S|-E|-M|-MM) mode=compile ;;
    esac
done

if [ "$mode" = "link" ]; then
    exec $GCC $CFLAGS "$@" \
        -Wl,--gc-sections \
        "$REPO/build/libc/crt.S.o" \
        "$REPO/build/libc.a"
else
    exec $GCC $CFLAGS "$@"
fi
