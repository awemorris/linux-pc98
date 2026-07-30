#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")" && pwd)"
root_stage="${ROOT_STAGE:-$repo/build/debian-i386-root}"

if [ ! -d "$root_stage" ]; then
	"$repo/build-debian-rootfs.sh"
fi
"$repo/build-kernel.sh"
"$repo/build-images.sh"
