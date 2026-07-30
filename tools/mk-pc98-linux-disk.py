#!/usr/bin/env python3
"""Build or update a two-partition PC-98 Linux raw disk image."""

import argparse
import math
import os
import struct
import subprocess
import tempfile

SECTOR_SIZE = 512
PC98_DOS_SECTOR_SIZE = 1024
PC98_DOS_SECTOR_SCALE = PC98_DOS_SECTOR_SIZE // SECTOR_SIZE
HEADS = 8
SECTORS = 17
CYL_SECTORS = HEADS * SECTORS
PARTITION_TABLE_LBA = 1
PARTITION_ENTRY_SIZE = 32
IPL_LOADER_SECTORS = 0x1F0
PBR_LOADER_SECTORS = 0x46
LOADER_LBA = 2


def read_file(path):
    with open(path, "rb") as stream:
        return stream.read()


def chs_lba(cylinder, head=0, sector=0):
    return (cylinder * HEADS + head) * SECTORS + sector


def partition_entry(mid, sid, start_cylinder, end_cylinder, name):
    label = name.encode("ascii")[:16].ljust(16, b" ")
    return struct.pack(
        "<BBBBBBHBBHBBH16s",
        mid,
        sid,
        0,
        0,
        0,
        0,
        start_cylinder,
        0,
        0,
        start_cylinder,
        SECTORS - 1,
        HEADS - 1,
        end_cylinder,
        label,
    )


def parse_partition(image, index):
    image.seek((PARTITION_TABLE_LBA * SECTOR_SIZE) +
               (index * PARTITION_ENTRY_SIZE))
    raw = image.read(PARTITION_ENTRY_SIZE)
    if len(raw) != PARTITION_ENTRY_SIZE:
        raise RuntimeError("short PC-98 partition table")
    fields = struct.unpack("<BBBBBBHBBHBBH16s", raw)
    start = chs_lba(fields[9], fields[8], fields[7])
    end = chs_lba(fields[12], fields[11], fields[10])
    if not fields[0] or end < start:
        raise RuntimeError(f"invalid PC-98 partition {index + 1}")
    return start, end - start + 1


def fat16_layout(total_sectors, reserved, sectors_per_cluster=2,
                 root_entries=512, fats=2,
                 logical_sector_size=PC98_DOS_SECTOR_SIZE):
    root_sectors = math.ceil(root_entries * 32 / logical_sector_size)
    sectors_per_fat = 1
    while True:
        data_sectors = total_sectors - reserved - root_sectors - (
            fats * sectors_per_fat)
        clusters = data_sectors // sectors_per_cluster
        needed = math.ceil((clusters + 2) * 2 / logical_sector_size)
        if needed == sectors_per_fat:
            break
        sectors_per_fat = needed
    if not 4085 <= clusters < 65525:
        raise RuntimeError(
            f"partition has {clusters} clusters and is not FAT16")
    return sectors_per_fat, root_sectors, clusters


def write_zeros(stream, offset, length):
    zero = b"\0" * (1 << 20)
    stream.seek(offset)
    while length:
        chunk = min(length, len(zero))
        stream.write(zero[:chunk])
        length -= chunk


