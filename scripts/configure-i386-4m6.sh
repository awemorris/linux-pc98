#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$repo/linux-7.1"
build="$repo/build/i386-4m6/kernel"
config="$build/.config"
base="$repo/configs/pc9800-i386-minimal-7.1.config"
output="$repo/configs/pc9800-i386-4m6-7.1.config"
sc="$source/scripts/config"

if [ ! -f "$base" ]; then
	echo "missing base configuration: $base" >&2
	exit 1
fi

mkdir -p "$build"
cp "$base" "$config"

# A PC-9801 with 640 KiB conventional plus 4 MiB extended memory has a
# 5 MiB physical address span in QEMU because 0xa0000-0xfffff is the legacy
# memory hole.  The full Linux IPv4 stack costs about 895 KiB of static image
# plus dynamic allocations, so the 4.6 MiB profile intentionally omits LGY-98.
# The direct PC-98 IDE driver, ext4, swap, GDC text, keyboard and serial remain.
"$sc" --file "$config" \
	--enable SWAP \
	--disable NET \
	--disable PACKET \
	--disable UNIX \
	--disable INET \
	--disable IP_PNP \
	--disable IP_PNP_DHCP \
	--disable NETDEVICES \
	--disable ETHERNET \
	--disable NE2K_LGY98

make -C "$source" O="$build" ARCH=i386 olddefconfig
cp "$config" "$output"

printf '4.6 MiB config: %s\n' "$output"
