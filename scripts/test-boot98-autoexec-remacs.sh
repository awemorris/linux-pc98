#!/usr/bin/env bash
set -euo pipefail

# Exercise the complete graphical startup path:
# AUTOEXEC.NCT -> BeUI menu -> BOOT_ACTION -> VM teardown -> Remacs.
repo="$(cd "$(dirname "$0")/.." && pwd)"
qemu="${QEMU:-$repo/qemu-pc98/build/qemu-system-i386}"
cpu="${BOOT98_TEST_CPU:-386}"
bios_dir="${PC98_BIOS_DIR:-$repo/qemu-pc98/roms/pc98bios}"
base="${BOOT98_TEST_BASE:-$repo/build/releases/linux-pc98-i386sx-busybox-ide.img}"
work="$repo/build/tests/boots-autoexec-remacs"
image="$work/autoexec-remacs.raw"
files="$work/files"
cfg="$work/BOOT.CFG"
monitor="$work/monitor.sock"
menu_screenshot="$work/autoexec-emacs-menu.ppm"
emacs_screenshot="$work/autoexec-remacs.ppm"
text_vram="$work/autoexec-remacs-text-vram.bin"
single_key_vram="$work/autoexec-remacs-single-key.bin"
wide_cursor_attr="$work/autoexec-remacs-wide-cursor-attr.bin"
qemu_debug="$work/qemu-debug.log"

test -x "$qemu" || { echo "QEMU not found: $qemu" >&2; exit 1; }
test -d "$bios_dir" || { echo "PC-98 BIOS directory not found: $bios_dir" >&2; exit 1; }
test -f "$base" || { echo "source image not found: $base" >&2; exit 1; }

mkdir -p "$work" "$files"
cp --reflink=auto "$base" "$image"
cp "$repo/bootloader/AUTOEXEC.NCT" "$files/AUTOEXEC.NCT"
printf '日本語表示テスト\n' > "$files/EDIT.TXT"
# AUTOEXEC must win.  Reaching this fallback makes the test stop before Emacs.
printf 'halt\n' > "$cfg"

make -C "$repo/bootloader" -j"$(nproc)" BOOT.SYS
"$repo/scripts/build-remacs-bytecode.sh"
BOOT98_FILES="$files" DISK_HEADS=8 DISK_SECTORS=17 \
	"$repo/scripts/install-boot98-image.sh" "$image" "" "$cfg"

offset="$(python3 - "$image" <<'PY'
import struct
import sys

with open(sys.argv[1], "rb") as stream:
    stream.seek(512)
    table = stream.read(512)
for pos in range(0, 512, 32):
    entry = table[pos:pos + 32]
    if entry[0] and entry[16:32] == b"BOOT".ljust(16, b" "):
        cylinder = struct.unpack_from("<H", entry, 6)[0]
        print(cylinder * 8 * 17 * 512)
        break
else:
    raise SystemExit("BOOT partition not found")
PY
)"

# A lowercase-visible HOME entry exercises the complete path used by Remacs:
# FileUtil.getHomeDirectory(), directory enumeration, TAB completion and FAT
# writeback through the /diskN namespace.
: > "$work/COMPLETE.TXT"
mcopy -o -i "$image@@$offset" "$work/COMPLETE.TXT" ::HOME/COMPLETE.TXT

rm -f -- "$monitor" "$menu_screenshot" "$emacs_screenshot" "$text_vram" \
	"$single_key_vram" "$wide_cursor_attr" "$qemu_debug"
"$qemu" -M pc9821 -cpu "$cpu" -m 16 -accel tcg -L "$bios_dir" \
	-nic none -drive "if=ide,bus=0,unit=0,format=raw,file=$image" \
	-display none -serial none -qmp "unix:$monitor,server=on,wait=off" \
	-no-reboot -d guest_errors,int -D "$qemu_debug" >/dev/null 2>&1 &
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

if ! python3 - "$monitor" "$menu_screenshot" "$emacs_screenshot" \
	"$text_vram" "$single_key_vram" "$wide_cursor_attr" <<'PY'
import json
import socket
import sys
import time

monitor, menu_screenshot, emacs_screenshot, text_vram, single_key_vram, wide_cursor_attr = sys.argv[1:]
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

def event(key, down):
    qmp("input-send-event", {"events": [{
        "type": "key",
        "data": {"down": down, "key": {"type": "qcode", "data": key}},
    }]})
    time.sleep(.025)

def press(key, modifier=None):
    if modifier:
        event(modifier, True)
        # The PC-98 BIOS exposes the current modifier state in its work
        # area.  Hold a synthetic modifier long enough for TCG and the ROM
        # IRQ handler to publish that state before the printable make code.
        time.sleep(.25)
    event(key, True)
    event(key, False)
    if modifier:
        # Keep the modifier asserted until a deliberately slow i386 TCG
        # guest has consumed the printable make code and sampled the BIOS
        # work-area shift byte.
        time.sleep(1)
        event(modifier, False)
        time.sleep(.25)

def type_text(text):
    named = {"-": "minus", "_": "minus", ".": "dot", "/": "slash"}
    for char in text:
        key = named.get(char, char.lower())
        modifier = "shift" if char.isupper() or char == "_" else None
        press(key, modifier)

def emacs_command(name):
    # Keep this portable full-system workflow independent of how a particular
    # BIOS queues Graph make/break events.  The genuine-ROM AX=2a81h Graph-x
    # conversion is covered by the host vector and the real-ROM QMP test.
    press("esc")
    press("x")
    type_text(name)
    press("ret")
    time.sleep(.5)

