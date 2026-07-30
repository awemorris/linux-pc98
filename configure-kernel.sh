#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")" && pwd)"
kernel_version="${KERNEL_VERSION:-6.12}"
source="${KERNEL_SOURCE:-$repo/linux-$kernel_version}"
cpu_family="${CPU_FAMILY:-686}"
device_profile="${DEVICE_PROFILE:-}"
if [ -z "$device_profile" ]; then
	if [ "$kernel_version" = 7.1 ]; then
		device_profile=pc98
	else
		device_profile=full
	fi
fi
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
		--disable SCSI_LOWLEVEL \
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

make -C "$source" O="$kernel_build" ARCH=i386 olddefconfig
if [ "$device_profile" = pc98 ]; then
	set +o pipefail
	yes "" | make -C "$source" O="$kernel_build" ARCH=i386 \
		LSMOD="$repo/configs/pc9800-modules.list" localmodconfig
	set -o pipefail
	# PC-9821 Ra43's onboard PC-9821X-B06-compatible adapter is an
	# Intel 82557 (8086:1229, subsystem 1033:8000).  Keep e100 built in
	# so the minimal, module-free i486 rootfs can use the real adapter.
	"$source/scripts/config" --file "$kernel_build/.config" \
		--enable NET_VENDOR_INTEL \
		--enable E100 \
		--enable MII
	make -C "$source" O="$kernel_build" ARCH=i386 olddefconfig
fi
mkdir -p "$(dirname "$output")"
cp "$kernel_build/.config" "$output"

echo "PC-98 Linux $kernel_version ($cpu_family) kernel config: $output"
