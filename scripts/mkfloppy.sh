#!/usr/bin/env bash
set -euo pipefail

out="${1:?usage: mkfloppy.sh <out.hdm> <ipl.bin> [payload.bin]}"
ipl="${2:?need ipl.bin}"
payload="${3:-}"

size=$((77 * 2 * 8 * 1024))
mkdir -p "$(dirname "$out")"
truncate -s "$size" "$out"
dd if="$ipl" of="$out" conv=notrunc status=none
if [ -n "$payload" ]; then
	dd if="$payload" of="$out" bs=1024 seek=1 conv=notrunc status=none
fi
echo "wrote $out: IPL $(stat -c%s "$ipl")B${payload:+, payload $(stat -c%s "$payload")B @sector2}"
