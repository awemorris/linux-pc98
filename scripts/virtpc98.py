#!/usr/bin/env python3

"""
QEMU PC98 Launcher and Disk Converter
Copyright (C) 2026 Awe Morris

- Opens a window when no arguments specified.
- Sub-command runs on the terminal.
"""

import configparser
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time

# ---------------------------------------------------------------- geometry

SECTOR = 512                    # physical sector on a PC-98 hard disk
SECTORS = 17
HEADS = 8
TRACK = SECTOR * SECTORS * HEADS
HDD_TYPE = 0x28

# MS-DOS uses 1024 byte logical sectors on a PC-98 hard disk
HDD_BPS = 1024
HDD_ROOTENTS = 3072
HDD_MEDIA = 0xF8
RESERVED = 1
NFATS = 2
FAT12_MAX = 4085
FAT16_MAX = 65525
# FAT32 keeps its boot record and FSInfo in reserved sectors, with the
# backup pair at 6 and 7 where the boot record says they are
FAT32_RESERVED = 8

ATTR_DIRECTORY = 0x10
ATTR_VOLUME = 0x08
ATTR_SYSTEM_FILE = 0x27
SYSTEM_ORDER = {"IO.SYS": 0, "MSDOS.SYS": 1}

# name: bps spc reserved nfats rootents media secperfat totsec sectors heads
#       cylinders fdi-type
FDD_FORMATS = {
    "1.2": (1024, 1, 1, 2, 192, 0xFE, 2, 1232, 8, 2, 77, 0x90),
    "1.44": (512, 1, 1, 2, 224, 0xF0, 9, 2880, 18, 2, 80, 0x30),
}

HDD_SIZES = ("40 MB", "80 MB", "160 MB", "320 MB", "640 MB",
             "1.2 GB", "2.1 GB", "4.3 GB")
HDD_MB = {"40 MB": 40, "80 MB": 80, "160 MB": 160, "320 MB": 320,
          "640 MB": 640, "1.2 GB": 1200, "2.1 GB": 2100, "4.3 GB": 4300}


class Geometry:
    """Everything the FAT builder needs to lay a volume out."""

    def __init__(self, bps, spc, reserved, nfats, rootents, media,
                 secperfat, totsec, sectors, heads, hidden=0, bits=12):
        self.bps, self.spc = bps, spc
        self.reserved, self.nfats = reserved, nfats
        self.rootents, self.media = rootents, media
        self.secperfat, self.totsec = secperfat, totsec
        self.sectors, self.heads = sectors, heads
        self.hidden, self.bits = hidden, bits
        self.clustersize = spc * bps
        self.fat_start = reserved * bps
        self.root_start = self.fat_start + nfats * secperfat * bps
        self.data_start = self.root_start + rootents * 32
        self.clusters = (totsec * bps - self.data_start) // self.clustersize

    def bpb(self):
        """Bytes 11..35 of a boot record.

        A volume past 65535 logical sectors reports zero in the 16-bit
        total-sectors field and uses the 32-bit one behind it, which is also
        where the hidden count goes once it stops fitting.
        """
        small = self.totsec if self.totsec <= 0xFFFF else 0
        large = 0 if self.totsec <= 0xFFFF else self.totsec
        if self.bits == 32:
            # FAT32 announces itself with a zero 16 bit FAT size; the real
            # one is in the extended record the boot sector carries
            small, large = 0, self.totsec
        secperfat = self.secperfat if self.bits != 32 else 0
        return struct.pack("<HBHBHHBHHHII", self.bps, self.spc, self.reserved,
                           self.nfats, self.rootents, small, self.media,
                           secperfat, self.sectors, self.heads,
                           self.hidden & 0xFFFFFFFF, large)


def floppy_geometry(name):
    (bps, spc, res, nfats, roote, media, spf,
     totsec, sectors, heads, _cyl, _type) = FDD_FORMATS[name]
    return Geometry(bps, spc, res, nfats, roote, media, spf, totsec,
                    sectors, heads)


