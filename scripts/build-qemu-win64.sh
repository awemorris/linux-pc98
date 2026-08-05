#!/usr/bin/env bash
set -euo pipefail

BASE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DOWNLOAD_DIR="$BASE_DIR/build/downloads"
SOURCE_DIR="$BASE_DIR/build/qemu-deps-src"
DEPS_BUILD_DIR="$BASE_DIR/build/qemu-deps-build"
ROOT_DIR="$BASE_DIR/build/qemu-deps-root"
QEMU_SOURCE_DIR="$BASE_DIR/qemu-pc98-src"
QEMU_BUILD_DIR="$BASE_DIR/build/qemu-win64"
STAGE_DIR="$BASE_DIR/build/qemu-stage"
DIST_DIR="$BASE_DIR/build/qemu-pc98-bin"
PACKAGE_ASSET_DIR=${PACKAGE_ASSET_DIR:-"$BASE_DIR/package-assets"}
LICENSE_DIR="$BASE_DIR/build/licenses"
STAMP_DIR="$ROOT_DIR/.stamps"
CROSS_FILE="$BASE_DIR/build/cross-mingw.ini"
CMAKE_TOOLCHAIN="$BASE_DIR/cmake/toolchain-mingw.cmake"
PKG_CONFIG_WRAPPER="$ROOT_DIR/bin/x86_64-w64-mingw32-pkg-config"

TARGET=x86_64-w64-mingw32
CC="$TARGET-gcc"
CXX="$TARGET-g++"
AR="$TARGET-ar"
RANLIB="$TARGET-ranlib"
STRIP="$TARGET-strip"
WINDRES="$TARGET-windres"
OBJDUMP="$TARGET-objdump"
STRINGS="$TARGET-strings"
QEMU_TARGET_LIST=i386-softmmu,x86_64-softmmu
QEMU_SYSTEM_TARGETS=(i386 x86_64)

detected_jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
if (( detected_jobs > 16 )); then
    detected_jobs=16
fi
JOBS=${JOBS:-$detected_jobs}

# shellcheck source=versions.conf
. "$BASE_DIR/versions.conf"

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

ensure_layout()
{
    mkdir -p "$DOWNLOAD_DIR" "$SOURCE_DIR" "$DEPS_BUILD_DIR" \
        "$ROOT_DIR/bin" "$ROOT_DIR/lib/pkgconfig" "$ROOT_DIR/share/pkgconfig" \
        "$STAMP_DIR" "$QEMU_BUILD_DIR" "$STAGE_DIR" "$DIST_DIR" "$LICENSE_DIR"
}

write_toolchain_files()
{
    mkdir -p "$ROOT_DIR/bin"
    cat >"$PKG_CONFIG_WRAPPER" <<EOF
#!/usr/bin/env bash
export PKG_CONFIG_LIBDIR="$ROOT_DIR/lib/pkgconfig:$ROOT_DIR/share/pkgconfig"
export PKG_CONFIG_PATH=
exec pkg-config "\$@"
EOF
    chmod +x "$PKG_CONFIG_WRAPPER"

    cat >"$CROSS_FILE" <<EOF
[binaries]
c = '$CC'
cpp = '$CXX'
ar = '$AR'
ranlib = '$RANLIB'
strip = '$STRIP'
windres = '$WINDRES'
pkg-config = '$PKG_CONFIG_WRAPPER'
pkgconfig = '$PKG_CONFIG_WRAPPER'

[host_machine]
system = 'windows'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'

[properties]
needs_exe_wrapper = true

[built-in options]
c_args = ['-I$ROOT_DIR/include']
cpp_args = ['-I$ROOT_DIR/include']
c_link_args = ['-L$ROOT_DIR/lib']
cpp_link_args = ['-L$ROOT_DIR/lib']
EOF

    cat >"$CMAKE_TOOLCHAIN" <<EOF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER $CC)
set(CMAKE_CXX_COMPILER $CXX)
set(CMAKE_RC_COMPILER $WINDRES)
set(CMAKE_AR $AR)
set(CMAKE_RANLIB $RANLIB)
set(CMAKE_FIND_ROOT_PATH "$ROOT_DIR" "/usr/$TARGET")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF
}

