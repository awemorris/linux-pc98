#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
release_dir="$repo/build/releases"
version="${RELEASE_VERSION:-v0.9.0}"
jobs="${JOBS:-$(nproc)}"
publish_rootfs=0
build_win64=1

usage()
{
	cat <<'EOF'
Usage: ./build.sh release [--version TAG] [--jobs N]
                          [--publish-rootfs-cache] [--no-win64]

Build every public image, standalone kernel, bootloader ZIP, Release note,
and the qemu-pc98 Windows ZIP under build/releases. Raw images are retained
locally; the distributable disk files are the .img.xz archives.
EOF
}

while test "$#" -gt 0; do
	case "$1" in
		--version | --jobs)
			test "$#" -ge 2 || { echo "Missing value for $1" >&2; exit 2; }
			case "$1" in --version) version="$2" ;; --jobs) jobs="$2" ;; esac
			shift 2
			;;
		--publish-rootfs-cache) publish_rootfs=1; shift ;;
		--no-win64) build_win64=0; shift ;;
		-h | --help) usage; exit 0 ;;
		*) echo "Unknown release option: $1" >&2; usage >&2; exit 2 ;;
	esac
done
case "$jobs" in '' | *[!0-9]* | 0) echo "Jobs must be positive" >&2; exit 2 ;; esac

note="$repo/releases/$version.md"
busybox_root="$repo/build/i386-video/buildroot/output/target"
debian_root="$repo/build/boot98/debian13-i486-root"
busybox_cache=busybox-i386-video-buildroot-2026.05
debian_manifest="$repo/configs/debian13-i486-packages.txt"
debian_manifest_sha256="$(sha256sum "$debian_manifest" | awk '{print $1}')"
debian_cache="debian13-i486-trixie-${debian_manifest_sha256:0:12}"

ensure_rootfs()
{
	local profile="$1" cache="$2" target="$3" marker="$4"
	if test ! -e "$target/$marker"; then
		echo "Restoring rootfs cache: $cache"
		if ! "$repo/build.sh" rootfs-cache materialize "$cache" "$target"; then
			echo "Cache unavailable; building rootfs profile $profile"
			ROOT_STAGE="$target" "$repo/build.sh" rootfs "$profile"
		fi
	fi
	test -e "$target/$marker" || { echo "Incomplete rootfs: $target" >&2; exit 1; }
}

