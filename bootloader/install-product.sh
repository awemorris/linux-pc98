#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
. "$repo/scripts/zedbsd-env.sh"
partition=1
arguments=("$@")
index=0
while test "$index" -lt "${#arguments[@]}"; do
	case "${arguments[$index]}" in
		--partition)
			index=$((index + 1))
			test "$index" -lt "${#arguments[@]}" || {
				echo "Missing value for --partition" >&2
				exit 2
			}
			partition="${arguments[$index]}"
			;;
		--install-disk-stubs) ;;
		-*) ;;
		*) image="${arguments[$index]}"; break ;;
	esac
	index=$((index + 1))
done
test -n "${image:-}" || {
	echo "usage: $0 [zedBSD install options] IMAGE [VMLINUX [BOOT.CFG]]" >&2
	exit 2
}
"$zedbsd/scripts/install-image.sh" "${arguments[@]}"
# zedBSD partition 0 means auto-select BOOT. linux-pc98 images place BOOT in
# partition 1, so normalize the overlay selection after auto-detection.
test "$partition" -ne 0 || partition=1
"$repo/bootloader/install-fs.sh" --partition "$partition" "$image"