bootstrap()
{
    log "Installing Debian build prerequisites"
    sudo apt-get update
    sudo apt-get install -y \
        gcc-mingw-w64-x86-64-posix \
        g++-mingw-w64-x86-64-posix \
        binutils-mingw-w64-x86-64 \
        mingw-w64-x86-64-dev \
        mingw-w64-tools \
        meson ninja-build cmake \
        autoconf automake libtool gettext autopoint bison flex \
        pkgconf python3 python3-setuptools python3-packaging \
        nasm zip unzip curl ca-certificates
}

check_tools()
{
    local tool
    for tool in "$CC" "$CXX" "$AR" "$RANLIB" "$STRIP" "$WINDRES" \
                "$OBJDUMP" "$STRINGS" meson ninja cmake pkg-config python3 \
                curl tar; do
        require_command "$tool"
    done
}

fetch()
{
    local archive=$1
    local url=$2
    local expected=$3
    local path="$DOWNLOAD_DIR/$archive"
    local actual

    if [[ ! -f "$path" ]]; then
        log "Downloading $archive"
        curl --fail --location --retry 3 --output "$path.part" "$url"
        mv "$path.part" "$path"
    fi
    actual=$(sha256sum "$path" | awk '{print $1}')
    [[ "$actual" == "$expected" ]] ||
        die "SHA-256 mismatch for $archive: expected $expected, got $actual"
}

fetch_all()
{
    fetch "$ZLIB_ARCHIVE" "$ZLIB_URL" "$ZLIB_SHA256"
    fetch "$LIBFFI_ARCHIVE" "$LIBFFI_URL" "$LIBFFI_SHA256"
    fetch "$PCRE2_ARCHIVE" "$PCRE2_URL" "$PCRE2_SHA256"
    fetch "$LIBICONV_ARCHIVE" "$LIBICONV_URL" "$LIBICONV_SHA256"
    fetch "$GLIB_ARCHIVE" "$GLIB_URL" "$GLIB_SHA256"
    fetch "$PROXY_INTL_ARCHIVE" "$PROXY_INTL_URL" "$PROXY_INTL_SHA256"
    fetch "$PIXMAN_ARCHIVE" "$PIXMAN_URL" "$PIXMAN_SHA256"
    fetch "$SDL_ARCHIVE" "$SDL_URL" "$SDL_SHA256"
    fetch "$SLIRP_ARCHIVE" "$SLIRP_URL" "$SLIRP_SHA256"
    fetch "$LIBUSB_ARCHIVE" "$LIBUSB_URL" "$LIBUSB_SHA256"
    fetch "$KEYCODEMAPDB_ARCHIVE" "$KEYCODEMAPDB_URL" "$KEYCODEMAPDB_SHA256"
}

extract()
{
    local archive=$1
    local directory=$2

    if [[ ! -d "$SOURCE_DIR/$directory" ]]; then
        log "Extracting $archive"
        tar -xf "$DOWNLOAD_DIR/$archive" -C "$SOURCE_DIR"
    fi
}

extract_all()
{
    extract "$ZLIB_ARCHIVE" "zlib-$ZLIB_VERSION"
    extract "$LIBFFI_ARCHIVE" "libffi-$LIBFFI_VERSION"
    extract "$PCRE2_ARCHIVE" "pcre2-$PCRE2_VERSION"
    extract "$LIBICONV_ARCHIVE" "libiconv-$LIBICONV_VERSION"
    extract "$GLIB_ARCHIVE" "glib-$GLIB_VERSION"
    extract "$PROXY_INTL_ARCHIVE" "proxy-libintl-$PROXY_INTL_COMMIT"
    extract "$PIXMAN_ARCHIVE" "pixman-$PIXMAN_VERSION"
    extract "$SDL_ARCHIVE" "SDL2-$SDL_VERSION"
    extract "$SLIRP_ARCHIVE" "libslirp-v$SLIRP_VERSION"
    extract "$LIBUSB_ARCHIVE" "libusb-$LIBUSB_VERSION"
    extract "$KEYCODEMAPDB_ARCHIVE" "keycodemapdb-$KEYCODEMAPDB_REVISION"
}

is_built()
{
    [[ -f "$STAMP_DIR/$1" ]]
}

