#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
family="${1:-}"
root="${GLIBC_BUILD_ROOT:-$repo/build/glibc-2.41}"
stage="$root/stage-$family"
output="$root/tests-$family"

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

test -x "$cross_prefix-gcc"
test -x "$stage/lib/ld-linux.so.2"
mkdir -p "$output"

common=(
	-O2 -g "-march=$march" "-mtune=$march"
	"--sysroot=$stage"
	-isystem "$root/sysroot/usr/include"
	"-B$stage/usr/lib"
	-Wl,--dynamic-linker=/lib/ld-linux.so.2
	"-Wl,-rpath-link,$stage/lib:$stage/usr/lib"
)

"$cross_prefix-gcc" "${common[@]}" -pthread \
	"$repo/tests/glibc-i386-smoke.c" -ldl -lm \
	-o "$output/glibc-$family-smoke"

if [ "$family" = i386 ]; then
	"$cross_prefix-gcc" "${common[@]}" -pthread \
		"$repo/external/kernel/linux-7.1/tools/testing/selftests/x86/i386_atomic.c" \
		-o "$output/i386-atomic-selftest"
fi

file "$output"/*
readelf -l "$output/glibc-$family-smoke" | grep interpreter
readelf -d "$output/glibc-$family-smoke" | grep NEEDED
