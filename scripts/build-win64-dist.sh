#!/usr/bin/env bash
set -euo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

case "${1:-build}" in
	bootstrap)
		"$repo/scripts/build-qemu-win64.sh" bootstrap
		"$repo/scripts/build-virtpc98.sh" bootstrap
		;;
	build)
		"$repo/scripts/build-virtpc98.sh" build
		"$repo/scripts/build-qemu-win64.sh" all
		;;
	-h | --help | help)
		cat <<EOF
Usage: $0 [bootstrap|build]

  bootstrap  Install host packages for both Windows builders
  build      Build QEMU, virtpc98.exe and build/releases/qemu-pc98-win64.zip
EOF
		;;
	*)
		echo "Unknown command: $1" >&2
		exit 2
		;;
esac