ensure_debian_rootfs()
{
	local target="$1" cache="$2" current=""
	if test -f "$target/etc/linux-pc98-rootfs-profile"; then
		current="$(cat "$target/etc/linux-pc98-rootfs-profile")"
	fi
	if test "$current" != "$debian_manifest_sha256"; then
		case "$target" in
			"$repo"/build/*) ;;
			*) echo "Refusing to replace rootfs outside build/: $target" >&2; exit 1 ;;
		esac
		echo "Replacing stale Debian rootfs profile: $target"
		sudo rm -rf -- "$target"
	fi
	ensure_rootfs debian13-i486 "$cache" "$target" bin/sh
	test "$(cat "$target/etc/linux-pc98-rootfs-profile" 2>/dev/null || true)" = \
		"$debian_manifest_sha256" || {
		echo "Debian rootfs profile does not match $debian_manifest" >&2
		exit 1
	}
}

compress_image()
{
	local raw="$1" archive part
	archive="$raw.xz"
	part="$archive.part.$$"
	rm -f -- "$part"
	xz -c -T"${XZ_THREADS:-0}" -"${XZ_LEVEL:-6}" "$raw" >"$part"
	xz -t "$part"
	mv -f -- "$part" "$archive"
}

copy_release_file()
{
	local source="$1" destination="$2"
	test -f "$source" || { echo "Release source missing: $source" >&2; exit 1; }
	install -m 0644 "$source" "$release_dir/$destination"
}

mkdir -p "$release_dir"
test -f "$note" || { echo "Release note not found: $note" >&2; exit 1; }
# Keep the staging directory unambiguous.  These files were uploaded by older
# releases, but current releases publish the complete loader kit as
# bootloader.zip.
rm -f -- \
	"$release_dir"/boot.bin "$release_dir"/boot.sys \
	"$release_dir"/boot.cfg "$release_dir"/IO.SYS \
	"$release_dir"/BOOT.SYS "$release_dir"/inst.exe \
	"$release_dir"/linux98.exe "$release_dir"/ipl-lba0.bin \
	"$release_dir"/ipl-lba2.bin "$release_dir"/ipl-lba0.img \
	"$release_dir"/ipl-lba2.img "$release_dir"/ipl-part.img \
	"$release_dir"/*.img.sha256 "$release_dir"/*.img.xz.sha256 \
	"$release_dir"/qemu-pc98-win64.zip.sha256
ensure_rootfs busybox-i386 "$busybox_cache" "$busybox_root" bin/busybox
ensure_debian_rootfs "$debian_root" "$debian_cache"

echo "Verifying Noct through M14 (v0.9.0 has a documented REPL issue)"
(
	. "$repo/scripts/boots-env.sh"
	"$boots/build.sh" pc98 noct-m14-verify
)

echo "Building i386 BusyBox IDE and SCSI images"
SKIP_ROOTFS_BUILD=1 SKIP_KERNEL_BUILD=0 JOBS="$jobs" \
	"$repo/build.sh" release-image busybox-i386-ide --jobs "$jobs"
for profile in busybox-i386-scsi55 busybox-i386-scsi92; do
	SKIP_ROOTFS_BUILD=1 SKIP_KERNEL_BUILD=1 JOBS="$jobs" \
		"$repo/build.sh" release-image "$profile" --jobs "$jobs"
done
for compact in \
	linux-pc98-i386sx-busybox-ide.img \
	linux-pc98-i386sx-busybox-scsi55.img \
	linux-pc98-i386sx-busybox-scsi92.img; do
	compact_size="$(stat -c %s "$release_dir/$compact")"
	if test "$compact_size" -ge 40000000; then
		echo "$compact exceeds the 40 MB compatibility limit: $compact_size" >&2
		exit 1
	fi
done

echo "Building the shared Debian/i486 kernel and modules"
ROOT_STAGE="$debian_root" INSTALL_MODULES=1 \
	"$repo/build.sh" kernel --cpu 486 --profile pc98 \
	--output-dir "$repo/build/kernel-7.1-i486" --jobs "$jobs"
"$repo/build.sh" rootfs-cache store "$busybox_cache" "$busybox_root" >/dev/null
"$repo/build.sh" rootfs-cache store "$debian_cache" "$debian_root" >/dev/null
if test "$publish_rootfs" -eq 1; then
	"$repo/build.sh" rootfs-cache publish "$busybox_cache" "$busybox_root"
	"$repo/build.sh" rootfs-cache publish "$debian_cache" "$debian_root"
fi

for profile in debian13-i486-ide debian13-i486-scsi55 debian13-i486-scsi92; do
	"$repo/build.sh" release-image "$profile" --jobs "$jobs"
done
for debian_image in \
	linux-pc98-i486dx-debian13-ide.img \
	linux-pc98-i486dx-debian13-scsi55.img \
	linux-pc98-i486dx-debian13-scsi92.img; do
	debian_size="$(stat -c %s "$release_dir/$debian_image")"
	if test "$debian_size" -ge 2000000000; then
		echo "$debian_image exceeds the 2 GB release limit: $debian_size" >&2
		exit 1
	fi
done

for raw in \
	linux-pc98-i386sx-busybox-ide.img \
	linux-pc98-i386sx-busybox-scsi55.img \
	linux-pc98-i386sx-busybox-scsi92.img \
	linux-pc98-i486dx-debian13-ide.img \
	linux-pc98-i486dx-debian13-scsi55.img \
	linux-pc98-i486dx-debian13-scsi92.img; do
	compress_image "$release_dir/$raw"
done

copy_release_file "$repo/build/i386-video/kernel/vmlinux.boot" vmlinux-i386
copy_release_file "$repo/build/kernel-7.1-i486/vmlinux.boot" vmlinux-i486-debian
"$repo/build.sh" bootloader-dist
copy_release_file "$note" RELEASE-NOTES.md

if test "$build_win64" -eq 1; then
	JOBS="$jobs" "$repo/build.sh" win64-dist build
fi

(
	cd "$release_dir"
	sha256sum \
		linux-pc98-i386sx-busybox-ide.img.xz \
		linux-pc98-i386sx-busybox-scsi55.img.xz \
		linux-pc98-i386sx-busybox-scsi92.img.xz \
		linux-pc98-i486dx-debian13-ide.img.xz \
		linux-pc98-i486dx-debian13-scsi55.img.xz \
		linux-pc98-i486dx-debian13-scsi92.img.xz \
		vmlinux-i386 vmlinux-i486-debian bootloader.zip \
		qemu-pc98-win64.zip >SHA256SUMS
)
printf 'Complete %s artifact set: %s\n' "$version" "$release_dir"
