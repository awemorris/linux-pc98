#!/usr/bin/env python3
"""Verify a bootsimple PC-98 FAT16 image and its uncompressed VMLINUX."""

import argparse
import hashlib
import os
import struct
import sys

PHYS_SECTOR = 512


class VerifyError(RuntimeError):
    pass


def u16(data, offset):
    return struct.unpack_from("<H", data, offset)[0]


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def chs_lba(raw, heads, sectors):
    sector = raw[0]
    head = raw[1]
    cylinder = u16(raw, 2)
    if sector >= sectors or head >= heads:
        raise VerifyError(
            f"invalid CHS {cylinder}/{head}/{sector} for H={heads}/S={sectors}")
    return (cylinder * heads + head) * sectors + sector


def partition(image, heads, sectors, selected):
    if len(image) < 1024:
        raise VerifyError("image is too small")
    entries = []
    table = image[512:1024]
    for index in range(16):
        entry = table[index * 32:(index + 1) * 32]
        if entry[0] == 0:
            continue
        start = chs_lba(entry[8:12], heads, sectors)
        end = chs_lba(entry[12:16], heads, sectors)
        name = entry[16:32].decode("ascii", "replace").rstrip()
        entries.append((index + 1, entry, start, end, name))
    if selected:
        matches = [item for item in entries if item[0] == selected]
    else:
        matches = [item for item in entries if item[4] == "BOOT"]
    if len(matches) != 1:
        raise VerifyError(f"cannot select one BOOT partition (matches={len(matches)})")
    index, entry, start, end, name = matches[0]
    if name != "BOOT":
        raise VerifyError(f"partition {index} name is {name!r}, expected 'BOOT'")
    if not entry[0] & 0x80 or not entry[1] & 0x80:
        raise VerifyError("BOOT partition is not active and bootable")
    if start > end or (end + 1) * PHYS_SECTOR > len(image):
        raise VerifyError("BOOT partition lies outside the image")
    return index, start, end - start + 1


class Fat16:
    def __init__(self, image, start_lba, physical_sectors):
        self.image = image
        self.start = start_lba * PHYS_SECTOR
        self.limit = self.start + physical_sectors * PHYS_SECTOR
        bpb = image[self.start:self.start + 1024]
        if len(bpb) != 1024:
            raise VerifyError("short FAT16 PBR")
        if bpb[510:512] != b"\x55\xaa" or bpb[1022:1024] != b"\x55\xaa":
            raise VerifyError("FAT16 PBR signature is missing")
        self.bps = u16(bpb, 11)
        self.spc = bpb[13]
        self.reserved = u16(bpb, 14)
        self.fats = bpb[16]
        self.root_entries = u16(bpb, 17)
        self.total = u16(bpb, 19) or u32(bpb, 32)
        self.spf = u16(bpb, 22)
        if self.bps != 1024:
            raise VerifyError(f"BOOT bytes/sector is {self.bps}, expected 1024")
        if not all((self.spc, self.reserved, self.fats,
                    self.root_entries, self.total, self.spf)):
            raise VerifyError("incomplete FAT16 BPB")
        if self.start + self.total * self.bps > self.limit:
            raise VerifyError("FAT16 BPB exceeds the BOOT partition")
        self.fat = self.start + self.reserved * self.bps
        self.root = self.fat + self.fats * self.spf * self.bps
        root_bytes = self.root_entries * 32
        self.root_sectors = (root_bytes + self.bps - 1) // self.bps
        self.data = self.root + self.root_sectors * self.bps
        data_sectors = self.total - (self.reserved + self.fats * self.spf +
                                     self.root_sectors)
        clusters = data_sectors // self.spc
        if not 4085 <= clusters < 65525:
            raise VerifyError(f"BOOT is not FAT16 (clusters={clusters})")
        self.clusters = clusters
        self.entries = self._root_entries()

    def _root_entries(self):
        result = {}
        for index in range(self.root_entries):
            pos = self.root + index * 32
            raw = self.image[pos:pos + 32]
            if raw[0] == 0:
                break
            if raw[0] == 0xE5 or raw[11] == 0x0F:
                continue
            name = raw[:11].decode("ascii", "replace")
            result[name] = {
                "attr": raw[11],
                "cluster": u16(raw, 26),
                "size": u32(raw, 28),
            }
        return result

    def chain(self, first, size):
        if size == 0:
            return []
        if first < 2:
            raise VerifyError("nonempty file has an invalid first cluster")
        need = (size + self.spc * self.bps - 1) // (self.spc * self.bps)
        chain = []
        seen = set()
        cluster = first
        while len(chain) < need:
            if cluster < 2 or cluster >= self.clusters + 2:
                raise VerifyError(f"FAT cluster {cluster:#x} is out of range")
            if cluster in seen:
                raise VerifyError("FAT chain loop")
            seen.add(cluster)
            chain.append(cluster)
            entry = self.fat + cluster * 2
            cluster = u16(self.image, entry)
            if len(chain) < need and cluster >= 0xFFF8:
                raise VerifyError("FAT chain ends before file size")
            if cluster == 0xFFF7:
                raise VerifyError("FAT chain contains a bad cluster")
        return chain

    def read_file(self, name):
        try:
            entry = self.entries[name]
        except KeyError as error:
            raise VerifyError(f"missing FAT root file {name!r}") from error
        chain = self.chain(entry["cluster"], entry["size"])
        chunks = []
        cluster_bytes = self.spc * self.bps
        for cluster in chain:
            pos = self.data + (cluster - 2) * cluster_bytes
            chunks.append(self.image[pos:pos + cluster_bytes])
        return entry, chain, b"".join(chunks)[:entry["size"]]