mark_built()
{
    printf '%s\n' "$1" >"$STAMP_DIR/$1"
}

meson_setup()
{
    local source=$1
    local build=$2
    shift 2

    if [[ -f "$build/build.ninja" ]]; then
        meson setup --reconfigure "$build" "$source" "$@"
    else
        meson setup "$build" "$source" "$@"
    fi
}

build_zlib()
{
    local stamp="zlib-$ZLIB_VERSION"
    local build="$DEPS_BUILD_DIR/$stamp"
    is_built "$stamp" && return
    log "Building $stamp"
    cmake -S "$SOURCE_DIR/zlib-$ZLIB_VERSION" -B "$build" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN" \
        -DCMAKE_INSTALL_PREFIX="$ROOT_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=ON \
        -DZLIB_BUILD_EXAMPLES=OFF
    cmake --build "$build" --parallel "$JOBS"
    cmake --install "$build"
    mark_built "$stamp"
}

fixup_zlib_import_library()
{
    # zlib's MinGW CMake build installs libzlib.dll.a, while its zlib.pc
    # advertises "-lz".  Provide the conventional linker name expected by
    # pkg-config consumers such as GLib.
    if [[ -f "$ROOT_DIR/lib/libzlib.dll.a" ]]; then
        ln -sfn libzlib.dll.a "$ROOT_DIR/lib/libz.dll.a"
    fi
}

build_libffi()
{
    local stamp="libffi-$LIBFFI_VERSION"
    local source="$SOURCE_DIR/$stamp"
    local build="$DEPS_BUILD_DIR/$stamp"
    is_built "$stamp" && return
    log "Building $stamp"
    mkdir -p "$build"
    (
        cd "$build"
        "$source/configure" \
            --host="$TARGET" \
            --prefix="$ROOT_DIR" \
            --enable-shared \
            --disable-static \
            --disable-docs \
            --disable-multi-os-directory
        make -j"$JOBS"
        make install
    )
    mark_built "$stamp"
}

build_pcre2()
{
    local stamp="pcre2-$PCRE2_VERSION"
    local build="$DEPS_BUILD_DIR/$stamp"
    is_built "$stamp" && return
    log "Building $stamp"
    cmake -S "$SOURCE_DIR/$stamp" -B "$build" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN" \
        -DCMAKE_INSTALL_PREFIX="$ROOT_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=ON \
        -DPCRE2_BUILD_PCRE2_8=ON \
        -DPCRE2_BUILD_PCRE2_16=OFF \
        -DPCRE2_BUILD_PCRE2_32=OFF \
        -DPCRE2_BUILD_PCRE2GREP=OFF \
        -DPCRE2_BUILD_TESTS=OFF
    cmake --build "$build" --parallel "$JOBS"
    cmake --install "$build"
    mark_built "$stamp"
}

build_libiconv()
{
    local stamp="libiconv-$LIBICONV_VERSION"
    local source="$SOURCE_DIR/$stamp"
    local build="$DEPS_BUILD_DIR/$stamp"
    is_built "$stamp" && return
    log "Building $stamp"
    mkdir -p "$build"
    (
        cd "$build"
        "$source/configure" \
            --host="$TARGET" \
            --prefix="$ROOT_DIR" \
            --enable-shared \
            --disable-static \
            --disable-nls
        make -j"$JOBS"
        make install
    )
    mark_built "$stamp"
}

build_glib()
{
    local stamp="glib-$GLIB_VERSION"
    local build="$DEPS_BUILD_DIR/$stamp"
    local proxy_source="$SOURCE_DIR/proxy-libintl-$PROXY_INTL_COMMIT"
    local proxy_target="$SOURCE_DIR/$stamp/subprojects/proxy-libintl"
    is_built "$stamp" && return
    log "Building $stamp"
    mkdir -p "$proxy_target"
    cp -a "$proxy_source"/. "$proxy_target"/
    PKG_CONFIG="$PKG_CONFIG_WRAPPER" \
    meson_setup "$SOURCE_DIR/$stamp" "$build" \
        --cross-file="$CROSS_FILE" \
        --prefix="$ROOT_DIR" \
        --libdir=lib \
        --buildtype=release \
        --default-library=shared \
        -Dtests=false \
        -Dinstalled_tests=false \
        -Ddocumentation=false \
        -Dintrospection=disabled \
        -Dnls=disabled \
        -Dlibmount=disabled \
        -Dselinux=disabled \
        -Dxattr=false \
        -Ddtrace=disabled \
        -Dsystemtap=disabled \
        -Dsysprof=disabled \
        -Dglib_debug=disabled
    meson compile -C "$build" -j "$JOBS"
    meson install -C "$build"
    mark_built "$stamp"
}

