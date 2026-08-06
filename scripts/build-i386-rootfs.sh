#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
jobs="${JOBS:-$(nproc)}"
version="${BUILDROOT_VERSION:-2026.05}"
work="${I386_BUILDROOT_WORK:-$repo/build/i386-buildroot}"
source="$work/buildroot-$version"
output="$work/output"
archive="$work/buildroot-$version.tar.xz"
url="https://buildroot.org/downloads/buildroot-$version.tar.xz"
gcc_source="$repo/toolchain/gcc"
musl_source="$repo/toolchain/musl"
console_mode="${I386_CONSOLE:-video}"

case "$console_mode" in
dual)
	rootfs_overlay="$repo/rootfs/i386"
	;;
video)
	rootfs_overlay="$repo/rootfs/i386-video"
	;;
*)
	echo "unsupported I386_CONSOLE mode: $console_mode" >&2
	echo "supported modes: dual, video" >&2
	exit 1
	;;
esac

mkdir -p "$work"
if [ ! -f "$gcc_source/gcc/BASE-VER" ]; then
	echo "GCC submodule is missing: $gcc_source" >&2
	echo "run: git submodule update --init --recursive" >&2
	exit 1
fi
if [ ! -f "$musl_source/VERSION" ]; then
	echo "musl submodule is missing: $musl_source" >&2
	echo "run: git submodule update --init --recursive" >&2
	exit 1
fi
if [ ! -f "$archive" ]; then
	curl --fail --location --retry 3 --output "$archive" "$url"
fi
if [ ! -d "$source" ]; then
	tar -C "$work" -xf "$archive"
	patch -d "$source" -p1 \
		< "$repo/patches/buildroot-2026.05-i386.patch"
fi
install -m 0644 \
	"$repo/patches/busybox-1.38.0-i386.patch" \
	"$source/package/busybox/0012-i386-do-not-require-i486-instructions.patch"

# Buildroot's override mechanism copies the repository-managed GCC and musl
# submodules to output/build/*-custom. It therefore does not download,
# extract, or patch separate GCC or musl release tarballs.
mkdir -p "$output"
printf 'GCC_INITIAL_OVERRIDE_SRCDIR = %s\n' "$gcc_source" \
	> "$output/local.mk"
printf 'GCC_FINAL_OVERRIDE_SRCDIR = %s\n' "$gcc_source" \
	>> "$output/local.mk"
printf 'MUSL_OVERRIDE_SRCDIR = %s\n' "$musl_source" \
	>> "$output/local.mk"

make -C "$source" O="$output" \
	BR2_DEFCONFIG="$repo/configs/buildroot-pc98-i386-busybox.defconfig" \
	defconfig

# Keep the runtime setup in Buildroot's rootfs finalization path so it is
# present both in output/target and in every generated rootfs archive.
sed -i \
	-e '/^BR2_ROOTFS_OVERLAY=/d' \
	-e '/^# BR2_ROOTFS_OVERLAY is not set/d' \
	-e '/^BR2_PACKAGE_BUSYBOX_CONFIG=/d' \
	-e '/^# BR2_PACKAGE_BUSYBOX_CONFIG is not set/d' \
	"$output/.config"
printf 'BR2_ROOTFS_OVERLAY="%s"\nBR2_PACKAGE_BUSYBOX_CONFIG="%s"\n' \
	"$rootfs_overlay" \
	"$repo/configs/busybox-pc98-i386.config" \
	>> "$output/.config"
make -C "$source" O="$output" olddefconfig

make -C "$source" O="$output" -j"$jobs"

busybox="$output/target/bin/busybox"
if [ ! -x "$busybox" ]; then
	echo "BusyBox was not installed: $busybox" >&2
	exit 1
fi
if ! "$busybox" --list | grep -qx ifconfig; then
	echo "BusyBox was built without the required ifconfig applet" >&2
	exit 1
fi
if [ ! -L "$output/target/sbin/ifconfig" ]; then
	echo "BusyBox ifconfig installation link is missing" >&2
	exit 1
fi

bad=$("$output/host/bin/i386-buildroot-linux-musl-objdump" -d "$busybox" |
	awk -F '\t' 'NF >= 3 {
		asm = $3
		sub(/[[:space:]]*#.*/, "", asm)
		if (asm ~ /(^|[[:space:]])(lock[[:space:]]+)?(bswap|cmov[a-z]*|cmpxchg[0-9a-z]*|cpuid|rdtsc|rdmsr|wrmsr|rdpmc|rsm|xadd|pause|emms|femms|sysenter|sysexit|syscall|sysret|fxsave|fxrstor|xsave|xrstor|prefetch[a-z]*|[slm]fence|clflush[a-z]*|movnti|monitor|mwait|popcnt|lzcnt|tzcnt|movbe|rdrand|rdseed|adcx|adox|mulx|sh[lr]x|sarx|rorx|andn|bextr|bzhi|bls[imr]|pdep|pext|crc32|ud2|nop[lw])([[:space:]]|$)/) {
			print
			next
		}
		if (asm ~ /^[[:space:]]*f[0-9a-z]+([[:space:]]|$)/) {
			print
			next
		}
		if (asm ~ /%[xyz]?mm[0-9]/)
			print
	}')
if [ -n "$bad" ]; then
	echo "BusyBox contains instructions newer than i386:" >&2
	printf '%s\n' "$bad" | head -40 >&2
	exit 1
fi

lock_xchg_count=$("$output/host/bin/i386-buildroot-linux-musl-objdump" \
	-d "$busybox" |
	awk -F '\t' 'NF >= 3 &&
		$3 ~ /^[[:space:]]*lock[[:space:]]+xchg([[:space:]]|$)/ {
			count++
		}
		END { print count + 0 }')
if [ "$lock_xchg_count" -eq 0 ]; then
	echo "BusyBox does not contain the required i386 LOCK XCHG operation" >&2
	exit 1
fi

printf 'i386 BusyBox rootfs: %s\n' "$output/target"
printf 'i386 console mode: %s\n' "$console_mode"
du -sh "$output/target"
file "$busybox"
printf 'i386 test-and-set lock sites (LOCK XCHG): %s\n' "$lock_xchg_count"
