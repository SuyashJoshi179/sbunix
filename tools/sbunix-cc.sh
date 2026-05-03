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

# Detect link mode by scanning args for compile-only flags and partial links.
# Also drop -lm: SBUnix has no math library; BusyBox tacks it on but minimal
# applets don't actually call any libm functions.
mode=link
filtered=""
for a in "$@"; do
    case "$a" in
        -c|-S|-E|-M|-MM) mode=compile ;;
        -r) mode=partial ;;
        -lm) continue ;;
    esac
    filtered="$filtered $a"
done

if [ "$mode" = "link" ]; then
    # crt0 must come first so its undefined `main` reference is live when
    # the rest of the link is processed. libc.a goes at the end inside its
    # own --start-group so unresolved libc symbols pulled by late objects
    # still get satisfied (foreign build systems like BusyBox supply their
    # own --start-group around their archives but don't include ours).
    exec $GCC $CFLAGS \
        "$REPO/build/libc/crt.S.o" \
        $filtered \
        -Wl,--gc-sections \
        -Wl,--start-group "$REPO/build/libc.a" -Wl,--end-group
else
    exec $GCC $CFLAGS $filtered
fi
