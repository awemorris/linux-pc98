#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/../.." && pwd)"
image="${1:?usage: $0 IMAGE ide|scsi [LOG]}"
storage="${2:?usage: $0 IMAGE ide|scsi [LOG]}"
log="${3:-$repo/build/bootsimple/qemu-test.log}"
qemu="${QEMU:-$HOME/qemu-pc98/build/qemu-system-i386}"
bios="${BIOS_DIR:-$HOME/qemu-pc98/roms/pc98bios}"
memory="${MEMORY:-8}"
seconds="${TIMEOUT:-35}"
case "$storage" in ide | scsi) ;; *) echo "storage must be ide or scsi" >&2; exit 2 ;; esac
test -x "$qemu"
test -d "$bios"
mkdir -p "$(dirname "$log")"
set +e
timeout --signal=INT --kill-after=5 "$seconds" "$qemu" \
	-M pc9821 -cpu 386 -m "${memory}M" -L "$bios" \
	-drive "if=$storage,bus=0,unit=0,format=raw,file=$image" \
	-snapshot -display none -serial stdio -monitor none -no-reboot \
	>"$log" 2>&1
status=$?
set -e
case "$status" in 0 | 124) ;; *) cat "$log" >&2; exit "$status" ;; esac
grep -q 'Linux version' "$log"
grep -q 'PC-98 disk: BIOS drive' "$log"
grep -q 'VFS: Mounted root' "$log"
grep -q 'Run /sbin/init' "$log"
echo "bootsimple QEMU $storage test: PASS ($log)"

