# Boot Loaders

The release images use a replaceable PC-98 boot chain.  Disk and partition
loaders use PC-98 BIOS disk services; the FAT16-hosted 32-bit program provides
the menu, interactive shell, and Linux ELF loader.  The PC/AT real-mode setup
code is not executed.

## Disk layout

| LBA or region | Contents |
|---------------|----------|
| 0 | Generic 512-byte `ipl-lba0.bin`, with `IPL1` at offset 4 |
| 1 | PC-98 partition table: sixteen 32-byte entries |
| 2 through 15 | Generic `ipl-lba2.bin` BOOT-partition selector |
| BOOT partition start | One 1024-byte DOS-compatible FAT16 PBR/BPB reserved sector |
| BOOT partition files | Contiguous `IO.SYS`, `boot.bin`, `boot.cfg`, `VMLINUX`, applets, and optional extension BIOS files |
| Partition 2 | ext4 root filesystem |

`ipl-lba0.bin` is deliberately generic and silent.  It immediately loads the
sector at LBA 2 of the current fixed disk and transfers control to it at the
conventional `1fc0:0000` address.  It has no knowledge of BOOT98, FAT, or the
length of the next program.

`ipl-lba2.bin` is a 14-sector replaceable image for LBA 2 through 15.  Its
first sector reads the native PC-98 partition table from LBA 1, locates the
entry whose padded 16-byte name is exactly `BOOT`, loads that entry's
IPL-start CHS through `INT 1Bh/AH=06h`, and jumps to it.  The other thirteen
sectors are zero-filled.  A user may replace this complete image with the NEC
fixed-disk boot menu or another project's IPL without changing the BOOT
partition.

The BOOT volume follows the DOS `IO.SYS` model. Its one 1024-byte reserved
logical sector contains only the PBR/BPB and is not part of the FAT cluster
area. Firmware may load only its first physical 512 bytes, so that half reads
the second half explicitly. The complete PBR then finds `IO.SYS` in the FAT16
root, loads the contiguous file at `1000:0000`, and enters it. No prefix of
`IO.SYS` is duplicated in the reserved sector. Consequently the same BOOT
partition can be selected by the distributed stubs or the NEC fixed-disk
boot menu, while DOS mounts it as an ordinary volume.

The complete real-mode `IO.SYS` probes BIOS-visible disks with
`INT 1Bh/AH=84h`, uses each disk's returned logical CHS to interpret its
PC-98 partition table, opens the FAT16 filesystem at the BOOT entry's start,
and loads `boot.bin`.  It does not program ATA or SCSI registers and is
therefore independent of the controller used by the BIOS.

Images created without geometry options use the BIOS 8/17 layout, so
Partition 1 begins at cylinder 1 (LBA 136). The image builder also accepts
`--heads` and `--sectors`; for example, `--heads 4 --sectors 17` creates an
image for an older BIOS four-head geometry.

Official distribution images currently use only the 8/17 layout. The older
four-head layout remains experimental and is not published as a prebuilt
image. `update-kernel.sh` detects an existing image's geometry from its
cylinder-aligned partition entries; `DISK_HEADS` and `DISK_SECTORS` can
override that detection for development. The selected geometry affects the
on-disk partition CHS fields. The geometry actually returned by BIOS SENSE,
the BIOS drive number, and an ABI version are also passed to Linux in a
`SETUP_PC98_DISK` setup-data node attached to `boot_params`.

`boot.bin` is the protected-mode third stage. Probe status and the menu are
written sequentially from the next text row. It displays this menu:

```text
Boot from:
  1) Auto (HDD 1 partition 1 boot.cfg)
  2) FDD 1
  3) FDD 2
  4) HDD 1
  5) HDD 2

Press ESC key to fallback to shell.
```

`Auto` executes `BOOT.CFG`.  Escape skips automatic configuration and enters
the interactive command shell.  FDD and HDD entries chain-load the selected
device's boot record.  Cursor-key selection is a planned user-interface
improvement; the current menu uses number keys.

