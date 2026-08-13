#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
noct_src="$repo/external/noct"
remacs_src="$noct_src/apps/remacs"
host_build="${NOCT_HOST_BUILD:-$repo/build/host-noct}"
output="$repo/bootloader/fs/apps/emacs.nap"
dictionary="$repo/bootloader/fs/home/skkjisyo.dic"
noct="${NOCT_HOST:-}"

test -f "$noct_src/CMakeLists.txt" || {
	echo "Noct submodule is missing: external/noct" >&2
	exit 1
}
test -x "$remacs_src/tools/build-nap.sh" || {
	echo "Remacs sources are missing from external/noct" >&2
	exit 1
}
test -s "$remacs_src/dict/SKK-JISYO.remacs" || {
	echo "Remacs SKK dictionary is missing" >&2
	exit 1
}
command -v python3 >/dev/null || { echo "python3 is required" >&2; exit 1; }

if test -z "$noct"; then
	command -v cmake >/dev/null || { echo "cmake is required" >&2; exit 1; }
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
		-DNOCT_ENABLE_OPTIMIZER=ON \
		-DNOCT_ENABLE_INSTALL=OFF
	cmake --build "$host_build" --target noctcli \
		--parallel "${JOBS:-$(nproc)}"
	noct="$host_build/noct"
fi
test -x "$noct" || { echo "Host Noct executable not found: $noct" >&2; exit 1; }

mkdir -p "$repo/build" "$(dirname "$output")" "$(dirname "$dictionary")"
temporary="$(mktemp -d "$repo/build/remacs-bytecode-XXXXXX")"
cleanup()
{
	find "$temporary" -depth -delete 2>/dev/null || true
}
trap cleanup EXIT INT TERM

(
	cd "$remacs_src"
	mkdir -p "$temporary/generated"
	python3 tools/gen-napi.py src/napi.def "$temporary/generated"
	tools/build-nap.sh "$noct" "$temporary/generated" "$temporary"
)
test -s "$temporary/remacs.nap" || {
	echo "Remacs bytecode compiler produced no remacs.nap" >&2
	exit 1
}
test "$(dd if="$temporary/remacs.nap" bs=1 count=13 status=none)" = \
	"Noct Bytecode" || {
	echo "remacs.nap has no Noct bytecode header" >&2
	exit 1
}

install -m 0644 "$temporary/remacs.nap" "$output.part"
mv -f -- "$output.part" "$output"
install -m 0644 "$remacs_src/dict/SKK-JISYO.remacs" "$dictionary.part"
mv -f -- "$dictionary.part" "$dictionary"
printf 'Remacs bytecode: %s (%s bytes)\n' "$output" "$(stat -c %s "$output")"
printf 'Remacs SKK dictionary: %s (%s bytes)\n' \
	"$dictionary" "$(stat -c %s "$dictionary")"
