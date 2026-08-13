# Environment for driving the external/zedBSD submodule from linux-pc98.
# Sourced by build.sh and the image scripts; the caller must set $repo.
#
# zedBSD resolves QEMU, the PC-98 BIOS, release base images, and the GCC and
# musl source trees (for the soft-float build) from this repository instead
# of its own vendor/ submodules.
zedbsd="$repo/external/zedBSD"
export QEMU="${QEMU:-$repo/external/qemu-pc98/build/qemu-system-i386}"
export PC98_BIOS_DIR="${PC98_BIOS_DIR:-$repo/external/qemu-pc98/roms/pc98bios}"
export ZEDBSD_RELEASES_DIR="${ZEDBSD_RELEASES_DIR:-$repo/build/releases}"
export ZEDBSD_GCC_ROOT="${ZEDBSD_GCC_ROOT:-$repo/external/gcc}"
export ZEDBSD_MUSL_ROOT="${ZEDBSD_MUSL_ROOT:-$repo/external/musl}"
