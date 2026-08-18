#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
root_stage="${ROOT_STAGE:-$repo/build/debian-i386-root}"

if [ ! -d "$root_stage" ]; then
	"$repo/scripts/build-debian-rootfs.sh"
fi
"$repo/scripts/build-kernel.sh"
common_cmdline='vdso=0 console=tty0 earlyprintk=pc9800 root=PARTLABEL=LINUXROOT rootfstype=ext4 rw sysctl.vm.min_free_kbytes=64 sysctl.vm.dirty_background_bytes=32768 sysctl.vm.dirty_bytes=65536 sysctl.vm.vfs_cache_pressure=200 sysctl.vm.swappiness=100 sysctl.vm.page-cluster=0'
BOOTLOADER=bootsimple BOOTSIMPLE_PROFILE=debian13-i486-ide \
	BOOTSIMPLE_CMDLINE="$common_cmdline" "$repo/scripts/build-images.sh"
