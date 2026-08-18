#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")" && pwd)"
zedbsd="$repo/external/zedBSD"

# Submodules are not populated by a plain clone.  Fail with the fix instead
# of letting the callee report a missing file.
require_submodule()
{
	local path="$repo/external/$1"
	test -e "$path/.git" && return 0
	echo "external/$1 is not checked out." >&2
	echo "Run: git submodule update --init --recursive external/$1" >&2
	exit 2
}

# The zedBSD bootloader lives in the external/zedBSD submodule and resolves
# QEMU, the PC-98 BIOS, release base images, and the soft-float source
# trees from this repository.
zedbsd_env()
{
	require_submodule zedBSD
	. "$repo/scripts/zedbsd-env.sh"
}

usage()
{
	cat <<'EOF'
Usage: ./build.sh COMMAND [options]

Primary commands:
  setup [options]             install Debian 13 host build/test dependencies
  bootloader [targets]        build the zedBSD binaries (external/zedBSD, pc98)
  bootsimple                  build build/releases/bootsimple.zip
  bootsimple PROFILE [opts]   build one BusyBox assembly IO.SYS loader
                              (--cmdline STRING is required)
  bootsimple-test NAME [args] run a bootsimple build/layout/failure/QEMU test
  bootloader-dist             build build/releases/bootloader.zip
  bootloader-fdd [OUTPUT]     build the zedBSD FDD image (zedbsd-fdd.img)
  bootloader-test NAME        run a zedBSD QEMU test (e.g. noct-repl, hdd-boot)
  remacs                      build bootloader/fs/apps/emacs.nap
  remacs-test                 run the bytecode editor under headless QEMU
  xoppai [options]            build the interactive X11 omega-curve demo
  noct build|clean            build Noct or remove the zedBSD pc98 build tree
  boot-install [options]      destructively create a BOOT partition environment
  dos-loader                  rebuild LINUX98.EXE and INST.EXE (OpenWatcom)
  kernel [options]            configure and build Linux 7.2
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
  glibc-tests FAMILY          cross-compile the glibc test binaries
  rootfs-inventory ROOTFS     inventory a Debian rootfs (--report/--packages-tsv
                              /--unowned-tsv required; see --help)
  qemu-win64 COMMAND          build Windows QEMU and dependencies
  virtpc98-win64 [COMMAND]    build virtpc98.exe with PyInstaller
  win64-dist [build]          build the complete Windows ZIP distribution
  run [IMAGE]                 start qemu-pc98 with an image
  clean [current|stale]       clean active outputs or superseded build trees

Rootfs profiles:
  debian13-i486 debian13-i686 busybox-i386 busybox-i486

Run './build.sh kernel --help' for kernel options.
Run './build.sh image --help' for image-specific options.
Run './build.sh test --help' for reproducible QEMU test options.
Run './build.sh noct --help' for Noct commands.
BOOT installation syntax:
  ./build.sh boot-install [--partition N] [--install-disk-stubs]
                          IMAGE [VMLINUX [BOOT.CFG]]
EOF
}

kernel_usage()
{
	cat <<'EOF'
Usage: ./build.sh kernel [options]

Options:
  --cpu 386|486|686           CPU baseline (default: 686)
  --profile pc98|full         device profile (default: pc98)
  --output-dir DIR            out-of-tree kernel build directory
                              (default: build/kernel-7.2[-i386|-i486])
  --jobs N                    parallel jobs (default: nproc)
  --console video|dual        built-in console selection (default: video)

The root staging tree defaults to the rootfs matching --cpu and can be
overridden with the ROOT_STAGE environment variable.
EOF
}

build_noct()
{
	local action="${1:-help}"
	shift || true
	require_submodule zedBSD
	case "$action" in
		-h | --help | help)
			cat <<'EOF'
Usage: ./build.sh noct build|clean [make options]

The Noct source itself is a submodule of zedBSD; update its pinned revision
in the external/zedBSD repository.

Commands:
  build          build the zedBSD Noct user program (NOCT.ELF)
  clean          remove the zedBSD pc98 build tree (build/pc98)
