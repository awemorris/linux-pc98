#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
cache_dir="${PC98_IMAGE_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/linux-pc98/images}"
base_url="${PC98_BASE_URL:-https://noctvm.io/debian-i486/images/pc98/bases}"
remote_dir="${PC98_BASE_REMOTE:-package-server:www/noctvm.io/debian-i486/images/pc98/bases}"

usage()
{
	cat <<'EOF'
Usage:
  image-cache.sh fetch NAME
  image-cache.sh materialize NAME OUTPUT.raw
  image-cache.sh publish NAME INPUT.raw[.xz]
  image-cache.sh list

Base images are stored as NAME.raw.xz plus NAME.raw.xz.sha256.  Fetches use
HTTPS only.  Publishing writes only to the fixed package-server base path.
EOF
}

validate_name()
{
	case "$1" in
		'' | *[!a-zA-Z0-9._-]*)
			echo "Invalid base-image name: $1" >&2
			exit 2
			;;
	esac
}

archive_path()
{
	printf '%s/%s.raw.xz\n' "$cache_dir" "$1"
}

verify_archive()
{
	local name="$1"
	local archive checksum
	archive="$(archive_path "$name")"
	checksum="$archive.sha256"
	test -f "$archive" && test -f "$checksum" || return 1
	(
		cd "$cache_dir"
		sha256sum -c "$(basename "$checksum")" >/dev/null
	)
	xz -t "$archive"
}

fetch_image()
{
	local name="$1"
	local archive checksum temporary_archive temporary_checksum
	validate_name "$name"
	mkdir -p "$cache_dir"
	archive="$(archive_path "$name")"
	checksum="$archive.sha256"

	if verify_archive "$name"; then
		printf '%s\n' "$archive"
		return
	fi

	temporary_archive="$archive.part.$$"
	temporary_checksum="$checksum.part.$$"
	trap 'rm -f -- "$temporary_archive" "$temporary_checksum"' RETURN
	curl --fail --location --retry 3 --output "$temporary_archive" \
		"$base_url/$name.raw.xz"
	curl --fail --location --retry 3 --output "$temporary_checksum" \
		"$base_url/$name.raw.xz.sha256"
	mv "$temporary_archive" "$archive"
	mv "$temporary_checksum" "$checksum"
	trap - RETURN
	verify_archive "$name"
	printf '%s\n' "$archive"
}

materialize_image()
{
	local name="$1"
	local output="$2"
	local archive temporary
	test ! -e "$output" || {
		echo "Refusing to overwrite existing image: $output" >&2
		exit 1
	}
	archive="$(fetch_image "$name")"
	mkdir -p "$(dirname "$output")"
	temporary="$output.part.$$"
	trap 'rm -f -- "$temporary"' RETURN
	xz -dc "$archive" >"$temporary"
	mv "$temporary" "$output"
	trap - RETURN
}

publish_image()
{
	local name="$1"
	local input="$2"
	local archive checksum temporary
	validate_name "$name"
	test -f "$input" || {
		echo "Base image not found: $input" >&2
		exit 1
	}
	command -v rsync >/dev/null
	command -v ssh >/dev/null
	mkdir -p "$cache_dir"
	archive="$(archive_path "$name")"
	checksum="$archive.sha256"
	temporary="$archive.part.$$"
	trap 'rm -f -- "$temporary"' RETURN

	case "$input" in
		*.xz)
			xz -t "$input"
			cp --reflink=auto "$input" "$temporary"
			;;
		*)
			xz -c -T"${XZ_THREADS:-0}" -"${XZ_LEVEL:-6}" \
				"$input" >"$temporary"
			;;
	esac
	mv "$temporary" "$archive"
	trap - RETURN
	(
		cd "$cache_dir"
		sha256sum "$(basename "$archive")" >"$(basename "$checksum")"
	)
	verify_archive "$name"

	# The package server is intentionally not searched or listed.  Only the
	# documented directory is created, then the two exact files are updated.
	ssh package-server \
		"mkdir -p www/noctvm.io/debian-i486/images/pc98/bases"
	rsync --archive --partial "$archive" "$checksum" "$remote_dir/"
	printf 'Published %s and checksum\n' "$base_url/$name.raw.xz"
}

command="${1:-}"
case "$command" in
	fetch)
		test "$#" -eq 2 || { usage >&2; exit 2; }
		fetch_image "$2"
		;;
	materialize)
		test "$#" -eq 3 || { usage >&2; exit 2; }
		materialize_image "$2" "$3"
		;;
	publish)
		test "$#" -eq 3 || { usage >&2; exit 2; }
		publish_image "$2" "$3"
		;;
	list)
		test "$#" -eq 1 || { usage >&2; exit 2; }
		mkdir -p "$cache_dir"
		find "$cache_dir" -maxdepth 1 -type f -name '*.raw.xz' \
			-printf '%f\n' | sed 's/\.raw\.xz$//' | sort
		;;
	*)
		usage >&2
		exit 2
		;;
esac
