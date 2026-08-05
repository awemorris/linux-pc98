#!/bin/sh
set -eu

memory=${1:-6M}
image=${IMAGE:-/home/awe/linux-pc98/build/i386-minimal/linux-7.1-pc98-i386-vmlinux.img}
log="/tmp/awe-i386-lgy-${memory}.log"
rm -f "$log"
timeout 12s /home/awe/qemu-pc98/build-i386-port/qemu-system-i386 \
	-M pc9801 \
	-cpu 386 \
	-m "$memory" \
	-accel tcg \
	-L /home/awe/qemu-pc98/pc-bios \
	-drive "if=ide,bus=0,unit=0,format=raw,file=$image" \
	-snapshot \
	-netdev user,id=n0 \
	-device pc98-lgy98,netdev=n0 \
	-serial "file:$log" \
	-display none \
	-monitor none \
	-no-reboot || true

grep -E 'LGY|lgy|eth|I386-|Memory:' "$log" || true
tail -25 "$log"
