#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
kernel_version="${KERNEL_VERSION:-7.1}"
source="${KERNEL_SOURCE:-$repo/external/kernel/linux-$kernel_version}"
default_kernel_build="$repo/build/kernel-$kernel_version"
kernel_build="${KERNEL_BUILD:-$default_kernel_build}"
root_stage="${ROOT_STAGE:-$repo/build/debian-i386-root}"
jobs="${JOBS:-$(nproc)}"
install_modules="${INSTALL_MODULES:-1}"
boot_vmlinux="${BOOT_VMLINUX:-$kernel_build/vmlinux.boot}"
objcopy="${OBJCOPY:-objcopy}"

"$repo/scripts/configure-kernel.sh"

make -C "$source" O="$kernel_build" ARCH=i386 -j"$jobs" \
	LOCALVERSION="${LOCALVERSION-}" \
	vmlinux modules

# Keep the symbol-rich vmlinux in the build directory for debugging, but put
# only the loadable, non-compressed ELF image on the CF card.  This removes
# debug and symbol-table sections without changing any PT_LOAD segment.
"$objcopy" --strip-all "$kernel_build/vmlinux" "$boot_vmlinux"
chmod 0644 "$boot_vmlinux"
if [ "$(readelf -h "$boot_vmlinux" | sed -n 's/.*Class:[[:space:]]*//p')" != ELF32 ]; then
	echo "boot vmlinux is not ELF32: $boot_vmlinux" >&2
	exit 1
fi
load_segments=$(readelf -lW "$boot_vmlinux" | grep -c '^[[:space:]]*LOAD[[:space:]]')
if [ "$load_segments" -lt 1 ] || [ "$load_segments" -gt 4 ]; then
	echo "boot vmlinux has unsupported PT_LOAD count: $load_segments" >&2
	exit 1
fi
printf 'Uncompressed boot vmlinux: %s\n' "$boot_vmlinux"

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
