#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
profile=""
output=""
cmdline=""

usage()
{
	cat <<'EOF'
Usage: bootsimple/build.sh --profile NAME --cmdline STRING [--output-dir DIR]

Build the assembly-only PC-98 IO.SYS Linux loader. The output directory
defaults to build/bootsimple/NAME.
EOF
}

while test "$#" -gt 0; do
	case "$1" in
		--profile | --cmdline | --output-dir)
			test "$#" -ge 2 || { echo "Missing value for $1" >&2; exit 2; }
			case "$1" in
				--profile) profile="$2" ;;
				--cmdline) cmdline="$2" ;;
				--output-dir) output="$2" ;;
			esac
			shift 2
			;;
		-h | --help) usage; exit 0 ;;
		*) echo "Unknown bootsimple build option: $1" >&2; exit 2 ;;
	esac
done

test -n "$profile" || { echo "--profile is required" >&2; exit 2; }
case "$profile" in
	*[!A-Za-z0-9_.-]* | '') echo "Invalid profile name: $profile" >&2; exit 2 ;;
esac
test -n "$cmdline" || { echo "--cmdline is required" >&2; exit 2; }
test "${#cmdline}" -lt 4096 || {
	echo "bootsimple command line must be shorter than 4096 bytes" >&2
	exit 2
}
case "$cmdline" in
	*$'\n'* | *$'\r'*) echo "bootsimple command line must be one line" >&2; exit 2 ;;
esac

output="${output:-$repo/build/bootsimple/$profile}"
resolved="$(realpath -m -- "$output")"
case "$resolved" in
	"$repo/build/bootsimple/"*) ;;
	*) echo "Output must be below $repo/build/bootsimple: $resolved" >&2; exit 2 ;;
esac
mkdir -p "$resolved"
printf '%s' "$cmdline" >"$resolved/cmdline.bin"

make -f "$repo/bootsimple/Makefile" \
	SRC_DIR="$repo/bootsimple" OUT="$resolved" all

