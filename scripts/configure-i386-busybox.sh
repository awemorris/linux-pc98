#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$repo/external/kernel/linux-7.1"
console_mode="${I386_CONSOLE:-video}"
cpu_family="${CPU_FAMILY:-386}"
build="${I386_KERNEL_BUILD:-$repo/build/i386-busybox/kernel}"
config="$build/.config"
base="$repo/configs/pc9800-i386-4m6-7.1.config"
output="${I386_CONFIG_OUTPUT:-$repo/configs/pc9800-i386-busybox-7.1.config}"
sc="$source/scripts/config"

case "$cpu_family" in
386)
	cpu_config=M386
	;;
486)
	cpu_config=M486
	;;
*)
	echo "unsupported minimal BusyBox CPU family: $cpu_family" >&2
	echo "supported CPU families: 386, 486" >&2
	exit 1
	;;
esac

if [ ! -f "$base" ]; then
	echo "missing base configuration: $base" >&2
	exit 1
fi

mkdir -p "$build"
cp "$base" "$config"

# The i386 and i486 release kernels intentionally share the same small PC-98
# device/filesystem profile.  Only the compiler CPU target differs.
"$sc" --file "$config" \
	--disable M386 \
	--disable M486SX \
	--disable M486 \
	--enable "$cpu_config" \
	--enable MODIFY_LDT_SYSCALL \
	--enable MATH_EMULATION \
	--enable FUTEX \
	--enable COMPAT_32BIT_TIME \
	--disable CMDLINE_OVERRIDE \
	--enable SCSI \
	--enable BLK_DEV_SD \
	--disable SCSI_PROC_FS \
	--disable BLK_DEV_BSG \
	--enable SCSI_LOWLEVEL \
	--enable SCSI_PC9801_92

case "$console_mode" in
dual)
	console_args="console=ttyPC0 console=tty0"
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

# BusyBox images are started by bootsimple, which owns the complete profile
# command line (including IDE/SCSI-specific arguments).  Keep the built-in
# string empty and reject CONFIG_CMDLINE_OVERRIDE so /proc/cmdline contains
# exactly what IO.SYS supplied.
"$sc" --file "$config" --set-str CMDLINE ""

make -C "$source" O="$build" ARCH=i386 olddefconfig
cp "$config" "$output"

printf 'i%s BusyBox config (%s console): %s\n' \
	"$cpu_family" "$console_mode" "$output"
