#!/usr/bin/env bash
set -euo pipefail

# Exercise the GDC safe-mode backend and BMP pipeline in a real BOOT.SYS VM.
repo="$(cd "$(dirname "$0")/.." && pwd)"
qemu="${QEMU:-$repo/qemu-pc98/build/qemu-system-i386}"
bios_dir="${PC98_BIOS_DIR:-$repo/qemu-pc98/roms/pc98bios}"
machine="${BOOT98_BEUI_MACHINE:-pc9801}"
expected_height="${BOOT98_BEUI_EXPECT_HEIGHT:-400}"
backend_name="${BOOT98_BEUI_BACKEND_NAME:-GDC}"
test_tag="${BOOT98_BEUI_TEST_TAG:-gdc}"
base="$repo/build/releases/linux-pc98-i386sx-busybox-ide.img"
work="$repo/build/tests/boot98-beui-g2a-$test_tag"
image="$work/g2a-ide.raw"
files="$work/files"
cfg="$work/BOOT.CFG"
monitor="$work/monitor.sock"
screenshot="$work/g2a.ppm"

test -x "$qemu" || { echo "QEMU not found: $qemu" >&2; exit 1; }
test -d "$bios_dir" || { echo "PC-98 BIOS directory not found: $bios_dir" >&2; exit 1; }
test -f "$base" || { echo "G2a source image not found: $base" >&2; exit 1; }
for command in mtype python3; do
	command -v "$command" >/dev/null || { echo "$command is required" >&2; exit 1; }
done

mkdir -p "$work" "$files"
cp --reflink=auto "$base" "$image"
cat > "$files/G2A.NCT" <<'EOF'
func main() {
    if (BeUI.init() != 1) { return 1; }
    if (BeUI.fill(0, 0, 640, 400, 255) != 1) { return 2; }
    if (BeUI.line(0, 0, 639, 399, 16777215) != 1) { return 3; }
    if (BeUI.patternFill(20, 20, 80, 80, 16711680,
                         6172840429334713770L) != 1) { return 4; }
    var image = BeUI.loadImage("G2A.BMP");
    if (BeUI.drawImage(image, 280, 160) != 1) { return 5; }
    if (BeUI.drawImagePattern(image, 120, 160,
                              6172840429334713770L) != 1) { return 6; }
    BeUI.flush();
    Keyboard.read();
    BeUI.destroyImage(image);
    BeUI.close();
    FileUtil.writeText("G2A.TXT", "BEUI GDC BMP PASS");
    return 0;
}
EOF
printf 'g2a\nhalt\n' > "$cfg"

# 80x80, uncompressed, 8-bit indexed BMP. Four colors make display failure
# distinguishable from a valid all-black screenshot without PNG/zlib tools.
python3 - "$files/G2A.BMP" <<'PY'
import struct
import sys

width = height = 80
stride = (width + 3) & ~3
palette = [
    (0, 0, 0, 0),
    (0, 0, 255, 0),
    (0, 255, 0, 0),
    (255, 255, 255, 0),
]
offset = 14 + 40 + len(palette) * 4
pixels = bytearray(stride * height)
for y in range(height):
    for x in range(width):
        pixels[(height - 1 - y) * stride + x] = 1 + (x >= 40) + (y >= 40)
header = struct.pack('<2sIHHI', b'BM', offset + len(pixels), 0, 0, offset)
dib = struct.pack('<IiiHHIIiiII', 40, width, height, 1, 8, 0,
                  len(pixels), 0, 0, len(palette), len(palette))
with open(sys.argv[1], 'wb') as stream:
    stream.write(header)
    stream.write(dib)
    for entry in palette:
        stream.write(bytes(entry))
    stream.write(pixels)
PY

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

rm -f -- "$monitor" "$screenshot"
"$qemu" -M "$machine" -cpu 386 -m 6 -accel tcg -L "$bios_dir" \
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

qmp('qmp_capabilities')
time.sleep(7)
qmp('screendump', {'filename': sys.argv[2]})
qmp('input-send-event', {'events': [
    {'type': 'key', 'data': {'down': True,
      'key': {'type': 'qcode', 'data': 'ret'}}},
    {'type': 'key', 'data': {'down': False,
      'key': {'type': 'qcode', 'data': 'ret'}}},
]})
time.sleep(3)
qmp('quit', wait_reply=False)
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
	# A halted guest may not complete the asynchronous QMP quit request.
	# Terminate only the QEMU process started by this test.
	kill "$qemu_pid"
fi
wait "$qemu_pid" 2>/dev/null || true
trap - EXIT INT TERM
rm -f -- "$monitor"

actual="$(mtype -i "$image@@$offset" ::G2A.TXT)"
test "$actual" = 'BEUI GDC BMP PASS' || {
	echo "G2a marker mismatch: '$actual'" >&2
	exit 1
}
python3 - "$screenshot" "$expected_height" <<'PY'
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
expected_height = int(sys.argv[2])
if width < 640 or height < expected_height or len(colors) < 3:
    raise SystemExit(f'display screenshot validation failed: {width}x{height}, {len(colors)} colors')
PY
printf 'Boots BeUI %s/BMP QEMU test: PASS (%s)\n' "$backend_name" "$image"
