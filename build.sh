#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")" && pwd)"

usage()
{
	cat <<'EOF'
Usage: ./build.sh COMMAND [options]

Primary commands:
  setup [options]             install Debian 13 host build/test dependencies
  bootloader                  build IPL and BOOT98 binaries
  bootloader-dist             build build/releases/bootloader.zip
  noct COMMAND                build or verify the imported Noct core
  boot-install [options]      destructively create a BOOT partition environment
  dos-loader                  rebuild LINUX98.EXE and INST.EXE (OpenWatcom)
  kernel [options]            configure and build Linux 7.1
  rootfs PROFILE              build a root filesystem
  rootfs-cache COMMAND        fetch/store/publish reusable rootfs archives
  image PROFILE [options]     create or update a named disk-image variant
  image list                  list image profiles
  release-image PROFILE       build a canonical image under build/releases
  release-image all           rebuild every canonical release/test image
  release [options]           build the complete public Release artifact set
  test PROFILE [options]      prepare/run a headless serial-console test
  test list                   list serial test profiles
  cache fetch NAME            cache a package-server base image
  cache publish NAME FILE     publish a base image and checksum
  cache materialize NAME OUT  expand a cached base image
  cache list                  list locally cached bases

Compatibility/developer commands:
  debian                      build the default Debian rootfs/kernel/image
  dist                        compress the default legacy image
  glibc FAMILY                build glibc for i386 or i486
  glibc-tests FAMILY          build glibc tests
  glibc-image FAMILY          build a glibc validation image
  qemu-win64 COMMAND          build Windows QEMU and dependencies
  virtpc98-win64 [COMMAND]    build virtpc98.exe with PyInstaller
  win64-dist [build]          build the complete Windows ZIP distribution
  run [IMAGE]                 start qemu-pc98 with an image
  clean [current|stale]       clean active outputs or superseded build trees

Kernel options:
  --cpu 386|486|686           CPU baseline (default: 686)
  --profile pc98|full         device profile (default: pc98)
  --output-dir DIR            out-of-tree kernel build directory
  --jobs N                    parallel jobs
  --console video|dual        built-in console selection (default: video)

Run './build.sh image --help' for image-specific options.
Run './build.sh test --help' for reproducible QEMU test options.
BOOT installation syntax:
  ./build.sh boot-install [--partition N] [--install-disk-stubs]
                          IMAGE [VMLINUX [BOOT.CFG]]
EOF
}

build_noct()
{
	local action="${1:-objects}"
	shift || true
	case "$action" in
		objects)
			make -C "$repo/bootloader" noct-objects "$@"
			;;
		opcode-check)
			make -C "$repo/bootloader" noct-opcode-check "$@"
			;;
		libc-test)
			make -C "$repo/bootloader" boot98-libc-host-test "$@"
			;;
		link-audit)
			make -C "$repo/bootloader" noct-link-audit "$@"
			;;
		verify)
			"$repo/scripts/update-noct.sh" verify
			make -C "$repo/bootloader" noct-m15-verify "$@"
			;;
		softfloat-test)
			make -C "$repo/bootloader" boot98-softfloat-host-test "$@"
			;;
		lifecycle-test)
			make -C "$repo/bootloader" boot98-noct-host-test "$@"
			;;
		status)
			"$repo/scripts/update-noct.sh" status
			;;
		init)
			"$repo/scripts/update-noct.sh" init
			;;
		update)
			"$repo/scripts/update-noct.sh" update "$@"
			;;
		clean)
			make -C "$repo/bootloader" noct-clean boot98-libc-clean "$@"
			;;
		-h | --help | help)
			cat <<'EOF'
Usage: ./build.sh noct COMMAND

Commands:
  objects       compile the selected PC98BE Noct core objects (JIT enabled)
  opcode-check  compile and reject post-i386 instructions
  libc-test     run heap/libc host tests, including allocation failures
  link-audit    relocatably link Noct/libc and audit undefined symbols
  lifecycle-test run the M15 lifecycle, File, utility, native-API, and REPL host test
  softfloat-test run the M5 arithmetic/conversion/math known vectors
  verify        verify M4-M15, static i386 opcodes, and QEMU REPL paths
  init          initialize the pinned Noct submodule
  update [REF]  fetch and stage a newer Noct gitlink (default: origin/main)
  status        print the submodule origin and pinned/current revisions
  clean         remove only the selected Noct object files
EOF
			;;
		*)
			echo "Unknown Noct command: $action" >&2
			exit 2
			;;
	esac
}