def verify_elf(data):
    if len(data) < 52 or data[:4] != b"\x7fELF":
        raise VerifyError("VMLINUX is not ELF")
    if data[4:7] != b"\x01\x01\x01" or u16(data, 18) != 3:
        raise VerifyError("VMLINUX must be ELF32 little-endian EM_386")
    entry = u32(data, 24)
    phoff = u32(data, 28)
    phentsize = u16(data, 42)
    phnum = u16(data, 44)
    if phentsize != 32 or not 1 <= phnum <= 16:
        raise VerifyError("unsupported VMLINUX program header table")
    if phoff + phnum * phentsize > min(len(data), 1024):
        raise VerifyError("VMLINUX program headers must fit in the first 1024 bytes")
    loads = []
    for index in range(phnum):
        pos = phoff + index * phentsize
        values = struct.unpack_from("<IIIIIIII", data, pos)
        p_type, offset, _vaddr, paddr, filesz, memsz, flags, _align = values
        if p_type != 1:
            continue
        if filesz > memsz or offset % 512 or offset + filesz > len(data):
            raise VerifyError(f"invalid PT_LOAD {index}")
        if paddr < 0x100000 or paddr + memsz > 0x100000000:
            raise VerifyError(f"PT_LOAD {index} has an unsafe physical range")
        loads.append((offset, offset + filesz, paddr, paddr + memsz, flags))
    if not 1 <= len(loads) <= 4:
        raise VerifyError(f"unsupported PT_LOAD count {len(loads)}")
    if loads != sorted(loads, key=lambda item: item[0]):
        raise VerifyError("PT_LOAD entries are not in file-offset order")
    for left, right in zip(loads, loads[1:]):
        if left[1] > right[0] or left[3] > right[2]:
            raise VerifyError("PT_LOAD ranges overlap")
    if not any(item[2] <= entry < item[3] for item in loads):
        raise VerifyError("ELF entry is outside PT_LOAD memory")
    return len(loads)


def verify_image(args):
    image = open(args.image, "rb").read()
    if image[510:512] != b"\x55\xaa" or image[4:8] != b"IPL1":
        raise VerifyError("invalid bootsimple LBA 0 IPL")
    if image[2 * PHYS_SECTOR + 510:2 * PHYS_SECTOR + 512] != b"\x55\xaa":
        raise VerifyError("invalid bootsimple LBA 2 selector")
    index, start, count = partition(
        image, args.heads, args.sectors, args.partition)
    fat = Fat16(image, start, count)
    io_entry, io_chain, io_data = fat.read_file("IO      SYS")
    vm_entry, vm_chain, vm_data = fat.read_file("VMLINUX    ")
    if not io_data or len(io_data) > 65024:
        raise VerifyError(f"IO.SYS size {len(io_data)} is outside PBR limits")
    if any(right != left + 1 for left, right in zip(io_chain, io_chain[1:])):
        raise VerifyError("IO.SYS is not physically contiguous")
    required_attr = 0x01 | 0x02 | 0x04
    if io_entry["attr"] & required_attr != required_attr:
        raise VerifyError("IO.SYS lacks read-only/hidden/system attributes")
    if vm_entry["attr"] & required_attr != required_attr:
        raise VerifyError("VMLINUX lacks read-only/hidden/system attributes")
    segments = verify_elf(vm_data)
    forbidden = {
        "VMUNIX     ", "BOOT    CFG", "SWAPFILE   ", "BIN        ",
        "ETC        ", "APPS       ", "LINUX98 EXE", "INST    EXE",
    }
    found = sorted(forbidden.intersection(fat.entries))
    if found:
        raise VerifyError(f"zedBSD/product files present in bootsimple BOOT: {found}")
    if args.kernel:
        expected = open(args.kernel, "rb").read()
        if hashlib.sha256(expected).digest() != hashlib.sha256(vm_data).digest():
            raise VerifyError("FAT VMLINUX differs from the requested kernel")
    print(
        f"bootsimple image OK: partition={index} LBA={start}+{count} "
        f"IO.SYS={len(io_data)} bytes/{len(io_chain)} clusters "
        f"VMLINUX={len(vm_data)} bytes/{len(vm_chain)} clusters "
        f"PT_LOAD={segments}")


def verify_elf_command(args):
    data = open(args.vmlinux, "rb").read()
    print(f"VMLINUX ELF OK: {verify_elf(data)} PT_LOAD, {len(data)} bytes")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    image = commands.add_parser("all")
    image.add_argument("image")
    image.add_argument("--heads", type=int, required=True)
    image.add_argument("--sectors", type=int, required=True)
    image.add_argument("--partition", type=int, default=1)
    image.add_argument("--kernel")
    image.set_defaults(function=verify_image)
    elf = commands.add_parser("elf")
    elf.add_argument("vmlinux")
    elf.set_defaults(function=verify_elf_command)
    args = parser.parse_args()
    try:
        args.function(args)
    except (OSError, VerifyError, struct.error) as error:
        parser.exit(1, f"bootsimple verify: {error}\n")


if __name__ == "__main__":
    main()

