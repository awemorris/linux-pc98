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
| Partition 1   | Standard DOS-compatible FAT16 containing `BZIMAGE` |
| Partition 2   | ext4 Debian root filesystem                        |

The fixed BIOS geometry is eight heads and seventeen sectors per track.
Partition 1 begins at cylinder 1, which corresponds to LBA 136.

`disk-ipl.bin` obtains the first-partition LBA from the first entry in the
LBA 1 partition table, then loads the second stage from LBA 2 at `1000:0000`.
The image builder writes the second-stage sector count at IPL offset 8. The
loader does not assume that BIOS service `INT 1Bh/AH=06h` preserves general
registers; it saves and restores them around every disk read.

Partition 1 does not require a custom PBR. The image builder installs a
normal FAT16 BPB and a non-booting PBR that DOS can recognize. A DOS
installer may replace that PBR without affecting the Linux boot path from
LBA 0.

## FAT16 second stage

`fat-loader.bin` reads the BPB and root directory from Partition 1 and
searches for the 8.3 name `BZIMAGE`. It follows the FAT16 cluster chain, so
the kernel file may be fragmented. The loader validates the bzImage
`setup_sects`, `syssize`, `HdrS`, and `LOADED_HIGH` fields before copying the
protected-mode payload to physical address `0x100000`.

## Memory layout

| Physical address | Contents                                           |
|------------------|----------------------------------------------------|
| `0x00000500`     | Handoff data from the disk IPL to the second stage |
| `0x00010000`     | FAT16 second-stage loader                          |
| `0x00020000`     | 4096-byte `boot_params` structure                  |
| `0x00021000`     | Kernel command line                                |
| `0x00028000`     | BPB and FAT read buffer                            |
| `0x00030000`     | Directory and kernel read buffer                   |
| `0x00100000`     | bzImage protected-mode payload                     |

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

`update-kernel.sh` updates the FAT16 filesystem and `BZIMAGE` in Partition 1
without modifying the Debian userland in Partition 2.
