#!/usr/bin/env python3
import binascii
import struct
import sys

path = sys.argv[1]
data = bytearray(open(path, "rb").read())

if len(data) < 36 or data[:4] != b"B98A":
    raise SystemExit("invalid BOOT98 applet")

if struct.unpack_from("<I", data, 8)[0] != len(data):
    raise SystemExit("inconsistent applet size")

data[16:20] = b"\0\0\0\0"

struct.pack_into("<I", data, 16, binascii.crc32(data) & 0xffffffff)

open(path, "wb").write(data)

print(f"{path}: {len(data)} bytes, crc32 {struct.unpack_from('<I', data, 16)[0]:08x}")
