#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
. "$repo/scripts/boots-env.sh"
boots_build="$boots/build/pc98"
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
"$boots/build.sh" all pc98
"$boots/scripts/build-remacs-bytecode.sh"

stage="$(mktemp -d "$release_dir/.bootloader-zip.XXXXXX")"
mkdir -p "$stage/CMD" "$stage/HOME"
install -m 0644 "$boots/platform/pc98/dos/linux98.exe" "$stage/LINUX98.EXE"
install -m 0644 "$boots/platform/pc98/dos/inst.exe" "$stage/INST.EXE"
install -m 0644 "$boots_build/ipl-lba0.img" "$stage/IPL-LBA0.IMG"
install -m 0644 "$boots_build/ipl-lba2.img" "$stage/IPL-LBA2.IMG"
install -m 0644 "$boots_build/ipl-part.img" "$stage/IPL-PART.IMG"
install -m 0644 "$boots_build/IO.SYS" "$stage/IO.SYS"
install -m 0644 "$boots_build/BOOT.SYS" "$stage/BOOT.SYS"
install -m 0644 "$boots/apps/AUTOEXEC.NCT" "$stage/AUTOEXEC.NCT"
install -m 0644 "$boots/apps/HELLO.NCT" "$stage/HELLO.NCT"
install -m 0644 "$boots/apps/LS.NCT" "$stage/CMD/LS.NCT"
install -m 0644 "$boots/apps/CP.NCT" "$stage/CMD/CP.NCT"
install -m 0644 "$boots/build/remacs/REMACS.NAP" \
	"$stage/CMD/REMACS.NAP"
install -m 0644 "$boots/build/remacs/SKKJISYO.DIC" \
	"$stage/HOME/SKKJISYO.DIC"
install -m 0644 "$boots/apps/EMACS.RC" "$stage/HOME/.remacs.el"
install -m 0644 "$boots/apps/EMACS.RC" "$stage/HOME/.emacs"
# Stage 2 reads BOOTS.CFG and falls back to BOOT.CFG for one release.
install -m 0644 "$repo/configs/boots.cfg" "$stage/BOOTS.CFG"
install -m 0644 "$repo/configs/boots.cfg" "$stage/BOOT.CFG"
install -m 0644 "$repo/README.md" "$stage/README.md"
install -m 0644 "$boots/platform/pc98/dos/README.md" "$stage/DOS-README.md"
install -m 0644 "$repo/external/gcc/COPYING.LIB" \
	"$stage/GCC-SOFT-FP-LICENSE.txt"
install -m 0644 "$repo/external/musl/COPYRIGHT" \
	"$stage/MUSL-COPYRIGHT.txt"
install -m 0644 "$boots/noct/LICENSE" \
	"$stage/NOCT-LICENSE.txt"

(
	cd "$stage"
	zip -X -9 -q "$output.part.$$" \
		AUTOEXEC.NCT BOOTS.CFG BOOT.CFG BOOT.SYS DOS-README.md \
		GCC-SOFT-FP-LICENSE.txt \
		CMD/CP.NCT CMD/LS.NCT CMD/REMACS.NAP HOME/.emacs HOME/.remacs.el \
		HOME/SKKJISYO.DIC HELLO.NCT \
		INST.EXE IO.SYS IPL-LBA0.IMG IPL-LBA2.IMG IPL-PART.IMG \
		LINUX98.EXE MUSL-COPYRIGHT.txt NOCT-LICENSE.txt README.md
)
unzip -tq "$output.part.$$"
unzip -p "$output.part.$$" INST.EXE | cmp -s - "$boots/platform/pc98/dos/inst.exe"
mv -f -- "$output.part.$$" "$output"
printf 'BOOT98 distribution: %s\n' "$output"
unzip -Z1 "$output"
