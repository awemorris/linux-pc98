#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
debian_repo="$repo/external/debian-i486"
source_file="$repo/demos/xoppai/xoppai.c"
output="$repo/build/xoppai/xoppai"
sysroot="${I486_SYSROOT:-}"

usage()
{
	cat <<'EOF'
Usage: ./build.sh xoppai [--output FILE] [--sysroot DIR]

Cross-compile the Xlib xoppai demo for the Debian/i486 image.
EOF
}

while test "$#" -gt 0; do
	case "$1" in
		--output | --sysroot)
			test "$#" -ge 2 || { echo "Missing value for $1" >&2; exit 2; }
			case "$1" in
				--output) output="$2" ;;
				--sysroot) sysroot="$2" ;;
			esac
			shift 2
			;;
		-h | --help) usage; exit 0 ;;
		*) echo "Unknown xoppai option: $1" >&2; usage >&2; exit 2 ;;
	esac
done

test -f "$source_file" || { echo "xoppai source not found: $source_file" >&2; exit 1; }
test -x "$debian_repo/scripts/i486-cross-compiler" || {
	echo "external/debian-i486 is not checked out" >&2
	exit 2
}

valid_sysroot()
{
	test -n "$1" &&
		test -r "$1/usr/include/X11/Xlib.h" &&
		test -r "$1/usr/lib/i386-linux-gnu/libX11.so" &&
		test -r "$1/usr/lib/i386-linux-gnu/crt1.o" &&
		test -r "$1/usr/lib/gcc/i686-linux-gnu/14/libgcc.a"
}

if ! valid_sysroot "$sysroot"; then
	while IFS= read -r candidate; do
		candidate="${candidate#* }"
		candidate="${candidate%/.i486-sysroot.json}"
		if valid_sysroot "$candidate"; then
			sysroot="$candidate"
			break
		fi
	done < <(find "$debian_repo/work/cross-sysroots" -mindepth 3 -maxdepth 3 \
		-type f -name .i486-sysroot.json -printf '%T@ %p\n' 2>/dev/null |
		sort -nr)
fi

if ! valid_sysroot "$sysroot"; then
	echo "No i486 Xlib development sysroot is available." >&2
	echo "Build xorg-server through external/debian-i486 first, or pass --sysroot." >&2
	exit 1
fi

build_dir="$(dirname "$output")"
tool_dir="$build_dir/.tools"
part="$output.part.$$"
mkdir -p "$build_dir" "$tool_dir"
ln -sfn "$debian_repo/scripts/i486-cross-compiler" \
	"$tool_dir/i686-linux-gnu-gcc"
trap 'rm -f -- "$part"' EXIT

I486_SYSROOT="$sysroot" "$tool_dir/i686-linux-gnu-gcc" \
	-std=c11 -O2 -march=i486 -mtune=i486 \
	-fno-tree-vectorize -fno-tree-slp-vectorize \
	-Wall -Wextra -Werror -Wl,--as-needed,-s \
	-o "$part" "$source_file" -lX11
chmod 0755 "$part"
mv -f -- "$part" "$output"
trap - EXIT

file "$output"
echo "xoppai i486 demo: $output"
