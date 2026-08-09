# Environment for driving the external/boots submodule from linux-pc98.
# Sourced by build.sh and the image scripts; the caller must set $repo.
#
# Boots resolves QEMU, the PC-98 BIOS, release base images, and the GCC and
# musl source trees (for the soft-float build) from this repository instead
# of its own vendor/ submodules.
boots="$repo/external/boots"
export QEMU="${QEMU:-$repo/qemu-pc98/build/qemu-system-i386}"
export PC98_BIOS_DIR="${PC98_BIOS_DIR:-$repo/qemu-pc98/roms/pc98bios}"
export BOOTS_RELEASES_DIR="${BOOTS_RELEASES_DIR:-$repo/build/releases}"
export BOOTS_GCC_ROOT="${BOOTS_GCC_ROOT:-$repo/toolchain/gcc}"
export BOOTS_MUSL_ROOT="${BOOTS_MUSL_ROOT:-$repo/toolchain/musl}"
