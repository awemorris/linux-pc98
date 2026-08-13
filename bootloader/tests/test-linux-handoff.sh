#!/usr/bin/env bash
set -euo pipefail

# Exercise the production GUI menu's default Linux selection, then verify
# that /bin/linux reaches the point-of-no-return handoff.

repo="$(cd "$(dirname "$0")/../.." && pwd)"
build="${ZEDBSD_BUILD_DIR:-$repo/build/bootloader-tests/pc98}"
qemu="${QEMU:-qemu-system-i386}"
bios="${PC98_BIOS_DIR:-$repo/roms/pc98bios}"
base="${ZEDBSD_TEST_BASE:-$repo/build/releases/linux-pc98-i386sx-busybox-ide.img}"
work="$build/tests/linux-handoff"
image="$work/linux-handoff.img"
monitor="$work/monitor.sock"

test -x "$qemu" || { echo "QEMU not found: $qemu" >&2; exit 1; }
test -d "$bios" || { echo "BIOS directory not found: $bios" >&2; exit 1; }
test -f "$base" || { echo "Linux image not found: $base" >&2; exit 1; }
command -v mdel >/dev/null || { echo "mdel is required" >&2; exit 1; }
mkdir -p "$work"
cp --reflink=auto "$base" "$image"
"$repo/bootloader/install-fs.sh" --partition 1 "$image"
offset="$(python3 - "$image" <<'PY'
import struct, sys
heads = 8
with open(sys.argv[1], "rb") as stream:
    stream.seek(512)
    table = stream.read(512)
for pos in range(0, 512, 32):
    entry = table[pos:pos + 32]
    if entry[0] and entry[16:32] == b"BOOT".ljust(16, b" "):
        print(struct.unpack_from("<H", entry, 6)[0] * heads * 17 * 512)
        break
else:
    raise SystemExit("BOOT partition not found")
PY
)"

# Deliberately remove the legacy script from the canonical image copy. If the
# production menu regresses to `source /boot.cfg`, this test must stop at the
# shell instead of accidentally demonstrating the obsolete fallback path.
mdel -i "$image@@$offset" ::BOOT.CFG 2>/dev/null || true

rm -f -- "$monitor"
"$qemu" -M pc9821 -cpu 486 -m 64 -accel tcg -L "$bios" -nic none \
	-drive "if=ide,bus=0,unit=0,format=raw,file=$image" -snapshot \
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

python3 - "$monitor" "$work/linux-menu.ppm" "$work/linux-tvram.bin" <<'PY'
import json
import pathlib
import re
import socket
import sys
import time

path, screenshot, tvram = sys.argv[1:]
client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
deadline = time.monotonic() + 20
while True:
    try:
        client.connect(path)
        break
    except (FileNotFoundError, ConnectionRefusedError):
        if time.monotonic() >= deadline:
            raise SystemExit("QMP monitor did not become ready")
        time.sleep(.1)
stream = client.makefile("rwb", buffering=0)
json.loads(stream.readline())

def qmp(execute, arguments=None, reply=True):
    request = {"execute": execute}
    if arguments is not None:
        request["arguments"] = arguments
    stream.write(json.dumps(request).encode("ascii") + b"\n")
    if not reply:
        return None
    while True:
        result = json.loads(stream.readline())
        if "return" in result:
            return result["return"]
        if "error" in result:
            raise SystemExit(f"QMP {execute} failed: {result['error']}")

def registers():
    return qmp("human-monitor-command", {"command-line": "info registers"})

def key(qcode):
    qmp("input-send-event", {"events": [
        {"type": "key", "data": {"down": True,
          "key": {"type": "qcode", "data": qcode}}},
        {"type": "key", "data": {"down": False,
          "key": {"type": "qcode", "data": qcode}}},
    ]})

qmp("qmp_capabilities")

# Compatible BIOS POST, the one-second zinit delay, and menu/BMP loading.
time.sleep(20)
qmp("screendump", {"filename": screenshot})
screen = pathlib.Path(screenshot).read_bytes()
header = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", screen)
if header is None or int(header.group(1)) != 640 or int(header.group(2)) != 480:
    raise SystemExit("production GUI menu was not displayed at 640x480")
pixels = screen[header.end():]
colors = {pixels[index:index + 3] for index in range(0, len(pixels), 3)}
if len(colors) < 16:
    raise SystemExit(f"production GUI menu has only {len(colors)} colors")
print(f"production GUI menu observed with {len(colors)} colors")

# Linux is the initially selected row. Enter must return a /bin/linux action
# to the shell; no BOOT.CFG is installed in this test image.
key("ret")

# A successful handoff leaves zedBSD' 0x8002xxxx low-loader execution range.
# Linux may quickly enable its own high mapping, so accept either a physical
# kernel entry or the normal 0xc0000000 kernel half.
deadline = time.monotonic() + 35
last = ""
handoff = None
while time.monotonic() < deadline:
    last = registers()
    match = re.search(r"EIP=([0-9A-Fa-f]{8})", last)
    if match:
        eip = int(match.group(1), 16)
        if (0x00100000 <= eip < 0x80000000) or eip >= 0xc0000000:
            print(f"Linux handoff observed at EIP=0x{eip:08x}")
            handoff = eip
            break
    time.sleep(1)
if handoff is None:
    print(last, file=sys.stderr)
    qmp("screendump", {"filename": screenshot})
    raise SystemExit("Linux never left the zedBSD loader execution range")

# BusyBox init prints this marker after mounting proc/sysfs and enabling the
# Linux swap partition. PC-98 text VRAM stores one character per little-endian
# word at physical 0xa0000, so seeing it proves Linux reached userspace.
deadline = time.monotonic() + 45
while time.monotonic() < deadline:
    qmp("pmemsave", {"val": 0xa0000, "size": 0x2000,
                     "filename": tvram})
    raw = pathlib.Path(tvram).read_bytes()
    text = raw[0::2]
    if b"I386-BUSYBOX-SUCCESS" in text:
        print("Linux BusyBox init and shell startup observed")
        stream.close()
        client.close()
        raise SystemExit(0)
    time.sleep(1)
qmp("screendump", {"filename": screenshot})
raise SystemExit("Linux handoff succeeded but BusyBox init marker was absent")
PY

kill "$qemu_pid" 2>/dev/null || true
wait "$qemu_pid" 2>/dev/null || true
trap - EXIT INT TERM
rm -f -- "$monitor"
echo "zedBSD Linux point-of-no-return QEMU test: PASS"