build_pixman()
{
    local stamp="pixman-$PIXMAN_VERSION"
    local build="$DEPS_BUILD_DIR/$stamp"
    is_built "$stamp" && return
    log "Building $stamp"
    meson_setup "$SOURCE_DIR/$stamp" "$build" \
        --cross-file="$CROSS_FILE" \
        --prefix="$ROOT_DIR" \
        --libdir=lib \
        --buildtype=release \
        --default-library=shared \
        -Dtests=disabled \
        -Ddemos=disabled \
        -Dgtk=disabled \
        -Dlibpng=disabled
    meson compile -C "$build" -j "$JOBS"
    meson install -C "$build"
    mark_built "$stamp"
}

build_sdl()
{
    local stamp="SDL2-$SDL_VERSION"
    local build="$DEPS_BUILD_DIR/$stamp"
    is_built "$stamp" && return
    log "Building $stamp"
    cmake -S "$SOURCE_DIR/$stamp" -B "$build" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN" \
        -DCMAKE_INSTALL_PREFIX="$ROOT_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DSDL_SHARED=ON \
        -DSDL_STATIC=OFF \
        -DSDL_TEST=OFF \
        -DSDL_TESTS=OFF
    cmake --build "$build" --parallel "$JOBS"
    cmake --install "$build"
    mark_built "$stamp"
}

build_libusb()
{
    local stamp="libusb-$LIBUSB_VERSION"
    local source="$SOURCE_DIR/$stamp"
    local build="$DEPS_BUILD_DIR/$stamp"
    is_built "$stamp" && return
    log "Building $stamp"
    mkdir -p "$build"
    (
        cd "$build"
        "$source/configure" \
            --host="$TARGET" \
            --prefix="$ROOT_DIR" \
            --enable-shared \
            --disable-static \
            --disable-udev \
            --disable-examples-build \
            --disable-tests-build
        make -j"$JOBS"
        make install
    )
    [[ -f "$ROOT_DIR/lib/pkgconfig/libusb-1.0.pc" ]] ||
        die "libusb pkg-config metadata was not installed"
    mark_built "$stamp"
}

build_slirp()
{
    local stamp="libslirp-$SLIRP_VERSION"
    local source="$SOURCE_DIR/libslirp-v$SLIRP_VERSION"
    local build="$DEPS_BUILD_DIR/$stamp"
    is_built "$stamp" && return
    log "Building $stamp"
    meson_setup "$source" "$build" \
        --cross-file="$CROSS_FILE" \
        --prefix="$ROOT_DIR" \
        --libdir=lib \
        --buildtype=release \
        --default-library=shared \
        -Dtarget_winver=0x0601
    meson compile -C "$build" -j "$JOBS"
    meson install -C "$build"
    mark_built "$stamp"
}

install_runtime_dlls()
{
    local name
    local path
    for name in libgcc_s_seh-1.dll libwinpthread-1.dll; do
        path=$("$CC" -print-file-name="$name")
        if [[ -f "$path" ]]; then
            cp -p "$path" "$ROOT_DIR/bin/"
        fi
    done
}

prepare_qemu_subprojects()
{
    local source="$SOURCE_DIR/keycodemapdb-$KEYCODEMAPDB_REVISION"
    local target="$QEMU_SOURCE_DIR/subprojects/keycodemapdb"

    fetch "$KEYCODEMAPDB_ARCHIVE" "$KEYCODEMAPDB_URL" "$KEYCODEMAPDB_SHA256"
    extract "$KEYCODEMAPDB_ARCHIVE" "keycodemapdb-$KEYCODEMAPDB_REVISION"
    if [[ ! -f "$target/meson.build" ]]; then
        mkdir -p "$target"
        cp -a "$source"/. "$target"/
    fi
}

