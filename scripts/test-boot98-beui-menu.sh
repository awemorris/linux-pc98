#!/usr/bin/env bash
set -euo pipefail

# Exercise the PC-98 CGROM glyph path and a keyboard-only graphical menu.
repo="$(cd "$(dirname "$0")/.." && pwd)"
qemu="${QEMU:-$repo/qemu-pc98/build/qemu-system-i386}"
bios_dir="${PC98_BIOS_DIR:-$repo/qemu-pc98/roms/pc98bios}"
machine="${BOOT98_BEUI_MACHINE:-pc9821}"
tag="${BOOT98_BEUI_TEST_TAG:-menu-cirrus}"
minimum_colors="${BOOT98_BEUI_MINIMUM_COLORS:-4}"
initial_wait="${BOOT98_BEUI_INITIAL_WAIT:-15}"
selection_wait="${BOOT98_BEUI_SELECTION_WAIT:-10}"
completion_wait="${BOOT98_BEUI_COMPLETION_WAIT:-10}"
if [[ "$machine" == pc9801 && -z "${BOOT98_BEUI_MINIMUM_COLORS+x}" ]]; then
	minimum_colors=3
	initial_wait="${BOOT98_BEUI_INITIAL_WAIT:-25}"
	selection_wait="${BOOT98_BEUI_SELECTION_WAIT:-25}"
fi
base="$repo/build/releases/linux-pc98-i386sx-busybox-ide.img"
work="$repo/build/tests/boot98-beui-$tag"
image="$work/menu.raw"
files="$work/files"
cfg="$work/BOOT.CFG"
monitor="$work/monitor.sock"
screenshot="$work/menu.ppm"
qemu_log="$work/qemu.log"

test -x "$qemu" || { echo "QEMU not found: $qemu" >&2; exit 1; }
test -d "$bios_dir" || { echo "PC-98 BIOS directory not found: $bios_dir" >&2; exit 1; }
test -f "$base" || { echo "source image not found: $base" >&2; exit 1; }
for command in mtype python3; do
	command -v "$command" >/dev/null || { echo "$command is required" >&2; exit 1; }
done

mkdir -p "$work" "$files"
cp --reflink=auto "$base" "$image"
cat > "$files/G3MENU.NCT" <<'EOF'
func row(y, text, active) {
    if (active == 1) {
        BeUI.fill(190, y - 8, 414, 36, 736602);
        BeUI.line(190, y - 8, 603, y - 8, 2857715);
        BeUI.line(190, y + 27, 603, y + 27, 2857715);
        BeUI.line(190, y - 8, 190, y + 27, 2857715);
        BeUI.line(603, y - 8, 603, y + 27, 2857715);
        BeUI.drawText(">", 204, y, 2857715, 736602);
        BeUI.drawText(text, 228, y, 16777215, 736602);
    } else {
        BeUI.fill(190, y - 8, 414, 36, 1842979);
        BeUI.drawText(" ", 204, y, 10069165, 1842979);
        BeUI.drawText(text, 228, y, 12698049, 1842979);
    }
}

func menu(selected) {
    var height = BeUI.getHeight();
    BeUI.fill(0, 0, 640, height, 1053204);

    BeUI.fill(0, 0, 640, 42, 1382680);
    BeUI.fill(0, 40, 640, 2, 47062);
    BeUI.drawText("Boots", 18, 12, 16777215, 1382680);
    BeUI.drawText("Boot loader and pre-boot environment", 190, 12,
                  10069165, 1382680);

    BeUI.fill(0, 42, 164, height - 42, 4737109);
    BeUI.drawText("SYSTEM", 16, 68, 6252390, 4737109);
    BeUI.drawText("Overview", 24, 100, 10069165, 4737109);
    BeUI.drawText("Storage", 24, 132, 10069165, 4737109);
    BeUI.drawText("Networking", 24, 164, 10069165, 4737109);
    BeUI.fill(0, 194, 164, 34, 1053204);
    BeUI.fill(0, 194, 3, 34, 2857715);
    BeUI.drawText("Boot menu", 24, 203, 16777215, 1053204);
    BeUI.drawText("TOOLS", 16, 246, 6252390, 4737109);
    BeUI.drawText("Emacs", 24, 274, 10069165, 4737109);
    BeUI.drawText("Noct shell", 24, 306, 10069165, 4737109);
    BeUI.drawText("Terminal", 24, 338, 10069165, 4737109);

    BeUI.drawText("ブートストラップ環境", 190, 66,
                  16777215, 1053204);
    BeUI.drawText("起動する項目を選択してください", 190, 94,
                  10069165, 1053204);
    BeUI.line(190, 118, 603, 118, 3224893);
    row(142, "Linux を起動", selected == 0);
    row(186, "シェルに戻る", selected == 1);
    row(230, "再起動", selected == 2);

    BeUI.fill(164, height - 42, 476, 42, 1382680);
    BeUI.drawText("↑ ↓", 190, height - 29, 2857715, 1382680);
    BeUI.drawText(": 選択", 230, height - 29, 10069165, 1382680);
    BeUI.drawText("Enter", 338, height - 29, 2857715, 1382680);
    BeUI.drawText(": 決定", 386, height - 29, 10069165, 1382680);
    BeUI.flush();
}