EOF
			;;
		build) zedbsd_env; "$zedbsd/build.sh" NOCT.ELF pc98 "$@" ;;
		clean) zedbsd_env; "$zedbsd/build.sh" clean pc98 "$@" ;;
		*)
			echo "Unknown Noct command: $action" >&2
			echo "Run './build.sh noct --help' for usage." >&2
			exit 2
			;;
	esac
}

build_kernel()
{
	local cpu=686 profile=pc98 output_dir="" jobs="${JOBS:-$(nproc)}"
	local kernel_version="${KERNEL_VERSION:-7.2}"
	local console=video root_stage="${ROOT_STAGE:-}"
	while test "$#" -gt 0; do
		case "$1" in
			-h | --help)
				kernel_usage
				return
				;;
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
			386) output_dir="$repo/build/kernel-$kernel_version-i386" ;;
			486) output_dir="$repo/build/kernel-$kernel_version-i486" ;;
			686) output_dir="$repo/build/kernel-$kernel_version" ;;
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
	bootloader)
		target="${1:-all}"
		test "$#" -eq 0 || shift
		zedbsd_env
		"$zedbsd/build.sh" "$target" pc98 "$@"
		;;
	bootsimple)
		if test "$#" -eq 0; then
			"$repo/scripts/build-bootsimple-dist.sh"
			exit
		fi
		profile="$1"
		shift
		"$repo/bootsimple/build.sh" --profile "$profile" "$@"
		;;
	bootsimple-test)
		name="${1:?usage: ./build.sh bootsimple-test build|image-layout|failures|qemu [args]}"
		shift
		test -x "$repo/bootsimple/tests/test-$name.sh" || {
			echo "Unknown bootsimple test: $name" >&2; exit 2; }
		"$repo/bootsimple/tests/test-$name.sh" "$@"
		;;
	bootloader-dist) "$repo/scripts/build-bootloader-dist.sh" "$@" ;;
	bootloader-fdd)
		output="${1:-$repo/build/releases/zedbsd-fdd.img}"
		zedbsd_env
		mkdir -p "$(dirname "$output")"
		rm -f -- "$output"
		"$zedbsd/scripts/make-fdd-image.sh" "$output"
		;;
	bootloader-test)
		name="${1:?usage: ./build.sh bootloader-test NAME [args]}"
		shift
		if test -x "$repo/bootloader/tests/test-$name.sh"; then
			"$repo/bootloader/tests/test-$name.sh" "$@"
			exit
		fi
		test -x "$zedbsd/scripts/test-$name.sh" || {
			echo "Unknown zedBSD test: $name" >&2
			ls "$repo/bootloader/tests" 2>/dev/null | \
				sed -n 's/^test-\(.*\)\.sh$/  \1/p' >&2
			ls "$zedbsd/scripts" | sed -n 's/^test-\(.*\)\.sh$/  \1/p' >&2
			exit 2
		}
		zedbsd_env
		"$zedbsd/scripts/test-$name.sh" "$@"
		;;
	remacs) "$repo/bootloader/build-remacs.sh" "$@" ;;
	remacs-test) "$repo/bootloader/tests/test-remacs.sh" "$@" ;;
	xoppai) "$repo/scripts/build-xoppai.sh" "$@" ;;
	noct) build_noct "$@" ;;
	boot-install) "$repo/bootloader/install-product.sh" "$@" ;;
	dos-loader)
		require_submodule zedBSD
		make -C "$zedbsd/platform/pc98/dos" "$@"
		;;
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
	glibc) require_submodule glibc; "$repo/scripts/build-glibc.sh" "$@" ;;
	glibc-tests) "$repo/scripts/build-glibc-tests.sh" "$@" ;;
	rootfs-inventory) "$repo/scripts/inventory-debian-rootfs.py" "$@" ;;
	qemu-win64) "$repo/scripts/build-qemu-win64.sh" "$@" ;;
	virtpc98-win64) "$repo/scripts/build-virtpc98.sh" "$@" ;;
	win64-dist) "$repo/scripts/build-win64-dist.sh" "$@" ;;
	run) require_submodule qemu-pc98; "$repo/scripts/run-qemu.sh" "$@" ;;
	clean) "$repo/scripts/clean-build.sh" "$@" ;;
	*)
		echo "Unknown command: $command" >&2
		usage >&2
		exit 2
		;;
esac