The Linux loader prints the ELF file size and updates separate `text` and
`data` transferred-size counters while reading the kernel.

The BOOT partition is marked active and bootable as a DOS FAT16 volume
(`MID=0xa1`, `SID=0x91`). This makes it visible from DOS and selectable by
the NEC fixed-disk boot menu. The image formatter is
intentionally destructive to that partition: it creates one 1024-byte FAT
reserved sector at the common IPL/data-start CHS, installs the matching PBR
and contiguous `IO.SYS`,
`boot.bin`, `boot.cfg`, and `VMLINUX`, and leaves root, swap, LBA 0, and
LBA 2–15
untouched. Pass `--install-disk-stubs` only when the distributed LBA 0 and
LBA 2–15 images should replace the existing disk IPL. The loaders do not
assume that BIOS service `INT 1Bh/AH=06h`
preserves general registers; they save and restore them around every read.

The current BOOT filesystem uses 1024-byte FAT logical sectors over the
PC-98 BIOS's 512-byte physical transfers. The native PC-98 partition table
records the same CHS for the BOOT IPL and filesystem start so NEC MS-DOS can
mount the volume.

## Legacy FAT16 Linux loader

`fat-loader.bin` is retained for legacy images.  It reads 512- or 1024-byte logical-sector BPBs and the root
directory from Partition 1, then searches for the 8.3 name `VMLINUX`. It
also recognizes `BZIMAGE` on disks made before release 0.3.0. It follows the
FAT16 cluster chain, so the kernel file may be
fragmented.

For a bzImage, the loader validates `setup_sects`, `syssize`, `HdrS`, and
`LOADED_HIGH` before copying the protected-mode payload to physical address
`0x100000`. It writes BSD-style progress directly to the first GDC text row:
total size, load address, a live byte count, and progress dots. It changes the
line to `Decompressing Linux...` before entering the compressed kernel.

For an ELF32/i386 `vmlinux`, the loader validates the ELF header and the
program-header table held in the first 1024 bytes. It accepts up to four
`PT_LOAD` segments, streams their file contents directly to each segment's
physical address, clears the file-to-memory tail, and jumps to `e_entry`.
This avoids retaining a compressed input image and a decompression workspace
in guest RAM. It is the preferred path for memory-constrained i386 PC-98
systems, where CF storage capacity is less important than RAM.

The ELF path clears the PC-98 text and planar graphics VRAM and displays a
fixed Japanese boot status screen. Code and data transfer counts are updated
separately in KiB. An optional root-directory file named `LOGO.RAW` is drawn
at the lower-right corner (x=560, y=280 on the 640 by 400 graphics screen).
It must be exactly 1200 bytes: 80 by 120 pixels,
packed 1bpp, 10 bytes per row, most-significant bit first. The same bitmap is
copied to the B, R, G, and I planes, producing a white image. If the file is
absent, loading continues without a logo.

Before clearing and drawing, the loader invokes the standard PC-98 graphics
BIOS services (`INT 18h`, `AH=42h` and `AH=40h`) to select the 640 by 400
colour display area and start the slave GDC. It also programs the standard
digital palette explicitly. The bitmap therefore does not depend on graphics
state left behind by either the NEC ROM BIOS or the compatible BIOS.

The loader explicitly writes bit 2 of port `0x43b` to expose the 15--16 MiB
region as RAM. Consecutive ELF segments reuse the current FAT stream position;
the loader does not reread the complete first segment while seeking the next
one.

The kernel command line enables `earlyprintk=pc9800`, which takes over as soon
as the kernel starts and unregisters when the normal PC-98 console is ready.

## Memory layout

