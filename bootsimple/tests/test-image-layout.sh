#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/../.." && pwd)"
image="${1:?usage: $0 IMAGE HEADS SECTORS [VMLINUX]}"
heads="${2:?usage: $0 IMAGE HEADS SECTORS [VMLINUX]}"
sectors="${3:?usage: $0 IMAGE HEADS SECTORS [VMLINUX]}"
kernel="${4:-}"
args=(all "$image" --heads "$heads" --sectors "$sectors" --partition 1)
test -z "$kernel" || args+=(--kernel "$kernel")
"$repo/bootsimple/verify-image.py" "${args[@]}"
echo "bootsimple image layout test: PASS"

