#!/usr/bin/env bash
set -euo pipefail

# Boot the bytecode-only Remacs bundle, edit and save a FAT16 file, return to
# Boots, and verify the full-screen mode line.  QMP qcodes cannot currently
# synthesize PC-98 C-x reliably, so the test invokes the same commands through
# M-x (ESC x) instead.
repo="$(cd "$(dirname "$0")/.." && pwd)"
qemu="${QEMU:-$repo/qemu-pc98/build/qemu-system-i386}"
bios_dir="${PC98_BIOS_DIR:-$repo/qemu-pc98/roms/pc98bios}"
base="${BOOT98_TEST_BASE:-$repo/build/releases/linux-pc98-i386sx-busybox-ide.img}"
work="$repo/build/tests/boots-remacs"
image="$work/remacs-qemu.raw"
cfg="$work/BOOT.CFG"
monitor="$work/monitor.sock"
screenshot="$work/remacs.ppm"
memory_mib="${BOOT98_TEST_MEMORY_MIB:-16}"

test -x "$qemu" || { echo "QEMU not found: $qemu" >&2; exit 1; }
test -d "$bios_dir" || { echo "PC-98 BIOS directory not found: $bios_dir" >&2; exit 1; }
test -f "$base" || { echo "Remacs source image not found: $base" >&2; exit 1; }
for command in mtype python3; do
	command -v "$command" >/dev/null || { echo "$command is required" >&2; exit 1; }
done

mkdir -p "$work"
cp --reflink=auto "$base" "$image"
printf 'emacs EDIT.TXT\nhalt\n' > "$cfg"
make -C "$repo/bootloader" -j"$(nproc)" BOOT.SYS
"$repo/scripts/build-remacs-bytecode.sh"
DISK_HEADS=8 DISK_SECTORS=17 \
	"$repo/scripts/install-boot98-image.sh" "$image" "" "$cfg"

offset="$(python3 - "$image" <<'PY'
import struct
import sys

with open(sys.argv[1], "rb") as stream:
    stream.seek(512)
    table = stream.read(512)
for offset in range(0, 512, 32):
    entry = table[offset:offset + 32]
    if entry[0] and entry[16:32] == b"BOOT".ljust(16, b" "):
        cylinder = struct.unpack_from("<H", entry, 6)[0]
        print(cylinder * 8 * 17 * 512)
        break
else:
    raise SystemExit("BOOT partition not found")
PY
)"

rm -f -- "$monitor"
"$qemu" -M pc9801 -cpu 386 -m "$memory_mib" -accel tcg -L "$bios_dir" \
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

python3 - "$monitor" "$screenshot" <<'PY'
import json
import socket
import sys
import time

path, screenshot = sys.argv[1:]
client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
deadline = time.monotonic() + 20
while True:
    try:
        client.connect(path)
        break
    except (FileNotFoundError, ConnectionRefusedError):
        if time.monotonic() >= deadline:
            raise SystemExit("QEMU monitor did not become ready")
        time.sleep(0.1)

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

qmp("qmp_capabilities")

def event(key, down):
    qmp("input-send-event", {"events": [{
        "type": "key",
        "data": {"down": down, "key": {"type": "qcode", "data": key}},
    }]})
    time.sleep(0.025)

def press(key, modifier=None):
    if modifier:
        event(modifier, True)
    event(key, True)
    event(key, False)
    if modifier:
        event(modifier, False)

def type_text(text):
    named = {"-": "minus", "_": "minus", ".": "dot", "/": "slash"}
    for char in text:
        key = named.get(char, char.lower())
        modifier = "shift" if char.isupper() or char == "_" else None
        press(key, modifier)

def command(name):
    press("esc")
    press("x")
    type_text(name)
    press("ret")
    time.sleep(0.5)

# Compatible BIOS POST + device scan + three-second automatic menu timeout,
# followed by bytecode loading on an emulated 386.
time.sleep(20)
type_text("boots")
command("save-buffer")
time.sleep(1)
qmp("screendump", {"filename": screenshot})
command("save-buffers-kill-terminal")
time.sleep(1)
qmp("quit", wait_reply=False)
stream.close()
client.close()
PY

for _ in $(seq 1 50); do
	if ! kill -0 "$qemu_pid" 2>/dev/null; then
		break
	fi
	sleep 0.1
done
if kill -0 "$qemu_pid" 2>/dev/null; then
	kill "$qemu_pid"
fi
wait "$qemu_pid" 2>/dev/null || true
trap - EXIT INT TERM
rm -f -- "$monitor"

mdir -i "$image@@$offset" ::CMD/REMACS.NB >/dev/null
saved="$(mtype -i "$image@@$offset" ::EDIT.TXT)"
test "$saved" = boots || {
	printf 'Remacs FAT16 save mismatch: expected "boots", got "%s"\n' \
		"$saved" >&2
	exit 1
}
python3 - "$screenshot" <<'PY'
import sys

data = open(sys.argv[1], "rb").read()
if not data.startswith(b"P6\n"):
    raise SystemExit("QEMU screendump is not binary PPM")
parts = data.split(b"\n", 3)
width, height = map(int, parts[1].split())
pixels = parts[3]
if len(pixels) != width * height * 3:
    raise SystemExit("short QEMU screendump")
# Remacs paints a reverse-video mode line across almost the complete screen.
for row in range(height * 2 // 3, height):
    line = pixels[row * width * 3:(row + 1) * width * 3]
    bright = sum(1 for column in range(width)
                 if min(line[column * 3:column * 3 + 3]) >= 200)
    if bright >= width * 3 // 4:
        break
else:
    raise SystemExit("Remacs reverse-video mode line was not detected")
PY
printf 'Boots Remacs bytecode QEMU launch test: PASS (%s MiB, %s)\n' \
	"$memory_mib" "$image"
