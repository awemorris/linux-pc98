#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
noct_src="$repo/third_party/noct"
remacs_src="$noct_src/apps/remacs"
host_build="${NOCT_HOST_BUILD:-$repo/build/host-noct}"
output_dir="${REMACS_OUTPUT_DIR:-$repo/build/bootloader/remacs}"
noct="${NOCT_HOST:-}"

test -f "$noct_src/CMakeLists.txt" || {
	echo "Noct submodule is missing; run git submodule update --init third_party/noct" >&2
	exit 1
}
test -x "$remacs_src/tools/build-nb.sh" || {
	echo "Remacs sources are missing from the Noct submodule" >&2
	exit 1
}
test -s "$remacs_src/dict/SKK-JISYO.remacs" || {
	echo "Remacs SKK dictionary is missing from the Noct submodule" >&2
	exit 1
}
command -v python3 >/dev/null 2>&1 || {
	echo "python3 is required; run ./build.sh setup" >&2
	exit 1
}

if test -z "$noct"; then
	command -v cmake >/dev/null 2>&1 || {
		echo "cmake is required; run ./build.sh setup" >&2
		exit 1
	}
	cmake -S "$noct_src" -B "$host_build" \
		-DCMAKE_BUILD_TYPE=Release \
		-DNOCT_ENABLE_STATIC=ON \
		-DNOCT_ENABLE_CLI=ON \
		-DNOCT_ENABLE_JIT=ON \
		-DNOCT_ENABLE_API=ON \
		-DNOCT_ENABLE_API_SYSTEM=ON \
		-DNOCT_ENABLE_API_CONSOLE=ON \
		-DNOCT_ENABLE_API_FILE=ON \
		-DNOCT_ENABLE_API_TERM=ON \
		-DNOCT_ENABLE_REPL=ON \
		-DNOCT_ENABLE_BCBACKEND=ON \
		-DNOCT_ENABLE_INSTALL=OFF
	cmake --build "$host_build" --target noctcli \
		--parallel "${JOBS:-$(nproc)}"
	noct="$host_build/noct"
fi

test -x "$noct" || {
	echo "Host Noct executable not found: $noct" >&2
	exit 1
}

mkdir -p "$output_dir"
rm -f -- "$output_dir/REMACS.NB.part"
rm -f -- "$output_dir/SKKJISYO.DIC.part"
# Keep the temporary path dot-free so this build also works with older Noct
# revisions from before output-extension parsing was fixed in 887bf89.
temporary="$(mktemp -d "$repo/build/remacs-bytecode-XXXXXX")"
cleanup()
{
	find "$temporary" -depth -delete 2>/dev/null || true
}
trap cleanup EXIT

(
	cd "$remacs_src"
	mkdir -p "$temporary/generated"
	python3 tools/gen-napi.py src/napi.def "$temporary/generated"
	tools/build-nb.sh "$noct" "$temporary/generated" "$temporary"
)
test -s "$temporary/REMACS.NB" || {
	echo "Remacs bytecode compiler produced no REMACS.NB" >&2
	exit 1
}

# Noct bytecode starts with the portable "Noct Bytecode" header. Checking it here
# catches accidental source/binary mix-ups before a release image is written.
test "$(dd if="$temporary/REMACS.NB" bs=1 count=13 status=none)" = \
	"Noct Bytecode" || {
	echo "REMACS.NB has no Noct bytecode header" >&2
	exit 1
}

install -m 0644 "$temporary/REMACS.NB" "$output_dir/REMACS.NB.part"
mv -f -- "$output_dir/REMACS.NB.part" "$output_dir/REMACS.NB"
install -m 0644 "$remacs_src/dict/SKK-JISYO.remacs" \
	"$output_dir/SKKJISYO.DIC.part"
mv -f -- "$output_dir/SKKJISYO.DIC.part" "$output_dir/SKKJISYO.DIC"
printf 'Remacs bytecode: %s (%s bytes)\n' \
	"$output_dir/REMACS.NB" "$(stat -c %s "$output_dir/REMACS.NB")"
printf 'Remacs SKK dictionary: %s (%s bytes)\n' \
	"$output_dir/SKKJISYO.DIC" \
	"$(stat -c %s "$output_dir/SKKJISYO.DIC")"
