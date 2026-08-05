#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
package_file="$repo/scripts/setup-packages.txt"
run_update=1
assume_yes=0

usage()
{
	cat <<'EOF'
Usage: ./build.sh setup [options]

Install Debian 13 packages required to build kernels, root filesystems, disk
images, qemu-pc98, and the headless TCG smoke tests.

Options:
  --no-update  do not run apt-get update before installation
  --yes        pass --yes to apt-get for unattended setup
  -h, --help   show this help

OpenWatcom 1.9 is not distributed by Debian and is not installed here.
EOF
}

while test "$#" -gt 0; do
	case "$1" in
		--no-update) run_update=0 ;;
		--yes) assume_yes=1 ;;
		-h | --help) usage; exit 0 ;;
		*) echo "Unknown setup option: $1" >&2; usage >&2; exit 2 ;;
	esac
	shift
done

test -r "$package_file" || {
	echo "Missing package inventory: $package_file" >&2
	exit 1
}
command -v apt-get >/dev/null 2>&1 || {
	echo "This setup command currently supports Debian/Ubuntu apt hosts." >&2
	exit 1
}

if test "$(id -u)" -eq 0; then
	privilege=()
elif command -v sudo >/dev/null 2>&1; then
	privilege=(sudo)
else
	echo "Run as root or install sudo before using this command." >&2
	exit 1
fi

mapfile -t packages < <(sed -e 's/[[:space:]]*#.*$//' -e '/^[[:space:]]*$/d' "$package_file")
apt_options=(--no-install-recommends)
test "$assume_yes" -eq 0 || apt_options+=(--yes)

if test "$run_update" -eq 1; then
	"${privilege[@]}" apt-get update
fi
"${privilege[@]}" apt-get install "${apt_options[@]}" "${packages[@]}"
