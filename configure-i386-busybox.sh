#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source="$repo/linux-7.1"
console_mode="${I386_CONSOLE:-dual}"
build="${I386_KERNEL_BUILD:-$repo/build/i386-busybox/kernel}"
config="$build/.config"
base="$repo/configs/pc9800-i386-4m6-7.1.config"
output="${I386_CONFIG_OUTPUT:-$repo/configs/pc9800-i386-busybox-7.1.config}"
sc="$source/scripts/config"

if [ ! -f "$base" ]; then
	echo "missing base configuration: $base" >&2
	exit 1
fi

mkdir -p "$build"
cp "$base" "$config"

case "$console_mode" in
dual)
	console_args="console=ttyS0 console=tty0"
	;;
video)
	console_args="console=tty0"
	"$sc" --file "$config" --disable SERIAL_PC98_8251_CONSOLE
	"$sc" --file "$config" --disable SERIAL_PC98_8251
	;;
*)
	echo "unsupported I386_CONSOLE mode: $console_mode" >&2
	echo "supported modes: dual, video" >&2
	exit 1
	;;
esac

# The root filesystem must mount before kernel-command-line sysctls are
# applied.  Once /sbin/init is about to run, keep less idle RAM in the VM
# watermarks, bound dirty data, reclaim VFS metadata aggressively, and favor
# the CF swap partition over retaining anonymous pages.
"$sc" --file "$config" --set-str CMDLINE \
	"vdso=0 $console_args earlyprintk=pc9800 root=/dev/hd98a2 rootfstype=ext4 rw sysctl.vm.min_free_kbytes=64 sysctl.vm.dirty_background_bytes=32768 sysctl.vm.dirty_bytes=65536 sysctl.vm.vfs_cache_pressure=200 sysctl.vm.swappiness=100 sysctl.vm.page-cluster=0"

make -C "$source" O="$build" ARCH=i386 olddefconfig
cp "$config" "$output"

printf 'i386 BusyBox config (%s console): %s\n' "$console_mode" "$output"