| Physical address | Contents                                           |
|------------------|----------------------------------------------------|
| `0x00000700`     | Handoff data between BOOT98 stages                 |
| `0x00010000`     | Complete `IO.SYS` or legacy FAT16 loader           |
| `0x00020000`     | 4096-byte `boot_params` structure                  |
| `0x00021000`     | Kernel command line                                |
| `0x00022000`     | `SETUP_PC98_DISK` setup-data node                   |
| `0x00028000`     | BPB and FAT read buffer                            |
| `0x00030000`     | Directory and kernel read buffer                   |
| `0x00100000`     | bzImage payload, or first ELF `PT_LOAD` segment    |

The partition IPL obtains the memory sizes used for the e820 map from the
PC-98 BIOS work area: `0:0501h` describes conventional RAM, `0:0401h`
describes RAM from 1 MiB through the 16 MiB boundary, and `0:0594h` describes
MiB above 16 MiB.  Omitting the last range makes a 64 MiB machine appear to
Linux as roughly 17 MiB and causes severe swapping. It enables A20 and uses a
flat unreal-mode segment to copy data above 1 MiB. It then enters the kernel
according to the Linux x86 boot protocol with `CS=0x10`, `DS/ES/SS=0x18`, and
`ESI` pointing to `boot_params`.

Linux uses the handed BIOS H/S values to interpret the NEC98 partition table.
The native `pc98_ide` block driver and `pata_pc9800`/libata then access the
device with LBA when ATA IDENTIFY advertises it and fall back to the device's
ATA CHS geometry otherwise. BIOS logical geometry is intentionally not used
as ATA device geometry.

## DOS loader

`bootloader/dos/linux98.exe` is a separate real-mode DOS command for systems whose
logical geometry is installed by an IPL utility before Linux is started. It
uses DOS file I/O to load an uncompressed `VMLINUX`, passes the current INT
1Bh SENSE result through the same `SETUP_PC98_DISK` ABI, and switches to
protected mode before entering the kernel. See `bootloader/dos/README.md` for its
current restrictions.

## DOS disk installer

`bootloader/dos/inst.exe` is a deliberately small installer. It does not
partition or format a disk. It has
only three subcommands:

```dos
INST /LBA0
INST /LBA2
INST /PART C:
```

`/LBA0` copies the adjacent `IPL-LBA0.IMG` to LBA 0. `/LBA2` copies
`IPL-LBA2.IMG` to LBA 2 through 15. Both commands identify the physical IDE
disk that contains the current DOS drive. `/PART C:` maps C: back to its
PC-98 partition, copies adjacent `IO.SYS` as a normal FAT file, verifies that
its cluster chain is contiguous, and installs adjacent `IPL-PART.IMG` in the
one-logical-sector PBR area. It preserves the existing BPB and refuses a
volume that is not named `BOOT` or is not FAT16 with 1024-byte sectors.

All writes use PC-98 BIOS `INT 1Bh` absolute CHS access and are read back for
verification. `BOOT.BIN`, `BOOT.CFG`, `VMLINUX`, and other contents are copied
normally with DOS commands.

## Building and updating

```sh
make -C bootloader
./build.sh rootfs debian13-i686
./build.sh image debian13-i686-h8
```

The repository includes the release bitmap as `bootloader/boot-logo.raw`. Set
`BOOT_LOGO` when creating or updating an image to add it:

```sh
BOOT_LOGO=bootloader/boot-logo.raw \
  ./build.sh image debian13-i686-h8 \
    --base-image path/to/disk.img --kernel path/to/vmlinux.boot
```

`update-kernel.sh` preserves the existing disk IPL code at LBA 0 and LBA 2–15, installs matching
`IO.SYS`, recreates the BOOT FAT16 filesystem, and stores `boot.bin`,
`boot.cfg`, and the ELF input as `VMLINUX`. It does not modify root or swap
partitions. Canonical release-image profiles additionally install
`ipl-lba0.bin` and `ipl-lba2.bin`. Image creation rejects compressed or other non-ELF kernel files;
the older bzImage loader path remains only for compatibility with disks made
before release 0.3.0.
