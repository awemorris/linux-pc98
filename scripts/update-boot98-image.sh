#!/usr/bin/env bash
set -euo pipefail

# Updating BOOT98 is an atomic boot-environment operation.  The disk stubs,
# partition PBR, FAT-hosted IO.SYS/BOOT.SYS, and their handoff ABI must
# always come from the same source revision.  The BOOT filesystem is rebuilt;
# root and swap partitions are not modified.
repo="$(cd "$(dirname "$0")/.." && pwd)"
image="${1:?usage: $0 IMAGE VMLINUX [BOOT.CFG]}"
kernel="${2:?usage: $0 IMAGE VMLINUX [BOOT.CFG]}"
cfg="${3:-}"

exec "$repo/scripts/install-boot98-image.sh" "$image" "$kernel" "$cfg"
