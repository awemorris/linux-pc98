#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
kernel_version="${KERNEL_VERSION:-7.1}"
source="${KERNEL_SOURCE:-$repo/linux-$kernel_version}"
cpu_family="${CPU_FAMILY:-686}"
device_profile="${DEVICE_PROFILE:-}"
console_mode="${CONSOLE_MODE:-video}"
case "$console_mode" in
video)
	console_args="console=tty0"
	;;
dual)
	console_args="console=ttyPC0 console=tty0"
	;;
*)
	echo "Unsupported CONSOLE_MODE: $console_mode (expected video or dual)" >&2
	exit 1
	;;
esac
if [ -z "$device_profile" ]; then
	device_profile=pc98
fi
default_kernel_build="$repo/build/kernel-$kernel_version"
kernel_build="${KERNEL_BUILD:-$default_kernel_build}"
base="${BASE_CONFIG:-$repo/configs/debian-i386-base.config}"
if [ "$cpu_family" = 686 ]; then
	default_output="$repo/configs/pc9800-debian-$kernel_version.config"
	cpu_config=M686
elif [ "$cpu_family" = 486 ]; then
	default_output="$repo/configs/pc9800-i486-$kernel_version.config"
	cpu_config=M486
elif [ "$cpu_family" = 386 ]; then
	default_output="$repo/configs/pc9800-i386-$kernel_version.config"
	cpu_config=M386
else
	echo "Unsupported CPU_FAMILY: $cpu_family (expected 686, 486, or 386)" >&2
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
	--disable M386 \
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
	--disable SERIAL_PC98_8251_CONSOLE \
	--enable PC98_CONSOLE \
	--enable EXT4_FS \
	--disable BLK_DEV_INITRD \
	--disable RD_GZIP \
	--disable RD_BZIP2 \
	--disable RD_LZMA \
	--disable RD_XZ \
	--disable RD_LZO \
	--disable RD_LZ4 \
	--disable RD_ZSTD \
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
	"$console_args earlyprintk=pc9800 root=/dev/sda2 rootfstype=ext4 rw"
if [ "$kernel_version" = 7.1 ]; then
	# Core-Graph is not a conventional PCI VGA function, so fbcon must not
	# restrict attachment to the framebuffer it considers primary.
	"$source/scripts/config" --file "$kernel_build/.config" \
		--enable FRAMEBUFFER_CONSOLE \
		--disable FRAMEBUFFER_CONSOLE_DETECT_PRIMARY \
		--disable FRAMEBUFFER_CONSOLE_DEFERRED_TAKEOVER
fi
"$source/scripts/config" --file "$kernel_build/.config" --enable "$cpu_config"
if [ "$console_mode" = dual ]; then
	"$source/scripts/config" --file "$kernel_build/.config" \
		--enable SERIAL_PC98_8251_CONSOLE
fi

if [ "$cpu_family" = 386 ]; then
	# The first 80386 target is deliberately UP-only and uses the legacy
	# PC-98 PIC/PIT.  Keep post-386 platform facilities out of the binary
	# until each instruction path has been audited.
	"$source/scripts/config" --file "$kernel_build/.config" \
		--disable SMP \
		--disable X86_LOCAL_APIC \
		--disable X86_IO_APIC \
		--disable PAE \
		--disable HIGHMEM64G \
		--disable MTRR \
		--disable X86_PAT \
		--disable PCI_MSI \
		--disable KVM_GUEST \
		--disable XEN \
		--disable RANDOMIZE_BASE \
		--disable CPU_MITIGATIONS \
		--disable PAGE_TABLE_ISOLATION \
		--disable STRICT_KERNEL_RWX \
		--disable STRICT_MODULE_RWX \
		--disable PREEMPT \
		--disable PREEMPT_DYNAMIC \
		--enable PREEMPT_NONE \
		--enable MATH_EMULATION \
		--set-str CMDLINE \
		"no387 vdso=0 $console_args earlyprintk=pc9800 root=/dev/sda2 rootfstype=ext4 rw init=/sbin/i386-init"
fi

