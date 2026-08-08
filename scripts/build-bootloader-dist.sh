#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
release_dir="$repo/build/releases"
output="${1:-$release_dir/bootloader.zip}"
case "$output" in
	/*) ;;
	*) output="$PWD/$output" ;;
esac
stage=""

cleanup()
{
	test -z "$stage" || {
		find "$stage" -depth -delete
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
"$repo/scripts/build-remacs-bytecode.sh"

stage="$(mktemp -d "$release_dir/.bootloader-zip.XXXXXX")"
mkdir -p "$stage/CMD" "$stage/HOME"
install -m 0644 "$repo/bootloader/dos/linux98.exe" "$stage/LINUX98.EXE"
install -m 0644 "$repo/bootloader/dos/inst.exe" "$stage/INST.EXE"
install -m 0644 "$repo/bootloader/ipl-lba0.img" "$stage/IPL-LBA0.IMG"
install -m 0644 "$repo/bootloader/ipl-lba2.img" "$stage/IPL-LBA2.IMG"
install -m 0644 "$repo/bootloader/ipl-part.img" "$stage/IPL-PART.IMG"
install -m 0644 "$repo/bootloader/IO.SYS" "$stage/IO.SYS"
install -m 0644 "$repo/bootloader/BOOT.SYS" "$stage/BOOT.SYS"
install -m 0644 "$repo/bootloader/AUTOEXEC.NCT" "$stage/AUTOEXEC.NCT"
install -m 0644 "$repo/bootloader/HELLO.NCT" "$stage/HELLO.NCT"
install -m 0644 "$repo/bootloader/LS.NCT" "$stage/CMD/LS.NCT"
install -m 0644 "$repo/bootloader/CP.NCT" "$stage/CMD/CP.NCT"
install -m 0644 "$repo/build/bootloader/remacs/REMACS.NB" \
	"$stage/CMD/REMACS.NB"
install -m 0644 "$repo/build/bootloader/remacs/SKKJISYO.DIC" \
	"$stage/HOME/SKKJISYO.DIC"
install -m 0644 "$repo/bootloader/EMACS.RC" "$stage/HOME/.emacs"
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
		AUTOEXEC.NCT BOOT.CFG BOOT.SYS DOS-README.md GCC-SOFT-FP-LICENSE.txt \
		CMD/CP.NCT CMD/LS.NCT CMD/REMACS.NB HOME/.emacs \
		HOME/SKKJISYO.DIC HELLO.NCT \
		INST.EXE IO.SYS IPL-LBA0.IMG IPL-LBA2.IMG IPL-PART.IMG \
		LINUX98.EXE MUSL-COPYRIGHT.txt NOCT-LICENSE.txt README.md
)
unzip -tq "$output.part.$$"
unzip -p "$output.part.$$" INST.EXE | cmp -s - "$repo/bootloader/dos/inst.exe"
mv -f -- "$output.part.$$" "$output"
printf 'BOOT98 distribution: %s\n' "$output"
unzip -Z1 "$output"
