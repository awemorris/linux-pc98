#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/../.." && pwd)"
profile="${1:-test-build}"
kernel="${2:-$repo/build/i386-video/kernel/vmlinux.boot}"
cmdline="console=tty0 earlyprintk=pc9800 root=PARTLABEL=LINUXROOT rootfstype=ext4 rw"

"$repo/bootsimple/build.sh" --profile "$profile" --cmdline "$cmdline"
"$repo/bootsimple/verify-image.py" elf "$kernel"
build="$repo/build/bootsimple/$profile"
test "$(stat -c %s "$build/ipl-lba0.bin")" -eq 512
test "$(stat -c %s "$build/ipl-lba2.bin")" -eq 7168
test "$(stat -c %s "$build/partition-pbr.bin")" -eq 1024
test "$(stat -c %s "$build/IO.SYS")" -le 65024
echo "bootsimple build test: PASS"

