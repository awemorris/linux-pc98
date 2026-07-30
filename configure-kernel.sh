#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")" && pwd)"
kernel_version="${KERNEL_VERSION:-6.12}"
source="${KERNEL_SOURCE:-$repo/linux-$kernel_version}"
cpu_family="${CPU_FAMILY:-686}"
if [ "$kernel_version" = 6.12 ]; then
	default_kernel_build="$repo/build/kernel"
else
	default_kernel_build="$repo/build/kernel-$kernel_version"
fi
kernel_build="${KERNEL_BUILD:-$default_kernel_build}"
base="${BASE_CONFIG:-$repo/configs/debian-i386-base.config}"
if [ "$cpu_family" = 686 ]; then
	if [ "$kernel_version" = 6.12 ]; then
		default_output="$repo/configs/pc9800-debian.config"
	else
		default_output="$repo/configs/pc9800-debian-$kernel_version.config"
	fi
	cpu_config=M686
elif [ "$cpu_family" = 486 ]; then
	default_output="$repo/configs/pc9800-i486-$kernel_version.config"
	cpu_config=M486
else
	echo "Unsupported CPU_FAMILY: $cpu_family (expected 686 or 486)" >&2
	exit 1
fi
output="${OUTPUT_CONFIG:-$default_output}"

if [ ! -x "$source/scripts/config" ]; then
	echo "Linux source tree not found at $source" >&2
	exit 1
fi
if [ ! -f "$base" ]; then
	echo "Base kernel configuration not found: $base" >&2
	exit 1
fi

mkdir -p "$kernel_build"
cp "$base" "$kernel_build/.config"
make -C "$source" O="$kernel_build" ARCH=i386 olddefconfig

# PC-98 platform and the devices required before the root filesystem mounts.
"$source/scripts/config" --file "$kernel_build/.config" \
	--disable MGEODE_LX \
	--disable M486SX \
	--disable M486 \
	--disable M686 \
	--disable SMP \
	--disable ACPI \
	--enable X86_EXTENDED_PLATFORM \
	--enable X86_PC9800 \
	--enable PATA_PC9800 \
	--enable ATA \
	--enable SCSI \
	--enable BLK_DEV_SD \
	--enable NEC98_PARTITION \
	--enable KEYBOARD_PC98 \
	--enable SERIAL_PC98_8251 \
	--enable SERIAL_PC98_8251_CONSOLE \
	--enable PC98_CONSOLE \
	--enable EXT4_FS \
	--enable DEVTMPFS \
	--enable DEVTMPFS_MOUNT \
	--enable MODULES \
	--disable SND_PCSP \
	--enable FB \
	--disable FB_TRIDENT \
	--module FB_PC98_CIRRUS \
	--module FB_PC98_TRIDENT \
	--disable FRAMEBUFFER_CONSOLE \
	--disable VGA_CONSOLE \
	--disable SERIO_I8042 \
	--disable KEYBOARD_ATKBD \
	--enable CMDLINE_BOOL \
	--enable CMDLINE_OVERRIDE \
	--set-str CMDLINE \
	"console=ttyS0 console=tty0 root=/dev/sda2 rootfstype=ext4 rw"
"$source/scripts/config" --file "$kernel_build/.config" --enable "$cpu_config"

make -C "$source" O="$kernel_build" ARCH=i386 olddefconfig
mkdir -p "$(dirname "$output")"
cp "$kernel_build/.config" "$output"

echo "PC-98 Linux $kernel_version ($cpu_family) kernel config: $output"
