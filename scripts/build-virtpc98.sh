#!/usr/bin/env bash
set -euo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
download_dir="$repo/build/downloads"
output_dir="$repo/build/virtpc98-win64"
wine_prefix="$repo/build/virtpc98-wine"
python_version=${PYTHON_VERSION:-3.12.10}
python_installer="python-${python_version}-amd64.exe"
python_url="https://www.python.org/ftp/python/${python_version}/${python_installer}"
python_sha256=${PYTHON_SHA256:-67b5635e80ea51072b87941312d00ec8927c4db9ba18938f7ad2d27b328b95fb}
pyinstaller_version=${PYINSTALLER_VERSION:-6.14.2}
python_exe="$wine_prefix/drive_c/Python312/python.exe"

log()
{
	printf '\n==> %s\n' "$*"
}

die()
{
	printf 'ERROR: %s\n' "$*" >&2
	exit 1
}

require_command()
{
	command -v "$1" >/dev/null 2>&1 ||
		die "required command not found: $1 (run: $0 bootstrap)"
}

bootstrap()
{
	log "Installing Wine/PyInstaller host prerequisites"
	sudo apt-get update
	if ! dpkg --print-foreign-architectures | grep -Fx i386 >/dev/null; then
		sudo dpkg --add-architecture i386
		sudo apt-get update
	fi
	sudo apt-get install -y wine64 wine32:i386 xvfb curl ca-certificates file binutils
}

wine_run()
{
	WINEPREFIX="$wine_prefix" WINEARCH=win64 WINEDEBUG=-all \
		xvfb-run -a wine "$@"
}

windows_path()
{
	local absolute
	absolute=$(realpath -m "$1")
	printf 'Z:%s' "${absolute//\//\\}"
}

fetch_python()
{
	local installer="$download_dir/$python_installer"
	local actual
	mkdir -p "$download_dir"
	if [[ ! -f "$installer" ]]; then
		log "Downloading Python $python_version for Windows x86-64"
		curl --fail --location --retry 3 --output "$installer.part" "$python_url"
		mv "$installer.part" "$installer"
	fi
	actual=$(sha256sum "$installer" | awk '{print $1}')
	[[ "$actual" == "$python_sha256" ]] ||
		die "SHA-256 mismatch for $python_installer: expected $python_sha256, got $actual"
}

install_python()
{
	local installer="$download_dir/$python_installer"
	[[ -x "$python_exe" ]] && return
	fetch_python
	log "Installing an isolated Windows Python into the Wine prefix"
	mkdir -p "$wine_prefix"
	wine_run "$(windows_path "$installer")" /quiet \
		InstallAllUsers=0 'TargetDir=C:\Python312' \
		Include_doc=0 Include_launcher=0 Include_test=0 Include_tcltk=1 \
		Include_pip=1 AssociateFiles=0 Shortcuts=0 PrependPath=0
	[[ -f "$python_exe" ]] || die "Windows Python installation failed"
}

install_pyinstaller()
{
	install_python
	if wine_run "$python_exe" -m PyInstaller --version 2>/dev/null | tr -d '\r' |
		grep -Fx "$pyinstaller_version" >/dev/null; then
		return
	fi
	log "Installing PyInstaller $pyinstaller_version"
	wine_run "$python_exe" -m pip install \
		--disable-pip-version-check --no-warn-script-location \
		"pyinstaller==$pyinstaller_version"
}

build_exe()
{
	local source="$repo/scripts/virtpc98.py"
	local dist="$output_dir/dist"
	local work="$output_dir/work"
	local spec="$output_dir/spec"
	require_command curl
	require_command sha256sum
	require_command xvfb-run
	require_command wine
	require_command file
	[[ -f "$source" ]] || die "virtpc98.py not found: $source"
	install_pyinstaller
	rm -rf -- "$dist" "$work" "$spec"
	mkdir -p "$dist" "$work" "$spec"
	log "Building virtpc98.exe"
	wine_run "$python_exe" -m PyInstaller \
		--noconfirm --clean --onefile --windowed --noupx \
		--name virtpc98 \
		--distpath "$(windows_path "$dist")" \
		--workpath "$(windows_path "$work")" \
		--specpath "$(windows_path "$spec")" \
		"$(windows_path "$source")"
	[[ -f "$dist/virtpc98.exe" ]] || die "PyInstaller did not create virtpc98.exe"
	cp -p "$dist/virtpc98.exe" "$output_dir/virtpc98.exe"
	file "$output_dir/virtpc98.exe"
	log "Windows GUI ready: $output_dir/virtpc98.exe"
}

clean()
{
	rm -rf -- "$output_dir"
}

usage()
{
	cat <<EOF
Usage: $0 [bootstrap|build|clean]

  bootstrap  Install Wine and host-side prerequisites with apt
  build      Build virtpc98.exe with Windows Python and PyInstaller (default)
  clean      Remove the PyInstaller output; retain downloads and Wine Python
EOF
}

case "${1:-build}" in
	bootstrap) bootstrap ;;
	build) build_exe ;;
	clean) clean ;;
	-h | --help | help) usage ;;
	*) usage >&2; exit 2 ;;
esac
