#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$repo/build"

usage()
{
	cat <<'EOF'
Usage: ./build.sh clean [current|stale]

  current  remove generated bootloader files and clean active 7.1 kernels
  stale    remove known superseded diagnostics, release staging, and old
           kernel build trees under build/
EOF
}

remove_build_dir()
{
	local name="$1" path resolved size
	path="$build/$name"
	test -e "$path" || return 0
	resolved="$(realpath -e -- "$path")"
	case "$resolved" in
		"$build"/*) ;;
		*) echo "Refusing path outside $build: $resolved" >&2; exit 1 ;;
	esac
	size="$(du -sh -- "$resolved" | cut -f1)"
	printf 'Removing obsolete build tree: %s (%s)\n' "$resolved" "$size"
	rm -rf -- "$resolved"
}

mode="${1:-current}"
case "$mode" in
	current)
		"$repo/external/boots/build.sh" clean pc98
		for name in kernel-7.1 kernel-7.1-i486; do
			test -d "$build/$name" || continue
			make -C "$repo/external/kernel/linux-7.1" O="$build/$name" ARCH=i386 clean
		done
		;;
	stale)
		# These are historical, diagnostic, or already-published staging trees.
		# Active toolchains, root filesystems, current images, and the current
		# 7.1 kernel trees are deliberately not included.
		for name in \
			diag-i686-reset-defaultcpu \
			diag-i686-reset-1 \
			diag-i686-reset-486cpu \
			diag-i686-debian-defaultcpu \
			diag-i686-debian-defaultcpu-2 \
			config-check-i386 config-check-i486 config-check-debian \
			kernel-7.1-localmod-config kernel-7.1-lean-config \
			kernel-7.1-full-config-check kernel-7.1-i686-lgy98-config \
			kernel-7.1-i486-lgy98-test kernel-7.0 kernel \
			kernel-scsi-test kernel-scsi-i486-test \
			musl-patch-dryrun musl-tas-input musl-vendor-import \
			patchset-check-musl buildroot-patch-dryrun \
			diagnose-old-rom loader-one-sector-test glibc-script-validation \
			codex-chs-i386 codex-chs-i486 codex-chs-release \
			release-h8 release-h8-base v0.4.0
		do
			remove_build_dir "$name"
		done
		;;
	-h | --help | help)
		usage
		;;
	*)
		echo "Unknown clean profile: $mode" >&2
		usage >&2
		exit 2
		;;
esac
