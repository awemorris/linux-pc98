#!/usr/bin/env python3
"""Build a PC-98 Linux raw disk image.

The result is an intermediate two/three-partition layout (FAT16 BOOT,
ext4 root, optional swap). It is not bootable on its own: the selected image
profile subsequently installs either bootsimple (BusyBox) or zedBSD (Debian).
"""

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


def set_geometry(heads, sectors):
    global HEADS, SECTORS, CYL_SECTORS
    if not 1 <= heads <= 255 or not 1 <= sectors <= 255:
        raise RuntimeError("heads and sectors must be in the range 1..255")
    HEADS = heads
    SECTORS = sectors
    CYL_SECTORS = heads * sectors


def read_file(path):
    with open(path, "rb") as stream:
        return stream.read()


def read_legacy_pbr(path):
    """Return the first physical sector of a PC-98 PBR template.

    Current partition loaders occupy one 1024-byte PC-98 DOS logical sector.
    This image builder still creates an intermediate FAT16
    volume with its historical 512-byte PBR; the zedBSD installer replaces
    that volume and installs the complete 1024-byte PBR afterwards.  Accept
    both template sizes here so the intermediate image remains buildable.
    """
    pbr = bytearray(read_file(path))
    if len(pbr) not in (SECTOR_SIZE, PC98_DOS_SECTOR_SIZE):
        raise RuntimeError("partition PBR must be 512 or 1024 bytes")
    if pbr[0x1FE:0x200] != b"\x55\xAA":
        raise RuntimeError("partition PBR must have 55AA at offset 510")
    return pbr[:SECTOR_SIZE]


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


def entry_chs_lba(entry, offset):
    sector = entry[offset]
    head = entry[offset + 1]
    cylinder = struct.unpack_from("<H", entry, offset + 2)[0]
    if sector >= SECTORS or head >= HEADS:
        raise RuntimeError(
            f"invalid CHS C/H/S={cylinder}/{head}/{sector} for "
            f"H={HEADS}/S={SECTORS}")
    return chs_lba(cylinder, head, sector)


