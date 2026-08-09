#!/usr/bin/env python3
import os
import struct
import subprocess
import sys
import tempfile

out, stage = sys.argv[1], sys.argv[2]
size_mb = int(sys.argv[3]) if len(sys.argv) > 3 else 16
fstype = sys.argv[4] if len(sys.argv) > 4 else "ext4"

if fstype not in ("ext2", "ext4"):
    raise SystemExit(f"unsupported filesystem: {fstype}")

HEADERSIZE = 0x1000
SECSIZE, SECTORS, SURFACES = 512, 32, 8
CYLSIZE = SECSIZE * SECTORS * SURFACES

cylinders = (size_mb * 1024 * 1024) // CYLSIZE
hddsize = cylinders * CYLSIZE

with tempfile.NamedTemporaryFile(suffix=f".{fstype}", delete=False) as tf:
    raw = tf.name
    tf.truncate(hddsize)

mkfs = [
    "mke2fs", "-q", "-F", "-t", fstype, "-b", "1024",
    "-E", "lazy_itable_init=0,lazy_journal_init=0",
    "-d", stage, raw,
]
subprocess.run(mkfs, check=True)

hdr = struct.pack(
    "<8I", 0, 0x10, HEADERSIZE, hddsize, SECSIZE, SECTORS, SURFACES, cylinders
)
os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
with open(out, "wb") as f:
    f.write(hdr)
    f.write(b"\0" * (HEADERSIZE - len(hdr)))
    with open(raw, "rb") as r:
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            f.write(chunk)
os.unlink(raw)

print(
    f"wrote {out}: {os.path.getsize(out)} bytes, {cylinders} cylinders, "
    f"{fstype} root of {size_mb} MB from {stage}"
)
