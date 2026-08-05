#!/usr/bin/env bash
set -euo pipefail

# Updating BOOT98 is an atomic boot-environment operation.  Stage 1, the
# FAT-hosted Stage 2, and their handoff ABI must always come from the same
# source revision.  Reuse the installer so a cached/base image cannot retain
# old LBA 2 sectors while receiving a new BOOT98.BIN.
repo="$(cd "$(dirname "$0")/.." && pwd)"
image="${1:?usage: $0 IMAGE VMLINUX [BOOT98.CFG]}"
kernel="${2:?usage: $0 IMAGE VMLINUX [BOOT98.CFG]}"
cfg="${3:-}"

exec "$repo/scripts/install-boot98-image.sh" "$image" "$kernel" "$cfg"
