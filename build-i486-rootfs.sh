#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")" && pwd)"
jobs="${JOBS:-$(nproc)}"
cpu_family="${CPU_FAMILY:-486}"
case "$cpu_family" in
	486)
		cpu_name=i486
		default_work="$repo/build/i486-toolchain"
		default_root_stage="$repo/build/i486-rootfs"
		;;
	686)
		cpu_name=i686
		default_work="$repo/build/i686-toolchain"
		default_root_stage="$repo/build/i686-rootfs"
		;;
	*)
		echo "Unsupported CPU_FAMILY: $cpu_family (expected 486 or 686)" >&2
		exit 1
		;;
esac
if [ "$cpu_family" = 486 ]; then
	work="${BUSYBOX_WORK:-${I486_WORK:-$default_work}}"
else
	work="${BUSYBOX_WORK:-$default_work}"
fi
root_stage="${ROOT_STAGE:-$default_root_stage}"
gcc_version="${GCC_VERSION:-14.2.0}"
musl_version="${MUSL_VERSION:-1.2.5}"
busybox_version="${BUSYBOX_VERSION:-1.36.1}"
target="$cpu_name-linux-musl"
prefix="$work/toolchain"
xgcc="$prefix/bin/$target-gcc"

download()
{
	url=$1
	output=$2
	if [ ! -f "$output" ]; then
		curl --fail --location --retry 3 --output "$output" "$url"
	fi
}

mkdir -p "$work/src" "$work/build" "$work/xbin"

download \
	"https://ftp.gnu.org/gnu/gcc/gcc-$gcc_version/gcc-$gcc_version.tar.xz" \
	"$work/src/gcc-$gcc_version.tar.xz"
download \
	"https://musl.libc.org/releases/musl-$musl_version.tar.gz" \
	"$work/src/musl-$musl_version.tar.gz"
download \
	"https://busybox.net/downloads/busybox-$busybox_version.tar.bz2" \
	"$work/src/busybox-$busybox_version.tar.bz2"

if [ ! -d "$work/src/gcc-$gcc_version" ]; then
	tar -C "$work/src" -xf "$work/src/gcc-$gcc_version.tar.xz"
	(
		cd "$work/src/gcc-$gcc_version"
		./contrib/download_prerequisites
	)
fi
if [ ! -d "$work/src/musl-$musl_version" ]; then
	tar -C "$work/src" -xf "$work/src/musl-$musl_version.tar.gz"
fi
if [ ! -d "$work/src/busybox-$busybox_version" ]; then
	tar -C "$work/src" -xf "$work/src/busybox-$busybox_version.tar.bz2"
fi

for tool in as ld ar ranlib nm objdump objcopy strip readelf; do
	ln -sfn "$(command -v "$tool")" "$work/xbin/$target-$tool"
done
export PATH="$work/xbin:$prefix/bin:$PATH"

if [ ! -x "$xgcc" ]; then
	rm -rf "$work/build/gcc"
	mkdir -p "$work/build/gcc"
	(
		cd "$work/build/gcc"
		"$work/src/gcc-$gcc_version/configure" \
			--target="$target" \
			--prefix="$prefix" \
			--enable-languages=c \
			--disable-bootstrap \
			--disable-shared \
			--disable-threads \
			--disable-multilib \
			--disable-nls \
			--disable-cet \
			--disable-libssp \
			--disable-libquadmath \
			--disable-libatomic \
			--disable-libgomp \
			--without-headers \
			--with-newlib
		make -j"$jobs" all-gcc
		make -j"$jobs" all-target-libgcc \
			CFLAGS_FOR_TARGET="-O2 -march=$cpu_name -fcf-protection=none"
		make install-gcc install-target-libgcc
	)
fi

