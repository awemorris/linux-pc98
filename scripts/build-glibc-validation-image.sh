#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
family="${1:-}"
root="${GLIBC_BUILD_ROOT:-$repo/build/glibc-2.41}"
stage="$root/stage-$family"
tests="$root/tests-$family"
busybox="$root/busybox-glibc-$family/busybox"
kernel_build="$root/kernel-$family"
root_stage="$root/root-$family-validation"
image="${OUTPUT_IMAGE:-$root/glibc-$family-validation.raw}"

case "$family" in
i386)
	cpu_family=386
	smoke="$tests/glibc-i386-smoke"
	pass_marker=GLIBC_I386_ALL_PASS
	;;
i486)
	cpu_family=486
	smoke="$tests/glibc-i486-smoke"
	pass_marker=GLIBC_I486_ALL_PASS
	;;
*) echo "usage: $0 i386|i486" >&2; exit 2 ;;
esac

if [ -n "${BUSYBOX_ROOT_TEMPLATE:-}" ]; then
	root_template="$BUSYBOX_ROOT_TEMPLATE"
elif [ -d "$repo/build/release-v0.3.0/i386-buildroot/output/target" ]; then
	root_template="$repo/build/release-v0.3.0/i386-buildroot/output/target"
else
	root_template="$repo/build/i386-buildroot/output/target"
fi

test -x "$stage/lib/ld-linux.so.2"
test -x "$busybox"
test -x "$smoke"
test -x "$root_template/bin/busybox"
if [ "$family" = i386 ]; then
	test -x "$tests/i386-atomic-selftest"
fi
if [ -e "$image" ]; then
	echo "refusing to overwrite validation image: $image" >&2
	exit 1
fi

CPU_FAMILY="$cpu_family" I386_CONSOLE=dual \
I386_KERNEL_BUILD="$kernel_build" \
I386_CONFIG_OUTPUT="$root/kernel-$family.config" \
	"$repo/scripts/configure-i386-busybox.sh"
make -C "$repo/linux-7.1" O="$kernel_build" ARCH=i386 \
	-j"${JOBS:-32}" vmlinux
objcopy --strip-all "$kernel_build/vmlinux" "$kernel_build/vmlinux.boot"

case "$root_stage" in
"$root"/*) ;;
*) echo "refusing unsafe validation root path: $root_stage" >&2; exit 1 ;;
esac
rm -rf -- "$root_stage"
mkdir -p "$root_stage"
cp -a "$root_template/." "$root_stage/"
install -m 0755 "$busybox" "$root_stage/bin/busybox"
rm -f -- "$root_stage"/lib/ld-musl*
cp -a "$stage/lib/." "$root_stage/lib/"
install -m 0755 "$smoke" "$root_stage/usr/bin/glibc-smoke"
if [ "$family" = i386 ]; then
	install -m 0755 "$tests/i386-atomic-selftest" \
		"$root_stage/usr/bin/i386-atomic-selftest"
fi

cat >"$root_stage/etc/fstab" <<'EOF'
/dev/hd98a2 / ext4 defaults,noatime 0 1
/dev/hd98a3 none swap sw 0 0
proc /proc proc defaults 0 0
sysfs /sys sysfs defaults 0 0
EOF

cat >"$root_stage/etc/inittab" <<'EOF'
::sysinit:/bin/mount -t proc proc /proc
::sysinit:/bin/mount -t sysfs sysfs /sys
::sysinit:/sbin/swapon -a
::sysinit:/bin/sh /etc/glibc-test.sh
tty1::askfirst:-/bin/sh
ttyS0::respawn:-/bin/sh
::ctrlaltdel:/bin/reboot
::shutdown:/bin/swapoff -a
::shutdown:/bin/umount -a -r
EOF

{
	echo '#!/bin/sh'
	cat <<'EOF'
if test "${GLIBC_TEST_CAPTURE:-0}" != 1; then
  GLIBC_TEST_CAPTURE=1 /bin/sh "$0" >/tmp/glibc-test.log 2>&1
  rc=$?
  cat /tmp/glibc-test.log
  cat /tmp/glibc-test.log >/dev/ttyS0
  exit "$rc"
fi
EOF
	echo 'failed=0'
	printf 'echo GLIBC_%s_TEST_BEGIN\n' "$(echo "$family" | tr '[:lower:]' '[:upper:]')"
	cat <<'EOF'
/usr/bin/glibc-smoke
rc=$?
echo GLIBC_SMOKE_RC=$rc
test "$rc" -eq 0 || failed=1
EOF
	if [ "$family" = i386 ]; then
		cat <<'EOF'
/usr/bin/i386-atomic-selftest
rc=$?
echo I386_ATOMIC_SELFTEST_RC=$rc
test "$rc" -eq 0 || failed=1
EOF
	fi
	cat <<EOF
if test "\$failed" -eq 0; then
  echo $pass_marker
else
  echo GLIBC_${family^^}_TEST_FAILED
fi
EOF
} >"$root_stage/etc/glibc-test.sh"
chmod 0755 "$root_stage/etc/glibc-test.sh"

KERNEL_VERSION=7.1 \
KERNEL_BUILD="$kernel_build" \
KERNEL_IMAGE="$kernel_build/vmlinux.boot" \
ROOT_STAGE="$root_stage" \
BOOT_MB="${BOOT_MB:-64}" \
ROOT_MB="${ROOT_MB:-64}" \
SWAP_MB="${SWAP_MB:-32}" \
SMALL_EXT4=1 \
OUTPUT_IMAGE="$image" \
	"$repo/scripts/build-images.sh"

printf 'glibc %s validation image: %s\n' "$family" "$image"
