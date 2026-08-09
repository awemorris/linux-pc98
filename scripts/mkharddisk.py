#!/usr/bin/env python3
import os
import struct
import sys

out, ipl, bz = sys.argv[1], sys.argv[2], sys.argv[3]

HEADERSIZE = 0x1000
SECSIZE, SECTORS, SURFACES = 512, 32, 8
CYLSIZE = SECSIZE * SECTORS * SURFACES

with open(ipl, "rb") as f:
    ipl_data = f.read()
assert len(ipl_data) <= 1024, "IPL must fit in the 1024 bytes NP2kai reads at boot"
with open(bz, "rb") as f:
    bz_data = f.read()

data_needed = 1024 + len(bz_data)
cylinders = -(-data_needed // CYLSIZE)
hddsize = cylinders * CYLSIZE

hdr = struct.pack(
    "<8I", 0, 0x10, HEADERSIZE, hddsize, SECSIZE, SECTORS, SURFACES, cylinders
)
assert len(hdr) == 32

os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
with open(out, "wb") as f:
    f.write(hdr)
    f.write(b"\0" * (HEADERSIZE - len(hdr)))
    f.write(ipl_data)
    f.write(b"\0" * (1024 - len(ipl_data)))
    f.write(bz_data)
    total = HEADERSIZE + hddsize
    f.write(b"\0" * (total - f.tell()))

print(
    f"wrote {out}: {os.path.getsize(out)} bytes, {cylinders} cylinders, "
    f"IPL {len(ipl_data)}B, bzImage {len(bz_data)}B (LBA2), "
    f"pm-kernel offset in image = 0x1400"
)
