#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")" && pwd)"
family="${1:-}"
root="${GLIBC_BUILD_ROOT:-$repo/build/glibc-2.41}"
stage="$root/stage-$family"

case "$family" in
i386) march=i386 ;;
i486) march=i486 ;;
*) echo "usage: $0 i386|i486" >&2; exit 2 ;;
esac

if [ -n "${I386_CROSS_PREFIX:-}" ]; then
	cross_prefix="$I386_CROSS_PREFIX"
elif [ -x "$repo/build/release-v0.3.0/i386-buildroot/output/host/bin/i386-buildroot-linux-musl-gcc" ]; then
	cross_prefix="$repo/build/release-v0.3.0/i386-buildroot/output/host/bin/i386-buildroot-linux-musl"
else
	cross_prefix="$repo/build/i386-buildroot/output/host/bin/i386-buildroot-linux-musl"
fi

if [ -n "${BUSYBOX_SOURCE:-}" ]; then
	busybox_source="$BUSYBOX_SOURCE"
elif [ -d "$repo/build/release-v0.3.0/i386-buildroot/output/build/busybox-1.38.0" ]; then
	busybox_source="$repo/build/release-v0.3.0/i386-buildroot/output/build/busybox-1.38.0"
else
	busybox_source="$repo/build/i386-buildroot/output/build/busybox-1.38.0"
fi

baseline_config="${BUSYBOX_CONFIG:-$busybox_source/.config}"
source_copy="$root/busybox-source-$family"
output="$root/busybox-glibc-$family"

test -x "$cross_prefix-gcc"
test -x "$stage/lib/ld-linux.so.2"
test -f "$root/sysroot/usr/include/linux/limits.h"
test -f "$busybox_source/Makefile"
test -f "$baseline_config"

for path in "$source_copy" "$output"; do
	case "$path" in
	"$root"/*) ;;
	*) echo "refusing unsafe BusyBox work path: $path" >&2; exit 1 ;;
	esac
done

# Buildroot builds BusyBox in its source directory.  An out-of-tree BusyBox
# build requires a clean source, so preserve Buildroot's tree and clean a
# disposable copy instead.
rm -rf -- "$source_copy" "$output"
cp -a "$busybox_source" "$source_copy"
make -C "$source_copy" mrproper >/dev/null
mkdir -p "$output"
cp "$baseline_config" "$output/.config"

# Reuse the full release BusyBox applet selection, but make it dynamically
# linked against the just-built glibc instead of the bootstrap musl libc.
sed -i \
	-e 's/^CONFIG_STATIC=y$/# CONFIG_STATIC is not set/' \
	-e "s|^CONFIG_SYSROOT=.*$|CONFIG_SYSROOT=\"$stage\"|" \
	-e "s|^CONFIG_EXTRA_CFLAGS=.*$|CONFIG_EXTRA_CFLAGS=\"-march=$march -mtune=$march -isystem $root/sysroot/usr/include\"|" \
	-e "s|^CONFIG_EXTRA_LDFLAGS=.*$|CONFIG_EXTRA_LDFLAGS=\"-Wl,--dynamic-linker=/lib/ld-linux.so.2 -Wl,-rpath-link,$stage/lib:$stage/usr/lib\"|" \
	"$output/.config"

make -C "$source_copy" O="$output" oldconfig </dev/null \
	>"$root/busybox-glibc-$family-config.log"
make -C "$source_copy" O="$output" \
	CROSS_COMPILE="$cross_prefix-" -j"${JOBS:-32}"

file "$output/busybox"
readelf -l "$output/busybox" | grep interpreter
readelf -d "$output/busybox" | grep NEEDED
printf 'glibc-linked BusyBox %s: %s\n' "$family" "$output/busybox"
