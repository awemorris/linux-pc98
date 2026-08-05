#!/bin/sh
set -eu

qemu="${QEMU:-/home/awe/qemu-pc98/build-i386-port/qemu-system-i386}"
bios="${BIOS:-/home/awe/qemu-pc98/pc-bios}"
image="${IMAGE:-/home/awe/linux-pc98/build/i386-minimal/linux-7.1-pc98-i386-vmlinux.img}"
out="${SWEEP_OUT:-/home/awe/linux-pc98/build/i386-minimal/memory-sweep}"
timeout="${BOOT_TIMEOUT:-15}"

mkdir -p "$out"

if [ "$#" -gt 0 ]; then
	memories="$*"
else
	memories="24M 16M 12M 10M 8M 6M 5M"
fi

for memory in $memories; do
	log="$out/serial-$memory.log"
	: > "$log"
	"$qemu" \
		-M pc9801 \
		-cpu 386 \
		-m "$memory" \
		-accel tcg \
		-L "$bios" \
		-drive if=ide,bus=0,unit=0,format=raw,file="$image" \
		-snapshot \
		-serial "file:$log" \
		-display none \
		-monitor none \
		-no-reboot &
	pid=$!

	result=TIMEOUT
	elapsed=0
	while kill -0 "$pid" 2>/dev/null && [ "$elapsed" -lt "$timeout" ]; do
		if grep -q I386-BOOT-SUCCESS "$log"; then
			result=PASS
			break
		fi
		sleep 1
		elapsed=$((elapsed + 1))
	done

	if [ "$result" != PASS ] && ! kill -0 "$pid" 2>/dev/null; then
		result=EXIT
	fi
	if kill -0 "$pid" 2>/dev/null; then
		kill "$pid"
	fi
	wait "$pid" 2>/dev/null || true

	printf '%s\t%s\t%ss\t%s bytes\n' \
		"$memory" "$result" "$elapsed" "$(wc -c < "$log")"
done
