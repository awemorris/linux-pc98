#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
release_dir="$repo/build/releases"

usage()
{
	cat <<'EOF'
Usage: ./build.sh release-image PROFILE [--jobs N]
       ./build.sh release-image all [--jobs N]
       ./build.sh release-image list

Build a review/test image under build/releases using its canonical filename.
The destination is replaced only after the complete image was built
successfully.  Kernel, rootfs, geometry, and output overrides are deliberately
not accepted here; use "./build.sh image" for development experiments.

Profiles:
  busybox-i386-ide
  busybox-i386-scsi55
  busybox-i386-scsi92
  debian13-i486-ide
  debian13-i486-scsi55
  debian13-i486-scsi92
  all
EOF
}

profiles=(
	busybox-i386-ide
	busybox-i386-scsi55
	busybox-i386-scsi92
	debian13-i486-ide
	debian13-i486-scsi55
	debian13-i486-scsi92
)

temporary=""
checksum_temporary=""
cleanup()
{
	test -z "$temporary" || rm -f -- "$temporary"
	test -z "$checksum_temporary" || rm -f -- "$checksum_temporary"
}
trap cleanup EXIT INT TERM

canonical_name()
{
	case "$1" in
		busybox-i386-ide)
			printf '%s\n' linux-pc98-i386sx-busybox-ide.img ;;
		busybox-i386-scsi55)
			printf '%s\n' linux-pc98-i386sx-busybox-scsi55.img ;;
		busybox-i386-scsi92)
			printf '%s\n' linux-pc98-i386sx-busybox-scsi92.img ;;
		debian13-i486-ide)
			printf '%s\n' linux-pc98-i486dx-debian13-ide.img ;;
		debian13-i486-scsi55)
			printf '%s\n' linux-pc98-i486dx-debian13-scsi55.img ;;
		debian13-i486-scsi92)
			printf '%s\n' linux-pc98-i486dx-debian13-scsi92.img ;;
		*) return 1 ;;
	esac
}

verify_bootloader()
{
	local selected="$1" image="$2" sectors loader
	case "$selected" in
		busybox-i386-ide | busybox-i386-scsi55)
			sectors=17 ;;
		busybox-i386-scsi92)
			sectors=32 ;;
		debian13-*)
			test "$(od -An -tx1 -j4 -N4 "$image" | tr -d ' \n')" = \
				49504c31 || {
				echo "Release image lacks the IPL1 marker: $image" >&2
				return 1
			}
			test "$(od -An -tx1 -j510 -N2 "$image" | tr -d ' \n')" = \
				55aa || {
				echo "Release image lacks the PC-9821 IPL signature: $image" >&2
				return 1
			}
			printf 'Release bootloader: zedBSD (%s)\n' "$selected"
			return ;;
		*) return 2 ;;
	esac
	loader="$repo/build/bootsimple/$selected"
	"$repo/bootsimple/verify-image.py" all "$image" \
		--heads 8 --sectors "$sectors" --partition 1 \
		--loader-dir "$loader"
	printf 'Release bootloader: bootsimple (%s)\n' "$selected"
}

profile="${1:-}"
case "$profile" in
	-h | --help | help | '') usage; exit 0 ;;
	list) printf '%s\n' "${profiles[@]}" all; exit 0 ;;
esac
shift

jobs="${JOBS:-$(nproc)}"
while test "$#" -gt 0; do
	case "$1" in
		--jobs)
			test "$#" -ge 2 || { echo "Missing value for --jobs" >&2; exit 2; }
			jobs="$2"
			shift 2
			;;
		-h | --help)
			usage
			exit 0
			;;
		*)
			echo "Unsupported release-image option: $1" >&2
			usage >&2
			exit 2
			;;
	esac
done
case "$jobs" in
	'' | *[!0-9]* | 0) echo "Jobs must be a positive integer" >&2; exit 2 ;;
esac

build_one()
{
	local selected="$1" filename destination checksum
	filename="$(canonical_name "$selected")" || {
		echo "Unknown release image profile: $selected" >&2
		exit 2
	}
	destination="$release_dir/$filename"
	temporary="$release_dir/.${filename}.new.$$"
	checksum="$destination.sha256"
	checksum_temporary="$checksum.new.$$"

	mkdir -p "$release_dir"
	rm -f -- "$temporary" "$checksum_temporary"

	printf 'Building canonical release image: %s\n' "$destination"
	"$repo/build.sh" image "$selected" --jobs "$jobs" --output "$temporary"
	test -s "$temporary" || {
		echo "Generated image is empty: $temporary" >&2
		exit 1
	}
	verify_bootloader "$selected" "$temporary"
	mv -f -- "$temporary" "$destination"
	temporary=""
	(
		cd "$release_dir"
		sha256sum "$filename" >"$(basename "$checksum_temporary")"
	)
	mv -f -- "$checksum_temporary" "$checksum"
	checksum_temporary=""
	printf 'Release image ready: %s\n' "$destination"
}

if test "$profile" = all; then
	for selected in "${profiles[@]}"; do
		build_one "$selected"
	done
else
	build_one "$profile"
fi
