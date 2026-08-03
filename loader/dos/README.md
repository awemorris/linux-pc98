# PC-98 DOS Linux loader

`linux98.exe` loads an uncompressed ELF32/i386 `VMLINUX` through DOS file
I/O, copies every `PT_LOAD` segment to its ELF physical address, builds Linux
`boot_params`, then switches to 32-bit protected mode and enters `e_entry`.

It also calls PC-98 BIOS INT 1Bh SENSE and passes the selected BIOS drive and
logical heads/sectors to Linux through `SETUP_PC98_DISK`.  This is intended to
work with BIOS geometry extensions installed by DOS utilities.

Build with OpenWatcom 1.9:

```sh
make
```

Run from plain real-mode DOS:

```dos
LINUX98.EXE VMLINUX 0 root=/dev/hd98a2 rw
```

The drive argument is the BIOS fixed-disk unit (`0` through `3`).  If it is
omitted, the loader uses BIOS work-area byte `0000:0584`.  The initial version
does not support EMM386, QEMM, a Windows DOS box, initrd, or compressed kernel
images.  HIMEM.SYS may be loaded only if it does not virtualize protected
mode; testing first without memory managers is recommended.