def validate_image(path):
    """Validate the PC-98 CHS table against the raw image's real size."""
    image_bytes = os.path.getsize(path)
    if image_bytes == 0 or image_bytes % SECTOR_SIZE:
        raise RuntimeError(
            f"image size {image_bytes} is not a nonzero multiple of "
            f"{SECTOR_SIZE}")
    image_sectors = image_bytes // SECTOR_SIZE
    if image_sectors <= PARTITION_TABLE_LBA:
        raise RuntimeError("image is too small to contain a partition table")
    with open(path, "rb") as image:
        image.seek(PARTITION_TABLE_LBA * SECTOR_SIZE)
        table = image.read(SECTOR_SIZE)
    if len(table) != SECTOR_SIZE:
        raise RuntimeError("cannot read the complete PC-98 partition table")

    previous_end = -1
    count = 0
    for index in range(SECTOR_SIZE // PARTITION_ENTRY_SIZE):
        entry = table[
            index * PARTITION_ENTRY_SIZE:
            (index + 1) * PARTITION_ENTRY_SIZE]
        if entry[0] == 0:
            continue
        try:
            start = entry_chs_lba(entry, 4)
            data = entry_chs_lba(entry, 8)
            end = entry_chs_lba(entry, 12)
        except RuntimeError as error:
            raise RuntimeError(f"partition {index + 1}: {error}") from error
        if not start <= data <= end:
            raise RuntimeError(
                f"partition {index + 1}: invalid start/data/end LBA "
                f"{start}/{data}/{end}")
        if start <= previous_end:
            raise RuntimeError(
                f"partition {index + 1}: LBA {start} overlaps the previous "
                f"partition ending at LBA {previous_end}")
        if end >= image_sectors:
            raise RuntimeError(
                f"partition {index + 1}: end LBA {end} exceeds image last "
                f"LBA {image_sectors - 1}; likely CHS geometry mismatch "
                f"(expected H={HEADS}/S={SECTORS})")
        previous_end = end
        count += 1
    if count == 0:
        raise RuntimeError("PC-98 partition table contains no partitions")
    if previous_end + 1 != image_sectors:
        raise RuntimeError(
            f"last partition ends at LBA {previous_end}, but image ends at "
            f"LBA {image_sectors - 1}; image is not cylinder-complete for "
            f"H={HEADS}/S={SECTORS}")
    return count, image_sectors


def validate(args):
    count, sectors = validate_image(args.image)
    print(
        f"validated {args.image}: {count} PC-98 partitions, {sectors} "
        f"sectors, H={HEADS}/S={SECTORS}")


def fat16_layout(total_sectors, reserved, sectors_per_cluster,
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


def write_fat16(image, start_lba, total_physical_sectors, kernel_path,
                pbr_template, logo_path=None, dos_loader_path=None):
    if total_physical_sectors % PC98_DOS_SECTOR_SCALE:
        raise RuntimeError("FAT16 partition is not 1024-byte-sector aligned")

    pbr = bytearray(pbr_template)
    if len(pbr) != SECTOR_SIZE:
        raise RuntimeError("partition PBR must be exactly 512 bytes")
    kernel = read_file(kernel_path)
    logo = read_file(logo_path) if logo_path else None
    dos_loader = read_file(dos_loader_path) if dos_loader_path else None
    if logo is not None and len(logo) != 1200:
        raise RuntimeError(
            f"boot logo must be exactly 1200 bytes, got {len(logo)}")
    reserved = 1
    fats = 2
    root_entries = 512
    total_sectors = total_physical_sectors // PC98_DOS_SECTOR_SCALE
    for spc in (1, 2, 4, 8, 16, 32, 64):
        try:
            spf, root_sectors, clusters = fat16_layout(
                total_sectors, reserved, spc, root_entries, fats)
            break
        except RuntimeError:
            continue
    else:
        raise RuntimeError("partition cannot be represented as FAT16")

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
    logo_clusters = math.ceil(len(logo) / cluster_bytes) if logo else 0
    dos_loader_clusters = (
        math.ceil(len(dos_loader) / cluster_bytes) if dos_loader else 0)
    if kernel_clusters + logo_clusters + dos_loader_clusters > clusters:
        raise RuntimeError(
            "kernel, boot logo, and DOS loader do not fit in boot partition")
    first_cluster = 2

    fat = bytearray(spf * PC98_DOS_SECTOR_SIZE)
    struct.pack_into("<HH", fat, 0, 0xFFF8, 0xFFFF)
    for number in range(kernel_clusters):
        cluster = first_cluster + number
        following = 0xFFFF if number + 1 == kernel_clusters else cluster + 1
        struct.pack_into("<H", fat, cluster * 2, following)
    logo_first_cluster = first_cluster + kernel_clusters
    for number in range(logo_clusters):
        cluster = logo_first_cluster + number
        following = 0xFFFF if number + 1 == logo_clusters else cluster + 1
        struct.pack_into("<H", fat, cluster * 2, following)
    dos_loader_first_cluster = logo_first_cluster + logo_clusters
    for number in range(dos_loader_clusters):
        cluster = dos_loader_first_cluster + number
        following = (
            0xFFFF if number + 1 == dos_loader_clusters else cluster + 1)
        struct.pack_into("<H", fat, cluster * 2, following)

    root = bytearray(root_sectors * PC98_DOS_SECTOR_SIZE)
    if not kernel.startswith(b"\x7fELF"):
        raise RuntimeError(
            "kernel must be an uncompressed ELF vmlinux image")
    root[0:11] = b"VMLINUX    "
    root[11] = 0x20
    struct.pack_into("<H", root, 26, first_cluster)
    struct.pack_into("<I", root, 28, len(kernel))
    if logo is not None:
        root[32:43] = b"LOGO    RAW"
        root[43] = 0x20
        struct.pack_into("<H", root, 32 + 26, logo_first_cluster)
        struct.pack_into("<I", root, 32 + 28, len(logo))
    if dos_loader is not None:
        entry = 64 if logo is not None else 32
        root[entry:entry + 11] = b"LINUX98 EXE"
        root[entry + 11] = 0x20
        struct.pack_into(
            "<H", root, entry + 26, dos_loader_first_cluster)
        struct.pack_into("<I", root, entry + 28, len(dos_loader))

    base = start_lba * SECTOR_SIZE
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
    if logo is not None:
        image.seek(data_offset + kernel_clusters * cluster_bytes)
        image.write(logo)
    if dos_loader is not None:
        image.seek(
            data_offset +
            (kernel_clusters + logo_clusters) * cluster_bytes)
        image.write(dos_loader)

    return {
        "kernel_bytes": len(kernel),
        "kernel_clusters": kernel_clusters,
        "logo_bytes": len(logo) if logo is not None else 0,
        "dos_loader_bytes": (
            len(dos_loader) if dos_loader is not None else 0),
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


def make_ext4(image, start_lba, total_sectors, root_stage, small):
    byte_size = total_sectors * SECTOR_SIZE
    with tempfile.NamedTemporaryFile(
            prefix="mirai98-root-", suffix=".ext4", delete=False) as temp:
        root_image = temp.name
        temp.truncate(byte_size)
    try:
        command = [
            "mke2fs", "-q", "-F", "-t", "ext4", "-b", "1024",
            "-L", "PC98ROOT",
        ]
        if small:
            command += [
                "-m", "0",
                "-J", "size=1",
                "-O",
                "^64bit,^resize_inode,^orphan_file,^huge_file,"
                "^dir_nlink,^flex_bg",
            ]
        command += [
            "-E", "lazy_itable_init=0,lazy_journal_init=0",
            "-d", root_stage, root_image,
        ]
        subprocess.run(
            command,
            check=True,
        )
        copy_sparse(root_image, image, start_lba * SECTOR_SIZE)
    finally:
        os.unlink(root_image)


def make_swap(image, start_lba, total_sectors):
    byte_size = total_sectors * SECTOR_SIZE
    with tempfile.NamedTemporaryFile(
            prefix="pc98-swap-", suffix=".swap", delete=False) as temp:
        swap_image = temp.name
        temp.truncate(byte_size)
    try:
        subprocess.run(
            [
                "mkswap", "--quiet", "--label", "PC98SWAP", swap_image,
            ],
            check=True,
        )
        copy_sparse(swap_image, image, start_lba * SECTOR_SIZE)
    finally:
        os.unlink(swap_image)


def create(args):
    ipl = bytearray(read_file(args.ipl))
    if len(ipl) != SECTOR_SIZE or ipl[4:8] != b"IPL1":
        raise RuntimeError("disk IPL must be 512 bytes with IPL1 at offset 4")
    pbr = read_legacy_pbr(args.pbr)
    if args.boot_mb <= 0:
        raise RuntimeError("boot partition size must be positive")
    boot_cylinders = math.ceil(
        args.boot_mb * 1024 * 1024 / (CYL_SECTORS * SECTOR_SIZE))
    if boot_cylinders < 64:
        raise RuntimeError("boot partition is too small")

    p1_start_cyl = 1
    p1_end_cyl = p1_start_cyl + boot_cylinders - 1
    p2_start_cyl = p1_end_cyl + 1
    p2_cylinders = math.ceil(
        args.root_mb * 1024 * 1024 / (CYL_SECTORS * SECTOR_SIZE))
    p2_end_cyl = p2_start_cyl + p2_cylinders - 1
    if args.swap_mb < 0:
        raise RuntimeError("swap partition size must not be negative")
    if args.swap_mb:
        p3_start_cyl = p2_end_cyl + 1
        p3_cylinders = math.ceil(
            args.swap_mb * 1024 * 1024 / (CYL_SECTORS * SECTOR_SIZE))
        p3_end_cyl = p3_start_cyl + p3_cylinders - 1
    else:
        p3_start_cyl = 0
        p3_cylinders = 0
        p3_end_cyl = p2_end_cyl
    if p3_end_cyl > 0xFFFF:
        raise RuntimeError("disk exceeds PC-98 CHS cylinder range")

    total_sectors = (p3_end_cyl + 1) * CYL_SECTORS
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, "w+b") as image:
        image.truncate(total_sectors * SECTOR_SIZE)
        image.seek(0)
        image.write(ipl)
        table = bytearray(SECTOR_SIZE)
        table[0:32] = partition_entry(
            0xA1, 0x81, p1_start_cyl, p1_end_cyl, "LINUXBOOT")
        table[32:64] = partition_entry(
            0x21, 0x83, p2_start_cyl, p2_end_cyl, "LINUXROOT")
        if args.swap_mb:
            table[64:96] = partition_entry(
                0x21, 0x82, p3_start_cyl, p3_end_cyl, "LINUXSWAP")
        image.seek(SECTOR_SIZE)
        image.write(table)

        p1_start = chs_lba(p1_start_cyl)
        p1_sectors = boot_cylinders * CYL_SECTORS
        fat_info = write_fat16(
            image, p1_start, p1_sectors, args.kernel, pbr, args.logo,
            args.dos_loader)
        make_ext4(
            image, chs_lba(p2_start_cyl),
            p2_cylinders * CYL_SECTORS, args.root_stage,
            args.small_ext4)
        if args.swap_mb:
            make_swap(
                image, chs_lba(p3_start_cyl),
                p3_cylinders * CYL_SECTORS)
        image.truncate(total_sectors * SECTOR_SIZE)

    validate_image(args.output)

    summary = (
        f"wrote {args.output}: {total_sectors * SECTOR_SIZE} bytes; "
        f"p1 FAT16 LBA {p1_start}+{p1_sectors}, "
        f"p2 ext4 LBA {chs_lba(p2_start_cyl)}+"
        f"{p2_cylinders * CYL_SECTORS}; "
        f"kernel {fat_info['kernel_bytes']} bytes")
    if args.swap_mb:
        summary += (
            f"; p3 swap LBA {chs_lba(p3_start_cyl)}+"
            f"{p3_cylinders * CYL_SECTORS}")
    if fat_info["logo_bytes"]:
        summary += f"; boot logo {fat_info['logo_bytes']} bytes"
    if fat_info["dos_loader_bytes"]:
        summary += f"; DOS loader {fat_info['dos_loader_bytes']} bytes"
    print(summary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    create_parser = subparsers.add_parser("create")
    create_parser.add_argument("output")
    create_parser.add_argument("ipl")
    create_parser.add_argument("pbr")
    create_parser.add_argument("kernel")
    create_parser.add_argument("root_stage")
    create_parser.add_argument("--boot-mb", type=int, default=128)
    create_parser.add_argument("--root-mb", type=int, default=200)
    create_parser.add_argument("--swap-mb", type=int, default=0)
    create_parser.add_argument("--heads", type=int, default=8)
    create_parser.add_argument("--sectors", type=int, default=17)
    create_parser.add_argument(
        "--logo",
        help="optional 80x120 packed 1bpp LOGO.RAW for the boot screen")
    create_parser.add_argument(
        "--dos-loader",
        help="optional DOS Linux loader stored as LINUX98.EXE")
    create_parser.add_argument(
        "--small-ext4", action="store_true",
        help="use a 1 MiB journal and omit large-filesystem ext4 features")
    create_parser.set_defaults(function=create)

    validate_parser = subparsers.add_parser(
        "validate", help="validate PC-98 CHS entries against image size")
    validate_parser.add_argument("image")
    validate_parser.add_argument("--heads", type=int, default=8)
    validate_parser.add_argument("--sectors", type=int, default=17)
    validate_parser.set_defaults(function=validate)

    args = parser.parse_args()
    set_geometry(args.heads, args.sectors)
    try:
        args.function(args)
    except RuntimeError as error:
        parser.exit(1, f"error: {error}\n")


if __name__ == "__main__":
    main()
