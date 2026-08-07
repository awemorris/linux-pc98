#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
release_dir="$repo/build/releases"
output="${1:-$release_dir/bootloader.zip}"
stage=""

cleanup()
{
	test -z "$stage" || {
		find "$stage" -maxdepth 1 -type f -delete
		rmdir "$stage"
	}
	rm -f -- "$output.part.$$"
}
trap cleanup EXIT

command -v zip >/dev/null 2>&1 || {
	echo "zip is required; run ./build.sh setup" >&2
	exit 1
}
command -v unzip >/dev/null 2>&1 || {
	echo "unzip is required; run ./build.sh setup" >&2
	exit 1
}

mkdir -p "$release_dir" "$(dirname "$output")"
make -C "$repo/bootloader" ipl-lba0.img ipl-lba2.img \
	ipl-part.img IO.SYS BOOT.SYS

stage="$(mktemp -d "$release_dir/.bootloader-zip.XXXXXX")"
install -m 0644 "$repo/bootloader/dos/linux98.exe" "$stage/LINUX98.EXE"
install -m 0644 "$repo/bootloader/dos/inst.exe" "$stage/INST.EXE"
install -m 0644 "$repo/bootloader/ipl-lba0.img" "$stage/IPL-LBA0.IMG"
install -m 0644 "$repo/bootloader/ipl-lba2.img" "$stage/IPL-LBA2.IMG"
install -m 0644 "$repo/bootloader/ipl-part.img" "$stage/IPL-PART.IMG"
install -m 0644 "$repo/bootloader/IO.SYS" "$stage/IO.SYS"
install -m 0644 "$repo/bootloader/BOOT.SYS" "$stage/BOOT.SYS"
install -m 0644 "$repo/releases/boot98.cfg" "$stage/BOOT.CFG"
install -m 0644 "$repo/bootloader/README.md" "$stage/README.md"
install -m 0644 "$repo/bootloader/dos/README.md" "$stage/DOS-README.md"
install -m 0644 "$repo/toolchain/gcc/COPYING.LIB" \
	"$stage/GCC-SOFT-FP-LICENSE.txt"
install -m 0644 "$repo/toolchain/musl/COPYRIGHT" \
	"$stage/MUSL-COPYRIGHT.txt"
install -m 0644 "$repo/third_party/noct/LICENSE" \
	"$stage/NOCT-LICENSE.txt"

(
	cd "$stage"
	zip -X -9 -q "$output.part.$$" \
		BOOT.CFG BOOT.SYS DOS-README.md GCC-SOFT-FP-LICENSE.txt \
		INST.EXE IO.SYS IPL-LBA0.IMG IPL-LBA2.IMG IPL-PART.IMG \
		LINUX98.EXE MUSL-COPYRIGHT.txt NOCT-LICENSE.txt README.md
)
unzip -tq "$output.part.$$"
unzip -p "$output.part.$$" INST.EXE | cmp -s - "$repo/bootloader/dos/inst.exe"
mv -f -- "$output.part.$$" "$output"
printf 'BOOT98 distribution: %s\n' "$output"
unzip -Z1 "$output"