build_kernel()
{
	local cpu=686 profile=pc98 output_dir="" jobs="${JOBS:-$(nproc)}"
	local console=video root_stage="${ROOT_STAGE:-}"
	while test "$#" -gt 0; do
		case "$1" in
			--cpu | --profile | --output-dir | --jobs | --console)
				test "$#" -ge 2 || { echo "Missing value for $1" >&2; exit 2; }
				case "$1" in
					--cpu) cpu="$2" ;;
					--profile) profile="$2" ;;
					--output-dir) output_dir="$2" ;;
					--jobs) jobs="$2" ;;
					--console) console="$2" ;;
				esac
				shift 2
				;;
			-h | --help)
				usage
				return
				;;
			*) echo "Unknown kernel option: $1" >&2; exit 2 ;;
		esac
	done
	case "$cpu" in
		386 | 486 | 686) ;;
		*) echo "Unsupported CPU: $cpu" >&2; exit 2 ;;
	esac
	case "$console" in
		video | dual) ;;
		*) echo "Unsupported console: $console" >&2; exit 2 ;;
	esac
	if test -z "$output_dir"; then
		case "$cpu" in
			386) output_dir="$repo/build/kernel-7.1-i386" ;;
			486) output_dir="$repo/build/kernel-7.1-i486" ;;
			686) output_dir="$repo/build/kernel-7.1" ;;
		esac
	fi
	if test -z "$root_stage"; then
		case "$cpu" in
			386) root_stage="$repo/build/i386-video/buildroot/output/target" ;;
			486) root_stage="$repo/build/boot98/debian13-i486-root" ;;
			686) root_stage="$repo/build/debian-i386-root" ;;
		esac
	fi
	CPU_FAMILY="$cpu" DEVICE_PROFILE="$profile" JOBS="$jobs" \
		CONSOLE_MODE="$console" \
		ROOT_STAGE="$root_stage" KERNEL_BUILD="$output_dir" \
		"$repo/scripts/build-kernel.sh"
}

build_rootfs()
{
	local profile="${1:-}"
	shift || true
	case "$profile" in
		debian13-i486)
			ROOT_STAGE="${ROOT_STAGE:-$repo/build/boot98/debian13-i486-root}" \
				"$repo/scripts/build-debian-i486-rootfs.sh" "$@"
			;;
		debian13-i686)
			"$repo/scripts/build-debian-rootfs.sh" "$@"
			;;
		busybox-i386 | busybox-i486)
			CPU_FAMILY="${profile#busybox-i}" \
				"$repo/scripts/build-i386-rootfs.sh" "$@"
			;;
		*)
			echo "Unknown rootfs profile: $profile" >&2
			echo "Profiles: debian13-i486 debian13-i686 busybox-i386 busybox-i486" >&2
			exit 2
			;;
	esac
}

command="${1:-help}"
shift || true
case "$command" in
	help | -h | --help) usage ;;
	setup) "$repo/scripts/setup.sh" "$@" ;;
	bootloader) make -C "$repo/bootloader" "$@" ;;
	bootloader-dist) "$repo/scripts/build-bootloader-dist.sh" "$@" ;;
	noct) build_noct "$@" ;;
	boot-install) "$repo/scripts/install-boot98-image.sh" "$@" ;;
	dos-loader) make -C "$repo/bootloader/dos" "$@" ;;
	kernel) build_kernel "$@" ;;
	rootfs) build_rootfs "$@" ;;
	rootfs-cache) "$repo/scripts/rootfs-cache.sh" "$@" ;;
	image) "$repo/scripts/build-image.sh" "$@" ;;
	release-image) "$repo/scripts/build-release-image.sh" "$@" ;;
	release) "$repo/scripts/build-release.sh" "$@" ;;
	test) "$repo/scripts/test-image.sh" "$@" ;;
	cache) "$repo/scripts/image-cache.sh" "$@" ;;
	debian) "$repo/scripts/build-debian.sh" "$@" ;;
	dist) "$repo/scripts/build-dist.sh" "$@" ;;
	glibc) "$repo/scripts/build-glibc.sh" "$@" ;;
	glibc-tests) "$repo/scripts/build-glibc-tests.sh" "$@" ;;
	glibc-image) "$repo/scripts/build-glibc-validation-image.sh" "$@" ;;
	qemu-win64) "$repo/scripts/build-qemu-win64.sh" "$@" ;;
	virtpc98-win64) "$repo/scripts/build-virtpc98.sh" "$@" ;;
	win64-dist) "$repo/scripts/build-win64-dist.sh" "$@" ;;
	run) "$repo/scripts/run-qemu.sh" "$@" ;;
	clean) "$repo/scripts/clean-build.sh" "$@" ;;
	*)
		echo "Unknown command: $command" >&2
		usage >&2
		exit 2
		;;
esac
