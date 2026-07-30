#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")" && pwd)"
source="$repo/linux-6.12"
kernel_build="${KERNEL_BUILD:-$repo/build/kernel}"
root_stage="${ROOT_STAGE:-$repo/build/debian-i386-root}"
jobs="${JOBS:-$(nproc)}"

"$repo/configure-kernel.sh"

make -C "$source" O="$kernel_build" ARCH=i386 -j"$jobs" bzImage modules

if [ -d "$root_stage" ]; then
	sudo make -C "$source" O="$kernel_build" ARCH=i386 modules_install \
		INSTALL_MOD_PATH="$root_stage" \
		INSTALL_MOD_STRIP=1
else
	echo "Rootfs staging tree does not exist; skipping modules_install: $root_stage"
fi
