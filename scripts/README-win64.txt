QEMU for NEC PC-9800 series - Windows x86-64 distribution
==========================================================

Run virtpc98.exe for the graphical launcher.  The package also contains
qemu-system-i386.exe and qemu-system-x86_64.exe for direct command-line use.

The included free PC-98 firmware is in share\pc98bios.  Select that directory
in virtpc98.exe, or pass it to QEMU with:

  -L share\pc98bios

IDE HDD example:

  qemu-system-x86_64.exe -M pc9821 -m 64M -L share\pc98bios ^
    -drive if=ide,bus=0,unit=0,format=raw,file=disk.raw

PC-9801-92 SCSI HDD example:

  qemu-system-x86_64.exe -M pc9821 -m 64M -L share\pc98bios ^
    -drive if=scsi,bus=0,unit=0,format=raw,file=scsi-disk.raw

The SCSI option ROM is share\pc98bios\pc98scsi.bin.  Keep it in that
directory when moving files from this package.

Sound example (PC-9801-86):

  -audiodev dsound,id=snd -device pc98-opna,audiodev=snd

Sound example (WSS):

  -audiodev dsound,id=snd -device pc98-wss,audiodev=snd

See the project README and documentation for supported devices, firmware,
disk formats and source-code licensing information.
