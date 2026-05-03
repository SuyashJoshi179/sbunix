#!/bin/sh
# Regenerate BusyBox's .config from an all-no base plus SBUnix's tiny seed.
set -e

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BB="$REPO/third_party/busybox"
SEED="$REPO/tools/busybox-min.config"
CONFIG="$BB/.config"

set_config() {
    sym="$1"
    val="$2"

    if grep -q "^$sym=" "$CONFIG"; then
        sed -i "s|^$sym=.*|$sym=$val|" "$CONFIG"
    elif grep -q "^# $sym is not set" "$CONFIG"; then
        sed -i "s|^# $sym is not set|$sym=$val|" "$CONFIG"
    else
        printf '%s=%s\n' "$sym" "$val" >> "$CONFIG"
    fi
}

make -C "$BB" allnoconfig

while IFS= read -r line; do
    case "$line" in
        CONFIG_*=*)
            set_config "${line%%=*}" "${line#*=}"
            ;;
    esac
done < "$SEED"

yes "" | make -C "$BB" oldconfig
