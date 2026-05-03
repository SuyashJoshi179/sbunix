#!/bin/sh
# Run SBUnix in QEMU with scripted stdin and timeout; capture serial output.
#
# Usage: tools/qemu-script.sh [timeout_seconds] [< commands]
#        tools/qemu-script.sh 5 <<EOF
#        echo HELLO
#        ls /bin
#        EOF
#
# Exit status reflects timeout (124) or qemu exit. Output goes to stdout.
set -e

TIMEOUT="${1:-10}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
KERNEL="$REPO/build/kernel.elf"
DISK="$REPO/build/disk.img"

[ -f "$KERNEL" ] || { echo "missing $KERNEL — run make first" >&2; exit 2; }

exec timeout --foreground --kill-after=2 "$TIMEOUT" \
    qemu-system-riscv64 -machine virt -bios default -kernel "$KERNEL" \
        -drive file="$DISK",format=raw,if=none,id=hd0 \
        -device virtio-blk-pci-non-transitional,drive=hd0 \
        -device virtio-net-pci-non-transitional,netdev=net0 \
        -netdev user,id=net0 \
        -nographic
