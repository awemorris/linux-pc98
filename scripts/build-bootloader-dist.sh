#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
. "$repo/scripts/zedbsd-env.sh"
zedbsd_build="$zedbsd/build/pc98"
release_dir="$repo/build/releases"
output="${1:-$release_dir/bootloader.zip}"
case "$output" in
	/*) ;;
	*) output="$PWD/$output" ;;
esac
stage=""

cleanup()
{
	test -z "$stage" || find "$stage" -depth -delete
	rm -f -- "$output.part.$$"
}
trap cleanup EXIT INT TERM

for command in zip unzip; do
	command -v "$command" >/dev/null 2>&1 || {
		echo "$command is required; run ./build.sh setup" >&2
		exit 1
	}
done

mkdir -p "$release_dir" "$(dirname "$output")"
"$zedbsd/build.sh" all pc98
"$repo/bootloader/build-remacs.sh"

stage="$(mktemp -d "$release_dir/.bootloader-zip.XXXXXX")"
mkdir -p "$stage/BIN" "$stage/APPS" "$stage/ETC" "$stage/HOME" "$stage/INST"

install -m 0644 "$zedbsd_build/IO.SYS" "$stage/IO.SYS"
install -m 0644 "$zedbsd_build/vmunix" "$stage/vmunix"
install -m 0644 "$zedbsd_build/bin/sh" "$stage/BIN/SH"
install -m 0644 "$zedbsd_build/bin/noct" "$stage/BIN/NOCT"
install -m 0644 "$zedbsd_build/bin/linux" "$stage/BIN/LINUX"
install -m 0644 "$repo/configs/boot.cfg" "$stage/BOOT.CFG"

install -m 0644 "$repo/bootloader/fs/etc/zinit.rc" "$stage/ETC/ZINIT.RC"
install -m 0644 "$repo/bootloader/fs/bin/menu.nct" "$stage/BIN/MENU.NCT"
install -m 0644 "$repo/bootloader/fs/bin/menuback.bmp" "$stage/BIN/MENUBACK.BMP"
install -m 0644 "$repo/bootloader/fs/apps/holoris.nct" "$stage/APPS/HOLORIS.NCT"
install -m 0644 "$repo/bootloader/fs/apps/emacs.nap" "$stage/APPS/EMACS.NAP"
install -m 0644 "$repo/bootloader/fs/home/skkjisyo.dic" "$stage/HOME/SKKJISYO.DIC"

for app in hello.nct ls.nct cp.nct bmpview.nct; do
	test -s "$zedbsd/apps/$app" || continue
	install -m 0644 "$zedbsd/apps/$app" "$stage/APPS/${app^^}"
done

install -m 0644 "$zedbsd/platform/pc98/dos/linux98.exe" "$stage/INST/LINUX98.EXE"
install -m 0644 "$zedbsd/platform/pc98/dos/inst.exe" "$stage/INST/INST.EXE"
install -m 0644 "$zedbsd_build/IO.SYS" "$stage/INST/IO.SYS"
install -m 0644 "$zedbsd_build/ipl-lba0.img" "$stage/INST/IPL-LBA0.IMG"
install -m 0644 "$zedbsd_build/ipl-lba2.img" "$stage/INST/IPL-LBA2.IMG"
install -m 0644 "$zedbsd_build/ipl-part.img" "$stage/INST/IPL-PART.IMG"

install -m 0644 "$repo/README.md" "$stage/README.md"
install -m 0644 "$zedbsd/platform/pc98/dos/README.md" "$stage/INST/README.md"
install -m 0644 "$repo/external/gcc/COPYING.LIB" "$stage/GCC-SOFT-FP-LICENSE.txt"
install -m 0644 "$repo/external/musl/COPYRIGHT" "$stage/MUSL-COPYRIGHT.txt"
install -m 0644 "$repo/external/noct/LICENSE" "$stage/NOCT-LICENSE.txt"

(
	cd "$stage"
	zip -X -9 -q -r "$output.part.$$" .
)
unzip -tq "$output.part.$$"
unzip -p "$output.part.$$" INST/INST.EXE | \
	cmp -s - "$zedbsd/platform/pc98/dos/inst.exe"
mv -f -- "$output.part.$$" "$output"
printf 'linux-pc98 bootloader distribution: %s\n' "$output"
unzip -Z1 "$output"