build_deps()
{
    ensure_layout
    check_tools
    write_toolchain_files
    fetch_all
    if [[ "${DOWNLOAD_ONLY:-0}" == 1 ]]; then
        return
    fi
    extract_all
    build_zlib
    fixup_zlib_import_library
    build_libffi
    build_pcre2
    build_libiconv
    build_glib
    build_pixman
    build_sdl
    build_slirp
    build_libusb
    install_runtime_dlls
    log "Target dependency prefix is ready: $ROOT_DIR"
}

build_qemu()
{
    local arch
    local exe

    ensure_layout
    check_tools
    write_toolchain_files
    [[ -x "$QEMU_SOURCE_DIR/configure" ]] ||
        die "QEMU source tree not found at $QEMU_SOURCE_DIR"
    [[ -f "$ROOT_DIR/lib/pkgconfig/glib-2.0.pc" ]] ||
        die "target dependencies are not built (run: $0 deps)"
    prepare_qemu_subprojects

    log "Configuring QEMU for Windows x86-64"
    (
        cd "$QEMU_BUILD_DIR"
        export PKG_CONFIG="$PKG_CONFIG_WRAPPER"
        export PKG_CONFIG_LIBDIR="$ROOT_DIR/lib/pkgconfig:$ROOT_DIR/share/pkgconfig"
        export PATH="$ROOT_DIR/bin:$PATH"
        "$QEMU_SOURCE_DIR/configure" \
            --cross-prefix="$TARGET-" \
            --target-list="$QEMU_TARGET_LIST" \
            --prefix=/ \
            --bindir=. \
            --datadir=share \
            --libdir=lib \
            --enable-relocatable \
            --disable-download \
            --without-default-features \
            --enable-system \
            --enable-tcg \
            --disable-whpx \
            --enable-qcow1 \
            --enable-vvfat \
            --enable-sdl \
            --enable-pixman \
            --enable-slirp \
            --enable-dsound \
            --enable-libusb \
            --disable-sdl-image \
            --disable-gtk \
            --disable-gio \
            --disable-iconv \
            --disable-rust \
            --disable-plugins \
            --disable-tools \
            --disable-docs \
            --disable-guest-agent \
            --disable-fdt \
            --audio-drv-list=dsound
    )
    ninja -C "$QEMU_BUILD_DIR" -j "$JOBS"
    for arch in "${QEMU_SYSTEM_TARGETS[@]}"; do
        exe="$QEMU_BUILD_DIR/qemu-system-$arch.exe"
        [[ -f "$exe" ]] || die "QEMU executable was not built: $exe"
        "$STRINGS" "$exe" |
            grep -x fat98 >/dev/null ||
            die "qemu-system-$arch.exe is missing the fat98 block protocol"
        "$STRINGS" "$exe" |
            grep -x qcow >/dev/null ||
            die "qemu-system-$arch.exe is missing qcow1 required by fat98:rw"
        log "QEMU executable built: $exe"
    done
}

reset_owned_dir()
{
    local directory=$1
    case "$directory" in
        "$STAGE_DIR"|"$DIST_DIR")
            rm -rf -- "$directory"
            mkdir -p "$directory"
            ;;
        *)
            die "refusing to reset unexpected directory: $directory"
            ;;
    esac
}

copy_package_assets()
{
    local name
    for name in README.txt virtpc98.py virtpc98.exe; do
        [[ -f "$PACKAGE_ASSET_DIR/$name" ]] ||
            die "package asset not found: $PACKAGE_ASSET_DIR/$name"
        cp -p "$PACKAGE_ASSET_DIR/$name" "$DIST_DIR/$name"
    done
}

organize_pc98_roms()
{
    local name
    local source="$DIST_DIR/share"
    local destination="$source/pc98bios"
    mkdir -p "$destination"
    for name in pc98bios.bin pc98itf.bin pc98ide.bin pc98pci.bin \
                pc98font.bin pc98basic.bin; do
        [[ -f "$source/$name" ]] ||
            die "installed PC-98 ROM not found: $source/$name"
        mv "$source/$name" "$destination/$name"
    done
}