func main() {
    FileUtil.writeText("G3TRACE.TXT", "start");
    if (BeUI.init() != 1) {
        FileUtil.writeText("G3TRACE.TXT", "init failed");
        return 1;
    }
    FileUtil.writeText("G3TRACE.TXT", "init ok");
    if (BeUI.textWidth("日本語") != 48 ||
        BeUI.textHeight("日本語") != 16) {
        FileUtil.writeText("G3TRACE.TXT", "metrics failed");
        return 2;
    }
    FileUtil.writeText("G3TRACE.TXT", "drawing");
    BeUI.fill(0, 0, BeUI.getWidth(), BeUI.getHeight(), 0);
    var selected = 0;
    menu(selected);
    FileUtil.writeText("G3TRACE.TXT", "waiting first key");
    var key = Keyboard.read();
    if (key == Key.Down) { selected = 1; }
    if (key == Key.Up) { selected = 2; }
    menu(selected);
    FileUtil.writeText("G3TRACE.TXT", "waiting enter");
    key = Keyboard.read();
    if (key != Key.Enter) { return 3; }
    BeUI.close();
    FileUtil.writeText("G3MENU.TXT", "BEUI GLYPH MENU " + selected);
    return 0;
}
EOF
printf 'g3menu\nhalt\n' > "$cfg"

make -C "$repo/bootloader" -j"$(nproc)" BOOT.SYS
BOOT98_FILES="$files" DISK_HEADS=8 DISK_SECTORS=17 \
	"$repo/scripts/install-boot98-image.sh" "$image" "" "$cfg"

offset="$(python3 - "$image" <<'PY'
import struct
import sys

with open(sys.argv[1], 'rb') as stream:
    stream.seek(512)
    table = stream.read(512)
for pos in range(0, 512, 32):
    entry = table[pos:pos + 32]
    if entry[0] and entry[16:32] == b'BOOT'.ljust(16, b' '):
        cylinder = struct.unpack_from('<H', entry, 6)[0]
        print(cylinder * 8 * 17 * 512)
        break
else:
    raise SystemExit('BOOT partition not found')
PY
)"

rm -f -- "$monitor" "$screenshot" "$qemu_log"
"$qemu" -M "$machine" -cpu 386 -m 6 -accel tcg -L "$bios_dir" \
	-nic none -drive "if=ide,bus=0,unit=0,format=raw,file=$image" \
	-display none -serial none -qmp "unix:$monitor,server=on,wait=off" \
	-no-reboot >"$qemu_log" 2>&1 &
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

python3 - "$monitor" "$screenshot" "$initial_wait" "$selection_wait" \
    "$completion_wait" <<'PY'
import json
import socket
import sys
import time

client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
deadline = time.monotonic() + 15
while True:
    try:
        client.connect(sys.argv[1])
        break
    except (FileNotFoundError, ConnectionRefusedError):
        if time.monotonic() >= deadline:
            raise SystemExit('QEMU monitor did not become ready')
        time.sleep(.1)
stream = client.makefile('rwb', buffering=0)
json.loads(stream.readline())

def qmp(execute, arguments=None, wait_reply=True):
    request = {'execute': execute}
    if arguments is not None:
        request['arguments'] = arguments
    stream.write(json.dumps(request).encode('ascii') + b'\n')
    if not wait_reply:
        return
    while True:
        reply = json.loads(stream.readline())
        if 'return' in reply:
            return reply['return']
        if 'error' in reply:
            raise SystemExit(f"QMP {execute} failed: {reply['error']}")

def key(qcode):
    qmp('input-send-event', {'events': [
        {'type': 'key', 'data': {'down': True,
          'key': {'type': 'qcode', 'data': qcode}}},
        {'type': 'key', 'data': {'down': False,
          'key': {'type': 'qcode', 'data': qcode}}},
    ]})

qmp('qmp_capabilities')
time.sleep(float(sys.argv[3]))
key('down')
time.sleep(float(sys.argv[4]))
qmp('screendump', {'filename': sys.argv[2]})
key('ret')
time.sleep(float(sys.argv[5]))
qmp('quit', wait_reply=False)
stream.close()
client.close()
PY

for _ in $(seq 1 50); do
	if ! kill -0 "$qemu_pid" 2>/dev/null; then break; fi
	sleep .1
done
if kill -0 "$qemu_pid" 2>/dev/null; then kill "$qemu_pid"; fi
wait "$qemu_pid" 2>/dev/null || true
trap - EXIT INT TERM
rm -f -- "$monitor"

actual="$(mtype -i "$image@@$offset" ::G3MENU.TXT)"
test "$actual" = 'BEUI GLYPH MENU 1' || {
	echo "G3 menu marker mismatch: '$actual'" >&2
	exit 1
}
python3 - "$screenshot" "$minimum_colors" <<'PY'
import sys

with open(sys.argv[1], 'rb') as stream:
    if stream.readline().strip() != b'P6':
        raise SystemExit('QEMU screenshot is not binary PPM')
    line = stream.readline()
    while line.startswith(b'#'):
        line = stream.readline()
    width, height = map(int, line.split())
    if int(stream.readline()) != 255:
        raise SystemExit('unsupported PPM range')
    pixels = stream.read()
colors = {pixels[pos:pos + 3] for pos in range(0, len(pixels), 3)}
minimum_colors = int(sys.argv[2])
if width < 640 or height < 400 or len(colors) < minimum_colors:
    raise SystemExit(f'menu screenshot validation failed: {width}x{height}, {len(colors)} colors')
PY
printf 'Boots BeUI CGROM keyboard menu QEMU test: PASS (%s)\n' "$image"
