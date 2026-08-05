# PC-98 DOS Linux loader

`linux98.exe` loads an uncompressed ELF32/i386 `VMLINUX` through DOS file
I/O, copies every `PT_LOAD` segment to its ELF physical address, builds Linux
`boot_params`, then switches to 32-bit protected mode and enters `e_entry`.

It calls PC-98 BIOS INT 1Bh SENSE for every visible IDE and SCSI fixed disk
and passes one `SETUP_PC98_DISK` record per drive to Linux.  Records are keyed
by the raw BIOS drive ID (80h-83h for IDE and A0h-A7h for SCSI), and the drive
selected for boot is marked separately.  This lets Linux decode each NEC98
partition table with that disk's BIOS logical heads/sectors, including values
installed by DOS geometry-extension utilities.

The repository tracks a prebuilt `linux98.exe`; normal image builds copy that
binary and do not require OpenWatcom. Maintainers can rebuild it with
OpenWatcom 1.9 from the repository root:

```sh
./build.sh dos-loader
```

Run from plain real-mode DOS:

```dos
LINUX98.EXE VMLINUX ide0 root=/dev/hd98a2 rw
LINUX98.EXE VMLINUX scsi0 root=/dev/sda2 rw
```

The drive argument accepts `ide0` through `ide3` (BIOS IDs 80h through 83h)
and `scsi0` through `scsi7` (BIOS IDs A0h through A7h). A bare digit `0`
through `3` remains an alias for the corresponding IDE unit. If it is
omitted, the loader uses BIOS work-area byte `0000:0584`. The selected device
must respond to SENSE; absent units encountered while enumerating the remaining
IDE and SCSI namespaces are skipped. All successful logical H/S results are
passed unchanged to Linux. The initial version does not support EMM386, QEMM,
a Windows DOS box, initrd, or compressed kernel images. HIMEM.SYS may be loaded
only if it does not virtualize protected mode; testing first without memory
managers is recommended.
