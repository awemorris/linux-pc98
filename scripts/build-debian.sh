#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
root_stage="${ROOT_STAGE:-$repo/build/debian-i386-root}"

if [ ! -d "$root_stage" ]; then
	"$repo/scripts/build-debian-rootfs.sh"
fi
"$repo/scripts/build-kernel.sh"
BOOTLOADER=zedbsd "$repo/scripts/build-images.sh"