if [ "$device_profile" = pc98 ]; then
	# Keep the PCI core used by pc9821 and the standard USB 1.x/2.0 host
	# controllers, but omit the large catalogue of unrelated PC/AT devices.
	"$source/scripts/config" --file "$kernel_build/.config" \
		--disable DRM \
		--disable MEDIA_SUPPORT \
		--disable SOUND \
		--disable WLAN \
		--disable INFINIBAND \
		--disable COMEDI \
		--disable IIO \
		--disable STAGING \
		--disable ACCESSIBILITY \
		--disable AUXDISPLAY \
		--disable MTD \
		--disable FIREWIRE \
		--disable NFC \
		--disable BT \
		--disable IEEE802154 \
		--disable CAN \
		--disable ATM \
		--disable FDDI \
		--disable HIPPI \
		--disable HAMRADIO \
		--disable ISDN \
		--enable SCSI_LOWLEVEL \
		--enable SCSI_PC9801_92 \
		--disable MMC \
		--disable MEMSTICK \
		--disable NVME_CORE \
		--disable PARPORT \
		--disable WATCHDOG \
		--disable INPUT_JOYSTICK \
		--disable INPUT_TABLET \
		--disable INPUT_TOUCHSCREEN

	# USB and HID have many vendor-specific drivers without a common Kconfig
	# switch. Reset them, then retain the host controllers and generic class
	# drivers useful with qemu-pc98 and physical USB passthrough.
	while IFS= read -r symbol; do
		"$source/scripts/config" --file "$kernel_build/.config" \
			--disable "$symbol"
	done < <(sed -n -E 's/^CONFIG_(USB[^=]*)=(y|m)$/\1/p' \
		"$kernel_build/.config")
	while IFS= read -r symbol; do
		"$source/scripts/config" --file "$kernel_build/.config" \
			--disable "$symbol"
	done < <(sed -n -E 's/^CONFIG_(HID_[^=]*)=(y|m)$/\1/p' \
		"$kernel_build/.config")
	while IFS= read -r symbol; do
		"$source/scripts/config" --file "$kernel_build/.config" \
			--disable "$symbol"
	done < <(sed -n -E 's/^CONFIG_(NET_VENDOR_[^=]*)=y$/\1/p' \
		"$kernel_build/.config")

	"$source/scripts/config" --file "$kernel_build/.config" \
		--enable PCI \
		--enable USB_SUPPORT \
		--enable USB_PCI \
		--module USB \
		--module USB_UHCI_HCD \
		--module USB_OHCI_HCD \
		--module USB_OHCI_HCD_PCI \
		--module USB_EHCI_HCD \
		--module USB_EHCI_PCI \
		--module USB_STORAGE \
		--module USB_ACM \
		--module USB_PRINTER \
		--module USB_WDM \
		--module USB_NET_DRIVERS \
		--module USB_USBNET \
		--module USB_NET_CDCETHER \
		--module USB_NET_CDC_NCM \
		--enable HID_SUPPORT \
		--module HID \
		--module HID_GENERIC \
		--module USB_HID
elif [ "$device_profile" != full ]; then
	echo "Unsupported DEVICE_PROFILE: $device_profile (expected pc98 or full)" >&2
	exit 1
fi

if [ "$cpu_family" = 386 ]; then
	# The i386 milestone is a research image whose acceptance test is a
	# freestanding static init.  Remove subsystems that either require
	# post-386 lock-free user-memory primitives (futex PI) or add large
	# tracing/BPF/module surfaces unrelated to that boot gate.
	"$source/scripts/config" --file "$kernel_build/.config" \
		--enable MODULES \
		--disable PARAVIRT \
		--disable KEXEC \
		--disable KEXEC_FILE \
		--disable KEXEC_CORE \
		--disable PERF_EVENTS \
		--disable X86_CPU_RESCTRL \
		--disable KPROBES \
		--disable FTRACE \
		--disable FUNCTION_TRACER \
		--disable TRACING \
		--disable BPF \
		--disable BPF_SYSCALL \
		--disable BPF_JIT \
		--disable AF_KCM \
		--disable PROFILING \
		--disable FUTEX \
		--disable FUTEX_PI \
		--disable SOCK_DIAG \
		--disable USB_SUPPORT \
		--disable USB \
		--disable HID_SUPPORT \
		--disable FB \
		--disable PCI
fi

make -C "$source" O="$kernel_build" ARCH=i386 olddefconfig
if [ "$device_profile" = pc98 ]; then
	set +o pipefail
	yes "" | make -C "$source" O="$kernel_build" ARCH=i386 \
		LSMOD="$repo/configs/pc9800-modules.list" localmodconfig
	set -o pipefail
	# PC-9821 Ra43's onboard PC-9821X-B06-compatible adapter is an
	# Intel 82557 (8086:1229, subsystem 1033:8000).  Keep e100 built in
	# so the minimal, module-free i486 rootfs can use the real adapter.
	# Keep the LGY-98 driver built in as well; qemu-pc98 exposes this
	# C-Bus NE2000-compatible adapter at I/O 0x00d0 and IRQ 6.
	"$source/scripts/config" --file "$kernel_build/.config" \
		--enable SCSI_LOWLEVEL \
		--enable SCSI_PC9801_92 \
		--enable NET_VENDOR_INTEL \
		--enable E100 \
		--enable MII \
		--enable NET_VENDOR_NATSEMI \
		--enable NET_VENDOR_8390 \
		--enable NE2K_LGY98
	if [ "$kernel_version" = 7.1 ] && [ "$cpu_family" != 386 ]; then
		# Native keyboard and mouse both use the Linux input subsystem.
		# evdev exposes them as /dev/input/event* for Xorg and other users;
		# keep it out of the deliberately tiny 80386 research image.
		"$source/scripts/config" --file "$kernel_build/.config" \
			--enable INPUT_EVDEV \
			--module MOUSE_PC98
	fi
	make -C "$source" O="$kernel_build" ARCH=i386 olddefconfig
fi
if [ "$kernel_version" = 7.1 ]; then
	# Core-Graph varies between PC-98 models and can freeze an untested
	# machine during modeset.  Keep it out of the i386/i486 kernels.  The
	# Debian/i686 build provides an explicitly loaded module with no modalias.
	if [ "$cpu_family" = 686 ]; then
		"$source/scripts/config" --file "$kernel_build/.config" \
			--module FB_PC98_CIRRUS
	else
		"$source/scripts/config" --file "$kernel_build/.config" \
			--disable FB_PC98_CIRRUS
	fi
	make -C "$source" O="$kernel_build" ARCH=i386 olddefconfig
fi
mkdir -p "$(dirname "$output")"
cp "$kernel_build/.config" "$output"

echo "PC-98 Linux $kernel_version ($cpu_family) kernel config: $output"
