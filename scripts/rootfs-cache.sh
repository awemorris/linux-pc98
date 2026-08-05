#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
cache_dir="${PC98_ROOTFS_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/linux-pc98/rootfs}"
base_url="${PC98_ROOTFS_BASE_URL:-https://noctvm.io/debian-i486/images/pc98/rootfs}"
remote_dir="${PC98_ROOTFS_REMOTE:-package-server:www/noctvm.io/debian-i486/images/pc98/rootfs}"

usage()
{
	cat <<'EOF'
Usage:
  ./build.sh rootfs-cache fetch NAME
  ./build.sh rootfs-cache materialize NAME OUTPUT-DIR
  ./build.sh rootfs-cache store NAME ROOTFS-DIR
  ./build.sh rootfs-cache publish NAME ROOTFS-DIR
  ./build.sh rootfs-cache list

Archives are NAME.tar.xz plus NAME.tar.xz.sha256. Fetches use HTTPS.
Publishing writes only to the documented package-server PC-98 rootfs path.
EOF
}

validate_name()
{
	case "$1" in
		'' | *[!a-zA-Z0-9._-]*)
			echo "Invalid rootfs cache name: $1" >&2
			exit 2
			;;
	esac
}

archive_path()
{
	printf '%s/%s.tar.xz\n' "$cache_dir" "$1"
}

verify_archive()
{
	local name="$1" archive checksum
	archive="$(archive_path "$name")"
	checksum="$archive.sha256"
	test -f "$archive" && test -f "$checksum" || return 1
	(cd "$cache_dir" && sha256sum -c "$(basename "$checksum")" >/dev/null)
	xz -t "$archive"
}

fetch_rootfs()
{
	local name="$1" archive checksum part_archive part_checksum
	validate_name "$name"
	mkdir -p "$cache_dir"
	archive="$(archive_path "$name")"
	checksum="$archive.sha256"
	if verify_archive "$name"; then
		printf '%s\n' "$archive"
		return
	fi
	part_archive="$archive.part.$$"
	part_checksum="$checksum.part.$$"
	trap 'rm -f -- "$part_archive" "$part_checksum"' RETURN
	curl --fail --location --retry 3 --output "$part_archive" \
		"$base_url/$name.tar.xz"
	curl --fail --location --retry 3 --output "$part_checksum" \
		"$base_url/$name.tar.xz.sha256"
	mv "$part_archive" "$archive"
	mv "$part_checksum" "$checksum"
	trap - RETURN
	verify_archive "$name"
	printf '%s\n' "$archive"
}

store_rootfs()
{
	local name="$1" rootfs="$2" archive checksum part
	validate_name "$name"
	test -d "$rootfs" || { echo "Rootfs not found: $rootfs" >&2; exit 1; }
	mkdir -p "$cache_dir"
	archive="$(archive_path "$name")"
	checksum="$archive.sha256"
	part="$archive.part.$$"
	trap 'rm -f -- "$part"' RETURN
	sudo tar --numeric-owner --xattrs --acls -C "$rootfs" -cf - . | \
		xz -c -T"${XZ_THREADS:-0}" -"${XZ_LEVEL:-6}" >"$part"
	xz -t "$part"
	mv "$part" "$archive"
	trap - RETURN
	(cd "$cache_dir" && sha256sum "$(basename "$archive")" >"$(basename "$checksum")")
	verify_archive "$name"
	printf '%s\n' "$archive"
}

materialize_rootfs()
{
	local name="$1" output="$2" archive parent temporary
	test ! -e "$output" || { echo "Refusing to overwrite rootfs: $output" >&2; exit 1; }
	archive="$(fetch_rootfs "$name")"
	parent="$(dirname "$output")"
	temporary="$parent/.$(basename "$output").part.$$"
	mkdir -p "$parent"
	trap 'sudo rm -rf -- "$temporary"' RETURN
	sudo mkdir "$temporary"
	xz -dc "$archive" | sudo tar --numeric-owner --xattrs --acls -xpf - -C "$temporary"
	sudo mv "$temporary" "$output"
	trap - RETURN
	printf 'Materialized rootfs: %s\n' "$output"
}

publish_rootfs()
{
	local name="$1" rootfs="$2" archive checksum
	archive="$(store_rootfs "$name" "$rootfs")"
	checksum="$archive.sha256"
	ssh package-server "mkdir -p www/noctvm.io/debian-i486/images/pc98/rootfs"
	rsync --archive --partial "$archive" "$checksum" "$remote_dir/"
	printf 'Published %s/%s.tar.xz\n' "$base_url" "$name"
}

case "${1:-}" in
	fetch) test "$#" -eq 2 || { usage >&2; exit 2; }; fetch_rootfs "$2" ;;
	materialize) test "$#" -eq 3 || { usage >&2; exit 2; }; materialize_rootfs "$2" "$3" ;;
	store) test "$#" -eq 3 || { usage >&2; exit 2; }; store_rootfs "$2" "$3" ;;
	publish) test "$#" -eq 3 || { usage >&2; exit 2; }; publish_rootfs "$2" "$3" ;;
	list)
		test "$#" -eq 1 || { usage >&2; exit 2; }
		mkdir -p "$cache_dir"
		find "$cache_dir" -maxdepth 1 -type f -name '*.tar.xz' -printf '%f\n' |
			sed 's/\.tar\.xz$//' | sort
		;;
	*) usage >&2; exit 2 ;;
esac
