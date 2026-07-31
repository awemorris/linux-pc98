# Boot Loaders

The qemu-pc98 raw disk uses two custom loader stages to bridge the PC-98
firmware interface and the Linux 32-bit boot protocol. The PC/AT real-mode
setup code is not executed.

## Disk layout

| LBA or region | Contents                                           |
|---------------|----------------------------------------------------|
| 0             | `disk-ipl.bin`, with `IPL1` at offset 4            |
| 1             | PC-98 partition table: sixteen 32-byte entries     |
| 2 through 135 | `fat-loader.bin` followed by unused space          |
| Partition 1   | PC-98 DOS-compatible FAT16 with `VMLINUX`          |
| Partition 2   | ext4 Debian root filesystem                        |

The fixed BIOS geometry is eight heads and seventeen sectors per track.
Partition 1 begins at cylinder 1, which corresponds to LBA 136.

`disk-ipl.bin` obtains the first-partition LBA from the first entry in the
LBA 1 partition table, then loads the second stage from LBA 2 at `1000:0000`.
The first partition is marked active (`MID=0xa1`) and contains
`partition-pbr.bin`. This provides a second boot path for a genuine PC-98
disk IPL: selecting the Linux partition loads the same second stage from
LBA 2. The image builder writes the second-stage sector count into both
boot records. In the disk IPL, this count is kept outside the `IPL1` header
at offset `0x1f0`, leaving the firmware-visible reserved bytes at offsets
8 through 10 clear. The loaders do not assume that BIOS service
`INT 1Bh/AH=06h`
preserves general registers; they save and restore them around every read.

The FAT BPB uses the PC-98 fixed-disk convention of 1024-byte logical DOS
sectors over 512-byte physical IDE sectors. Its offsets `0x3e..0x45`
contain the NEC DOS extension: absolute partition start, relative data
start, and physical sector size. A DOS formatter or installer may replace
the PBR and filesystem to turn the first partition into a DOS system
partition; that also replaces the Linux kernel file, so Linux must be restored
afterwards if both uses are desired.

## FAT16 second stage

`fat-loader.bin` reads 512- or 1024-byte logical-sector BPBs and the root
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

The kernel command line enables `earlyprintk=pc9800`, which takes over as soon
as the kernel starts and unregisters when the normal PC-98 console is ready.

## Memory layout

| Physical address | Contents                                           |
|------------------|----------------------------------------------------|
| `0x00000700`     | Handoff data from the disk IPL to the second stage |
| `0x00010000`     | FAT16 second-stage loader                          |
| `0x00020000`     | 4096-byte `boot_params` structure                  |
| `0x00021000`     | Kernel command line                                |
| `0x00028000`     | BPB and FAT read buffer                            |
| `0x00030000`     | Directory and kernel read buffer                   |
| `0x00100000`     | bzImage payload, or first ELF `PT_LOAD` segment    |

The second stage obtains the memory sizes used for the e820 map from the
PC-98 BIOS work area. It enables A20 and uses a flat unreal-mode segment to
copy data above 1 MiB. It then enters the kernel according to the Linux x86
boot protocol with `CS=0x10`, `DS/ES/SS=0x18`, and `ESI` pointing to
`boot_params`.

## Building and updating

```sh
make -C loader
./build-debian.sh
./update-kernel.sh
```

`update-kernel.sh` updates both boot records, the second-stage loader, and the
FAT16 filesystem without modifying Partition 2. The image builder stores an
ELF input as `VMLINUX`. Image creation rejects compressed or other non-ELF
kernel files; the older bzImage loader path remains only for compatibility
with disks made before release 0.3.0.
