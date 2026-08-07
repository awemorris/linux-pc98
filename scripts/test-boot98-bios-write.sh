#!/usr/bin/env bash
set -euo pipefail

# Destructive BIOS write/read/restore test.  Only private copies below build/
# are modified; release images and source images are never opened writable.
repo="$(cd "$(dirname "$0")/.." && pwd)"
qemu="${QEMU:-$repo/qemu-pc98/build/qemu-system-i386}"
bios_dir="${PC98_BIOS_DIR:-$repo/qemu-pc98/roms/pc98bios}"
selection="${1:-all}"
work="$repo/build/tests/boot98-m9-bios-write"

test -x "$qemu" || { echo "QEMU not found: $qemu" >&2; exit 1; }
test -d "$bios_dir" || { echo "PC-98 BIOS directory not found: $bios_dir" >&2; exit 1; }
mkdir -p "$work"
make -C "$repo/bootloader" -j"$(nproc)" BOOT-M9.SYS

run_one()
{
	local profile="$1" base="$2" heads="$3" sectors="$4" interface="$5"
	local image="$work/$profile.raw" cfg="$work/$profile.cfg"
	local log="$work/$profile.debug" before="$work/$profile.before"
	local after="$work/$profile.after" old_bytes old_sectors track new_sectors

	test -f "$base" || { echo "M9 source image not found: $base" >&2; exit 1; }
	old_bytes="$(stat -c %s "$base")"
	test $((old_bytes % 512)) -eq 0 || { echo "Unaligned image: $base" >&2; exit 1; }
	old_sectors=$((old_bytes / 512))
	track=$((heads * sectors))
	new_sectors=$((((old_sectors + track - 1) / track + 1) * track))
	cp --reflink=auto "$base" "$image"
	truncate -s $((new_sectors * 512)) "$image"
	printf 'm9-write-test %s\nhalt\n' "$old_sectors" > "$cfg"
	BOOT98_STAGE2_IMAGE="$repo/bootloader/BOOT-M9.SYS" \
		DISK_HEADS="$heads" DISK_SECTORS="$sectors" \
		"$repo/scripts/install-boot98-image.sh" "$image" "" "$cfg"
	dd if="$image" of="$before" bs=512 skip="$old_sectors" count=1 status=none
	: > "$log"

	local drive="if=$interface,bus=0,unit=0,format=raw,file=$image"
	set +e
	timeout --signal=INT --kill-after=5 20 \
		"$qemu" -M pc9801 -cpu 386 -m 6 -accel tcg -L "$bios_dir" \
		-nic none -drive "$drive" -display none -serial none -monitor none \
		-chardev "file,id=m9debug,path=$log" \
		-device isa-debugcon,iobase=0xe9,chardev=m9debug \
		-no-reboot >/dev/null 2>&1
	local status=$?
	set -e
	if test "$status" -ne 0 && test "$status" -ne 124; then
		echo "$profile: QEMU failed with status $status" >&2
		return 1
	fi
	dd if="$image" of="$after" bs=512 skip="$old_sectors" count=1 status=none
	cmp "$before" "$after"
	grep -q '^M9 BIOS write/read/restore: PASS$' "$log"
	printf '%s: PASS (BIOS H=%s/S=%s, LBA %s restored)\n' \
		"$profile" "$heads" "$sectors" "$old_sectors"
}

case "$selection" in
ide)
	run_one ide "$repo/build/releases/linux-pc98-i386sx-busybox-ide.img" \
		8 17 ide
	;;
scsi92)
	run_one scsi92 "$repo/build/releases/linux-pc98-i386sx-busybox-scsi92.img" \
		8 32 scsi
	;;
all)
	"$0" ide
	"$0" scsi92
	;;
*)
	echo "usage: $0 [all|ide|scsi92]" >&2
	exit 2
	;;
esac