def write_fat16(image, start_lba, total_physical_sectors, kernel_path,
                pbr_template):
    if total_physical_sectors % PC98_DOS_SECTOR_SCALE:
        raise RuntimeError("FAT16 partition is not 1024-byte-sector aligned")

    pbr = bytearray(pbr_template)
    if len(pbr) != SECTOR_SIZE:
        raise RuntimeError("partition PBR must be exactly 512 bytes")
    kernel = read_file(kernel_path)
    reserved = 1
    spc = 2
    fats = 2
    root_entries = 512
    total_sectors = total_physical_sectors // PC98_DOS_SECTOR_SCALE
    spf, root_sectors, clusters = fat16_layout(
        total_sectors, reserved, spc, root_entries, fats)

    pbr[3:11] = b"NEC  5.0"
    struct.pack_into("<H", pbr, 0x0B, PC98_DOS_SECTOR_SIZE)
    pbr[0x0D] = spc
    struct.pack_into("<H", pbr, 0x0E, reserved)
    pbr[0x10] = fats
    struct.pack_into("<H", pbr, 0x11, root_entries)
    struct.pack_into(
        "<H", pbr, 0x13, total_sectors if total_sectors <= 0xFFFF else 0)
    pbr[0x15] = 0xF8
    struct.pack_into("<H", pbr, 0x16, spf)
    struct.pack_into("<H", pbr, 0x18, SECTORS)
    struct.pack_into("<H", pbr, 0x1A, HEADS)
    struct.pack_into("<I", pbr, 0x1C, start_lba)
    struct.pack_into(
        "<I", pbr, 0x20, total_sectors if total_sectors > 0xFFFF else 0)
    pbr[0x24] = 0x80
    pbr[0x26] = 0x29
    struct.pack_into("<I", pbr, 0x27, 0x3938394C)
    pbr[0x2B:0x36] = b"MIRAI98BOOT"
    pbr[0x36:0x3E] = b"FAT16   "
    pbr[0x1FE:0x200] = b"\x55\xAA"

    cluster_bytes = spc * PC98_DOS_SECTOR_SIZE
    kernel_clusters = math.ceil(len(kernel) / cluster_bytes)
    if kernel_clusters > clusters:
        raise RuntimeError("kernel does not fit in boot partition")
    first_cluster = 2

    fat = bytearray(spf * PC98_DOS_SECTOR_SIZE)
    struct.pack_into("<HH", fat, 0, 0xFFF8, 0xFFFF)
    for number in range(kernel_clusters):
        cluster = first_cluster + number
        following = 0xFFFF if number + 1 == kernel_clusters else cluster + 1
        struct.pack_into("<H", fat, cluster * 2, following)

    root = bytearray(root_sectors * PC98_DOS_SECTOR_SIZE)
    root[0:11] = b"BZIMAGE    "
    root[11] = 0x20
    struct.pack_into("<H", root, 26, first_cluster)
    struct.pack_into("<I", root, 28, len(kernel))

    base = start_lba * SECTOR_SIZE
    write_zeros(image, base, total_physical_sectors * SECTOR_SIZE)
    image.seek(base)
    image.write(pbr)

    fat_offset = base + reserved * PC98_DOS_SECTOR_SIZE
    for copy in range(fats):
        image.seek(fat_offset + copy * len(fat))
        image.write(fat)
    root_offset = fat_offset + fats * len(fat)
    image.seek(root_offset)
    image.write(root)
    data_offset = root_offset + len(root)
    data_start = (data_offset - base) // SECTOR_SIZE
    struct.pack_into("<I", pbr, 0x3E, start_lba)
    struct.pack_into("<H", pbr, 0x42, data_start)
    struct.pack_into("<H", pbr, 0x44, SECTOR_SIZE)
    image.seek(base)
    image.write(pbr)
    image.seek(data_offset)
    image.write(kernel)

    return {
        "kernel_bytes": len(kernel),
        "kernel_clusters": kernel_clusters,
        "spf": spf,
    }


def copy_sparse(source_path, destination, destination_offset):
    zero = b"\0" * (1 << 20)
    destination.seek(destination_offset)
    with open(source_path, "rb") as source:
        while True:
            chunk = source.read(len(zero))
            if not chunk:
                break
            if chunk == zero[:len(chunk)]:
                destination.seek(len(chunk), os.SEEK_CUR)
            else:
                destination.write(chunk)


def make_ext4(image, start_lba, total_sectors, root_stage):
    byte_size = total_sectors * SECTOR_SIZE
    with tempfile.NamedTemporaryFile(
            prefix="mirai98-root-", suffix=".ext4", delete=False) as temp:
        root_image = temp.name
        temp.truncate(byte_size)
    try:
        subprocess.run(
            [
                "mke2fs", "-q", "-F", "-t", "ext4", "-b", "1024",
                "-E", "lazy_itable_init=0,lazy_journal_init=0",
                "-d", root_stage, root_image,
            ],
            check=True,
        )
        copy_sparse(root_image, image, start_lba * SECTOR_SIZE)
    finally:
        os.unlink(root_image)


