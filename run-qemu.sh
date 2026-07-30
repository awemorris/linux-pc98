#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")" && pwd)"
disk="${1:-$repo/build/qemu-pc98-linux.raw}"

qemu="${QEMU:-$HOME/qemu-codex/qemu-pc98-dev/build-release-codex/qemu-system-i386}"
bios_dir="${BIOS_DIR:-$HOME/qemu-codex/qemu-pc98-dev/pc-bios}"
accel="${ACCEL:-tcg}"
cpu="${CPU:-pentium2,-apic}"
memory="${MEMORY:-64}"
display_backend="${DISPLAY_BACKEND:-none}"
machine="${MACHINE:-pc9801}"

exec "$qemu" \
	-M "$machine" \
	-cpu "$cpu" \
	-m "$memory" \
	-accel "$accel" \
	-L "$bios_dir" \
	-drive if=ide,bus=0,unit=0,format=raw,file="$disk" \
	-serial stdio \
	-display "$display_backend" \
	-monitor none \
	-no-reboot
