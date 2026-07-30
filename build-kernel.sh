#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")" && pwd)"
kernel_version="${KERNEL_VERSION:-6.12}"
source="${KERNEL_SOURCE:-$repo/linux-$kernel_version}"
if [ "$kernel_version" = 6.12 ]; then
	default_kernel_build="$repo/build/kernel"
else
	default_kernel_build="$repo/build/kernel-$kernel_version"
fi
kernel_build="${KERNEL_BUILD:-$default_kernel_build}"
root_stage="${ROOT_STAGE:-$repo/build/debian-i386-root}"
jobs="${JOBS:-$(nproc)}"
install_modules="${INSTALL_MODULES:-1}"

"$repo/configure-kernel.sh"

make -C "$source" O="$kernel_build" ARCH=i386 -j"$jobs" \
	LOCALVERSION="${LOCALVERSION-}" \
	bzImage modules

if [ "$install_modules" = 1 ] && [ -d "$root_stage" ]; then
	sudo make -C "$source" O="$kernel_build" ARCH=i386 modules_install \
		LOCALVERSION="${LOCALVERSION-}" \
		INSTALL_MOD_PATH="$root_stage" \
		INSTALL_MOD_STRIP=1
elif [ "$install_modules" != 1 ]; then
	echo "INSTALL_MODULES=$install_modules; skipping modules_install"
else
	echo "Rootfs staging tree does not exist; skipping modules_install: $root_stage"
fi