qmp("qmp_capabilities")
# BIOS, fixed-disk probing, then the one-second automatic timeout.
time.sleep(20)
press("down")
# CGROM drawing on a 386/GDC-compatible path is deliberately synchronous and
# can take several seconds.  Do not send Enter while the selection redraw is
# still consuming the display hardware.
time.sleep(10)
qmp("screendump", {"filename": menu_screenshot})
press("ret")
# AUTOEXEC returns, BOOT_ACTION is consumed, and the 386 JIT initializes the
# complete Remacs bytecode.  Keep this generous enough for TCG and real 386s.
time.sleep(45)
# One ordinary key must be redrawn without waiting indefinitely for a second
# key.  This catches a finite Term.readKey(20) being mapped to BIOS blocking
# input, which made interactive Remacs appear several seconds behind.
# Modifier-only make events are covered deterministically by the host test.
# Keep this full-system path representative of human input rather than
# queueing three synthetic modifiers ahead of the text being verified.
# Exercise the compatible BIOS IRQ-path Shift conversion, not only the
# modifier filtering in the 32-bit Term adapter.
press("b", modifier="shift")
time.sleep(.5)
qmp("pmemsave", {"val": 0xa0000, "size": 2,
                  "filename": single_key_vram})
type_text("oots")
time.sleep(.5)
# The cursor now precedes the first Japanese glyph at row 0, column 5.  PC-98
# hardware cursor width is one cell, so Boots must reverse both attribute cells.
qmp("pmemsave", {"val": 0xa2000 + 5 * 2, "size": 4,
                  "filename": wide_cursor_attr})
emacs_command("save-buffer")
time.sleep(1)
qmp("screendump", {"filename": emacs_screenshot})
qmp("pmemsave", {"val": 0xa0000, "size": 0x4000,
                  "filename": text_vram})
# Remacs must traverse the /disk1 namespace, enumerate the FAT directory with
# lowercase names, accept a genuine Tab key, and open complete.txt.  HOME is
# verified separately through the startup-file path because QMP cannot express
# the PC-98 keyboard's tilde key portably.
emacs_command("find-file")
type_text("/disk1/home/comp")
press("tab")
press("ret")
time.sleep(.5)
type_text("tabok")
emacs_command("save-buffer")
time.sleep(.5)
emacs_command("save-buffers-kill-terminal")
time.sleep(1)
qmp("quit", wait_reply=False)
stream.close()
client.close()
PY
then
	tail -100 "$qemu_debug" >&2 || true
	exit 1
fi

for _ in $(seq 1 50); do
	if ! kill -0 "$qemu_pid" 2>/dev/null; then break; fi
	sleep .1
done
if kill -0 "$qemu_pid" 2>/dev/null; then kill "$qemu_pid"; fi
wait "$qemu_pid" 2>/dev/null || true
trap - EXIT INT TERM
rm -f -- "$monitor"

rm -f -- "$work/EDIT-SAVED.TXT" "$work/COMPLETE-SAVED.TXT"
mcopy -i "$image@@$offset" ::EDIT.TXT "$work/EDIT-SAVED.TXT"
mcopy -i "$image@@$offset" ::HOME/COMPLETE.TXT "$work/COMPLETE-SAVED.TXT"
python3 - "$work/EDIT-SAVED.TXT" "$work/COMPLETE-SAVED.TXT" \
	"$menu_screenshot" "$emacs_screenshot" "$single_key_vram" \
	"$wide_cursor_attr" <<'PY'
import sys

saved = open(sys.argv[1], "rb").read()
expected = "Boots日本語表示テスト\n".encode()
if saved != expected:
    raise SystemExit(f"Remacs modifier/input mismatch: {saved.hex()}")
completion = open(sys.argv[2], "rb").read()
if completion != b"tabok":
    raise SystemExit(f"Remacs HOME/TAB completion mismatch: {completion!r}")
if open(sys.argv[5], "rb").read(2) != b"B\0":
    raise SystemExit("Remacs did not redraw a single key promptly")
attributes = open(sys.argv[6], "rb").read()
if len(attributes) != 4 or not (attributes[0] & 4 and attributes[2] & 4):
    raise SystemExit(f"Japanese cursor did not cover both cells: {attributes.hex()}")

def ppm(path):
    data = open(path, "rb").read()
    if not data.startswith(b"P6\n"):
        raise SystemExit(f"{path}: not a binary PPM")
    parts = data.split(b"\n", 3)
    width, height = map(int, parts[1].split())
    return width, height, parts[3]

width, height, pixels = ppm(sys.argv[3])
colors = {pixels[pos:pos + 3] for pos in range(0, len(pixels), 3)}
if width < 640 or height < 400 or len(colors) < 4:
    raise SystemExit("AUTOEXEC graphical menu was not captured")

width, height, pixels = ppm(sys.argv[4])
for row in range(height * 2 // 3, height):
    line = pixels[row * width * 3:(row + 1) * width * 3]
    bright = sum(1 for col in range(width)
                 if min(line[col * 3:col * 3 + 3]) >= 200)
    if bright >= width * 3 // 4:
        break
else:
    raise SystemExit("Remacs mode line was not detected")
PY
printf 'Boots AUTOEXEC graphical Emacs path QEMU test: PASS (%s)\n' "$image"
