#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")" && pwd)"
boots="$repo/external/boots"

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

# The Boots bootloader lives in the external/boots submodule and resolves
# QEMU, the PC-98 BIOS, release base images, and the soft-float source
# trees from this repository.
boots_env()
{
	require_submodule boots
	. "$repo/scripts/boots-env.sh"
}

usage()
{
	cat <<'EOF'
Usage: ./build.sh COMMAND [options]

Primary commands:
  setup [options]             install Debian 13 host build/test dependencies
  bootloader [targets]        build the Boots binaries (external/boots, pc98)
  bootloader-dist             build build/releases/bootloader.zip
  bootloader-fdd [OUTPUT]     build the Boots FDD image (boots-fdd.img)
  bootloader-test NAME        run a Boots QEMU test (e.g. noct-repl, hdd-boot)
  remacs                      build CMD/REMACS.NB with the pinned Noct compiler
  remacs-test                 run the bytecode editor under headless QEMU
  noct TARGET                 build or verify the imported Noct core
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
Run './build.sh noct --help' for the imported Noct target list.
BOOT installation syntax:
  ./build.sh boot-install [--partition N] [--install-disk-stubs]
                          IMAGE [VMLINUX [BOOTS.CFG]]
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
                              (default: build/kernel-7.1[-i386|-i486])
  --jobs N                    parallel jobs (default: nproc)
  --console video|dual        built-in console selection (default: video)

The root staging tree defaults to the rootfs matching --cpu and can be
overridden with the ROOT_STAGE environment variable.
EOF
}

# Noct targets are defined in the Boots tree, so read them from there rather
# than duplicating a map that silently rots when Boots renames a target.
noct_targets()
{
	sed -n 's/^\(noct-[a-z0-9-]*\):.*/\1/p' \
		"$boots/Makefile" "$boots/platform/pc98/platform.mk" 2>/dev/null |
		sort -u
}

build_noct()
{
	local action="${1:-help}"
	shift || true
	require_submodule boots
	case "$action" in
		-h | --help | help)
			cat <<'EOF'
Usage: ./build.sh noct TARGET [make options]

The Noct source itself is a submodule of Boots; update its pinned revision
in the external/boots repository.

Aliases:
  verify         verify M4-M15, static i386 opcodes, and QEMU REPL paths
  lifecycle-test run the lifecycle, File, utility, native-API, and REPL host test
  clean          remove the Boots pc98 build tree (build/pc98)

Boots Noct targets:
EOF
			noct_targets | sed 's/^/  /'
			;;
		verify) boots_env; "$boots/build.sh" pc98 noct-m15-verify "$@" ;;
		lifecycle-test) "$boots/build.sh" pc98 noct-host-test "$@" ;;
		clean) "$boots/build.sh" pc98 clean "$@" ;;
		*)
			if ! noct_targets | grep -qx -- "$action"; then
				echo "Unknown Noct target: $action" >&2
				echo "Run './build.sh noct --help' for the target list." >&2
				exit 2
			fi
			boots_env
			"$boots/build.sh" pc98 "$action" "$@"
			;;
	esac
}

build_kernel()
{
	local cpu=686 profile=pc98 output_dir="" jobs="${JOBS:-$(nproc)}"
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
	bootloader) boots_env; "$boots/build.sh" pc98 "$@" ;;
	bootloader-dist) "$repo/scripts/build-bootloader-dist.sh" "$@" ;;
	bootloader-fdd)
		output="${1:-$repo/build/releases/boots-fdd.img}"
		boots_env
		mkdir -p "$(dirname "$output")"
		rm -f -- "$output"
		"$boots/scripts/make-fdd-image.sh" "$output"
		;;
	bootloader-test)
		name="${1:?usage: ./build.sh bootloader-test NAME [args]}"
		shift
		test -x "$boots/scripts/test-$name.sh" || {
			echo "Unknown Boots test: $name" >&2
			ls "$boots/scripts" | sed -n 's/^test-\(.*\)\.sh$/  \1/p' >&2
			exit 2
		}
		boots_env
		"$boots/scripts/test-$name.sh" "$@"
		;;
	remacs) boots_env; "$boots/scripts/build-remacs-bytecode.sh" "$@" ;;
	remacs-test) boots_env; "$boots/scripts/test-remacs.sh" "$@" ;;
	noct) build_noct "$@" ;;
	boot-install) boots_env; "$boots/scripts/install-image.sh" "$@" ;;
	dos-loader)
		require_submodule boots
		make -C "$boots/platform/pc98/dos" "$@"
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
