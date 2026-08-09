#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
family="${1:-}"
jobs="${JOBS:-32}"
root="${GLIBC_BUILD_ROOT:-$repo/build/glibc-2.41}"
source_dir="$repo/external/glibc"
sysroot="$root/sysroot"
kernel_build="$root/kernel-headers"

case "$family" in
i386)
	march=i386
	host=i386-linux-gnu
	extra_configure=(--enable-i386-kernel-atomics)
	;;
i486)
	march=i486
	host=i486-linux-gnu
	extra_configure=()
	;;
*)
	echo "usage: $0 i386|i486" >&2
	exit 2
	;;
esac

if [ -n "${I386_CROSS_PREFIX:-}" ]; then
	cross_prefix="$I386_CROSS_PREFIX"
elif [ -x "$repo/build/release-v0.3.0/i386-buildroot/output/host/bin/i386-buildroot-linux-musl-gcc" ]; then
	cross_prefix="$repo/build/release-v0.3.0/i386-buildroot/output/host/bin/i386-buildroot-linux-musl"
else
	cross_prefix="$repo/build/i386-buildroot/output/host/bin/i386-buildroot-linux-musl"
fi

if [ ! -x "$cross_prefix-gcc" ]; then
	echo "i386 cross compiler not found: $cross_prefix-gcc" >&2
	echo "set I386_CROSS_PREFIX or build the i386 Buildroot toolchain first" >&2
	exit 1
fi

mkdir -p "$root" "$sysroot"

# The exact-i386 configure check consumes the versioned atomic UAPI.  Generate
# headers from this repository rather than relying on host kernel headers.
CPU_FAMILY=386 I386_CONSOLE=dual \
I386_KERNEL_BUILD="$kernel_build" \
I386_CONFIG_OUTPUT="$root/kernel-headers.config" \
	"$repo/scripts/configure-i386-busybox.sh"
make -C "$repo/external/kernel/linux-7.1" O="$kernel_build" ARCH=i386 \
	headers_install INSTALL_HDR_PATH="$sysroot/usr"

build_dir="$root/build-$family"
stage="$root/stage-$family"
case "$build_dir" in
"$root"/*) ;;
*) echo "refusing unsafe build directory: $build_dir" >&2; exit 1 ;;
esac
rm -rf -- "$build_dir" "$stage"
mkdir -p "$build_dir" "$stage"

cd "$build_dir"
CC="$cross_prefix-gcc" CXX=false \
CFLAGS="-O2 -g -march=$march -mtune=$march -fno-omit-frame-pointer" \
"$source_dir/configure" \
	--build=x86_64-linux-gnu \
	--host="$host" \
	--prefix=/usr \
	--with-headers="$sysroot/usr/include" \
	--enable-kernel=7.1.0 \
	--disable-multi-arch \
	--disable-werror \
	"${extra_configure[@]}"

if [ "$family" = i386 ]; then
	grep -q 'sysdeps/i386/i386-kernel-atomic' config.make
	grep -q '^#define HAVE_I386_KERNEL_ATOMICS 1$' config.h
elif grep -q 'i386-kernel-atomic' config.make; then
	echo "exact-i386 sysdeps leaked into the i486 build" >&2
	exit 1
fi

make -j"$jobs"
make install DESTDIR="$stage"

test -x "$stage/lib/ld-linux.so.2"
file "$stage/lib/ld-linux.so.2" "$stage/lib/libc.so.6"
if [ "$family" = i486 ]; then
	# The i486 path must retain glibc's guarded CPUID probing.  This catches
	# an over-broad __i386__ preprocessor condition which would also disable
	# CPU discovery in i586/i686 builds.
	"$cross_prefix-objdump" -d "$stage/lib/ld-linux.so.2" |
		awk '$0 ~ /[[:space:]]cpuid([[:space:]]|$)/ { found = 1 }
		     END { exit !found }'
fi
printf 'glibc 2.41 %s stage: %s\n' "$family" "$stage"