def plan_fat(totsec):
    """Pick cluster size and FAT size; FAT12 while it fits, then FAT16."""
    rootsec = HDD_ROOTENTS * 32 // HDD_BPS
    for bits, limit in ((12, FAT12_MAX), (16, FAT16_MAX)):
        for spc in (1, 2, 4, 8, 16, 32, 64, 128):
            for secperfat in range(1, 8192):
                data = totsec - RESERVED - NFATS * secperfat - rootsec
                if data <= 0:
                    break
                clusters = data // spc
                if bits == 12:
                    need = -(-((clusters + 2) * 3 // 2) // HDD_BPS)
                else:
                    need = -(-((clusters + 2) * 2) // HDD_BPS)
                if secperfat >= need:
                    if clusters < limit:
                        return spc, secperfat, bits
                    break
    raise ValueError("volume too large for FAT16")


def plan_fat32(totsec):
    """Cluster size and FAT size for FAT32.

    Anything under 65525 clusters would be taken for FAT12/16 by every
    driver that follows Microsoft's rule, so the smallest cluster that
    keeps the count above that line wins.
    """
    for spc in (8, 4, 2, 1):
        secperfat = 1
        while True:
            clusters = (totsec - FAT32_RESERVED - NFATS * secperfat) // spc
            need = -(-((clusters + 2) * 4) // HDD_BPS)
            if need <= secperfat:
                break
            secperfat = need
        if clusters >= FAT16_MAX:
            return spc, secperfat
    raise ValueError("FAT32 needs a volume of about 64 MB or more")


def hdd_geometry(megabytes, fat32=False):
    cylinders = megabytes * 1024 * 1024 // TRACK
    if cylinders < 2:
        raise ValueError("size must be at least 1 MB")
    totsec = (cylinders - 1) * TRACK // HDD_BPS
    if fat32:
        spc, secperfat = plan_fat32(totsec)
        geom = Geometry(HDD_BPS, spc, FAT32_RESERVED, NFATS, 0, HDD_MEDIA,
                        secperfat, totsec, SECTORS, HEADS,
                        hidden=TRACK // SECTOR, bits=32)
    else:
        spc, secperfat, bits = plan_fat(totsec)
        geom = Geometry(HDD_BPS, spc, RESERVED, NFATS, HDD_ROOTENTS, HDD_MEDIA,
                        secperfat, totsec, SECTORS, HEADS,
                        hidden=TRACK // SECTOR, bits=bits)
    return geom, cylinders


# ------------------------------------------------------------ Anex86 files

def anex86_header(path):
    """(header, payload size) for an Anex86 FDI/HDI, else None."""
    with open(path, "rb") as f:
        head = f.read(4096)
    if len(head) < 4096:
        return None
    offset, size = struct.unpack_from("<II", head, 8)
    secsize = struct.unpack_from("<I", head, 16)[0]
    if offset != 4096 or secsize not in (256, 512, 1024):
        return None
    return head, size


def read_image(path):
    """Payload bytes plus (sectors, heads); raw images pass straight through."""
    wrapper = anex86_header(path)
    with open(path, "rb") as f:
        if wrapper is None:
            return f.read(), SECTORS, HEADS
        head, size = wrapper
        sectors, heads = struct.unpack_from("<II", head, 20)
        f.seek(4096)
        return f.read(size), sectors, heads


def is_bare(path):
    """Whether an output name asks for a plain image with no Anex86 header."""
    return os.path.splitext(path)[1].lower() in (".raw", ".img")


def write_image(path, payload, kind, sectors, heads, cylinders, secsize):
    """Write payload, wrapping it in an Anex86 header unless .raw/.img."""
    with open(path, "wb") as dst:
        if not is_bare(path):
            head = struct.pack("<8I", 0, kind, 4096, len(payload),
                               secsize, sectors, heads, cylinders)
            dst.write(head.ljust(4096, b"\0"))
        dst.write(payload)


# ------------------------------------------------------------ FAT: writing

def fat_name(name, taken):
    stem, dot, ext = name.rpartition(".")
    if not dot:
        stem, ext = name, ""
    keep = lambda s: "".join(
        c if c.isalnum() or c in "$%'-_@~`!(){}^#&" else "_" for c in s.upper())
    stem, ext = keep(stem)[:8] or "_", keep(ext)[:3]
    candidate = (stem, ext)
    n = 1
    while candidate in taken:
        suffix = "~%d" % n
        candidate = ((stem[:8 - len(suffix)] + suffix), ext)
        n += 1
    taken.add(candidate)
    return candidate


def fat_time(path):
    try:
        t = time.localtime(os.path.getmtime(path))
    except OSError:
        t = time.localtime()
    if t.tm_year < 1980:
        return 0, 0
    date = ((t.tm_year - 1980) << 9) | (t.tm_mon << 5) | t.tm_mday
    clock = (t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec // 2)
    return date, clock


def dir_entry(stem, ext, attr, cluster, size, date, clock):
    """One 32 byte directory entry; the cluster's high half sits at 20."""
    # attr, reserved, tenths, create time/date, access date, cluster high,
    # write time/date, cluster low, size
    return (stem.ljust(8).encode("cp932") + ext.ljust(3).encode("cp932")
            + struct.pack("<BBBHHHHHHHI", attr, 0, 0, clock, date, date,
                          cluster >> 16, clock, date, cluster & 0xFFFF, size))


def patch_bpb(sector, geom):
    """Keep a donor boot record's code, replace the BPB it carries.

    A PC-98 1024 byte logical sector holds a boot record in each of its two
    512 byte halves, so both get patched.
    """
    out = bytearray(sector)
    for base in (0, 512):
        if base + 0x24 > len(out) or out[base] not in (0xEB, 0xE9):
            continue
        out[base + 11:base + 36] = geom.bpb()
    return bytes(out)


def donor_record(image, geom):
    """The boot record of an existing bootable image, for --boot."""
    if geom.hidden:
        if image[4:8] != b"IPL1":
            raise ValueError("no IPL1 signature in sector 0")
        entry = image[512:544]
        if entry[0] == 0:
            raise ValueError("empty partition table")
        base = struct.unpack_from("<H", entry, 10)[0] * TRACK
    else:
        base = 0
    record = image[base:base + geom.bps]
    if record[:1] not in (b"\xeb", b"\xe9"):
        raise ValueError("no boot sector where one was expected")
    return record


class FatBuilder:
    """Turn a host directory into a FAT12, FAT16 or FAT32 volume."""

    def __init__(self, geom, log=print, label=""):
        self.g = geom
        self.log = log
        # "NO NAME" is how FAT spells the absence of a label
        self.label = "" if label.upper().strip() in ("", "NO NAME") else label
        self.eoc = {12: 0xFFF, 16: 0xFFFF, 32: 0x0FFFFFFF}[geom.bits]
        self.image = bytearray(geom.totsec * geom.bps)
        self.fat = bytearray(geom.secperfat * geom.bps)
        self.set_cluster(0, (self.eoc & ~0xFF) | geom.media)
        self.set_cluster(1, self.eoc)
        self.next_free = 2
        self.root_cluster = 0

    def set_cluster(self, n, value):
        if self.g.bits == 32:
            struct.pack_into("<I", self.fat, n * 4, value & 0x0FFFFFFF)
            return
        if self.g.bits == 16:
            struct.pack_into("<H", self.fat, n * 2, value & 0xFFFF)
            return
        i = n + n // 2
        if n & 1:
            self.fat[i] = (self.fat[i] & 0x0F) | ((value << 4) & 0xF0)
            self.fat[i + 1] = (value >> 4) & 0xFF
        else:
            self.fat[i] = value & 0xFF
            self.fat[i + 1] = (self.fat[i + 1] & 0xF0) | ((value >> 8) & 0x0F)

    def allocate(self, nbytes):
        count = max(1, -(-nbytes // self.g.clustersize))
        if self.next_free + count - 1 >= self.g.clusters + 2:
            raise ValueError("image full")
        chain = list(range(self.next_free, self.next_free + count))
        self.next_free += count
        for a, b in zip(chain, chain[1:]):
            self.set_cluster(a, b)
        self.set_cluster(chain[-1], self.eoc)
        return chain

    def write_chain(self, chain, blob):
        step = self.g.clustersize
        for i, n in enumerate(chain):
            off = self.g.data_start + (n - 2) * step
            self.image[off:off + step] = blob[i * step:(i + 1) * step].ljust(
                step, b"\0")

    def record(self, name, ext, attr, cluster, size, date, clock):
        return dir_entry(name, ext, attr, cluster, size, date, clock)

    def build_dir(self, path, parent_cluster):
        children = sorted(os.listdir(path))
        if parent_cluster is None:
            # boot code expects IO.SYS first, on the first data cluster
            children.sort(key=lambda n: (SYSTEM_ORDER.get(n.upper(), 2), n))
        needed = (len(children) + (2 if parent_cluster is not None
                                   else 1 if self.label else 0)) * 32
        if parent_cluster is None and self.g.bits != 32:
            first, capacity, chain = 0, self.g.rootents * 32, []
        else:
            # a FAT32 root is an ordinary cluster chain, minus the dot pair
            chain = self.allocate(needed)
            first, capacity = chain[0], len(chain) * self.g.clustersize
            if parent_cluster is None:
                self.root_cluster = first
        if needed > capacity:
            raise ValueError("%s: too many entries" % path)

        blob = bytearray()
        if parent_cluster is not None:
            date, clock = fat_time(path)
            blob += self.record(".", "", ATTR_DIRECTORY, first, 0, date, clock)
            blob += self.record("..", "", ATTR_DIRECTORY, parent_cluster, 0,
                                date, clock)

        taken = set()
        for child in children:
            full = os.path.join(path, child)
            stem, ext = fat_name(child, taken)
            date, clock = fat_time(full)
            shown = stem + ("." + ext if ext else "")
            if os.path.isdir(full):
                self.log("  %s/" % shown)
                # ".." of a directory right under the root is 0 by
                # definition, even when a FAT32 root has a real cluster
                sub = self.build_dir(full,
                                     0 if parent_cluster is None else first)
                blob += self.record(stem, ext, ATTR_DIRECTORY, sub, 0,
                                    date, clock)
            else:
                with open(full, "rb") as f:
                    data = f.read()
                start = 0
                if data:
                    sub_chain = self.allocate(len(data))
                    self.write_chain(sub_chain, data)
                    start = sub_chain[0]
                attr = (ATTR_SYSTEM_FILE if parent_cluster is None
                        and child.upper() in SYSTEM_ORDER else 0x20)
                blob += self.record(stem, ext, attr, start, len(data),
                                    date, clock)
                self.log("  %s (%d bytes)" % (shown, len(data)))

        if parent_cluster is None and self.label:
            # the label lives last so boot code still finds IO.SYS first
            date, clock = fat_time(path)
            name = self.label.upper().ljust(11)[:11]
            blob += self.record(name[:8], name[8:11], ATTR_VOLUME, 0, 0,
                                date, clock)
        if parent_cluster is None and self.g.bits != 32:
            self.image[self.g.root_start:self.g.root_start + len(blob)] = blob
        else:
            self.write_chain(chain, bytes(blob))
        return first

    def fat32_boot(self, label):
        g = self.g
        boot = bytearray(g.bps)
        boot[0:3] = b"\xeb\x58\x90"
        boot[3:11] = b"PC98QEMU"
        boot[11:36] = g.bpb()
        # FAT size, mirroring, version, root cluster, FSInfo, backup boot
        struct.pack_into("<IHHIHH", boot, 36, g.secperfat, 0, 0,
                         self.root_cluster, 1, 6)
        boot[64] = 0x80
        boot[66] = 0x29
        struct.pack_into("<I", boot, 67, 0x12345678)
        boot[71:82] = label.upper().ljust(11).encode("cp932")[:11]
        boot[82:90] = b"FAT32   "
        boot[510:512] = b"\x55\xaa"
        return boot

    def finish(self, label, template=None):
        g = self.g
        if template is not None:
            if g.bits == 32:
                raise ValueError("boot code from another image needs FAT12/16")
            boot = bytearray(patch_bpb(template[:g.bps].ljust(g.bps, b"\0"), g))
        elif g.bits == 32:
            boot = self.fat32_boot(label)
        else:
            boot = bytearray(g.bps)
            boot[0:3] = b"\xeb\x3c\x90"
            boot[3:11] = b"PC98QEMU"
            boot[11:36] = g.bpb()
            boot[38] = 0x29
            struct.pack_into("<I", boot, 39, 0x12345678)
            boot[43:54] = label.upper().ljust(11).encode("cp932")[:11]
            boot[54:62] = b"FAT12   " if g.bits == 12 else b"FAT16   "
            boot[510:512] = b"\x55\xaa"
        self.image[0:g.bps] = boot
        if g.bits == 32:
            info = bytearray(g.bps)
            info[0:4] = b"RRaA"
            info[484:488] = b"rrAa"
            struct.pack_into("<II", info, 488,
                             g.clusters - (self.next_free - 2), self.next_free)
            info[508:512] = b"\x00\x00\x55\xaa"
            self.image[g.bps:2 * g.bps] = info
            self.image[6 * g.bps:7 * g.bps] = boot
            self.image[7 * g.bps:8 * g.bps] = info
        for i in range(g.nfats):
            off = g.fat_start + i * g.secperfat * g.bps
            self.image[off:off + len(self.fat)] = self.fat
        return bytes(self.image)

    @property
    def used(self):
        return (self.next_free - 2) * self.g.clustersize


# ------------------------------------------------------------ FAT: reading

class FatReader:
    """Read a FAT12/16/32 volume that starts at `base` inside `image`."""

    def __init__(self, image, base=0):
        self.image, self.base = image, base
        bps, spc = struct.unpack_from("<HB", image, base + 11)
        reserved, nfats, rootents = struct.unpack_from("<HBH", image, base + 14)
        secperfat = struct.unpack_from("<H", image, base + 22)[0]
        self.rootclus = 0
        if secperfat:
            self.bits = 0                       # 12 or 16, decided below
        else:
            # a zero 16 bit FAT size is how FAT32 announces itself
            secperfat = struct.unpack_from("<I", image, base + 36)[0]
            self.rootclus = struct.unpack_from("<I", image, base + 44)[0]
            self.bits = 32
        if not bps or not spc or not secperfat:
            raise ValueError("not a FAT volume")
        self.bps = bps
        self.clustersize = spc * bps
        self.fat = base + reserved * bps
        self.root = self.fat + nfats * secperfat * bps
        self.rootsize = rootents * 32
        self.data = self.root + self.rootsize
        clusters = (len(image) - self.data) // self.clustersize
        if not self.bits:
            self.bits = 16 if clusters >= FAT12_MAX else 12

    def follow(self, first):
        end = {12: 0xFF0, 16: 0xFFF0, 32: 0x0FFFFFF0}[self.bits]
        n, seen = first, set()
        while 2 <= n < end:
            if n in seen:
                raise ValueError("cluster chain loops at %d" % n)
            seen.add(n)
            yield n
            if self.bits == 32:
                n = struct.unpack_from("<I", self.image,
                                       self.fat + n * 4)[0] & 0x0FFFFFFF
            elif self.bits == 16:
                n = struct.unpack_from("<H", self.image, self.fat + n * 2)[0]
            else:
                pair = struct.unpack_from("<H", self.image,
                                          self.fat + n + n // 2)[0]
                n = pair >> 4 if n & 1 else pair & 0xFFF

    def read(self, first, size=None):
        out = bytearray()
        for n in self.follow(first):
            off = self.data + (n - 2) * self.clustersize
            out += self.image[off:off + self.clustersize]
            if size is not None and len(out) >= size:
                break
        return bytes(out[:size]) if size is not None else bytes(out)

    def root_records(self):
        if self.bits == 32:
            return self.read(self.rootclus)
        return self.image[self.root:self.root + self.rootsize]


def dir_records(blob, fat32=False):
    for i in range(0, len(blob), 32):
        entry = blob[i:i + 32]
        if len(entry) < 32 or entry[0] == 0x00:
            return
        if entry[0] == 0xE5 or entry[11] & ATTR_VOLUME:
            continue
        stem = entry[0:8].rstrip(b" ")
        ext = entry[8:11].rstrip(b" ")
        name = stem.decode("cp932", "replace")
        if ext:
            name += "." + ext.decode("cp932", "replace")
        cluster, size = struct.unpack_from("<HI", entry, 26)
        if fat32:
            # FAT12/16 leave the high half to OS/2's extended attributes,
            # so it only means something on a FAT32 volume
            cluster |= struct.unpack_from("<H", entry, 20)[0] << 16
        yield name, entry[11], cluster, size


def extract_fat(fat, blob, outdir, log, prefix=""):
    count = 0
    os.makedirs(outdir, exist_ok=True)
    for name, attr, cluster, size in dir_records(blob, fat.bits == 32):
        if attr & ATTR_DIRECTORY:
            if name in (".", ".."):
                continue
            log("  %s%s/" % (prefix, name))
            count += extract_fat(fat, fat.read(cluster),
                                 os.path.join(outdir, name), log,
                                 prefix + name + "/")
        else:
            with open(os.path.join(outdir, name), "wb") as out:
                out.write(fat.read(cluster, size) if size else b"")
            log("  %s%s (%d bytes)" % (prefix, name, size))
            count += 1
    return count


def partitions(image, sectors, heads):
    """(name, byte offset) for each entry of the PC-98 partition table."""
    track = 512 * sectors * heads
    for i in range(512, 1024, 32):
        entry = image[i:i + 32]
        if len(entry) < 32 or entry[0] == 0:
            continue
        cylinder = struct.unpack_from("<H", entry, 10)[0]
        name = entry[16:32].rstrip(b" \0").decode("cp932", "replace")
        yield name, cylinder * track


# ----------------------------------------------------------- FAT: updating

DIR_FREE = 0xE5


class FatUpdater:
    """Write a host folder into an existing FAT volume, file by file.

    A file whose 8.3 name is already present has its contents replaced in
    place; everything else on the volume, the boot record included, is
    left exactly as it was.
    """

    def __init__(self, image, base=0, log=print):
        self.image, self.base, self.log = image, base, log
        bps, spc = struct.unpack_from("<HB", image, base + 11)
        reserved, nfats, rootents = struct.unpack_from("<HBH", image,
                                                       base + 14)
        small, media, secperfat = struct.unpack_from("<HBH", image, base + 19)
        large = struct.unpack_from("<I", image, base + 32)[0]
        self.rootclus = self.fsinfo = 0
        if secperfat:
            self.bits = 0                       # 12 or 16, decided below
        else:
            secperfat = struct.unpack_from("<I", image, base + 36)[0]
            self.rootclus = struct.unpack_from("<I", image, base + 44)[0]
            self.fsinfo = struct.unpack_from("<H", image, base + 48)[0]
            self.bits = 32
        if not bps or not spc or not secperfat:
            raise ValueError("not a FAT volume")
        totsec = small or large
        self.bps, self.spc = bps, spc
        self.nfats, self.secperfat = nfats, secperfat
        self.clustersize = spc * bps
        self.fat_start = base + reserved * bps
        self.root_start = self.fat_start + nfats * secperfat * bps
        self.rootsize = rootents * 32
        self.data_start = self.root_start + self.rootsize
        self.clusters = ((totsec * bps - (self.data_start - base))
                         // self.clustersize)
        if not self.bits:
            self.bits = 16 if self.clusters >= FAT12_MAX else 12
        self.eoc = {12: 0xFFF, 16: 0xFFFF, 32: 0x0FFFFFFF}[self.bits]
        self._free = None
        self.count = 0

    # --------------------------------------------------- allocation table
    def get(self, n):
        if self.bits == 32:
            return struct.unpack_from(
                "<I", self.image, self.fat_start + n * 4)[0] & 0x0FFFFFFF
        if self.bits == 16:
            return struct.unpack_from("<H", self.image,
                                      self.fat_start + n * 2)[0]
        pair = struct.unpack_from("<H", self.image,
                                  self.fat_start + n + n // 2)[0]
        return pair >> 4 if n & 1 else pair & 0xFFF

    def set(self, n, value):
        for copy in range(self.nfats):
            off = self.fat_start + copy * self.secperfat * self.bps
            if self.bits == 32:
                old = struct.unpack_from("<I", self.image, off + n * 4)[0]
                struct.pack_into("<I", self.image, off + n * 4,
                                 (old & 0xF0000000) | (value & 0x0FFFFFFF))
            elif self.bits == 16:
                struct.pack_into("<H", self.image, off + n * 2,
                                 value & 0xFFFF)
            else:
                i = off + n + n // 2
                if n & 1:
                    self.image[i] = ((self.image[i] & 0x0F)
                                     | ((value << 4) & 0xF0))
                    self.image[i + 1] = (value >> 4) & 0xFF
                else:
                    self.image[i] = value & 0xFF
                    self.image[i + 1] = ((self.image[i + 1] & 0xF0)
                                         | ((value >> 8) & 0x0F))

    def follow(self, first):
        end = {12: 0xFF0, 16: 0xFFF0, 32: 0x0FFFFFF0}[self.bits]
        n, seen = first, set()
        while 2 <= n < end:
            if n in seen:
                raise ValueError("cluster chain loops at %d" % n)
            seen.add(n)
            yield n
            n = self.get(n)

    def free_chain(self, first):
        freed = list(self.follow(first))
        for n in freed:
            self.set(n, 0)
        if self._free is not None:
            self._free.extend(freed)

    def allocate(self, nbytes):
        count = max(1, -(-nbytes // self.clustersize))
        if self._free is None:
            # one scan pays for every allocation this run will make
            self._free = [n for n in range(2, self.clusters + 2)
                          if self.get(n) == 0]
        if count > len(self._free):
            raise ValueError("image full")
        chain, self._free = self._free[:count], self._free[count:]
        for a, b in zip(chain, chain[1:]):
            self.set(a, b)
        self.set(chain[-1], self.eoc)
        return chain

    def cluster_off(self, n):
        return self.data_start + (n - 2) * self.clustersize

    def read_chain(self, first):
        out = bytearray()
        for n in self.follow(first):
            off = self.cluster_off(n)
            out += self.image[off:off + self.clustersize]
        return out

    def write_chain(self, chain, blob):
        step = self.clustersize
        for i, n in enumerate(chain):
            off = self.cluster_off(n)
            self.image[off:off + step] = blob[i * step:(i + 1) * step].ljust(
                step, b"\0")

    # ---------------------------------------------------------- directories
    def load_dir(self, first):
        """(entries blob, cluster chain); the fixed root has no chain."""
        if not first:
            return (bytearray(self.image[self.root_start:self.data_start]),
                    None)
        return self.read_chain(first), list(self.follow(first))

    def store_dir(self, blob, chain):
        if chain is None:
            self.image[self.root_start:self.data_start] = blob
            return
        while len(chain) * self.clustersize < len(blob):
            extra = self.allocate(self.clustersize)
            self.set(chain[-1], extra[0])
            chain.extend(extra)
        self.write_chain(chain, bytes(blob))

    def merge(self, src):
        self.merge_dir(src, self.rootclus, True, "")
        if self.bits == 32 and self._free is not None:
            # the FSInfo free-cluster summary went stale the moment
            # anything was allocated
            off = self.base + self.fsinfo * self.bps
            if self.image[off:off + 4] == b"RRaA":
                struct.pack_into("<II", self.image, off + 488,
                                 len(self._free),
                                 self._free[0] if self._free else 2)
        return self.count

    def merge_dir(self, path, first, is_root, prefix):
        blob, chain = self.load_dir(first)
        existing, frees = {}, []
        end = len(blob)
        for i in range(0, len(blob), 32):
            if blob[i] == 0x00:
                end = i
                break
            if blob[i] == DIR_FREE:
                frees.append(i)
            elif not blob[i + 11] & ATTR_VOLUME:
                existing[(bytes(blob[i:i + 8]), bytes(blob[i + 8:i + 11]))] = i
        # whatever sits past the end marker is dead space; clearing it lets
        # appended entries terminate the directory by themselves
        blob[end:] = b"\0" * (len(blob) - end)
        taken = {(stem.decode("cp932", "replace").rstrip(" "),
                  ext.decode("cp932", "replace").rstrip(" "))
                 for stem, ext in existing}

        def entry_cluster(at):
            cluster = struct.unpack_from("<H", blob, at + 26)[0]
            if self.bits == 32:
                cluster |= struct.unpack_from("<H", blob, at + 20)[0] << 16
            return cluster

        def place(entry):
            nonlocal end, blob
            if frees:
                at = frees.pop(0)
            else:
                at = end
                end += 32
                if end > len(blob):
                    if chain is None:
                        raise ValueError("the root directory is full")
                    blob.extend(b"\0" * self.clustersize)
            blob[at:at + 32] = entry
            return at

        for child in sorted(os.listdir(path)):
            full = os.path.join(path, child)
            date, clock = fat_time(full)
            stem, ext = fat_name(child, set())
            key = (stem.ljust(8).encode("cp932"), ext.ljust(3).encode("cp932"))
            at = existing.get(key)
            shown = stem + ("." + ext if ext else "")
            if os.path.isdir(full):
                if at is not None:
                    if not blob[at + 11] & ATTR_DIRECTORY:
                        raise ValueError("%s%s is a file on the image"
                                         % (prefix, shown))
                    sub = entry_cluster(at)
                else:
                    stem, ext = fat_name(child, taken)
                    shown = stem + ("." + ext if ext else "")
                    grown = self.allocate(2 * 32)
                    dots = (dir_entry(".", "", ATTR_DIRECTORY, grown[0], 0,
                                      date, clock)
                            + dir_entry("..", "", ATTR_DIRECTORY,
                                        0 if is_root else first, 0,
                                        date, clock))
                    self.write_chain(grown, dots)
                    place(dir_entry(stem, ext, ATTR_DIRECTORY, grown[0], 0,
                                    date, clock))
                    sub = grown[0]
                self.log("  %s%s/" % (prefix, shown))
                self.merge_dir(full, sub, False, prefix + shown + "/")
            else:
                with open(full, "rb") as f:
                    data = f.read()
                if at is not None:
                    if blob[at + 11] & ATTR_DIRECTORY:
                        raise ValueError("%s%s is a directory on the image"
                                         % (prefix, shown))
                    old = entry_cluster(at)
                    if old:
                        self.free_chain(old)
                    attr = blob[at + 11]
                else:
                    stem, ext = fat_name(child, taken)
                    shown = stem + ("." + ext if ext else "")
                    attr = 0x20
                start = 0
                if data:
                    grown = self.allocate(len(data))
                    self.write_chain(grown, data)
                    start = grown[0]
                entry = dir_entry(stem, ext, attr, start, len(data),
                                  date, clock)
                if at is not None:
                    blob[at:at + 32] = entry
                else:
                    place(entry)
                self.log("  %s%s (%d bytes)" % (prefix, shown, len(data)))
                self.count += 1
        self.store_dir(blob, chain)


# ------------------------------------------------------- IPL and boot

PBR_SEGMENT = 0x1FC0        # where NEC's boot selector loads a boot record
PBR_SEGMENT_HIRESO = 0x3FC0
IPL_SEGMENT = 0x1000        # scratch the IPL moves itself to
IPL_CODE = 0x0C             # first byte after the IPL1 header
IPL_DATA = 0x100            # past the 256 byte sector signature

MSG_BOOT = b"QEMU IPL: booting"
MSG_NOPART = b"QEMU IPL: no partition"
MSG_READ = b"QEMU IPL: read error"


def build_ipl():
    """Assemble an IPL that starts the first partition's boot record.

    The disk BIOS enters at CS:0000 with DS = 0.  The code copies itself to
    IPL_SEGMENT so the boot record it is about to load cannot land on top of
    it, reads the partition table from cylinder 0 head 0 sector 1, then reads
    the first entry's boot record to PBR_SEGMENT:0000 and jumps there with
    AL = the DA/UA from 0000:0584 and SI = DX = 0, which is the state NEC's
    own boot selector leaves behind.  Progress and failures go straight to
    text VRAM so a machine that stops here says why.
    """
    code = bytearray()
    labels = {}
    rel8 = []
    rel16 = []
    imm16 = []

    def emit(*parts):
        for p in parts:
            code.extend(p)

    def here():
        return IPL_CODE + len(code)

    def mark(name):
        labels[name] = here()

    def jump(opcode, name):
        rel8.append((len(code) + len(opcode), name))
        emit(opcode, b"\x00")

    def call(name):
        rel16.append((len(code) + 1, name))
        emit(b"\xE8\x00\x00")

    def word(opcode, name):
        imm16.append((len(code) + len(opcode), name))
        emit(opcode, b"\x00\x00")

    # move out of the way: the boot record we load would land on top of us
    emit(b"\xB8" + IPL_SEGMENT.to_bytes(2, "little"))   # mov ax, IPL_SEGMENT
    emit(b"\x8E\xC0\x0E\x1F")                   # mov es,ax / push cs / pop ds
    emit(b"\x33\xF6\x33\xFF\xB9\x00\x01")       # xor si,si / di,di / cx,256
    emit(b"\xFC\xF3\xA5")                       # cld / rep movsw
    emit(b"\xEA" + (here() + 5).to_bytes(2, "little")
         + IPL_SEGMENT.to_bytes(2, "little"))

    emit(b"\x33\xC0\x8E\xD8")                   # xor ax,ax / mov ds,ax
    emit(b"\xB8\x00\xA0")                       # mov ax,0xa000  text vram
    emit(b"\xBB" + PBR_SEGMENT.to_bytes(2, "little"))
    emit(b"\xF6\x06\x01\x05\x08")               # test byte [0x0501],8 hi-res
    emit(b"\x74\x06")                           # jz +6
    emit(b"\xB8\x00\xE0")                       # mov ax,0xe000
    emit(b"\xBB" + PBR_SEGMENT_HIRESO.to_bytes(2, "little"))
    word(b"\x2E\xA3", "vram")                    # mov [cs:vram],ax
    emit(b"\x2E\x89\x1E\x0A\x00")               # mov [cs:0x000a],bx

    word(b"\xBE", "msg_boot")                   # mov si,msg_boot
    call("putmsg")

    emit(b"\xA0\x84\x05\x0C\x80\xA2\x84\x05")   # al = [0x0584] | 0x80
    emit(b"\xBB\x00\x01\xB4\x84\xCD\x1B")       # int 1bh ah=84h -> bx = bps

    emit(b"\x0E\x07\xBD\x00\x02")               # es = cs / mov bp,0x0200
    emit(b"\x33\xC9\xBA\x01\x00")               # cylinder 0, head 0, sector 1
    emit(b"\xB4\x06\xCD\x1B")                   # int 1bh ah=06h read
    jump(b"\x73", "scan")                       # jnc scan
    word(b"\xBE", "msg_read")
    jump(b"\xEB", "fail")

    mark("scan")
    emit(b"\xBE\x00\x02\xB9\x10\x00")           # mov si,0x0200 / mov cx,16
    mark("next")
    emit(b"\x2E\x80\x3C\x00")                   # cmp byte [cs:si],0
    jump(b"\x75", "found")
    emit(b"\x83\xC6\x20")                       # add si,32
    emit(b"\xE2" + bytes([(labels["next"] - (here() + 2)) & 0xFF]))
    word(b"\xBE", "msg_nopart")
    jump(b"\xEB", "fail")

    mark("found")
    emit(b"\x2E\x8A\x54\x04")                   # mov dl,[cs:si+4]   sector
    emit(b"\x2E\x8A\x74\x05")                   # mov dh,[cs:si+5]   head
    emit(b"\x2E\x8B\x4C\x06")                   # mov cx,[cs:si+6]   cylinder
    emit(b"\x2E\x8E\x06\x0A\x00")               # mov es,[cs:0x000a]
    emit(b"\x33\xED\xD1\xE3")                   # xor bp,bp / shl bx,1
    emit(b"\xA0\x84\x05\xB4\x06\xCD\x1B")       # int 1bh ah=06h read
    jump(b"\x73", "launch")
    word(b"\xBE", "msg_read")

    mark("fail")
    call("putmsg")
    emit(b"\xB0\x06\xE6\x37")                   # mov al,6 / out 0x37,al
    mark("stop")
    emit(b"\xF4\xEB" + bytes([(labels["stop"] - (here() + 3)) & 0xFF]))

    mark("launch")
    emit(b"\x33\xF6\x33\xD2")                   # xor si,si / xor dx,dx
    emit(b"\x2E\xFF\x2E\x08\x00")               # jmp far [cs:0x0008]

    # si = message, ds = 0 on entry and on return
    mark("putmsg")
    emit(b"\x06\x57\x0E\x1F")                   # push es / push di / ds = cs
    word(b"\x2E\x8E\x06", "vram")                # mov es,[cs:vram]
    word(b"\x2E\x8B\x3E", "cursor")              # mov di,[cs:cursor]
    imm16.append((len(code) + 3, "cursor"))
    emit(b"\x2E\x81\x06\x00\x00\xA0\x00")       # add word [cs:cursor],160
    mark("putc")
    emit(b"\xAC\x0A\xC0")                       # lodsb / or al,al
    jump(b"\x74", "done")
    emit(b"\xAA\x26\xC6\x85\xFF\x1F\xE1\x47")   # stosb / attribute / inc di
    emit(b"\xEB" + bytes([(labels["putc"] - (here() + 2)) & 0xFF]))
    mark("done")
    emit(b"\x33\xC0\x8E\xD8\x5F\x07\xC3")       # ds = 0 / pop di / pop es / ret

    if IPL_CODE + len(code) > 0xFE:
        raise MemoryError("IPL code does not fit before the sector signature")

    data = bytearray(b"\x00\x00\x00\x00")       # vram, cursor
    labels["vram"] = IPL_DATA
    labels["cursor"] = IPL_DATA + 2
    for name, text in (("msg_boot", MSG_BOOT), ("msg_nopart", MSG_NOPART),
                       ("msg_read", MSG_READ)):
        labels[name] = IPL_DATA + len(data)
        data += text + b"\0"
    if IPL_DATA + len(data) > 0x1FE:
        raise MemoryError("IPL messages do not fit in the sector")

    for at, name in rel8:
        code[at] = (labels[name] - (IPL_CODE + at + 1)) & 0xFF
    for at, name in rel16:
        struct.pack_into("<h", code, at, labels[name] - (IPL_CODE + at + 2))
    for at, name in imm16:
        struct.pack_into("<H", code, at, labels[name])

    ipl = bytearray(SECTOR)
    ipl[0:4] = b"\xeb\x0a\x90\x90"
    ipl[4:8] = b"IPL1"
    struct.pack_into("<HH", ipl, 8, 0, PBR_SEGMENT)
    ipl[IPL_CODE:IPL_CODE + len(code)] = code
    ipl[0xFE:0x100] = b"\x55\xaa"
    ipl[IPL_DATA:IPL_DATA + len(data)] = data
    ipl[0x1FE:0x200] = b"\x55\xaa"
    return bytes(ipl)



def ipl_sectors(cylinders, label, bits=12):
    """Sector 0 is the IPL, sector 1 holds the PC-98 partition table."""
    ipl = bytearray(build_ipl())
    table = bytearray(SECTOR)
    entry = bytearray(32)
    entry[0] = 0xA1                                  # bootable, DOS
    entry[1] = {12: 0x81, 16: 0x91, 32: 0xE1}[bits]
    entry[4] = 0                                     # IPL sector
    entry[5] = 0                                     # IPL head
    struct.pack_into("<H", entry, 6, 1)              # IPL cylinder
    entry[8] = 0                                     # first sector
    entry[9] = 0                                     # first head
    struct.pack_into("<H", entry, 10, 1)             # first cylinder
    entry[12] = 0
    entry[13] = 0
    struct.pack_into("<H", entry, 14, cylinders - 1)  # last cylinder
    entry[16:32] = label.upper().ljust(16).encode("cp932")[:16]
    table[0:32] = entry
    return bytes(ipl) + bytes(table)



# ---------------------------------------------------------------- ISO

ISO_SECTOR = 2048
ISO_ATTR_DIR = 0x02


def iso_name(name, is_dir, taken):
    """ISO 9660 level 1: 8.3, upper case, a very small alphabet."""
    stem, dot, ext = name.rpartition(".")
    if not dot or is_dir:
        stem, ext = name, ""
    keep = lambda s: "".join(c if c.isalnum() or c == "_" else "_"
                             for c in s.upper())
    stem, ext = keep(stem)[:8] or "_", keep(ext)[:3]
    candidate = stem + ("." + ext if ext else "")
    n = 1
    while candidate in taken:
        suffix = "%d" % n
        candidate = stem[:8 - len(suffix)] + suffix + ("." + ext if ext else "")
        n += 1
    taken.add(candidate)
    return candidate


def both(value, size):
    """ISO 9660 stores numbers little endian then big endian."""
    fmt = {2: ("<H", ">H"), 4: ("<I", ">I")}[size]
    return struct.pack(fmt[0], value) + struct.pack(fmt[1], value)


def rec_datetime(stamp):
    t = time.localtime(stamp)
    return bytes([t.tm_year - 1900, t.tm_mon, t.tm_mday,
                  t.tm_hour, t.tm_min, t.tm_sec, 0])


def directory_record(name, extent, length, is_dir, stamp):
    ident = b"\x00" if name == "." else (b"\x01" if name == ".."
                                         else name.encode("ascii"))
    size = 33 + len(ident)
    pad = size & 1
    return (bytes([size + pad, 0]) + both(extent, 4) + both(length, 4)
            + rec_datetime(stamp) + bytes([ISO_ATTR_DIR if is_dir else 0, 0, 0])
            + both(1, 2) + bytes([len(ident)]) + ident + b"\0" * pad)


class Tree:
    """Lay the whole hierarchy out before writing: sizes decide extents."""

    def __init__(self, root):
        self.nodes = []
        self.scan(root, None)

    def scan(self, path, parent):
        index = len(self.nodes)
        node = {"path": path, "parent": index if parent is None else parent,
                "files": [], "dirs": [], "index": index}
        self.nodes.append(node)
        taken = set()
        for child in sorted(os.listdir(path)):
            full = os.path.join(path, child)
            name = iso_name(child, os.path.isdir(full), taken)
            if os.path.isdir(full):
                node["dirs"].append((name, self.scan(full, index)))
            else:
                node["files"].append((name, full, os.path.getsize(full)))
        return index

    def dir_bytes(self, node, extents):
        stamp = os.path.getmtime(node["path"])
        blob = directory_record(".", 0, 0, True, stamp)
        blob += directory_record("..", 0, 0, True, stamp)
        for name, index in node["dirs"]:
            e, ln = extents.get(index, (0, 0))
            blob += directory_record(name, e, ln, True, stamp)
        for name, full, size in node["files"]:
            e = extents.get(("f", full), (0,))[0]
            blob += directory_record(name, e, size, False,
                                     os.path.getmtime(full))
        return blob


def build_iso(root, label, log=print):
    tree = Tree(root)
    extents = {}
    # two passes: the first sizes every directory, the second fills extents in
    for _ in range(2):
        lba = 18            # 16 system area + PVD + terminator
        sizes = {}
        for node in tree.nodes:
            blob = tree.dir_bytes(node, extents)
            length = -(-len(blob) // ISO_SECTOR) * ISO_SECTOR
            sizes[node["index"]] = (lba, len(blob))
            lba += length // ISO_SECTOR
        for node in tree.nodes:
            for name, full, size in node["files"]:
                sizes[("f", full)] = (lba, size)
                lba += -(-size // ISO_SECTOR) or 1
        extents = sizes
    total = lba

    out = bytearray(16 * ISO_SECTOR)
    root_extent, root_len = extents[0]
    pvd = bytearray(ISO_SECTOR)
    pvd[0:7] = b"\x01CD001\x01"
    pvd[8:40] = b" " * 32
    pvd[40:72] = label.upper().ljust(32).encode("ascii")[:32]
    struct.pack_into("<8s", pvd, 72, b"\0" * 8)
    pvd[80:88] = both(total, 4)
    pvd[120:124] = both(1, 2)
    pvd[124:128] = both(1, 2)
    pvd[128:132] = both(ISO_SECTOR, 2)
    pvd[132:140] = both(0, 4)          # no path table (DOS reads records)
    pvd[156:190] = directory_record(".", root_extent, root_len, True,
                                    os.path.getmtime(root)).ljust(34, b"\0")
    for start in (190, 318, 446, 574):
        pvd[start:start + 128] = b" " * 128
    stamp = time.strftime("%Y%m%d%H%M%S00", time.localtime()).encode()
    for start in (813, 830, 847, 864):
        pvd[start:start + 17] = stamp + b"\0"
    pvd[881] = 1
    out += pvd
    term = bytearray(ISO_SECTOR)
    term[0:7] = b"\xffCD001\x01"
    out += term

    for node in tree.nodes:
        blob = tree.dir_bytes(node, extents)
        out += blob.ljust(-(-len(blob) // ISO_SECTOR) * ISO_SECTOR, b"\0")
    for node in tree.nodes:
        for name, full, size in node["files"]:
            with open(full, "rb") as f:
                data = f.read()
            out += data.ljust(-(-len(data) // ISO_SECTOR) * ISO_SECTOR or ISO_SECTOR, b"\0")
            log("  %s (%d bytes)" % (name, size))
    return bytes(out)



def find_pvd(f):
    for lba in range(16, 32):
        f.seek(lba * ISO_SECTOR)
        block = f.read(ISO_SECTOR)
        if len(block) < ISO_SECTOR or block[1:6] != b"CD001":
            continue
        if block[0] == 1:
            return block
        if block[0] == 255:
            break
    raise ValueError("no ISO 9660 primary volume descriptor")


def iso_records(blob):
    i = 0
    while i < len(blob):
        size = blob[i]
        if size == 0:
            # the rest of this sector is padding; skip to the next one
            i = (i // ISO_SECTOR + 1) * ISO_SECTOR
            if i >= len(blob):
                return
            continue
        rec = blob[i:i + size]
        i += size
        if len(rec) < 33:
            return
        extent = struct.unpack_from("<I", rec, 2)[0]
        length = struct.unpack_from("<I", rec, 10)[0]
        flags = rec[25]
        nlen = rec[32]
        ident = rec[33:33 + nlen]
        if ident in (b"\x00", b"\x01"):
            continue
        name = ident.decode("cp932", "replace").split(";")[0]
        yield name, extent, length, bool(flags & ISO_ATTR_DIR)


def extract_iso(f, extent, length, outdir, log, prefix=""):
    count = 0
    os.makedirs(outdir, exist_ok=True)
    f.seek(extent * ISO_SECTOR)
    blob = f.read(length)
    for name, sub_extent, sub_length, is_dir in iso_records(blob):
        if is_dir:
            log("  %s%s/" % (prefix, name))
            count += extract_iso(f, sub_extent, sub_length,
                             os.path.join(outdir, name), log,
                             prefix + name + "/")
        else:
            f.seek(sub_extent * ISO_SECTOR)
            data = f.read(sub_length)
            with open(os.path.join(outdir, name), "wb") as out:
                out.write(data)
            log("  %s%s (%d bytes)" % (prefix, name, sub_length))
            count += 1
    return count




# --------------------------------------------------------------- operations

def _log(quiet):
    return (lambda *a: None) if quiet else print


def folder_to_floppy(src, dst, label="NO NAME", boot=None, fmt="1.2",
                     log=print):
    geom = floppy_geometry(fmt)
    template = donor_record(read_image(boot)[0], geom) if boot else None
    builder = FatBuilder(geom, log, label)
    builder.build_dir(src, None)
    payload = builder.finish(label, template)
    cyl, kind = FDD_FORMATS[fmt][10], FDD_FORMATS[fmt][11]
    write_image(dst, payload, kind, geom.sectors, geom.heads, cyl, geom.bps)
    log("%s -> %s (%d bytes, %d used of %d)"
        % (src, dst, os.path.getsize(dst), builder.used,
           geom.clusters * geom.clustersize))


def folder_to_hard_disk(src, dst, megabytes=40, label="NO NAME", boot=None,
                        log=print, fat32=False):
    geom, cylinders = hdd_geometry(megabytes, fat32)
    template = donor_record(read_image(boot)[0], geom) if boot else None
    builder = FatBuilder(geom, log, label)
    builder.build_dir(src, None)
    volume = builder.finish(label, template)
    image = bytearray(cylinders * TRACK)
    image[0:2 * SECTOR] = ipl_sectors(cylinders, label, geom.bits)
    image[TRACK:TRACK + len(volume)] = volume
    write_image(dst, bytes(image), HDD_TYPE, SECTORS, HEADS, cylinders, SECTOR)
    log("%s -> %s (%d bytes)" % (src, dst, os.path.getsize(dst)))
    log("geometry: %d bytes/sector, %d sectors/track, %d heads, %d cylinders"
        % (SECTOR, SECTORS, HEADS, cylinders))
    log("partition: FAT%d, %d byte clusters, %d used of %d"
        % (geom.bits, geom.clustersize, builder.used,
           geom.clusters * geom.clustersize))


def image_to_folder(src, dst, floppy=False, partition=1, log=print):
    image, sectors, heads = read_image(src)
    base = 0
    if not floppy:
        table = list(partitions(image, sectors, heads))
        if not table:
            raise ValueError("no PC-98 partition table found")
        log("partitions:")
        for n, (name, off) in enumerate(table, 1):
            log("  %d: %-16s at byte %d" % (n, name or "(unnamed)", off))
        if not 1 <= partition <= len(table):
            raise ValueError("no partition %d" % partition)
        base = table[partition - 1][1]
    if (image[base + 54:base + 58] != b"FAT1"
            and image[base + 82:base + 86] != b"FAT3"):
        raise ValueError("no FAT boot sector at byte %d" % base)
    fat = FatReader(image, base)
    count = extract_fat(fat, fat.root_records(), dst, log)
    log("extracted %d files to %s" % (count, dst))


def folder_into_image(src, dst, partition=1, log=print):
    """Copy a folder's files into an existing image, replacing same names."""
    if is_qcow2(dst):
        payload = bytearray(qcow2_read(dst, log))
        offset, sectors, heads = None, SECTORS, HEADS
    else:
        wrapper = anex86_header(dst)
        raw, sectors, heads = read_image(dst)
        payload = bytearray(raw)
        offset = 0 if wrapper is None else 4096
    base = 0
    if looks_like_hdd(payload):
        table = list(partitions(payload, sectors, heads))
        if not table:
            raise ValueError("no PC-98 partition table found")
        if not 1 <= partition <= len(table):
            raise ValueError("no partition %d" % partition)
        name, base = table[partition - 1]
        log("partition %d: %s" % (partition, name or "(unnamed)"))
    if payload[base:base + 1] not in (b"\xeb", b"\xe9"):
        raise ValueError("no FAT boot record at byte %d" % base)
    count = FatUpdater(payload, base, log).merge(src)
    if offset is None:
        qcow2_write(dst, bytes(payload), log)
    else:
        # the payload kept its size, so it can go back where it came from
        with open(dst, "r+b") as f:
            f.seek(offset)
            f.write(payload)
    log("wrote %d file(s) into %s" % (count, dst))


def convert_image(src, dst, kind, sectors, heads, secsize, log=print):
    """Move an image between raw and its Anex86 container."""
    payload = read_image(src)[0]
    track = secsize * sectors * heads
    cylinders = -(-len(payload) // track)
    payload = payload.ljust(cylinders * track, b"\0")
    write_image(dst, payload, kind, sectors, heads, cylinders, secsize)
    log("%s -> %s (%d bytes)" % (src, dst, os.path.getsize(dst)))
    log("geometry: %d bytes/sector, %d sectors/track, %d heads, %d cylinders"
        % (secsize, sectors, heads, cylinders))


def folder_to_iso(src, dst, label="PC98", log=print):
    image = build_iso(src, label, log)
    with open(dst, "wb") as f:
        f.write(image)
    log("%s -> %s (%d bytes)" % (src, dst, len(image)))


def iso_to_folder(src, dst, log=print):
    with open(src, "rb") as f:
        pvd = find_pvd(f)
        log("volume: %s" % (pvd[40:72].decode("ascii", "replace").strip()
                            or "(unnamed)"))
        root = pvd[156:190]
        extent = struct.unpack_from("<I", root, 2)[0]
        length = struct.unpack_from("<I", root, 10)[0]
        count = extract_iso(f, extent, length, dst, log)
    log("extracted %d files to %s" % (count, dst))


def new_image(dst, megabytes=None, fmt=None, label="NO NAME", kind=None,
              fat32=False, log=print):
    """A formatted, empty volume in whichever container was asked for."""
    empty = tempfile.mkdtemp(prefix="pc98-empty-")
    try:
        if kind in (None, KIND_RAW_HDD, KIND_RAW_FDD, KIND_HDI, KIND_FDI):
            # the builders write these containers themselves
            if fmt:
                folder_to_floppy(empty, dst, label, None, fmt, log)
            else:
                folder_to_hard_disk(empty, dst, megabytes, label, None, log,
                                    fat32=fat32)
            return
        convert_pair(KIND_FOLDER, empty, kind, dst,
                     {"size": str(megabytes or 40), "label": label,
                      "format": fmt or "1.2",
                      "fat32": "1" if fat32 else ""}, log)
    finally:
        os.rmdir(empty)


def fdd_format_of(path):
    """1.2 or 1.44, whichever the image's own BPB says."""
    image = read_image(path)[0]
    bps = struct.unpack_from("<H", image, 11)[0]
    return "1.44" if bps == 512 else "1.2"


# ---------------------------------------------------------- the launcher

SETTINGS = "virtpc98.ini"
SECTION = "virtpc98"
# The Windows package ships both system emulators.  Prefer x86_64, while
# keeping the i386 names as a fallback for older and hand-made packages.
QEMU_NAMES = ("qemu-system-x86_64.exe", "qemu-system-x86_64w.exe",
              "qemu-system-x86_64",
              "qemu-system-i386.exe", "qemu-system-i386w.exe",
              "qemu-system-i386")
COMPAT_ROM_DIR = os.path.join("share", "pc98bios")
ROM_NAMES = ("bios-xa7c9w", "roms", "bios")
ROM_FILES = ("pc98bank0.bin", "pc98font.bin")


def here():
    if getattr(sys, "frozen", False):
        return os.path.dirname(os.path.abspath(sys.executable))
    return os.path.dirname(os.path.abspath(__file__))


def find(names, want_dir=False):
    """Beside us, then one level up, then dist, then the cwd, then PATH.

    A packaged build keeps QEMU next to this program; a distribution install
    puts it on PATH instead, which is the normal case on Linux.
    """
    for root in (here(), os.path.dirname(here()),
                 os.path.join(here(), "dist"), os.getcwd()):
        for name in names:
            path = os.path.join(root, name)
            if os.path.isdir(path) if want_dir else os.path.isfile(path):
                return os.path.normpath(path)
    if not want_dir:
        for name in names:
            path = shutil.which(name)
            if path:
                return path
    return ""


def compat_roms(qemu):
    root = os.path.dirname(qemu) if qemu else here()
    return os.path.normpath(os.path.join(root, COMPAT_ROM_DIR))


def default_roms(qemu):
    for root in (os.path.dirname(qemu) if qemu else here(), here(),
                 os.path.dirname(here()), os.getcwd()):
        for name in ROM_NAMES:
            path = os.path.join(root, name)
            if os.path.isdir(path):
                return os.path.normpath(path)
    return ""


def is_host_device(path):
    r"""A whole drive rather than a file: \\.\D: on Windows, /dev/... here."""
    if os.name == "nt":
        return path.startswith("\\\\.\\")
    return path.startswith("/dev/")


def drive_backing(path):
    """The format= and file= part of a -drive.

    Even a real drive needs the format named: left to probe it, QEMU
    guesses raw and then forbids writes to block 0, which would make a
    floppy unwritable.  A .qcow2 name is the one thing that is not raw.
    """
    fmt = "qcow2" if path.lower().endswith(".qcow2") else "raw"
    return "format=%s,file=%s" % (fmt, path)


def serial_path(port):
    r"""What QEMU wants for a host port.

    COM1 to COM9 answer to their bare names, but COM10 and above are only
    reachable through the \\.\ device namespace.
    """
    name = port.strip()
    if (os.name == "nt" and name.upper().startswith("COM")
            and name[3:].isdigit() and int(name[3:]) >= 10):
        return r"\\.\%s" % name
    return name


def whpx_available():
    if os.name != "nt":
        return False
    root = os.environ.get("SystemRoot", r"C:\Windows")
    return os.path.isfile(os.path.join(root, "System32", "WinHvPlatform.dll"))


def is_admin():
    """Whether raw drive access would be allowed right now."""
    if os.name != "nt":
        return os.geteuid() == 0
    import ctypes
    try:
        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except Exception:
        return False


def relaunch_elevated():
    """Windows only: start this program again behind the UAC prompt.

    Returns True when the new copy is on its way, at which point the
    caller should quietly leave; the pipes to a child QEMU only work
    when the launcher itself is the elevated one.
    """
    import ctypes
    if getattr(sys, "frozen", False):
        exe, params = sys.executable, subprocess.list2cmdline(sys.argv[1:])
    else:
        exe, params = sys.executable, subprocess.list2cmdline(sys.argv)
    try:
        code = ctypes.windll.shell32.ShellExecuteW(None, "runas", exe,
                                                   params, None, 1)
    except Exception:
        return False
    return code > 32


def qemu_command(cfg):
    """Build the QEMU argv from a settings dict; also returns any notes.

    The PC-98 disk BIOS only enumerates the two units on the primary IDE
    channel as hard disks, so the 1st/2nd disk and a mounted folder compete
    for those; the secondary channel is the CD-ROM's.
    """
    argv = [cfg["qemu"], "-M", "pc9821", "-m", cfg.get("memory") or "64M"]
    notes = []
    if cfg.get("kvm"):
        argv += ["-accel", "kvm"]
    elif cfg.get("hyperv"):
        argv += ["-accel", "whpx"]
    if cfg.get("roms"):
        argv += ["-L", cfg["roms"]]
    floppies = [cfg[k] for k in ("fdd1", "fdd2") if cfg.get(k)]
    for unit, image in enumerate(floppies):
        # a lone drive needs no unit; two of them do
        spec = "if=floppy,unit=%d," % unit if len(floppies) > 1 else "if=floppy,"
        argv += ["-drive", spec + drive_backing(image)]
    disks = [(k, cfg.get(k)) for k in ("hdd1", "hdd2", "mount") if cfg.get(k)]
    for unit, (key, value) in enumerate(disks[:2]):
        if key == "mount":
            value = "fat98:rw:" + value
        argv += ["-drive",
                 "if=ide,bus=0,unit=%d,format=raw,file=%s" % (unit, value)]
    for key, value in disks[2:]:
        notes.append("no room for %s: the BIOS sees only two hard disks" % key)
    if cfg.get("cd"):
        argv += ["-drive", "if=ide,bus=1,unit=0,media=cdrom,readonly=on,"
                 + drive_backing(cfg["cd"])]
    for unit, key in enumerate(SCSI_KEYS):
        if cfg.get(key):
            argv += ["-drive", "if=scsi,bus=0,unit=%d," % unit
                     + drive_backing(cfg[key])]
    if cfg.get("serial"):
        argv += ["-chardev", "serial,id=ser0,path=" +
                 serial_path(cfg["serial"]),
                 "-device", "serial98,chardev=ser0"]
        notes.append("serial98 is not in QEMU yet, so this will not start"
                     " until the 8251 device lands")
    # the 86 board's FM and the Sound System's PCM are separate parts, and a
    # guest that only wants one of them should not be handed the other
    fm, pcm = cfg.get("fm", True), cfg.get("pcm", True)
    if fm or pcm:
        argv += ["-audiodev", "dsound,id=snd" if os.name == "nt"
                 else "sdl,id=snd"]
        # the boards are ISA devices of their own, not part of the machine
        if fm:
            argv += ["-device", "pc98-opna,audiodev=snd"]
        if pcm:
            argv += ["-device", "pc98-wss,audiodev=snd"]
    if cfg.get("lan") == "nat":
        # the guest sits behind QEMU's own NAT; no host privileges needed
        argv += ["-netdev", "user,id=lan",
                 "-device", "pc98-lgy98,netdev=lan"]
    if cfg.get("extra"):
        argv += cfg["extra"].split()
    return argv, notes


# ------------------------------------------------------- physical CF cards

CHUNK = 1 << 20


# floppies are known by the name on the box, not by either megabyte
FLOPPY_NAMES = {1474560: "1.44 MB", 1457664: "1.44 MB", 1261568: "1.2 MB",
                1228800: "1.2 MB", 737280: "720 kB", 1440 * 1024: "1.44 MB"}


def human_size(size):
    """Sizes here span a 1.44 MB floppy to a 2 TB disk, so pick the unit."""
    if not size:
        return "(no media)"
    if size in FLOPPY_NAMES:
        return FLOPPY_NAMES[size]
    for unit, step in (("TB", 1000.0 ** 4), ("GB", 1000.0 ** 3),
                       ("MB", 1000.0 ** 2), ("kB", 1000.0)):
        if size >= step:
            return "%.1f %s" % (size / step, unit)
    return "%d B" % size


class Disk:
    def __init__(self, path, model, size, removable, system):
        self.path, self.model = path, model
        self.size, self.removable, self.system = size, removable, system

    def __str__(self):
        tags = []
        if self.removable:
            tags.append("removable")
        if self.system:
            tags.append("SYSTEM DISK")
        return "%s  %s  %s%s" % (
            self.path, self.model or "(no model)", human_size(self.size),
            "  [%s]" % ", ".join(tags) if tags else "")


def no_window():
    """Keep helper processes from flashing a console over the window."""
    if os.name != "nt":
        return {}
    return {"creationflags": subprocess.CREATE_NO_WINDOW}


def _powershell(script):
    out = subprocess.run(["powershell", "-NoProfile", "-NonInteractive",
                          "-Command", script],
                         capture_output=True, text=True, timeout=60,
                         **no_window())
    return out.stdout


def list_disks():
    """Physical disks the host can see, system disk flagged, never hidden."""
    disks = []
    if os.name == "nt":
        system = ""
        try:
            system = _powershell(
                "(Get-Partition -DriveLetter $env:SystemDrive[0] |"
                " Get-Disk).Number").strip()
        except Exception:
            pass
        text = _powershell(
            "Get-CimInstance Win32_DiskDrive | ForEach-Object {"
            " '{0}|{1}|{2}|{3}|{4}' -f $_.Index, $_.Model, $_.Size,"
            " $_.MediaType, $_.InterfaceType }")
        for line in text.splitlines():
            parts = line.strip().split("|")
            if len(parts) != 5 or not parts[0].isdigit():
                continue
            index, model, size, media, interface = parts
            # an empty card reader reports no media type at all, so the bus
            # it hangs off is the reliable signal
            removable = "emovable" in media or interface.strip() == "USB"
            disks.append(Disk(r"\\.\PhysicalDrive" + index, model.strip(),
                              int(size or 0), removable, index == system))
        return disks

    root = "/sys/block"
    if not os.path.isdir(root):
        return disks
    for name in sorted(os.listdir(root)):
        if name.startswith(("loop", "ram", "dm-", "sr", "zram")):
            continue
        base = os.path.join(root, name)

        def value(*parts):
            try:
                with open(os.path.join(base, *parts)) as f:
                    return f.read().strip()
            except OSError:
                return ""

        size = int(value("size") or 0) * 512
        if not size:
            continue
        mounted = False
        try:
            with open("/proc/mounts") as f:
                mounted = any(line.split()[0].startswith("/dev/" + name)
                              and line.split()[1] == "/"
                              for line in f)
        except OSError:
            pass
        disks.append(Disk("/dev/" + name, value("device", "model"), size,
                          value("removable") == "1", mounted))
    return disks


def _windows_locks(device):
    """Lock and dismount the volumes on a disk so writes are not refused."""
    import ctypes
    from ctypes import wintypes

    handles = []
    index = device.rsplit("PhysicalDrive", 1)[-1]
    letters = _powershell(
        "Get-Partition -DiskNumber %s -ErrorAction SilentlyContinue |"
        " Where-Object DriveLetter | ForEach-Object { $_.DriveLetter }"
        % index)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    for letter in letters.split():
        handle = kernel32.CreateFileW(
            "\\\\.\\%s:" % letter.strip(), 0xC0000000, 3, None, 3, 0, None)
        if handle == wintypes.HANDLE(-1).value:
            continue
        returned = wintypes.DWORD()
        for code in (0x00090018, 0x00090020):    # LOCK, DISMOUNT
            kernel32.DeviceIoControl(handle, code, None, 0, None, 0,
                                     ctypes.byref(returned), None)
        handles.append(handle)
    return handles


def _windows_unlock(handles):
    if not handles:
        return
    import ctypes
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    for handle in handles:
        kernel32.CloseHandle(handle)


def find_disk(path):
    for disk in list_disks():
        if disk.path.lower() == path.lower():
            return disk
    return None


def raw_to_cf(src, device, log=print, force=False):
    """Write an image onto a physical card. This destroys what is there."""
    disk = find_disk(device)
    if disk is None:
        raise ValueError("no such disk: %s (try list-disks)" % device)
    if disk.system and not force:
        raise ValueError("%s holds the running system; refusing" % device)
    size = os.path.getsize(src)
    if disk.size and size > disk.size:
        raise ValueError("image is %d bytes, the card holds %d"
                         % (size, disk.size))
    log("writing %s to %s" % (src, disk))
    handles = _windows_locks(device) if os.name == "nt" else []
    try:
        with open(src, "rb") as fin, open(device, "r+b" if os.name == "nt"
                                          else "wb") as fout:
            done = 0
            while True:
                block = fin.read(CHUNK)
                if not block:
                    break
                # a physical disk wants whole sectors
                if len(block) % 512:
                    block += b"\0" * (512 - len(block) % 512)
                fout.write(block)
                done += len(block)
                if done % (64 << 20) == 0:
                    log("  %d MB" % (done >> 20))
            fout.flush()
            os.fsync(fout.fileno())
    except PermissionError:
        raise ValueError("permission denied; run as Administrator (Windows) "
                         "or root (Linux)")
    finally:
        _windows_unlock(handles)
    log("wrote %d bytes to %s" % (done, device))


def cf_to_raw(device, dst, log=print, limit=0):
    """Read a physical card into an image file."""
    disk = find_disk(device)
    if disk is None:
        raise ValueError("no such disk: %s (try list-disks)" % device)
    total = limit or disk.size
    log("reading %s" % disk)
    try:
        with open(device, "rb") as fin, open(dst, "wb") as fout:
            done = 0
            while done < total:
                block = fin.read(min(CHUNK, total - done))
                if not block:
                    break
                fout.write(block)
                done += len(block)
                if done % (64 << 20) == 0:
                    log("  %d MB" % (done >> 20))
    except PermissionError:
        raise ValueError("permission denied; run as Administrator (Windows) "
                         "or root (Linux)")
    log("read %d bytes into %s" % (done, dst))


def list_optical():
    """CD-ROM drives, for ripping an ISO."""
    drives = []
    if os.name == "nt":
        text = _powershell(
            "Get-CimInstance Win32_CDROMDrive | ForEach-Object {"
            " '{0}|{1}|{2}' -f $_.Drive, $_.Caption, $_.Size }")
        for line in text.splitlines():
            parts = line.strip().split("|")
            if len(parts) != 3 or not parts[0]:
                continue
            letter, model, size = parts
            drives.append(Disk(r"\\.\%s:" % letter.rstrip(":"),
                               model.strip(), int(size or 0), True, False))
        return drives
    for name in sorted(os.listdir("/sys/block")):
        if not name.startswith("sr"):
            continue
        try:
            with open("/sys/block/%s/size" % name) as f:
                size = int(f.read().strip()) * 512
            with open("/sys/block/%s/device/model" % name) as f:
                model = f.read().strip()
        except OSError:
            size, model = 0, ""
        drives.append(Disk("/dev/" + name, model, size, True, False))
    return drives


def list_floppies():
    """Floppy drives, for reading a disk into an image."""
    drives = []
    if os.name == "nt":
        text = _powershell(
            "Get-CimInstance Win32_LogicalDisk -Filter 'DriveType=2' |"
            " ForEach-Object { '{0}|{1}|{2}' -f $_.DeviceID,"
            " $_.VolumeName, $_.Size }")
        for line in text.splitlines():
            parts = line.strip().split("|")
            if len(parts) != 3 or not parts[0]:
                continue
            letter, name, size = parts
            drives.append(Disk(r"\\.\%s" % letter.strip(), name.strip(),
                               int(size or 0), True, False))
        return drives
    for name in sorted(os.listdir("/dev")):
        if name.startswith("fd") and name[2:].isdigit():
            drives.append(Disk("/dev/" + name, "floppy", 1474560, True, False))
    return drives


class SerialPort:
    def __init__(self, path, detail=""):
        self.path, self.detail = path, detail

    def __str__(self):
        if not self.detail:
            return self.path
        return "%s  %s" % (self.path, self.detail)


def _com_order(port):
    """COM2 before COM10, which a plain string sort gets backwards."""
    digits = "".join(c for c in port.path if c.isdigit())
    return (port.path.rstrip("0123456789"), int(digits or 0))


def list_serial_ports():
    """Host serial ports, for handing one to the guest's RS-232C.

    Windows keeps the authoritative list in the registry, which answers
    instantly; asking WMI for prettier names costs seconds.
    """
    ports = []
    if os.name == "nt":
        import winreg

        try:
            key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                                 r"HARDWARE\DEVICEMAP\SERIALCOMM")
        except OSError:
            return ports        # no ports at all: the key is absent
        with key:
            for i in range(winreg.QueryInfoKey(key)[1]):
                # the driver's device name hints at what the port is
                device, name, _ = winreg.EnumValue(key, i)
                ports.append(SerialPort(name, device))
        return sorted(ports, key=_com_order)

    # /dev/serial/by-id names the adapter, which is what a user recognises
    described = {}
    by_id = "/dev/serial/by-id"
    if os.path.isdir(by_id):
        for name in os.listdir(by_id):
            described[os.path.realpath(os.path.join(by_id, name))] = name
    try:
        names = os.listdir("/dev")
    except OSError:
        return ports
    for name in names:
        if not name.startswith(("ttyS", "ttyUSB", "ttyACM")):
            continue
        # most ttyS* are stubs with no hardware behind them
        if not os.path.exists("/sys/class/tty/%s/device" % name):
            continue
        path = "/dev/" + name
        ports.append(SerialPort(path, described.get(path, "")))
    return sorted(ports, key=_com_order)


def read_device(device, dst, log=print, size=0):
    """Copy a whole device into a file: CF card, CD-ROM or floppy."""
    if not size:
        for finder in (find_disk, lambda p: _match(list_optical(), p),
                       lambda p: _match(list_floppies(), p)):
            found = finder(device)
            if found is not None:
                size = found.size
                log("reading %s" % found)
                break
    try:
        with open(device, "rb") as fin, open(dst, "wb") as fout:
            done = 0
            while not size or done < size:
                want = CHUNK if not size else min(CHUNK, size - done)
                block = fin.read(want)
                if not block:
                    break
                fout.write(block)
                done += len(block)
                if done % (64 << 20) == 0:
                    log("  %d MB" % (done >> 20))
    except PermissionError:
        raise ValueError("permission denied; run as Administrator (Windows) "
                         "or root (Linux)")
    if not done:
        raise ValueError("read nothing from %s; is a disc loaded?" % device)
    log("read %d bytes into %s" % (done, dst))


def _match(disks, path):
    for disk in disks:
        if disk.path.lower() == path.lower():
            return disk
    return None


# -------------------------------------------------------------------- qcow2
#
# Enough of the format to move whole images in and out: version 2 and 3,
# no compression, no encryption and no backing file.  Writing lays the
# metadata out in one pass, which is possible because the whole payload is
# known up front; clusters that are entirely zero are left unallocated, so
# a mostly empty 4 GB volume still costs almost nothing on disk.

QCOW_MAGIC = 0x514649FB
QCOW_OFLAG_COPIED = 1 << 63
QCOW_OFLAG_COMPRESSED = 1 << 62
QCOW_OFFSET_MASK = 0x00FFFFFFFFFFFE00
QCOW_CLUSTER_BITS = 16


def is_qcow2(path):
    try:
        with open(path, "rb") as f:
            return struct.unpack(">I", f.read(4))[0] == QCOW_MAGIC
    except (OSError, struct.error):
        return False


def qcow2_read(path, log=print):
    """Decode a qcow2 file into the raw bytes it stands for."""
    with open(path, "rb") as f:
        head = f.read(104)
        if len(head) < 72:
            raise ValueError("not a qcow2 file")
        (magic, version, backing_offset, backing_size, cluster_bits, size,
         crypt, l1_size, l1_offset, refcount_offset, refcount_clusters,
         snapshots, snapshots_offset) = struct.unpack(">IIQIIQIIQQIIQ", head[:72])
        if magic != QCOW_MAGIC:
            raise ValueError("not a qcow2 file")
        if version not in (2, 3):
            raise ValueError("qcow2 version %d is not supported" % version)
        if backing_offset:
            raise ValueError("qcow2 with a backing file is not supported")
        if crypt:
            raise ValueError("encrypted qcow2 is not supported")
        if version == 3 and len(head) >= 76:
            incompatible = struct.unpack(">Q", head[72:80])[0]
            if incompatible:
                raise ValueError("qcow2 uses features this tool lacks "
                                 "(0x%x)" % incompatible)

        cluster = 1 << cluster_bits
        per_l2 = cluster // 8
        f.seek(l1_offset)
        l1 = struct.unpack(">%dQ" % l1_size, f.read(8 * l1_size))

        out = bytearray(size)
        zero = bytes(cluster)
        l2_cache = {}
        for index in range((size + cluster - 1) // cluster):
            entry = l1[index // per_l2] if index // per_l2 < l1_size else 0
            l2_offset = entry & QCOW_OFFSET_MASK
            if not l2_offset:
                continue
            table = l2_cache.get(l2_offset)
            if table is None:
                f.seek(l2_offset)
                table = struct.unpack(">%dQ" % per_l2, f.read(cluster))
                l2_cache[l2_offset] = table
            item = table[index % per_l2]
            if item & QCOW_OFLAG_COMPRESSED:
                raise ValueError("compressed qcow2 clusters are not supported")
            offset = item & QCOW_OFFSET_MASK
            if not offset:
                continue
            f.seek(offset)
            block = f.read(cluster)
            start = index * cluster
            out[start:start + len(block)] = block[:max(0, size - start)]
        log("read %d bytes from %s" % (size, path))
        return bytes(out)


def qcow2_write(path, payload, log=print):
    """Encode raw bytes as a fresh qcow2 image."""
    cluster = 1 << QCOW_CLUSTER_BITS
    size = len(payload)
    virtual = (size + cluster - 1) // cluster
    per_l2 = cluster // 8
    per_refblock = cluster // 2          # refcount_order 4, 16 bit entries
    zero = bytes(cluster)

    used = [i for i in range(virtual)
            if payload[i * cluster:(i + 1) * cluster].strip(b"\0")]
    l2_tables = (virtual + per_l2 - 1) // per_l2 or 1
    l1_clusters = (l2_tables * 8 + cluster - 1) // cluster or 1

    # the refcount table has to describe itself, so settle it by iteration
    refblocks, rt_clusters = 1, 1
    for _ in range(8):
        total = (1 + l1_clusters + rt_clusters + refblocks + l2_tables
                 + len(used))
        want_blocks = (total + per_refblock - 1) // per_refblock or 1
        want_rt = (want_blocks * 8 + cluster - 1) // cluster or 1
        if (want_blocks, want_rt) == (refblocks, rt_clusters):
            break
        refblocks, rt_clusters = want_blocks, want_rt

    l1_at = 1
    rt_at = l1_at + l1_clusters
    rb_at = rt_at + rt_clusters
    l2_at = rb_at + refblocks
    data_at = l2_at + l2_tables
    total = data_at + len(used)

    header = bytearray(cluster)
    struct.pack_into(">IIQIIQIIQQIIQ", header, 0, QCOW_MAGIC, 3, 0, 0,
                     QCOW_CLUSTER_BITS, size, 0, l2_tables, l1_at * cluster,
                     rt_at * cluster, rt_clusters, 0, 0)
    struct.pack_into(">QQQII", header, 72, 0, 0, 0, 4, 104)

    l1 = bytearray(l1_clusters * cluster)
    for i in range(l2_tables):
        struct.pack_into(">Q", l1, i * 8,
                         ((l2_at + i) * cluster) | QCOW_OFLAG_COPIED)

    l2 = bytearray(l2_tables * cluster)
    for slot, index in enumerate(used):
        struct.pack_into(">Q", l2, index * 8,
                         ((data_at + slot) * cluster) | QCOW_OFLAG_COPIED)

    rt = bytearray(rt_clusters * cluster)
    for i in range(refblocks):
        struct.pack_into(">Q", rt, i * 8, (rb_at + i) * cluster)
    rb = bytearray(refblocks * cluster)
    for index in range(total):
        struct.pack_into(">H", rb, index * 2, 1)

    with open(path, "wb") as f:
        f.write(header)
        f.write(l1)
        f.write(rt)
        f.write(rb)
        f.write(l2)
        for index in used:
            block = payload[index * cluster:(index + 1) * cluster]
            f.write(block.ljust(cluster, b"\0"))
    log("wrote %s (%d of %d clusters used, %d bytes on disk)"
        % (path, len(used), virtual, os.path.getsize(path)))


# ------------------------------------------------- source/destination pairs

KIND_HDI = "HDI"
KIND_FDI = "FDI"
KIND_RAW_HDD = "RAW (HDD)"
KIND_RAW_FDD = "RAW (FDD)"
KIND_QCOW2 = "QCOW2"
KIND_DRIVE = "Host Drive"
KIND_FOLDER = "Folder"
KINDS = (KIND_HDI, KIND_FDI, KIND_RAW_HDD, KIND_RAW_FDD, KIND_QCOW2,
         KIND_DRIVE, KIND_FOLDER)
IMAGE_KINDS = (KIND_HDI, KIND_FDI, KIND_RAW_HDD, KIND_RAW_FDD, KIND_QCOW2)


def removable_devices():
    """Only what is safe to touch: removable media, never a fixed disk.

    A CF card reaches the host through a USB reader or a CF-IDE adapter, so
    the filter is "removable and not the system disk"; NVMe, SATA and IDE
    fixed disks never appear, which is the point.
    """
    out = []
    for disk in list_disks():
        if disk.removable and not disk.system:
            disk.kind = "disk"
            out.append(disk)
    for disk in list_optical():
        disk.kind = "optical"
        out.append(disk)
    for disk in list_floppies():
        disk.kind = "floppy"
        out.append(disk)
    return out


def device_kind(path, known=None):
    """Kind of a device, from an already-scanned list when there is one.

    Scanning asks the host about every drive, which is slow enough to be
    felt, so callers pass the list they already have.
    """
    for disk in known if known is not None else removable_devices():
        if disk.path.lower() == path.lower():
            return getattr(disk, "kind", "disk")
    return "disk"


def looks_like_hdd(payload):
    """A PC-98 hard disk starts with an IPL; a floppy starts with its BPB."""
    return payload[4:8] == b"IPL1"


def _floppy_kind(payload):
    bps = struct.unpack_from("<H", payload, 11)[0] if len(payload) > 13 else 0
    return "1.44" if bps == 512 else "1.2"


def load_side(kind, ref, log):
    """Read a source into (payload, is_hdd); a folder stays a folder."""
    if kind == KIND_FOLDER:
        return None, None
    if kind == KIND_QCOW2:
        payload = qcow2_read(ref, log)
        return payload, looks_like_hdd(payload)
    if kind == KIND_DRIVE:
        tmp = tempfile.mktemp(prefix="pc98-read-", suffix=".raw")
        try:
            read_device(ref, tmp, log)
            with open(tmp, "rb") as f:
                payload = f.read()
        finally:
            if os.path.exists(tmp):
                os.remove(tmp)
        return payload, looks_like_hdd(payload)
    payload = read_image(ref)[0]
    return payload, kind in (KIND_HDI, KIND_RAW_HDD)


def store_side(kind, ref, payload, is_hdd, log, force=False):
    """Write a payload out as an image, onto a device, or into a folder."""
    if kind == KIND_FOLDER:
        base = 0
        if is_hdd:
            table = list(partitions(payload, SECTORS, HEADS))
            if not table:
                raise ValueError("no PC-98 partition table found")
            log("partitions:")
            for n, (name, off) in enumerate(table, 1):
                log("  %d: %-16s at byte %d" % (n, name or "(unnamed)", off))
            base = table[0][1]
        if (payload[base + 54:base + 58] != b"FAT1"
                and payload[base + 82:base + 86] != b"FAT3"):
            raise ValueError("no FAT boot sector at byte %d" % base)
        fat = FatReader(payload, base)
        count = extract_fat(fat, fat.root_records(), ref, log)
        log("extracted %d files to %s" % (count, ref))
        return

    if kind == KIND_DRIVE:
        tmp = tempfile.mktemp(prefix="pc98-write-", suffix=".raw")
        try:
            with open(tmp, "wb") as f:
                f.write(payload)
            raw_to_cf(tmp, ref, log, force)
        finally:
            if os.path.exists(tmp):
                os.remove(tmp)
        return

    if kind == KIND_QCOW2:
        qcow2_write(ref, payload, log)
        return

    if kind in (KIND_RAW_HDD, KIND_RAW_FDD):
        with open(ref, "wb") as f:
            f.write(payload)
        log("wrote %d bytes to %s" % (len(payload), ref))
        return

    if kind == KIND_HDI:
        cylinders = -(-len(payload) // TRACK)
        payload = payload.ljust(cylinders * TRACK, b"\0")
        head = struct.pack("<8I", 0, HDD_TYPE, 4096, len(payload),
                           SECTOR, SECTORS, HEADS, cylinders)
    else:
        fmt = FDD_FORMATS[_floppy_kind(payload)]
        cylinders = fmt[10]
        head = struct.pack("<8I", 0, fmt[11], 4096, len(payload),
                           fmt[0], fmt[8], fmt[9], cylinders)
    with open(ref, "wb") as f:
        f.write(head.ljust(4096, b"\0"))
        f.write(payload)
    log("wrote %d bytes to %s" % (len(payload) + 4096, ref))


def convert_pair(src_kind, src_ref, dst_kind, dst_ref, opts=None, log=print):
    """Everything the Convert tab does, as one source-to-destination step."""
    opts = opts or {}
    if src_kind == dst_kind:
        raise ValueError("source and destination are the same kind")
    if not src_ref or not dst_ref:
        raise ValueError("choose both a source and a destination")

    if src_kind == KIND_FOLDER:
        if opts.get("into"):
            # fill an image that already exists instead of building one
            if dst_kind == KIND_DRIVE:
                raise ValueError("write into an image file, not a device")
            if not os.path.isfile(dst_ref):
                raise ValueError("%s is not an existing image" % dst_ref)
            folder_into_image(src_ref, dst_ref,
                              int(opts.get("partition") or 1), log)
            return
        # build a fresh volume shaped by whatever the destination is
        if dst_kind == KIND_DRIVE:
            hdd = device_kind(dst_ref) != "floppy"
        else:
            hdd = dst_kind in (KIND_HDI, KIND_RAW_HDD, KIND_QCOW2)
        tmp = tempfile.mktemp(prefix="pc98-build-",
                              suffix=".raw" if hdd else ".raw")
        try:
            if hdd:
                folder_to_hard_disk(src_ref, tmp,
                                    int(opts.get("size") or 40),
                                    opts.get("label") or "NO NAME",
                                    opts.get("boot") or None, log,
                                    fat32=bool(opts.get("fat32")))
            else:
                folder_to_floppy(src_ref, tmp,
                                 opts.get("label") or "NO NAME",
                                 opts.get("boot") or None,
                                 opts.get("format") or "1.2", log)
            with open(tmp, "rb") as f:
                payload = f.read()
        finally:
            if os.path.exists(tmp):
                os.remove(tmp)
        store_side(dst_kind, dst_ref, payload, hdd, log,
                   force="force" in opts)
        return

    payload, is_hdd = load_side(src_kind, src_ref, log)
    if src_kind == KIND_DRIVE and dst_kind in (KIND_HDI, KIND_RAW_HDD,
                                              KIND_QCOW2):
        is_hdd = True
    elif src_kind == KIND_DRIVE and dst_kind in (KIND_FDI, KIND_RAW_FDD):
        is_hdd = False
    # a hard disk cannot become a floppy container, or the geometry in the
    # header would describe something the payload is not
    want = SHAPE.get(dst_kind)
    if want is not None and want != is_hdd:
        raise ValueError("this is a %s image; %s expects a %s"
                         % ("hard disk" if is_hdd else "floppy", dst_kind,
                            "hard disk" if want else "floppy"))
    store_side(dst_kind, dst_ref, payload, is_hdd, log,
               force="force" in opts)


# ---------------------------------------------------------------------- CLI

RAW_TYPES = [("Raw images", "*.raw *.img"), ("All files", "*.*")]
HDI_TYPES = [("HDI images", "*.hdi"), ("All files", "*.*")]
FDI_TYPES = [("FDI images", "*.fdi"), ("All files", "*.*")]
ISO_TYPES = [("ISO images", "*.iso"), ("All files", "*.*")]
KIND_SUFFIX = {KIND_HDI: ".hdi", KIND_FDI: ".fdi", KIND_RAW_HDD: ".raw",
               KIND_RAW_FDD: ".raw", KIND_QCOW2: ".qcow2"}
# which kinds pin the geometry: qcow2, a device and a folder take either
SHAPE = {KIND_HDI: True, KIND_RAW_HDD: True,
         KIND_FDI: False, KIND_RAW_FDD: False}
QCOW_TYPES = [("QCOW2 images", "*.qcow2 *.qcow *.img"), ("All files", "*.*")]
DISK_TYPES = [("Raw images", "*.raw *.img *.qcow2"), ("All files", "*.*")]
CD_TYPES = [("CD-ROM images", "*.iso *.cue *.bin"), ("All files", "*.*")]
EXE_TYPES = [("QEMU", "qemu-system-*"), ("All files", "*.*")]

SIZE_FIELD = ("size", "Size (MB)", "text", "40")
LABEL_FIELD = ("label", "Volume label", "text", "NO NAME")
FORMAT_FIELD = ("format", "Format (1.2/1.44)", "text", "1.2")
PART_FIELD = ("partition", "Partition", "text", "1")
HBOOT_FIELD = ("boot", "Boot record from", "open", "")
FBOOT_FIELD = ("boot", "Boot code from", "open", "")

# label, command, input kind, output kind, extra fields
CONVERSIONS = [
    ("HDI to RAW", "hdi-to-raw", ("open", HDI_TYPES),
     ("save", ".raw", RAW_TYPES), ()),
    ("FDI to RAW", "fdi-to-raw", ("open", FDI_TYPES),
     ("save", ".raw", RAW_TYPES), ()),
    ("RAW to HDI", "raw-to-hdi", ("open", RAW_TYPES),
     ("save", ".hdi", HDI_TYPES), ()),
    ("RAW to FDI", "raw-to-fdi", ("open", RAW_TYPES),
     ("save", ".fdi", FDI_TYPES), ()),
    ("HDI to Folder", "hdd-to-folder", ("open", HDI_TYPES),
     ("dir", "", None), (PART_FIELD,)),
    ("RAW HDD to Folder", "hdd-to-folder", ("open", RAW_TYPES),
     ("dir", "", None), (PART_FIELD,)),
    ("FDI to Folder", "fdd-to-folder", ("open", FDI_TYPES),
     ("dir", "", None), ()),
    ("RAW FDD to Folder", "fdd-to-folder", ("open", RAW_TYPES),
     ("dir", "", None), ()),
    ("Folder to HDI", "folder-to-hdd", ("dir", None),
     ("save", ".hdi", HDI_TYPES), (SIZE_FIELD, LABEL_FIELD, HBOOT_FIELD)),
    ("Folder to RAW HDD", "folder-to-hdd", ("dir", None),
     ("save", ".raw", RAW_TYPES), (SIZE_FIELD, LABEL_FIELD, HBOOT_FIELD)),
    ("Folder to FDI", "folder-to-fdd", ("dir", None),
     ("save", ".fdi", FDI_TYPES), (LABEL_FIELD, FORMAT_FIELD, FBOOT_FIELD)),
    ("Folder to RAW FDD", "folder-to-fdd", ("dir", None),
     ("save", ".raw", RAW_TYPES), (LABEL_FIELD, FORMAT_FIELD, FBOOT_FIELD)),
    ("Folder to ISO", "folder-to-iso", ("dir", None),
     ("save", ".iso", ISO_TYPES), (("label", "Volume label", "text", "PC98"),)),
    ("ISO to Folder", "iso-to-folder", ("open", ISO_TYPES),
     ("dir", "", None), ()),
    ("RAW to CF card", "raw-to-cf", ("open", RAW_TYPES), ("disk", "", None),
     ()),
    ("CF card to RAW", "cf-to-raw", ("disk", None), ("save", ".raw",
     RAW_TYPES), ()),
    ("CD-ROM drive to ISO", "cd-to-iso", ("optical", None),
     ("save", ".iso", ISO_TYPES), ()),
    ("FDD drive to RAW", "fdd-to-raw", ("floppy", None),
     ("save", ".raw", RAW_TYPES), ()),
]

COMMANDS = sorted({c[1] for c in CONVERSIONS}
                  | {"new-hdd", "new-fdd", "list-disks", "folder-into-image",
                     "raw-to-qcow2", "qcow2-to-raw"})


def run_command(name, src, dst, opts, log=print):
    """One entry point for every conversion, shared by the CLI and the GUI."""
    if name in ("hdi-to-raw", "fdi-to-raw"):
        payload = read_image(src)[0]
        with open(dst, "wb") as f:
            f.write(payload)
        log("%s -> %s (%d bytes)" % (src, dst, len(payload)))
    elif name == "raw-to-hdi":
        convert_image(src, dst, HDD_TYPE, SECTORS, HEADS, SECTOR, log)
    elif name == "raw-to-fdi":
        f = FDD_FORMATS[fdd_format_of(src)]
        convert_image(src, dst, f[11], f[8], f[9], f[0], log)
    elif name == "hdd-to-folder":
        image_to_folder(src, dst, False, int(opts.get("partition") or 1), log)
    elif name == "fdd-to-folder":
        image_to_folder(src, dst, True, 1, log)
    elif name == "folder-to-hdd":
        folder_to_hard_disk(src, dst, int(opts.get("size") or 40),
                            opts.get("label") or "NO NAME",
                            opts.get("boot") or None, log,
                            fat32="fat32" in opts)
    elif name == "folder-into-image":
        folder_into_image(src, dst, int(opts.get("partition") or 1), log)
    elif name == "folder-to-fdd":
        folder_to_floppy(src, dst, opts.get("label") or "NO NAME",
                         opts.get("boot") or None,
                         opts.get("format") or "1.2", log)
    elif name == "folder-to-iso":
        folder_to_iso(src, dst, opts.get("label") or "PC98", log)
    elif name == "iso-to-folder":
        iso_to_folder(src, dst, log)
    elif name == "new-hdd":
        new_image(dst, megabytes=int(opts.get("size") or 40),
                  label=opts.get("label") or "NO NAME",
                  fat32="fat32" in opts, log=log)
    elif name == "new-fdd":
        new_image(dst, fmt=opts.get("format") or "1.2",
                  label=opts.get("label") or "NO NAME", log=log)
    elif name == "raw-to-cf":
        raw_to_cf(src, dst, log, force="force" in opts)
    elif name == "cf-to-raw":
        cf_to_raw(src, dst, log)
    elif name == "raw-to-qcow2":
        with open(src, "rb") as f:
            qcow2_write(dst, f.read(), log)
    elif name == "qcow2-to-raw":
        with open(dst, "wb") as f:
            f.write(qcow2_read(src, log))
    elif name in ("cd-to-iso", "fdd-to-raw"):
        read_device(src, dst, log)
    elif name == "list-disks":
        for group, finder in (("disks", list_disks),
                              ("optical", list_optical),
                              ("floppy", list_floppies)):
            found = finder()
            if found:
                print("%s:" % group)
                for disk in found:
                    print("  %s" % disk)
    else:
        raise ValueError("unknown command: %s" % name)


USAGE = """PC-98 disk images and QEMU launcher

  virtpc98.py                  open the window
  virtpc98.py --help           this text
  virtpc98.py <command> [options]  run on the terminal

Images
  hdi-to-raw     IN OUT          fdi-to-raw     IN OUT
  raw-to-hdi     IN OUT          raw-to-fdi     IN OUT
  hdd-to-folder  IN OUTDIR       fdd-to-folder  IN OUTDIR
  folder-to-hdd  INDIR OUT       folder-to-fdd  INDIR OUT
  folder-to-iso  INDIR OUT       iso-to-folder  IN OUTDIR
  raw-to-qcow2   IN OUT          qcow2-to-raw   IN OUT
  new-hdd        OUT             new-fdd        OUT
  folder-into-image INDIR IMAGE  copy files into an existing image

Host devices (CF cards run most PC-9821s today through an IDE adapter)
  list-disks                     what the host can see
  raw-to-cf      IN DEVICE       cf-to-raw      DEVICE OUT
  cd-to-iso      DEVICE OUT      fdd-to-raw     DEVICE OUT

Talking to a disk device needs Administrator on Windows or root on Linux,
for reading as well as writing; writing overwrites the card.  The disk
holding the running system is refused.

An OUT name ending in .raw or .img is written bare; .hdi and .fdi get the
Anex86 header.

Options
  --size=MB        hard disk size (40 80 160 320 640 1200 2100 4300)
  --format=1.2     floppy format, 1.2 or 1.44
  --fat32          format a new hard disk as FAT32 (80 MB and up;
                   for Windows 95 OSR2 and later)
  --label=NAME     volume label
  --partition=N    which partition to read or update (default 1)
  --boot=IMAGE     take boot code from an existing bootable image
  --force          allow writing to the system disk (do not)
  --quiet          only report errors

Booting
  virtpc98.py boot [--hdd1=IMAGE] [--hdd2=IMAGE] [--fdd1=IMAGE] [--fdd2=IMAGE]
               [--cd=ISO] [--scsi1=IMAGE] ... [--scsi4=IMAGE]
               [--serial=PORT] [--mount=DIR] [--roms=DIR]
               [--memory=64M] [--kvm] [--hyperv] [--qemu=PATH]
               [--sound=86|wss|none] [--lan] [--dry-run]

  --kvm and --hyperv are experimental; without them the guest runs
  under TCG, which is the tested path.

  --sound fits one board: 86 is the PC-9801-86 (FM and PCM), wss is the
  Mate-X built-in Sound System.  The default is 86.

  --lan adds an LGY-98 board behind QEMU's user-mode NAT.

  --fdd1, --fdd2 and --cd also take a host drive; the Drive button fills
  one in, and a 1.2 MB PC-98 disk needs a 3-mode drive.

  --serial hands a host port to the guest's RS-232C: COM3 on Windows,
  /dev/ttyUSB0 here.  The Scan button lists what the host has.
"""


def boot_cli(opts, log):
    cfg = {k: opts.get(k, "") for k in
           ("qemu", "roms", "hdd1", "hdd2", "fdd1", "fdd2", "cd", "mount")
           + SCSI_KEYS + ("serial", "memory", "extra")}
    board = str(opts.get("sound") or "86").lower()
    if "no_sound" in opts:
        board = "none"
    cfg["fm"] = board in ("86", "opna")
    cfg["pcm"] = board == "wss"
    cfg["lan"] = "nat" if "lan" in opts else ""
    cfg["kvm"] = "kvm" in opts
    cfg["hyperv"] = "hyperv" in opts
    if not cfg["qemu"]:
        cfg["qemu"] = find(QEMU_NAMES)
    if not cfg["qemu"]:
        print("no QEMU executable found; pass --qemu=PATH")
        return 1
    if not cfg["roms"]:
        cfg["roms"] = compat_roms(cfg["qemu"])
    argv, notes = qemu_command(cfg)
    for note in notes:
        log(note)
    line = " ".join('"%s"' % a if " " in a else a for a in argv)
    if "dry_run" in opts:
        print(line)
        return 0
    log(line)
    return subprocess.call(argv)


def main(argv):
    args = argv[1:]
    if not args:
        return gui()
    if args[0] in ("-h", "--help", "help"):
        print(USAGE)
        return 0

    command = args[0]
    opts, positional = {}, []
    for a in args[1:]:
        if a.startswith("--"):
            key, _, value = a[2:].partition("=")
            opts[key.replace("-", "_")] = value or "1"
        else:
            positional.append(a)
    log = _log("quiet" in opts)

    if command == "boot":
        return boot_cli(opts, log)
    if command not in COMMANDS:
        print("unknown command: %s\n" % command)
        print(USAGE)
        return 2
    if command == "list-disks":
        run_command(command, None, None, opts, log)
        return 0
    need = 1 if command.startswith("new-") else 2
    if len(positional) != need:
        print("%s needs %d path argument%s"
              % (command, need, "" if need == 1 else "s"))
        return 2
    try:
        run_command(command, positional[0] if need == 2 else None,
                    positional[-1], opts, log)
    except (OSError, ValueError) as err:
        print("error: %s" % err)
        return 1
    return 0


# ---------------------------------------------------------------------- GUI

SCSI_TYPES = [("Disk images", "*.raw *.img *.qcow2 *.hdi"),
              ("All files", "*.*")]

# the memory slider's stops, spelled the way -m wants them (M, not MB)
MEMORY_STEPS = (("640K",)
                + tuple("%dM" % (1 << i) for i in range(1, 10))
                + tuple("%dG" % (1 << i) for i in range(6)))

# label shown in the drop-down, value handed to qemu_command as cfg["lan"]
NETWORK_CHOICES = ("None", "NAT")
NETWORK_ARG = {"None": "", "NAT": "nat"}

# key, label, browse kind, file types, host devices this row can take.
# The left column is what the machine boots from, the right is how it runs.
STORAGE_ROWS = (("hdd1", "IDE HDD 1", "file", DISK_TYPES, "disk"),
                ("hdd2", "IDE HDD 2", "file", DISK_TYPES, "disk"),
                ("cd", "IDE CD-ROM", "file", CD_TYPES, "optical"),
                ("mount", "IDE Folder Share", "dir", None, None),
                ("fdd1", "FDD 1", "file", DISK_TYPES, "floppy"),
                ("fdd2", "FDD 2", "file", DISK_TYPES, "floppy"),
                ("scsi1", "SCSI 1", "file", SCSI_TYPES, "disk"),
                ("scsi2", "SCSI 2", "file", SCSI_TYPES, "disk"),
                ("scsi3", "SCSI 3", "file", SCSI_TYPES, "disk"),
                ("scsi4", "SCSI 4", "file", SCSI_TYPES, "disk"))
SCSI_KEYS = ("scsi1", "scsi2", "scsi3", "scsi4")
PATH_ROWS = (("qemu", "QEMU", "file", EXE_TYPES, None),
             ("roms", "BIOS Folder", "dir", None, None))
BOOT_ROWS = STORAGE_ROWS + PATH_ROWS

LABEL_WIDTH = 16        # so both columns start their fields at the same x

# One board per machine, the way a real one was fitted: label, the 86
# board (FM and PCM on the same card), the built-in Sound System
SOUND_CHOICES = (("PC-9801-86", True, False),
                 ("WSS", False, True),
                 ("None", False, False))


def sound_label(fm, pcm):
    for label, has_fm, has_pcm in SOUND_CHOICES:
        if (has_fm, has_pcm) == (bool(fm), bool(pcm)):
            return label
    # anything else came from the days of two boards at once
    return SOUND_CHOICES[0][0] if fm else SOUND_CHOICES[1][0]


def sound_parts(label):
    for name, has_fm, has_pcm in SOUND_CHOICES:
        if name == label:
            return has_fm, has_pcm
    return True, False


def gui():
    import tkinter as tk
    from tkinter import filedialog, ttk

    class App(tk.Tk):
        def __init__(self):
            tk.Tk.__init__(self)
            self.title("QEMU Virtualization Platform for PC98 Architecture")
            self.minsize(840, 620)
            self.columnconfigure(0, weight=1)
            self.busy = False
            self.process = None
            self.saved_roms = ""

            book = ttk.Notebook(self)
            book.grid(row=0, column=0, sticky="ew", padx=8, pady=(8, 4))
            book.add(self.build_boot(book), text="Boot")
            book.add(self.build_create(book), text="Create")
            book.add(self.build_convert(book), text="Convert")

            self.log = tk.Text(self, height=14, wrap="none")
            self.log.grid(row=1, column=0, sticky="nsew", padx=8, pady=(4, 8))
            bar = ttk.Scrollbar(self, orient="vertical",
                                command=self.log.yview)
            bar.grid(row=1, column=1, sticky="ns", pady=(4, 8))
            self.log.configure(yscrollcommand=bar.set, state="disabled")
            self.rowconfigure(1, weight=1)

            self.load()
            self.protocol("WM_DELETE_WINDOW", self.close)

        # ------------------------------------------------------------ util
        def write(self, text):
            self.log.configure(state="normal")
            self.log.insert("end", text if text.endswith("\n") else text + "\n")
            self.log.see("end")
            self.log.configure(state="disabled")

        def post(self, text):
            self.after(0, self.write, text)

        def work(self, fn, done=None):
            if self.busy:
                return
            self.busy = True

            def runner():
                try:
                    fn(self.post)
                    ok = True
                except Exception as err:
                    self.post("%s: %s" % (type(err).__name__, err))
                    ok = False
                self.after(0, finish, ok)

            def finish(ok):
                self.busy = False
                self.write("done" if ok else "failed")
                if ok and done:
                    done()

            threading.Thread(target=runner, daemon=True).start()

        # ------------------------------------------------------------ boot
        def add_clear(self, frame, row, key):
            """Every field gets one, in the same column, so it is findable."""
            ttk.Button(frame, text="Clear", width=7,
                       command=lambda: self.vars[key].set("")).grid(
                row=row, column=4, padx=(4, 0), pady=2)

        def add_row(self, frame, row, key, text, browse, types, device):
            """One labelled path field with its buttons; returns the next row."""
            ttk.Label(frame, text=text, width=LABEL_WIDTH).grid(
                row=row, column=0, sticky="w", pady=2)
            entry = ttk.Entry(frame, textvariable=self.vars[key])
            entry.grid(row=row, column=1, sticky="ew", pady=2)
            self.entries[key] = entry
            button = ttk.Button(frame, text="...", width=3,
                                command=lambda: self.pick_row(key, browse,
                                                              types))
            button.grid(row=row, column=2, padx=(6, 0), pady=2)
            self.browsers[key] = button
            # column 3 is where a row reaches for real hardware, if it can
            if device:
                ttk.Button(frame, text="Drive...", width=9,
                           command=lambda: self.pick_drive(key, device)).grid(
                    row=row, column=3, padx=(4, 0), pady=2)
            self.add_clear(frame, row, key)
            return row + 1

        def add_group(self, frame, row, title):
            """A titled box across the column; the caller fills its grid."""
            group = ttk.LabelFrame(frame, text=title, padding=6)
            group.grid(row=row, column=0, columnspan=5, sticky="ew",
                       pady=(8, 0))
            group.columnconfigure(1, weight=1)
            return group

        def add_checks(self, group, row, boxes):
            """A line of checkbuttons, flush with the group's own edge.

            Indenting them to the field column would push the accelerator
            pair wider than the half they sit in.
            """
            line = ttk.Frame(group)
            line.grid(row=row, column=0, columnspan=5, sticky="w")
            made = []
            for text, var in boxes:
                box = ttk.Checkbutton(line, text=text, variable=var)
                box.pack(side="left", padx=(0, 12))
                made.append(box)
            return made

        def build_boot(self, parent):
            frame = ttk.Frame(parent, padding=8)
            frame.columnconfigure(0, weight=1)
            self.vars = {k: tk.StringVar() for k, _, _, _, _ in BOOT_ROWS}
            # not a file, so it gets its own row rather than a BOOT_ROWS entry
            self.vars["serial"] = tk.StringVar()
            self.compat = tk.BooleanVar(value=True)
            self.memory = tk.StringVar(value="64M")
            self.extra = tk.StringVar()
            self.sound = tk.StringVar(value=SOUND_CHOICES[0][0])
            self.kvm = tk.BooleanVar(value=False)
            self.hyperv = tk.BooleanVar(value=False)
            self.entries, self.browsers = {}, {}

            halves = ttk.Frame(frame)
            halves.grid(row=0, column=0, sticky="ew")
            left, right = ttk.Frame(halves), ttk.Frame(halves)
            for column, half in ((0, left), (1, right)):
                # uniform keeps the two sides the same width whatever is in them
                halves.columnconfigure(column, weight=1, uniform="half")
                # nsew so the shorter column still fills the row's height,
                # which is what lets the buttons sit at its foot
                half.grid(row=0, column=column, sticky="nsew",
                          padx=(0, 8) if column == 0 else (8, 0))
                half.columnconfigure(1, weight=1)

            storage = self.add_group(left, 0, "Storage")
            inner = 0
            for spec in STORAGE_ROWS:
                inner = self.add_row(storage, inner, *spec)

            hardware = self.add_group(right, 0, "Hardware")
            row = 1
            ttk.Label(hardware, text="Memory", width=LABEL_WIDTH).grid(
                row=0, column=0, sticky="w", pady=2)
            self.mem_scale = ttk.Scale(hardware, from_=0,
                                       to=len(MEMORY_STEPS) - 1,
                                       orient="horizontal",
                                       command=self.slide_memory)
            self.mem_scale.grid(row=0, column=1, sticky="ew", pady=2)
            # free text on purpose: the slider covers the usual sizes and
            # the box still takes anything -m would
            ttk.Entry(hardware, textvariable=self.memory, width=9).grid(
                row=0, column=3, padx=(4, 0), pady=2)
            self.memory.trace_add("write", self.memory_edited)
            self.memory_edited()
            ttk.Label(hardware, text="Serial Port", width=LABEL_WIDTH).grid(
                row=1, column=0, sticky="w", pady=2)
            self.serial_box = ttk.Combobox(hardware,
                                           textvariable=self.vars["serial"])
            self.serial_box.grid(row=1, column=1, sticky="ew", pady=2)
            # Scan sits where the other rows keep Drive...: both go looking
            # for real hardware
            ttk.Button(hardware, text="Scan", width=9,
                       command=self.scan_serial).grid(
                row=1, column=3, padx=(4, 0), pady=2)
            self.add_clear(hardware, 1, "serial")
            ttk.Label(hardware, text="Sound", width=LABEL_WIDTH).grid(
                row=2, column=0, sticky="w", pady=2)
            # a fixed set of boards, so the field is a choice and not free text
            ttk.Combobox(hardware, textvariable=self.sound, state="readonly",
                         values=[c[0] for c in SOUND_CHOICES]).grid(
                row=2, column=1, columnspan=4, sticky="ew", pady=2)
            ttk.Label(hardware, text="Network", width=LABEL_WIDTH).grid(
                row=3, column=0, sticky="w", pady=2)
            self.network = tk.StringVar(value=NETWORK_CHOICES[0])
            ttk.Combobox(hardware, textvariable=self.network,
                         state="readonly", values=NETWORK_CHOICES).grid(
                row=3, column=1, columnspan=4, sticky="ew", pady=2)

            path = self.add_group(right, row, "Path")
            row += 1
            inner = self.add_row(path, 0, *PATH_ROWS[0])          # QEMU
            inner = self.add_row(path, inner, *PATH_ROWS[1])      # BIOS Folder
            ttk.Checkbutton(path, text="Use compatible BIOS",
                            variable=self.compat,
                            command=self.apply_compat).grid(
                row=inner, column=1, sticky="w", pady=(0, 2))

            virt = self.add_group(right, row, "Virtualization Options")
            row += 1
            # only one accelerator at a time, and only where it exists
            self.kvm_button, self.hv_button = self.add_checks(
                virt, 0,
                (("KVM (Experimental)", self.kvm),
                 ("Hyper-V (Experimental)", self.hyperv)))
            self.kvm_button.configure(command=lambda: self.hyperv.set(False))
            self.hv_button.configure(command=lambda: self.kvm.set(False))
            if not os.path.exists("/dev/kvm"):
                self.kvm_button.state(["disabled"])
            if not whpx_available():
                self.hv_button.state(["disabled"])
            ttk.Label(virt, text="Extra Options", width=LABEL_WIDTH).grid(
                row=1, column=0, sticky="w", pady=(4, 2))
            ttk.Entry(virt, textvariable=self.extra).grid(
                row=1, column=1, columnspan=4, sticky="ew", pady=(4, 2))

            # the right column runs shorter than the left, so the buttons go
            # in the space it leaves rather than costing a row of their own
            right.rowconfigure(row, weight=1)
            bar = ttk.Frame(right)
            bar.grid(row=row + 1, column=0, columnspan=5, sticky="se",
                     pady=(12, 0))
            self.start_button = ttk.Button(bar, text="Start",
                                           command=self.start)
            self.start_button.pack(side="right")
            self.stop_button = ttk.Button(bar, text="Stop", command=self.stop,
                                          state="disabled")
            self.stop_button.pack(side="right", padx=6)
            return frame

        def slide_memory(self, raw):
            """Snap the knob to a stop and spell that stop in the box."""
            index = int(round(float(raw)))
            if float(raw) != index:
                self.mem_scale.set(index)
                return
            if self.memory.get().strip().upper() != MEMORY_STEPS[index]:
                self.memory.set(MEMORY_STEPS[index])

        def memory_edited(self, *_args):
            """A typed size that is one of the stops drags the knob along."""
            value = self.memory.get().strip().upper()
            if value in MEMORY_STEPS:
                index = MEMORY_STEPS.index(value)
                if int(round(float(self.mem_scale.get()))) != index:
                    self.mem_scale.set(index)

        def pick_row(self, key, browse, types):
            path = (filedialog.askdirectory(parent=self) if browse == "dir"
                    else filedialog.askopenfilename(filetypes=types,
                                                    parent=self))
            if path:
                self.vars[key].set(os.path.normpath(path))
                if key == "qemu" and self.compat.get():
                    self.vars["roms"].set(compat_roms(path))

        def pick_drive(self, key, kind):
            """Point a boot row at a real drive instead of an image file."""
            self.write("scanning %s drives..." % kind)
            self.update_idletasks()
            finder = {"floppy": list_floppies, "optical": list_optical,
                      # a CF card in a reader; fixed disks are never offered
                      "disk": lambda: [d for d in list_disks()
                                       if d.removable and not d.system]}[kind]
            try:
                found = finder()
            except Exception as err:
                self.write("could not list drives: %s" % err)
                return
            if not found:
                self.write("no %s drives found" % kind)
                return
            path = self.choose_from(found, "Select a %s drive" % kind)
            if path:
                self.vars[key].set(path)
                if kind == "floppy":
                    self.write("a PC-98 1.2 MB disk needs a 3-mode drive; "
                               "1.44 MB works on any of them")
                elif kind == "disk":
                    self.write("raw disk access needs %s; the guest can "
                               "write to the card"
                               % ("Administrator" if os.name == "nt"
                                  else "root"))

        def scan_serial(self):
            """Fill the drop-down; typing anything else stays allowed."""
            try:
                ports = list_serial_ports()
            except Exception as err:
                self.write("could not list serial ports: %s" % err)
                return
            self.serial_box["values"] = [p.path for p in ports]
            if not ports:
                self.write("no serial ports found")
                return
            for port in ports:
                self.write("   %s" % port)
            if not self.vars["serial"].get():
                self.vars["serial"].set(ports[0].path)

        def choose_from(self, disks, title):
            top = tk.Toplevel(self)
            top.title(title)
            top.transient(self)
            top.grab_set()
            box = tk.Listbox(top, width=72, height=min(10, len(disks)))
            box.grid(row=0, column=0, columnspan=2, padx=8, pady=8)
            for disk in disks:
                box.insert("end", str(disk))
            box.selection_set(0)
            picked = []

            def accept():
                sel = box.curselection()
                if sel:
                    picked.append(disks[sel[0]].path)
                top.destroy()

            ttk.Button(top, text="Select", command=accept).grid(
                row=1, column=1, sticky="e", padx=8, pady=(0, 8))
            ttk.Button(top, text="Cancel", command=top.destroy).grid(
                row=1, column=0, sticky="w", padx=8, pady=(0, 8))
            self.wait_window(top)
            return picked[0] if picked else ""

        def apply_compat(self):
            compat = compat_roms(self.vars["qemu"].get())
            if self.compat.get():
                current = self.vars["roms"].get()
                if current and not current.endswith(COMPAT_ROM_DIR):
                    self.saved_roms = current
                self.vars["roms"].set(compat)
                self.entries["roms"].state(["readonly"])
                self.browsers["roms"].state(["disabled"])
            else:
                self.entries["roms"].state(["!readonly"])
                self.browsers["roms"].state(["!disabled"])
                self.vars["roms"].set(
                    self.saved_roms or default_roms(self.vars["qemu"].get()))

        def config(self):
            cfg = {k: v.get().strip() for k, v in self.vars.items()}
            fm, pcm = sound_parts(self.sound.get())
            cfg.update(memory=self.memory.get().strip(),
                       extra=self.extra.get().strip(), fm=fm, pcm=pcm,
                       kvm=self.kvm.get(), hyperv=self.hyperv.get(),
                       lan=NETWORK_ARG.get(self.network.get(), ""))
            return cfg

        def start(self):
            if self.process is not None:
                return
            cfg = self.config()
            if not cfg["qemu"]:
                self.write("select the QEMU executable first")
                return
            if not any(cfg[k] for k in ("hdd1", "hdd2", "fdd1", "fdd2",
                                        "cd", "mount") + SCSI_KEYS):
                self.write("select something to boot from")
                return
            # any real drive at all needs raw access rights first
            raw = [cfg[k] for k in ("hdd1", "hdd2", "fdd1", "fdd2", "cd")
                   + SCSI_KEYS if cfg[k] and is_host_device(cfg[k])]
            if raw and os.name == "nt" and not is_admin():
                from tkinter import messagebox
                if messagebox.askyesno(
                        "Administrator needed",
                        "Raw access to\n\n  %s\n\nneeds Administrator.\n\n"
                        "Restart the launcher elevated?"
                        % "\n  ".join(raw), icon="warning", parent=self):
                    self.save()
                    if relaunch_elevated():
                        self.destroy()
                        return
                    self.write("elevation was refused")
                else:
                    self.write("cancelled")
                return
            if raw and os.name != "nt" and not is_admin():
                # udev rules may still allow it, so warn and carry on
                self.write("note: raw drive access usually needs root")
            # the CD-ROMs are attached read-only, but these rows are writable,
            # and on real hardware whatever the guest does there sticks
            drives = [cfg[k] for k in ("hdd1", "hdd2", "fdd1", "fdd2")
                      if cfg[k] and is_host_device(cfg[k])]
            drives += [cfg[k] for k in SCSI_KEYS
                       if cfg[k] and is_host_device(cfg[k])]
            if drives:
                from tkinter import messagebox
                if not messagebox.askyesno(
                        "Write to a physical drive?",
                        "The guest can write to\n\n  %s\n\n"
                        "Whatever it changes there is changed for real.\n\n"
                        "Continue?" % "\n  ".join(drives),
                        icon="warning", parent=self):
                    self.write("cancelled")
                    return
            self.save()
            self.start_button.state(["disabled"])
            self.stop_button.state(["!disabled"])
            threading.Thread(target=self.run_qemu, args=(cfg,),
                             daemon=True).start()

        def run_qemu(self, cfg):
            try:
                argv, notes = qemu_command(cfg)
                for note in notes:
                    self.post(note)
                self.post(" ".join('"%s"' % a if " " in a else a
                                   for a in argv))
                self.process = subprocess.Popen(
                    argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    **no_window())
                for line in self.process.stdout:
                    self.post(line.decode("utf-8", "replace").rstrip())
                self.post("qemu exited (%s)" % self.process.wait())
            except Exception as err:
                self.post("%s: %s" % (type(err).__name__, err))
            finally:
                self.process = None
                self.after(0, self.idle)

        def idle(self):
            self.start_button.state(["!disabled"])
            self.stop_button.state(["disabled"])

        def stop(self):
            if self.process is not None:
                self.process.terminate()

        # ---------------------------------------------------------- create
        def build_create(self, parent):
            frame = ttk.Frame(parent, padding=8)
            frame.columnconfigure(1, weight=1)
            self.hdd_size = tk.StringVar(value=HDD_SIZES[0])
            self.hdd_label = tk.StringVar(value="NO NAME")
            self.hdd_format = tk.StringVar(value=KIND_QCOW2)
            self.hdd_fat32 = tk.BooleanVar(value=False)
            self.fdd_size = tk.StringVar(value="1.2 MB (2HD)")
            self.fdd_label = tk.StringVar(value="NO NAME")
            self.fdd_format = tk.StringVar(value=KIND_RAW_FDD)

            ttk.Label(frame, text="New hard disk").grid(row=0, column=0,
                                                        sticky="w")
            ttk.Combobox(frame, textvariable=self.hdd_size, state="readonly",
                         values=HDD_SIZES, width=12).grid(row=1, column=0,
                                                          pady=2)
            ttk.Entry(frame, textvariable=self.hdd_label).grid(
                row=1, column=1, sticky="ew", padx=8, pady=2)
            ttk.Combobox(frame, textvariable=self.hdd_format, state="readonly",
                         values=(KIND_QCOW2, KIND_RAW_HDD, KIND_HDI),
                         width=10).grid(row=1, column=2, pady=2)
            ttk.Checkbutton(frame, text="FAT32",
                            variable=self.hdd_fat32).grid(
                row=1, column=3, padx=(8, 0), pady=2)
            ttk.Button(frame, text="Create...", command=self.create_hdd).grid(
                row=1, column=4, padx=(8, 0), pady=2)

            ttk.Separator(frame, orient="horizontal").grid(
                row=2, column=0, columnspan=5, sticky="ew", pady=10)

            ttk.Label(frame, text="New floppy disk").grid(row=3, column=0,
                                                          sticky="w")
            ttk.Combobox(frame, textvariable=self.fdd_size, state="readonly",
                         values=("1.2 MB (2HD)", "1.44 MB (2HD)"),
                         width=14).grid(row=4, column=0, pady=2)
            ttk.Entry(frame, textvariable=self.fdd_label).grid(
                row=4, column=1, sticky="ew", padx=8, pady=2)
            ttk.Combobox(frame, textvariable=self.fdd_format, state="readonly",
                         values=(KIND_RAW_FDD, KIND_FDI),
                         width=10).grid(row=4, column=2, pady=2)
            ttk.Button(frame, text="Create...", command=self.create_fdd).grid(
                row=4, column=4, padx=(8, 0), pady=2)

            ttk.Label(frame, wraplength=680, text=(
                "The image is formatted and empty. Save it as .raw for QEMU, "
                "The format picker decides what is written, and the "
                "file name gets a matching suffix. FAT32 needs 80 MB or "
                "more and an OS from Windows 95 OSR2 on.")).grid(
                row=5, column=0, columnspan=5, sticky="w", pady=(12, 0))
            return frame

        def create_hdd(self):
            kind = self.hdd_format.get()
            target = filedialog.asksaveasfilename(
                filetypes=self.kind_types(kind),
                defaultextension=KIND_SUFFIX[kind], parent=self)
            if target:
                mb = HDD_MB[self.hdd_size.get()]
                label = self.hdd_label.get().strip() or "NO NAME"
                fat32 = self.hdd_fat32.get()
                self.write("--- new %d MB hard disk as %s%s"
                           % (mb, kind, " (FAT32)" if fat32 else ""))
                self.work(lambda log: new_image(os.path.normpath(target),
                                                megabytes=mb, label=label,
                                                kind=kind, fat32=fat32,
                                                log=log))

        def create_fdd(self):
            kind = self.fdd_format.get()
            target = filedialog.asksaveasfilename(
                filetypes=self.kind_types(kind),
                defaultextension=KIND_SUFFIX[kind], parent=self)
            if target:
                fmt = ("1.44" if self.fdd_size.get().startswith("1.44")
                       else "1.2")
                label = self.fdd_label.get().strip() or "NO NAME"
                self.write("--- new %s MB floppy disk as %s" % (fmt, kind))
                self.work(lambda log: new_image(os.path.normpath(target),
                                                fmt=fmt, label=label,
                                                kind=kind, log=log))

        # --------------------------------------------------------- convert
        def build_convert(self, parent):
            frame = ttk.Frame(parent, padding=8)
            # "pane" ties both columns to one width, so the two sides match
            # however long the paths inside them get
            frame.columnconfigure(0, weight=1, uniform="pane")
            frame.columnconfigure(1, weight=1, uniform="pane")
            self.src_kind = tk.StringVar(value=KIND_HDI)
            self.dst_kind = tk.StringVar(value=KIND_RAW_HDD)
            self.into_existing = tk.BooleanVar(value=False)
            self.conv_fat32 = tk.BooleanVar(value=False)
            self.devices = []

            top = ttk.Frame(frame)
            top.grid(row=0, column=0, columnspan=2, sticky="ew")
            ttk.Label(top, text="Source").pack(side="left")
            src = ttk.Combobox(top, textvariable=self.src_kind,
                               state="readonly", width=12, values=KINDS)
            src.pack(side="left", padx=(6, 16))
            ttk.Label(top, text="Destination").pack(side="left")
            dst = ttk.Combobox(top, textvariable=self.dst_kind,
                               state="readonly", width=12, values=KINDS)
            dst.pack(side="left", padx=6)
            for box in (src, dst):
                box.bind("<<ComboboxSelected>>", lambda e: self.refresh())
            self.convert_button = ttk.Button(top, text="Convert",
                                             command=self.convert)
            self.convert_button.pack(side="right")
            ttk.Button(top, text="Scan Devices",
                       command=self.reload_devices).pack(side="right", padx=6)

            self.panes = {}
            for column, (side, title) in enumerate((("src", "Source"),
                                                    ("dst", "Destination"))):
                self.panes[side] = self.build_pane(frame, side, title)
                # symmetric padding, or the two panes end up a pixel apart
                self.panes[side]["frame"].grid(
                    row=1, column=column, sticky="nsew",
                    padx=(0, 4) if column == 0 else (4, 0), pady=(10, 0))

            # devices are not scanned until asked: the query is slow
            self.refresh()
            return frame

        def build_pane(self, parent, side, title):
            """One column: a device, an image file, or a folder."""
            box = ttk.LabelFrame(parent, text=title, padding=8)
            box.columnconfigure(0, weight=1)
            pane = {"frame": box,
                    "device": tk.StringVar(), "image": tk.StringVar(),
                    "folder": tk.StringVar(), "fields": {}}

            ttk.Label(box, text="Device").grid(row=0, column=0, sticky="w")
            pane["device_box"] = ttk.Combobox(
                box, textvariable=pane["device"], state="disabled", width=1)
            pane["device_box"].grid(row=1, column=0, columnspan=2,
                                    sticky="ew", pady=(0, 8))

            ttk.Label(box, text="Image file").grid(row=2, column=0, sticky="w")
            pane["image_entry"] = ttk.Entry(box, textvariable=pane["image"],
                                            width=1)
            pane["image_entry"].grid(row=3, column=0, sticky="ew")
            pane["image_button"] = ttk.Button(
                box, text="...", width=3,
                command=lambda s=side: self.pick_image(s))
            pane["image_button"].grid(row=3, column=1, padx=(6, 0))

            ttk.Label(box, text="Folder").grid(row=4, column=0, sticky="w",
                                               pady=(8, 0))
            pane["folder_entry"] = ttk.Entry(box, textvariable=pane["folder"],
                                             width=1)
            pane["folder_entry"].grid(row=5, column=0, sticky="ew")
            pane["folder_button"] = ttk.Button(
                box, text="...", width=3,
                command=lambda s=side: self.pick_folder(s))
            pane["folder_button"].grid(row=5, column=1, padx=(6, 0))

            pane["options"] = ttk.Frame(box)
            pane["options"].grid(row=6, column=0, columnspan=2, sticky="ew",
                                 pady=(8, 0))
            pane["options"].columnconfigure(1, weight=1)
            return pane

        def reload_devices(self):
            self.write("scanning devices...")
            self.update_idletasks()
            try:
                self.devices = removable_devices()
            except Exception as err:
                self.devices = []
                self.write("could not list devices: %s" % err)
            labels = [str(d) for d in self.devices]
            for pane in self.panes.values():
                pane["device_box"].configure(values=labels)
                if pane["device"].get() not in labels:
                    pane["device"].set(labels[0] if labels else "")
            self.write("found %d removable device%s"
                       % (len(labels), "" if len(labels) == 1 else "s")
                       if labels else
                       "no removable devices found "
                       "(fixed disks are never offered)")
            self.refresh_options()

        def device_path(self, side):
            label = self.panes[side]["device"].get()
            for disk in self.devices:
                if str(disk) == label:
                    return disk.path
            return ""

        def refresh(self):
            """Only the widgets the chosen kinds actually use stay live."""
            same = self.src_kind.get() == self.dst_kind.get()
            self.convert_button.state(["disabled"] if same else ["!disabled"])
            if same:
                self.write("source and destination must differ")
            for side, var in (("src", self.src_kind), ("dst", self.dst_kind)):
                pane, kind = self.panes[side], var.get()
                on = lambda w, yes: w.state(
                    ["!disabled"] if yes else ["disabled"])
                pane["device_box"].configure(
                    state="readonly" if kind == KIND_DRIVE else "disabled")
                if kind == KIND_DRIVE and not self.devices:
                    self.write("press Scan Devices to list removable drives")
                on(pane["image_entry"], kind in IMAGE_KINDS)
                on(pane["image_button"], kind in IMAGE_KINDS)
                on(pane["folder_entry"], kind == KIND_FOLDER)
                on(pane["folder_button"], kind == KIND_FOLDER)
            self.refresh_options()

        def refresh_options(self):
            """Building a volume from a folder needs a size and a label."""
            pane = self.panes["dst"]
            for child in pane["options"].winfo_children():
                child.destroy()
            pane["fields"] = {}
            if self.src_kind.get() != KIND_FOLDER:
                return
            dst = self.dst_kind.get()
            if dst == KIND_DRIVE:
                hdd = device_kind(self.device_path("dst"),
                                  self.devices) != "floppy"
            else:
                hdd = dst in (KIND_HDI, KIND_RAW_HDD, KIND_QCOW2)
            row = 0
            if dst != KIND_DRIVE:
                ttk.Checkbutton(
                    pane["options"], text="Write into existing image",
                    variable=self.into_existing,
                    command=self.refresh_options).grid(
                    row=row, column=0, columnspan=3, sticky="w", pady=2)
                row += 1
            into = dst != KIND_DRIVE and self.into_existing.get()
            if into:
                fields = [("partition", "Partition", "1")] if hdd else []
            elif hdd:
                fields = [("size", "Size (MB)", "40"),
                          ("label", "Volume label", "NO NAME"),
                          ("boot", "Boot record from", "")]
            else:
                fields = [("label", "Volume label", "NO NAME"),
                          ("format", "Format (1.2/1.44)", "1.2"),
                          ("boot", "Boot code from", "")]
            for key, text, default in fields:
                var = tk.StringVar(value=default)
                pane["fields"][key] = var
                ttk.Label(pane["options"], text=text).grid(row=row, column=0,
                                                           sticky="w", pady=2)
                ttk.Entry(pane["options"], textvariable=var, width=1).grid(
                    row=row, column=1, sticky="ew", pady=2)
                if key == "boot":
                    ttk.Button(
                        pane["options"], text="...", width=3,
                        command=lambda v=var: self.pick_into(v)).grid(
                        row=row, column=2, padx=(6, 0))
                row += 1
            if hdd and not into:
                ttk.Checkbutton(pane["options"],
                                text="FAT32 (Windows 95 OSR2 and later)",
                                variable=self.conv_fat32).grid(
                    row=row, column=0, columnspan=3, sticky="w", pady=2)

        def pick_into(self, var):
            path = filedialog.askopenfilename(
                filetypes=RAW_TYPES + HDI_TYPES + FDI_TYPES, parent=self)
            if path:
                var.set(os.path.normpath(path))

        def kind_types(self, kind):
            return {KIND_HDI: HDI_TYPES, KIND_FDI: FDI_TYPES,
                    KIND_QCOW2: QCOW_TYPES}.get(kind, RAW_TYPES)

        def pick_image(self, side):
            kind = (self.src_kind if side == "src" else self.dst_kind).get()
            types = self.kind_types(kind)
            # writing into an existing image wants a file that is there,
            # not a save box offering to truncate one
            into = (side == "dst" and self.src_kind.get() == KIND_FOLDER
                    and self.into_existing.get())
            if side == "src" or into:
                path = filedialog.askopenfilename(filetypes=types, parent=self)
            else:
                suffix = {KIND_HDI: ".hdi", KIND_FDI: ".fdi",
                          KIND_QCOW2: ".qcow2"}.get(kind, ".raw")
                path = filedialog.asksaveasfilename(
                    defaultextension=suffix, filetypes=types, parent=self)
            if path:
                self.panes[side]["image"].set(os.path.normpath(path))

        def pick_folder(self, side):
            path = filedialog.askdirectory(parent=self)
            if path:
                self.panes[side]["folder"].set(os.path.normpath(path))

        def side_ref(self, side):
            kind = (self.src_kind if side == "src" else self.dst_kind).get()
            pane = self.panes[side]
            if kind == KIND_DRIVE:
                return self.device_path(side)
            if kind == KIND_FOLDER:
                return pane["folder"].get().strip()
            return pane["image"].get().strip()

        def convert(self):
            src_kind, dst_kind = self.src_kind.get(), self.dst_kind.get()
            src_ref, dst_ref = self.side_ref("src"), self.side_ref("dst")
            if src_kind == dst_kind:
                self.write("source and destination must differ")
                return
            if not src_ref or not dst_ref:
                self.write("fill in both sides first")
                return
            # raw device access is Administrator-only in both directions
            if (KIND_DRIVE in (src_kind, dst_kind) and os.name == "nt"
                    and not is_admin()):
                from tkinter import messagebox
                if messagebox.askyesno(
                        "Administrator needed",
                        "Raw access to a physical drive needs "
                        "Administrator.\n\nRestart the launcher elevated?",
                        icon="warning", parent=self):
                    self.save()
                    if relaunch_elevated():
                        self.destroy()
                        return
                    self.write("elevation was refused")
                else:
                    self.write("cancelled")
                return
            opts = {k: v.get().strip()
                    for k, v in self.panes["dst"]["fields"].items()}
            if src_kind == KIND_FOLDER and dst_kind != KIND_DRIVE \
                    and self.into_existing.get():
                opts["into"] = "1"
            elif self.conv_fat32.get() and "size" in opts:
                opts["fat32"] = "1"
            if dst_kind == KIND_DRIVE:
                from tkinter import messagebox
                if not messagebox.askyesno(
                        "Overwrite the device?",
                        "Everything on\n\n  %s\n\nwill be destroyed.\n\n"
                        "Continue?" % self.panes["dst"]["device"].get(),
                        icon="warning", parent=self):
                    self.write("cancelled")
                    return
            self.write("--- %s to %s" % (src_kind, dst_kind))
            self.work(lambda log: convert_pair(src_kind, src_ref, dst_kind,
                                               dst_ref, opts, log))

        # -------------------------------------------------------- settings
        def load(self):
            ini = os.path.join(here(), SETTINGS)
            parser = configparser.ConfigParser(interpolation=None)
            if os.path.isfile(ini):
                parser.read(ini, encoding="utf-8")
                self.write("settings from %s" % ini)
            saved = parser[SECTION] if parser.has_section(SECTION) else {}
            for key in self.vars:
                self.vars[key].set(saved.get(key, ""))
            args = {v: k for k, v in NETWORK_ARG.items()}
            self.network.set(args.get(saved.get("lan", ""),
                                      NETWORK_CHOICES[0]))
            self.memory.set(saved.get("memory", "64M"))
            self.extra.set(saved.get("extra", ""))
            # settings written before FM and PCM were told apart say "sound",
            # which was a plain on/off
            both = parser.getboolean(SECTION, "sound", fallback=True)
            self.sound.set(sound_label(
                parser.getboolean(SECTION, "fm", fallback=both),
                parser.getboolean(SECTION, "pcm", fallback=both)))
            self.kvm.set(parser.getboolean(SECTION, "kvm", fallback=False)
                         and os.path.exists("/dev/kvm"))
            self.hyperv.set(
                parser.getboolean(SECTION, "hyperv", fallback=False)
                and whpx_available() and not self.kvm.get())
            if not self.vars["qemu"].get():
                self.vars["qemu"].set(find(QEMU_NAMES))
            self.compat.set(parser.getboolean(SECTION, "compat",
                                              fallback=True))
            self.saved_roms = saved.get("roms", "") or default_roms(
                self.vars["qemu"].get())
            self.apply_compat()
            roms = self.vars["roms"].get()
            if not self.compat.get() and roms and not all(
                    os.path.isfile(os.path.join(roms, f)) for f in ROM_FILES):
                self.write("put the PC-98 BIOS and font ROMs in %s" % roms)

        def save(self):
            state = {k: v.get() for k, v in self.vars.items()}
            if self.compat.get():
                state["roms"] = self.saved_roms
            # the two boards are stored separately, so the wording of the
            # drop-down can change without stranding anyone's settings
            fm, pcm = sound_parts(self.sound.get())
            state["lan"] = NETWORK_ARG.get(self.network.get(), "")
            parser = configparser.ConfigParser(interpolation=None)
            parser[SECTION] = dict(
                state, compat=str(self.compat.get()).lower(),
                memory=self.memory.get(), extra=self.extra.get(),
                fm=str(fm).lower(), pcm=str(pcm).lower(),
                kvm=str(self.kvm.get()).lower(),
                hyperv=str(self.hyperv.get()).lower())
            try:
                with open(os.path.join(here(), SETTINGS), "w",
                          encoding="utf-8") as f:
                    parser.write(f)
            except OSError as err:
                self.write("could not save settings: %s" % err)

        def close(self):
            self.save()
            if self.process is not None:
                self.process.terminate()
            self.destroy()

    App().mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
