#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/../.." && pwd)"
kernel="${1:-$repo/build/i386-video/kernel/vmlinux.boot}"
work="$repo/build/bootsimple/failure-tests"
mkdir -p "$work"
bad="$work/bad-vmlinux"
cp "$kernel" "$bad"
printf 'BAD!' | dd of="$bad" bs=1 count=4 conv=notrunc status=none
if "$repo/bootsimple/verify-image.py" elf "$bad" >/dev/null 2>&1; then
	echo "corrupt ELF was accepted" >&2
	exit 1
fi
cp "$kernel" "$bad"
printf '\002' | dd of="$bad" bs=1 seek=4 count=1 conv=notrunc status=none
if "$repo/bootsimple/verify-image.py" elf "$bad" >/dev/null 2>&1; then
	echo "wrong ELF class was accepted" >&2
	exit 1
fi
rm -f "$bad"
echo "bootsimple failure validation test: PASS"