if [ ! -x "$prefix/musl/bin/musl-gcc" ]; then
	rm -rf "$work/build/musl"
	mkdir -p "$work/build/musl"
	(
		cd "$work/build/musl"
		"$work/src/musl-$musl_version/configure" \
			--prefix="$prefix/musl" \
			--disable-shared \
			--target="$target" \
			--enable-wrapper=gcc \
			CC="$xgcc" \
			CFLAGS="-O2 -march=$cpu_name -fcf-protection=none" \
			AR="$target-ar" \
			RANLIB="$target-ranlib"
		make -j"$jobs"
		make install
	)
fi

mkdir -p "$work/build/headers"
make -s -C "$repo/linux-7.1" \
	O="$work/build/headers" \
	ARCH=i386 \
	headers_install \
	INSTALL_HDR_PATH="$prefix/musl"

if [ -e "$root_stage" ]; then
	echo "Refusing to overwrite existing $cpu_name rootfs: $root_stage" >&2
	exit 1
fi

busybox_build="$work/build/busybox"
rm -rf "$busybox_build"
cp -a "$work/src/busybox-$busybox_version" "$busybox_build"
(
	cd "$busybox_build"
	make defconfig
	sed -i \
		-e 's/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/' \
		-e 's/^CONFIG_TC=y/# CONFIG_TC is not set/' \
		-e 's/^CONFIG_POWERTOP=y/# CONFIG_POWERTOP is not set/' \
		-e 's/^CONFIG_SHA1_HWACCEL=y/# CONFIG_SHA1_HWACCEL is not set/' \
		-e 's/^CONFIG_SHA256_HWACCEL=y/# CONFIG_SHA256_HWACCEL is not set/' \
		-e 's/^CONFIG_WERROR=y/# CONFIG_WERROR is not set/' \
		.config

	export REALGCC="$xgcc"
	make -j"$jobs" \
		CC="$prefix/musl/bin/musl-gcc -Wl,-melf_i386" \
		LD="$target-ld -m elf_i386" \
		HOSTCC=gcc \
		CONFIG_EXTRA_CFLAGS="-march=$cpu_name -fcf-protection=none -Wno-error"

	if [ "$cpu_family" = 486 ]; then
		bad=$("$target-objdump" -d busybox | awk -F'\t' 'NF >= 3 {
			split($3, field, " ")
			mnemonic = field[1]
			if (mnemonic ~ /^(cmov|endbr|nop[lw]$|fu?comip?$|fisttp|cmpxchg8b|cpuid|rdtsc|rdmsr|wrmsr|rdpmc|rsm$|emms|femms|sysenter|sysexit|syscall|sysret|fxsave|fxrstor|xsave|xrstor|prefetch|[slm]fence|clflush|movnti|monitor|mwait|popcnt|lzcnt|tzcnt|movbe|rdrand|rdseed|adcx|adox|mulx|sh[lr]x|sarx|rorx|andn$|bextr|bzhi|bls[imr]|pdep|pext|crc32)/) {
				print
				next
			}
			if ($3 ~ /%[xyz]?mm[0-9]/)
				print
		}')
		if [ -n "$bad" ]; then
			echo "BusyBox contains instructions newer than i486:" >&2
			printf '%s\n' "$bad" | head -20 >&2
			exit 1
		fi
	fi

	mkdir -p \
		"$root_stage/dev" \
		"$root_stage/proc" \
		"$root_stage/sys" \
		"$root_stage/tmp" \
		"$root_stage/root" \
		"$root_stage/var" \
		"$root_stage/mnt"
	cp -a "$repo/rootfs/i486/." "$root_stage/"
	if [ "$cpu_family" = 686 ]; then
		sed -i 's/i486/i686/g' \
			"$root_stage/etc/profile" \
			"$root_stage/etc/inittab"
	fi
	make \
		CC="$prefix/musl/bin/musl-gcc -Wl,-melf_i386" \
		LD="$target-ld -m elf_i386" \
		HOSTCC=gcc \
		CONFIG_EXTRA_CFLAGS="-march=$cpu_name -fcf-protection=none -Wno-error" \
		CONFIG_PREFIX="$root_stage" \
		install
)

printf '%s rootfs: %s\n' "$cpu_name" "$root_stage"
du -sh "$root_stage"
file "$root_stage/bin/busybox"
