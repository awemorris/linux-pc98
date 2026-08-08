#!/usr/bin/env bash
set -euo pipefail

# Exercise the interactive Noct REPL through the emulated PC-98 keyboard.
# The release image is copied before BOOT.SYS or FAT16 contents are changed.
repo="$(cd "$(dirname "$0")/.." && pwd)"
qemu="${QEMU:-$repo/qemu-pc98/build/qemu-system-i386}"
bios_dir="${PC98_BIOS_DIR:-$repo/qemu-pc98/roms/pc98bios}"
base="$repo/build/releases/linux-pc98-i386sx-busybox-ide.img"
work="$repo/build/tests/boot98-m15-repl"
image="$work/m15-ide.raw"
files="$work/files"
cfg="$work/BOOT.CFG"
monitor="$work/monitor.sock"
screenshot="$work/m15.ppm"
shift_test="${BOOT98_REPL_SHIFT_TEST:-0}"
memory_mib="${BOOT98_TEST_MEMORY_MIB:-6}"

test -x "$qemu" || { echo "QEMU not found: $qemu" >&2; exit 1; }
test -d "$bios_dir" || {
	echo "PC-98 BIOS directory not found: $bios_dir" >&2
	exit 1
}
test -f "$base" || { echo "M15 source image not found: $base" >&2; exit 1; }
for command in mtype python3; do
	command -v "$command" >/dev/null || {
		echo "$command is required" >&2
		exit 1
	}
done

mkdir -p "$work" "$files"
cp --reflink=auto "$base" "$image"
printf 'noct\nm15post\nhalt\n' > "$cfg"
printf '%s\n' \
	'func main() {' \
	'    FileUtil.writeText("M15POST.TXT", "SHELL");' \
	'    return 0;' \
	'}' > "$files/M15POST.NCT"
make -C "$repo/bootloader" -j"$(nproc)" BOOT.SYS
BOOT98_FILES="$files" DISK_HEADS=8 DISK_SECTORS=17 \
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
	-display none -serial none \
	-qmp "unix:$monitor,server=on,wait=off" -no-reboot \
	>/dev/null 2>&1 &
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

python3 - "$monitor" "$screenshot" "$shift_test" <<'PY'
import json
import socket
import sys
import time

path = sys.argv[1]
screenshot = sys.argv[2]
shift_test = sys.argv[3] == "1"
client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
deadline = time.monotonic() + 15
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

def key_event(key, down):
    qmp("input-send-event", {"events": [{
        "type": "key",
        "data": {
            "down": down,
            "key": {"type": "qcode", "data": key},
        },
    }]})
    # PC-98 serial keyboard bytes are intentionally delivered several ms
    # apart. Keep modifiers asserted across distinct make/break events.
    time.sleep(0.025)

def press(key, modifier=None):
    if modifier is not None:
        key_event(modifier, True)
    key_event(key, True)
    key_event(key, False)
    if modifier is not None:
        key_event(modifier, False)

shifted = {
    '(': 'shift-9', ')': 'shift-0', '{': 'shift-bracket_left',
    '}': 'shift-bracket_right', '"': 'shift-apostrophe',
    '_': 'shift-minus', ':': 'shift-semicolon',
}
plain = {
    ' ': 'spc', '.': 'dot', ',': 'comma', ';': 'semicolon',
    '=': 'equal', '-': 'minus', '/': 'slash', '*': 'asterisk',
    "'": 'apostrophe',
}

def type_text(text):
    for char in text:
        if 'a' <= char <= 'z' or '0' <= char <= '9':
            key = char
            modifier = None
        elif 'A' <= char <= 'Z':
            key = char.lower()
            modifier = 'shift'
        elif char in shifted:
            modifier, key = shifted[char].split('-', 1)
        elif char in plain:
            key = plain[char]
            modifier = None
        else:
            raise SystemExit(f"no QEMU key mapping for {char!r}")
        press(key, modifier)

# Let the compatible BIOS and BOOT.CFG reach the argument-free `noct` command.
time.sleep(6)
if shift_test:
    # A real PC-98 BIOS applies the injected Shift state, so exercise the
    # complete language and native File API path when those ROMs are present.
    lines = [
        "func mark()",
        "{",
        'FileUtil.writeText("M15.TXT","MARK");',
        "}",
        "mark()",
        "var = 1",
        'FileUtil.writeText("M15REC.TXT","RECOVERED")',
    ]
else:
    # QMP qcodes bypass the PC-98 keysym remapper, and the compatible BIOS
    # keyboard service currently does not apply injected Shift state. The
    # host lifecycle test covers multiline functions and File API calls.
    lines = [
        "1",
        "var",
        "1",
    ]
for line in lines:
    type_text(line)
    press("ret")
    time.sleep(0.35)

# Exit the REPL. BOOT.CFG must resume and execute M15POST.NCT before halt.
press("c", "ctrl")
time.sleep(4)
qmp("screendump", {"filename": screenshot})
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
	# A guest halted by the final BOOT.CFG command can stop servicing the
	# asynchronous QMP quit request. The FAT marker below is the authoritative
	# completion check, so terminate only this test-owned QEMU after the grace
	# period.
	kill "$qemu_pid"
fi
wait "$qemu_pid" 2>/dev/null || true
trap - EXIT INT TERM
rm -f -- "$monitor"

if test "$shift_test" = 1; then
	test "$(mtype -i "$image@@$offset" ::M15.TXT)" = "MARK" || {
		echo "M15 multiline/function marker is missing" >&2
		exit 1
	}
	test "$(mtype -i "$image@@$offset" ::M15REC.TXT)" = "RECOVERED" || {
		echo "M15 syntax-error recovery marker is missing" >&2
		exit 1
	}
fi
test "$(mtype -i "$image@@$offset" ::M15POST.TXT)" = "SHELL" || {
	echo "M15 Ctrl-C did not return to BOOT.CFG" >&2
	exit 1
}
printf 'M15 Boots Noct REPL QEMU test: PASS (%s)\n' "$image"
