#!/bin/sh
set -eu

repo=/home/awe/linux-pc98
source="$repo/linux-7.1"
build="$repo/build/i386-busybox/kernel"
config="$build/.config"
base="$repo/configs/pc9800-i386-4m6-7.1.config"
output="$repo/configs/pc9800-i386-busybox-7.1.config"
sc="$source/scripts/config"

if [ ! -f "$base" ]; then
	echo "missing base configuration: $base" >&2
	exit 1
fi

mkdir -p "$build"
cp "$base" "$config"

# The root filesystem must mount before kernel-command-line sysctls are
# applied.  Once /sbin/init is about to run, keep less idle RAM in the VM
# watermarks, bound dirty data, reclaim VFS metadata aggressively, and favor
# the CF swap partition over retaining anonymous pages.
"$sc" --file "$config" --set-str CMDLINE \
	"vdso=0 console=ttyS0 console=tty0 earlyprintk=pc9800 root=/dev/hd98a2 rootfstype=ext4 rw sysctl.vm.min_free_kbytes=64 sysctl.vm.dirty_background_bytes=32768 sysctl.vm.dirty_bytes=65536 sysctl.vm.vfs_cache_pressure=200 sysctl.vm.swappiness=100 sysctl.vm.page-cluster=0"

make -C "$source" O="$build" ARCH=i386 olddefconfig
cp "$config" "$output"

printf 'i386 BusyBox config: %s\n' "$output"