copy_licenses()
{
    local destination="$DIST_DIR/licenses"
    mkdir -p "$destination/QEMU" "$destination/zlib" "$destination/libffi" \
        "$destination/PCRE2" "$destination/libiconv" "$destination/GLib" \
        "$destination/proxy-libintl" "$destination/Pixman" \
        "$destination/SDL2" "$destination/libslirp" \
        "$destination/libusb" "$destination/keycodemapdb"
    cp "$QEMU_SOURCE_DIR/COPYING" "$destination/QEMU/"
    cp "$SOURCE_DIR/zlib-$ZLIB_VERSION/LICENSE" "$destination/zlib/"
    cp "$SOURCE_DIR/libffi-$LIBFFI_VERSION/LICENSE" "$destination/libffi/"
    cp "$SOURCE_DIR/pcre2-$PCRE2_VERSION/LICENCE.md" "$destination/PCRE2/"
    cp "$SOURCE_DIR/libiconv-$LIBICONV_VERSION/COPYING" \
        "$SOURCE_DIR/libiconv-$LIBICONV_VERSION/COPYING.LIB" \
        "$destination/libiconv/"
    cp "$SOURCE_DIR/glib-$GLIB_VERSION/COPYING" "$destination/GLib/"
    cp "$SOURCE_DIR/proxy-libintl-$PROXY_INTL_COMMIT/COPYING" \
        "$destination/proxy-libintl/"
    cp "$SOURCE_DIR/pixman-$PIXMAN_VERSION/COPYING" "$destination/Pixman/"
    cp "$SOURCE_DIR/SDL2-$SDL_VERSION/LICENSE.txt" "$destination/SDL2/"
    cp "$SOURCE_DIR/libslirp-v$SLIRP_VERSION/COPYRIGHT" \
        "$destination/libslirp/"
    cp "$SOURCE_DIR/libusb-$LIBUSB_VERSION/COPYING" "$destination/libusb/"
    cp "$SOURCE_DIR/keycodemapdb-$KEYCODEMAPDB_REVISION/LICENSE.BSD" \
        "$SOURCE_DIR/keycodemapdb-$KEYCODEMAPDB_REVISION/LICENSE.GPL2" \
        "$destination/keycodemapdb/"
}

make_dist()
{
    local arch
    local exe
    local suffix
    local qemu_commit
    local archive="$BASE_DIR/qemu-pc98-bin.zip"
    local -a exes=()
    local -a qemu_exes=()

    for arch in "${QEMU_SYSTEM_TARGETS[@]}"; do
        [[ -f "$QEMU_BUILD_DIR/qemu-system-$arch.exe" ]] ||
            die "qemu-system-$arch.exe has not been built (run: $0 qemu)"
    done
    reset_owned_dir "$STAGE_DIR"
    reset_owned_dir "$DIST_DIR"

    log "Installing QEMU into the staging directory"
    DESTDIR="$STAGE_DIR" meson install -C "$QEMU_BUILD_DIR" --no-rebuild
    cp -a "$STAGE_DIR"/. "$DIST_DIR"/
    copy_package_assets
    organize_pc98_roms

    for arch in "${QEMU_SYSTEM_TARGETS[@]}"; do
        for suffix in "" w; do
            exe="$DIST_DIR/qemu-system-$arch$suffix.exe"
            [[ -f "$exe" ]] || die "installed QEMU executable not found: $exe"
            qemu_exes+=("$exe")
        done
    done
    exes=("${qemu_exes[@]}" "$DIST_DIR/virtpc98.exe")

    log "Collecting recursive DLL dependencies"
    python3 "$BASE_DIR/bundle-deps.py" \
        --objdump "$OBJDUMP" \
        --search "$ROOT_DIR/bin" \
        --search "$ROOT_DIR/lib" \
        --dest "$DIST_DIR" \
        --report "$DIST_DIR/DLL-DEPENDENCIES.txt" \
        "${exes[@]}"

    for exe in "${qemu_exes[@]}"; do
        "$STRIP" --strip-unneeded "$exe"
    done
    find "$DIST_DIR" -maxdepth 1 -type f -name '*.dll' -print0 |
        xargs -0 -r "$STRIP" --strip-unneeded

    copy_licenses
    qemu_commit=$(git -C "$QEMU_SOURCE_DIR" rev-parse HEAD)
    {
        printf 'QEMU commit: %s\n' "$qemu_commit"
        printf 'Target: %s\n' "$TARGET"
        "$CC" --version | sed -n '1p'
        printf 'zlib: %s\n' "$ZLIB_VERSION"
        printf 'libffi: %s\n' "$LIBFFI_VERSION"
        printf 'PCRE2: %s\n' "$PCRE2_VERSION"
        printf 'libiconv: %s\n' "$LIBICONV_VERSION"
        printf 'GLib: %s\n' "$GLIB_VERSION"
        printf 'Pixman: %s\n' "$PIXMAN_VERSION"
        printf 'SDL2: %s\n' "$SDL_VERSION"
        printf 'libslirp: %s\n' "$SLIRP_VERSION"
        printf 'libusb: %s\n' "$LIBUSB_VERSION"
        printf 'keycodemapdb: %s\n' "$KEYCODEMAPDB_REVISION"
    } >"$DIST_DIR/BUILD-INFO.txt"

    (
        cd "$DIST_DIR"
        find . -type f ! -name SHA256SUMS -print0 |
            sort -z |
            xargs -0 sha256sum >SHA256SUMS
    )
    rm -f "$archive"
    (
        cd "$BASE_DIR"
        zip -X -q -r "$archive" "$(basename "$DIST_DIR")"
    )
    sha256sum "$archive" >"$archive.sha256"
    log "Distribution ready: $DIST_DIR"
    log "Archive ready: $archive"
}

