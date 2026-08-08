#!/usr/bin/env bash
set -euo pipefail

# Verify the PC-98 Term.* UTF-8 to JIS kuten conversion against the actual
# text VRAM words, then preserve a screenshot for visual regression review.
repo="$(cd "$(dirname "$0")/.." && pwd)"
qemu="${QEMU:-$repo/qemu-pc98/build/qemu-system-i386}"
bios_dir="${PC98_BIOS_DIR:-$repo/qemu-pc98/roms/pc98bios}"
base="${BOOT98_TEST_BASE:-$repo/build/releases/linux-pc98-i386sx-busybox-ide.img}"
work="$repo/build/tests/boots-term-japanese"
image="$work/term-japanese.raw"
files="$work/files"
cfg="$work/BOOT.CFG"
monitor="$work/monitor.sock"
screenshot="$work/term-japanese.ppm"
vram="$work/term-japanese-vram.bin"

test -x "$qemu" || { echo "QEMU not found: $qemu" >&2; exit 1; }
test -d "$bios_dir" || { echo "PC-98 BIOS directory not found: $bios_dir" >&2; exit 1; }
test -f "$base" || { echo "source image not found: $base" >&2; exit 1; }

mkdir -p "$work" "$files"
cp --reflink=auto "$base" "$image"
cat > "$files/TERMJIS.NCT" <<'EOF'
func main() {
    if (Term.open() != 1) { return 1; }
    Term.moveTo(1, 1);
    Term.write("日本語");
    Term.flush();
    Term.readKey(-1);
    Term.close();
    return 0;
}
EOF
printf 'noct TERMJIS.NCT\nhalt\n' > "$cfg"

make -C "$repo/bootloader" -j"$(nproc)" BOOT.SYS
BOOT98_FILES="$files" DISK_HEADS=8 DISK_SECTORS=17 \
	"$repo/scripts/install-boot98-image.sh" "$image" "" "$cfg"

rm -f -- "$monitor" "$screenshot" "$vram"
"$qemu" -M pc9801 -cpu 386 -m 6 -accel tcg -L "$bios_dir" \
	-nic none -drive "if=ide,bus=0,unit=0,format=raw,file=$image" \
	-display none -serial none -qmp "unix:$monitor,server=on,wait=off" \
	-no-reboot >/dev/null 2>&1 &
qemu_pid=$!
cleanup()
{
	if kill -0 "$qemu_pid" 2>/dev/null; then
		kill "$qemu_pid" 2>/dev/null || true
		wait "$qemu_pid" 2>/dev/null || true
	fi
	rm -f -- "$monitor"
}
trap cleanup EXIT INT TERM

python3 - "$monitor" "$screenshot" "$vram" <<'PY'
import json
import socket
import sys
import time

monitor, screenshot, vram = sys.argv[1:]
client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
deadline = time.monotonic() + 20
while True:
    try:
        client.connect(monitor)
        break
    except (FileNotFoundError, ConnectionRefusedError):
        if time.monotonic() >= deadline:
            raise SystemExit("QEMU monitor did not become ready")
        time.sleep(.1)
stream = client.makefile("rwb", buffering=0)
json.loads(stream.readline())

def qmp(execute, arguments=None, wait_reply=True):
    request = {"execute": execute}
    if arguments is not None:
        request["arguments"] = arguments
    stream.write(json.dumps(request).encode("ascii") + b"\n")
    if not wait_reply:
        return
    while True:
        reply = json.loads(stream.readline())
        if "return" in reply:
            return reply["return"]
        if "error" in reply:
            raise SystemExit(f"QMP {execute} failed: {reply['error']}")

def key(qcode):
    qmp("input-send-event", {"events": [
        {"type": "key", "data": {"down": True,
          "key": {"type": "qcode", "data": qcode}}},
        {"type": "key", "data": {"down": False,
          "key": {"type": "qcode", "data": qcode}}},
    ]})

qmp("qmp_capabilities")
time.sleep(20)
qmp("screendump", {"filename": screenshot})
qmp("pmemsave", {"val": 0xa0000, "size": 12, "filename": vram})
key("ret")
time.sleep(1)
qmp("quit", wait_reply=False)
stream.close()
client.close()
PY

for _ in $(seq 1 50); do
	if ! kill -0 "$qemu_pid" 2>/dev/null; then
		break
	fi
	sleep .1
done
if kill -0 "$qemu_pid" 2>/dev/null; then
	kill "$qemu_pid"
fi
wait "$qemu_pid" 2>/dev/null || true
trap - EXIT INT TERM
rm -f -- "$monitor"

python3 - "$vram" <<'PY'
import struct
import sys

# PC-98 text VRAM stores (ku - 0x20) in the low byte and ten in the high
# byte.  The following word has bit 15 set to select the right glyph half.
expected = (0x7c26, 0xfc26, 0x5c2b, 0xdc2b, 0x6c18, 0xec18)
actual = struct.unpack("<6H", open(sys.argv[1], "rb").read(12))
if actual != expected:
    raise SystemExit(
        "Term Japanese VRAM mismatch: " +
        " ".join(f"{word:04x}" for word in actual))
PY
printf 'Boots Term Japanese QEMU test: PASS (%s)\n' "$image"