def create(args):
    ipl = bytearray(read_file(args.ipl))
    if len(ipl) != SECTOR_SIZE or ipl[4:8] != b"IPL1":
        raise RuntimeError("disk IPL must be 512 bytes with IPL1 at offset 4")
    pbr = bytearray(read_file(args.pbr))
    if len(pbr) != SECTOR_SIZE or pbr[0x1FE:0x200] != b"\x55\xAA":
        raise RuntimeError("partition PBR must be 512 bytes with 55AA")
    loader = read_file(args.loader)
    loader_sectors = math.ceil(len(loader) / SECTOR_SIZE)
    if not 0 < loader_sectors <= chs_lba(1) - LOADER_LBA:
        raise RuntimeError("second-stage loader does not fit before partition 1")
    struct.pack_into("<H", ipl, IPL_LOADER_SECTORS, loader_sectors)
    struct.pack_into("<H", pbr, PBR_LOADER_SECTORS, loader_sectors)
    if args.boot_cylinders < 64:
        raise RuntimeError("boot partition is too small")

    p1_start_cyl = 1
    p1_end_cyl = p1_start_cyl + args.boot_cylinders - 1
    p2_start_cyl = p1_end_cyl + 1
    p2_cylinders = math.ceil(
        args.root_mb * 1024 * 1024 / (CYL_SECTORS * SECTOR_SIZE))
    p2_end_cyl = p2_start_cyl + p2_cylinders - 1
    if p2_end_cyl > 0xFFFF:
        raise RuntimeError("disk exceeds PC-98 CHS cylinder range")

    total_sectors = (p2_end_cyl + 1) * CYL_SECTORS
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, "w+b") as image:
        image.truncate(total_sectors * SECTOR_SIZE)
        image.seek(0)
        image.write(ipl)
        image.seek(LOADER_LBA * SECTOR_SIZE)
        image.write(loader)
        image.write(b"\0" * (loader_sectors * SECTOR_SIZE - len(loader)))
        table = bytearray(SECTOR_SIZE)
        table[0:32] = partition_entry(
            0xA1, 0x81, p1_start_cyl, p1_end_cyl, "LINUXBOOT")
        table[32:64] = partition_entry(
            0x21, 0x83, p2_start_cyl, p2_end_cyl, "LINUXROOT")
        image.seek(SECTOR_SIZE)
        image.write(table)

        p1_start = chs_lba(p1_start_cyl)
        p1_sectors = args.boot_cylinders * CYL_SECTORS
        fat_info = write_fat16(
            image, p1_start, p1_sectors, args.kernel, pbr)
        make_ext4(
            image, chs_lba(p2_start_cyl),
            p2_cylinders * CYL_SECTORS, args.root_stage)
        image.truncate(total_sectors * SECTOR_SIZE)

    print(
        f"wrote {args.output}: {total_sectors * SECTOR_SIZE} bytes; "
        f"p1 FAT16 LBA {p1_start}+{p1_sectors}, "
        f"p2 ext4 LBA {chs_lba(p2_start_cyl)}+"
        f"{p2_cylinders * CYL_SECTORS}; "
        f"kernel {fat_info['kernel_bytes']} bytes")


def update_kernel(args):
    ipl = bytearray(read_file(args.ipl))
    if len(ipl) != SECTOR_SIZE or ipl[4:8] != b"IPL1":
        raise RuntimeError("disk IPL must be 512 bytes with IPL1 at offset 4")
    pbr = bytearray(read_file(args.pbr))
    if len(pbr) != SECTOR_SIZE or pbr[0x1FE:0x200] != b"\x55\xAA":
        raise RuntimeError("partition PBR must be 512 bytes with 55AA")
    loader = read_file(args.loader)
    loader_sectors = math.ceil(len(loader) / SECTOR_SIZE)
    struct.pack_into("<H", ipl, IPL_LOADER_SECTORS, loader_sectors)
    struct.pack_into("<H", pbr, PBR_LOADER_SECTORS, loader_sectors)

    with open(args.image, "r+b") as image:
        image.seek(4)
        if image.read(4) != b"IPL1":
            raise RuntimeError("not a Mirai98 PC-98 disk image")
        start_lba, sectors = parse_partition(image, 0)
        if not 0 < loader_sectors <= start_lba - LOADER_LBA:
            raise RuntimeError(
                "second-stage loader does not fit before partition 1")
        image.seek(0)
        image.write(ipl)
        write_zeros(
            image, LOADER_LBA * SECTOR_SIZE,
            (start_lba - LOADER_LBA) * SECTOR_SIZE)
        image.seek(LOADER_LBA * SECTOR_SIZE)
        image.write(loader)
        fat_info = write_fat16(
            image, start_lba, sectors, args.kernel, pbr)
    print(
        f"updated {args.image} partition 1: "
        f"kernel {fat_info['kernel_bytes']} bytes; "
        "partition 2 was not modified")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    create_parser = subparsers.add_parser("create")
    create_parser.add_argument("output")
    create_parser.add_argument("ipl")
    create_parser.add_argument("pbr")
    create_parser.add_argument("loader")
    create_parser.add_argument("kernel")
    create_parser.add_argument("root_stage")
    create_parser.add_argument("--boot-cylinders", type=int, default=1024)
    create_parser.add_argument("--root-mb", type=int, default=1024)
    create_parser.set_defaults(function=create)

    update_parser = subparsers.add_parser("update-kernel")
    update_parser.add_argument("image")
    update_parser.add_argument("ipl")
    update_parser.add_argument("pbr")
    update_parser.add_argument("loader")
    update_parser.add_argument("kernel")
    update_parser.set_defaults(function=update_kernel)

    args = parser.parse_args()
    args.function(args)


if __name__ == "__main__":
    main()
