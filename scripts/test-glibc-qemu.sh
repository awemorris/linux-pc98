#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
family="${1:-}"
root="${GLIBC_BUILD_ROOT:-$repo/build/glibc-2.41}"
image="${IMAGE:-$root/glibc-$family-validation.raw}"
log="${QEMU_LOG:-$root/qemu-$family-runtime.log}"

case "$family" in
i386) cpu=386; pass_marker=GLIBC_I386_ALL_PASS ;;
i486) cpu=486; pass_marker=GLIBC_I486_ALL_PASS ;;
*) echo "usage: $0 i386|i486" >&2; exit 2 ;;
esac

if [ -n "${QEMU:-}" ]; then
	qemu="$QEMU"
else
	for candidate in \
		"$repo/qemu-pc98/build-i386-port/qemu-system-i386" \
		"$repo/qemu-pc98/build/qemu-system-i386" \
		"/home/awe/qemu-pc98/build-i386-port/qemu-system-i386"
	do
		if [ -x "$candidate" ]; then
			qemu="$candidate"
			break
		fi
	done
fi

if [ -z "${qemu:-}" ] || [ ! -x "$qemu" ]; then
	echo "qemu-pc98 binary not found; set QEMU=/path/to/qemu-system-i386" >&2
	exit 1
fi
test -f "$image"
bios="${QEMU_BIOS:-$repo/qemu-pc98/pc-bios}"
if [ ! -d "$bios" ]; then
	bios="$(dirname "$(dirname "$qemu")")/pc-bios"
fi
test -d "$bios"

rm -f "$log"
set +e
timeout "${QEMU_TIMEOUT:-90}" "$qemu" \
	-accel tcg \
	-M pc9801 \
	-cpu "$cpu" \
	-m 32M \
	-L "$bios" \
	-drive "if=ide,bus=0,unit=0,format=raw,file=$image" \
	-display none \
	-serial "file:$log" \
	-monitor none \
	-no-reboot
qemu_rc=$?
set -e

if [ "$qemu_rc" -ne 0 ] && [ "$qemu_rc" -ne 124 ]; then
	echo "qemu exited unexpectedly: $qemu_rc" >&2
	tail -n 80 "$log" >&2 || true
	exit 1
fi
grep -q "$pass_marker" "$log"
tail -n 40 "$log"
printf 'qemu-pc98 glibc %s validation: PASS\n' "$family"
