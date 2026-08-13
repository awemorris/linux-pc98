#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
release_dir="$repo/build/releases"
output="${1:-$release_dir/bootsimple.zip}"
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

for command in cmp install python3 unzip zip; do
	command -v "$command" >/dev/null 2>&1 || {
		echo "$command is required; run ./build.sh setup" >&2
		exit 1
	}
done

common_cmdline='vdso=0 console=tty0 earlyprintk=pc9800 root=PARTLABEL=LINUXROOT rootfstype=ext4 rw sysctl.vm.min_free_kbytes=64 sysctl.vm.dirty_background_bytes=32768 sysctl.vm.dirty_bytes=65536 sysctl.vm.vfs_cache_pressure=200 sysctl.vm.swappiness=100 sysctl.vm.page-cluster=0'
profiles=(busybox-i386-ide busybox-i386-scsi55 busybox-i386-scsi92)
cmdlines=(
	"$common_cmdline"
	"$common_cmdline rootwait pc9801_scsi=55,irq=5,dma=0,clock=12,mode=async-pio"
	"$common_cmdline rootwait pc9801_scsi=92,mode=dma"
)

mkdir -p "$release_dir" "$(dirname "$output")"
stage="$(mktemp -d "$release_dir/.bootsimple-zip.XXXXXX")"
mkdir -p "$stage/bootsimple/include" "$stage/bootsimple/pc98" \
	"$stage/bootsimple/tests" "$stage/profiles"

install -m 0644 "$repo/bootsimple/README.md" "$stage/README.md"
install -m 0644 "$repo/bootsimple/LICENSE" "$stage/LICENSE"
for file in Makefile README.md LICENSE; do
	install -m 0644 "$repo/bootsimple/$file" "$stage/bootsimple/$file"
done
for file in build.sh install-image.sh; do
	install -m 0755 "$repo/bootsimple/$file" "$stage/bootsimple/$file"
done
install -m 0755 "$repo/bootsimple/verify-image.py" \
	"$stage/bootsimple/verify-image.py"
for file in "$repo"/bootsimple/include/*; do
	install -m 0644 "$file" "$stage/bootsimple/include/$(basename "$file")"
done
for file in "$repo"/bootsimple/pc98/*; do
	install -m 0644 "$file" "$stage/bootsimple/pc98/$(basename "$file")"
done
for file in "$repo"/bootsimple/tests/*; do
	install -m 0755 "$file" "$stage/bootsimple/tests/$(basename "$file")"
done

for index in "${!profiles[@]}"; do
	profile="${profiles[$index]}"
	cmdline="${cmdlines[$index]}"
	build="$repo/build/bootsimple/$profile"
	destination="$stage/profiles/$profile"
	"$repo/bootsimple/build.sh" --profile "$profile" \
		--output-dir "$build" --cmdline "$cmdline"
	mkdir -p "$destination"
	for file in ipl-lba0.bin ipl-lba2.bin partition-pbr.bin IO.SYS; do
		install -m 0644 "$build/$file" "$destination/$file"
	done
	printf '%s\n' "$cmdline" >"$destination/CMDLINE.txt"
done

(
	cd "$stage"
	zip -X -9 -q -r "$output.part.$$" .
)
unzip -tq "$output.part.$$"
for index in "${!profiles[@]}"; do
	profile="${profiles[$index]}"
	test "$(unzip -p "$output.part.$$" \
		"profiles/$profile/ipl-lba0.bin" | wc -c)" -eq 512
	test "$(unzip -p "$output.part.$$" \
		"profiles/$profile/ipl-lba2.bin" | wc -c)" -eq 7168
	test "$(unzip -p "$output.part.$$" \
		"profiles/$profile/partition-pbr.bin" | wc -c)" -eq 1024
	test "$(unzip -p "$output.part.$$" \
		"profiles/$profile/IO.SYS" | wc -c)" -gt 0
	printf '%s\n' "${cmdlines[$index]}" | cmp -s - \
		<(unzip -p "$output.part.$$" "profiles/$profile/CMDLINE.txt")
done
mv -f -- "$output.part.$$" "$output"
printf 'linux-pc98 bootsimple distribution: %s\n' "$output"
unzip -Z1 "$output"