verify()
{
    local arch
    local exe
    local machine
    local missing=0
    local suffix
    local -a exes=()
    local -a qemu_exes=()
    [[ -d "$DIST_DIR" ]] || die "distribution not found (run: $0 dist)"

    for arch in "${QEMU_SYSTEM_TARGETS[@]}"; do
        for suffix in "" w; do
            exe="$DIST_DIR/qemu-system-$arch$suffix.exe"
            [[ -f "$exe" ]] || {
                printf 'Missing executable: %s\n' "$exe" >&2
                missing=1
                continue
            }
            qemu_exes+=("$exe")
        done
    done
    [[ -f "$DIST_DIR/virtpc98.exe" ]] || {
        printf 'Missing executable: %s\n' "$DIST_DIR/virtpc98.exe" >&2
        missing=1
    }
    exes=("${qemu_exes[@]}")
    [[ -f "$DIST_DIR/virtpc98.exe" ]] &&
        exes+=("$DIST_DIR/virtpc98.exe")

    log "Inspecting Windows executables"
    for exe in "${exes[@]}"; do
        file "$exe"
        machine=$("$OBJDUMP" -f "$exe" | sed -n 's/^architecture: //p')
        [[ "$machine" == i386:x86-64* ]] || {
            printf 'Unexpected architecture: %s: %s\n' "$exe" "$machine" >&2
            missing=1
        }
    done

    log "Verifying checksums"
    (cd "$DIST_DIR" && sha256sum -c SHA256SUMS)

    log "Checking bundled DLL closure"
    python3 "$BASE_DIR/bundle-deps.py" \
        --objdump "$OBJDUMP" \
        --search "$DIST_DIR" \
        --dest "$DIST_DIR" \
        --report "$DIST_DIR/DLL-DEPENDENCIES.verify.txt" \
        "${exes[@]}"
    return "$missing"
}

usage()
{
    cat <<EOF
Usage: $0 COMMAND

Commands:
  bootstrap  Install Debian host packages with apt
  deps       Download, verify and build target dependencies
  qemu       Cross-build i386 and x86_64 QEMU system executables
  dist       Assemble qemu-pc98-bin/ and qemu-pc98-bin.zip
  verify     Inspect PE architecture, checksums and DLL closure
  all        Run deps, qemu, dist and verify
EOF
}

main()
{
    local command=${1:-}
    ensure_layout
    case "$command" in
        bootstrap) bootstrap ;;
        deps) build_deps ;;
        qemu) build_qemu ;;
        dist) make_dist ;;
        verify) verify ;;
        all)
            build_deps
            build_qemu
            make_dist
            verify
            ;;
        *) usage; exit 2 ;;
    esac
}

main "$@"
