#!/usr/bin/env python3

"""Patch and verify the compact BOOT98 Stage 2 header."""
import pathlib
import struct
import sys

path = pathlib.Path(sys.argv[1])
image = bytearray(path.read_bytes())
max_image_size = 256 * 1024

if len(image) < 20 or image[:4] != b"B98S":
    raise SystemExit("invalid BOOT98 Stage 2 image")

if len(image) > max_image_size:
    raise SystemExit("BOOT98 Stage 2 image exceeds 256 KiB")

version, header_size = struct.unpack_from("<HH", image, 4)
image_size, entry_offset = struct.unpack_from("<II", image, 8)

if version != 1 or header_size != 20 or image_size != len(image):
    raise SystemExit("inconsistent BOOT98 Stage 2 header")

if not header_size <= entry_offset < image_size:
    raise SystemExit("invalid BOOT98 Stage 2 entry offset")

checksum = sum(image[header_size:]) & 0xffffffff

struct.pack_into("<I", image, 16, checksum)

path.write_bytes(image)

print(f"{path}: {len(image)} bytes, payload checksum {checksum:08x}")
