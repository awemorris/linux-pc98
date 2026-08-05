BOOT98 build and test
=====================

Build on the Debian host:

```sh
cd ~/work/boot98-phase1
make -C loader
```

The build reports the Stage 1 size against its 7 KiB limit and produces
`bootloader/BOOT98.BIN`, the applet self-test, and both IPLware return-style
self-tests.

Create a basic FAT16 test disk:

```sh
BOOT98_STAGE2=bootloader/BOOT98.BIN \
  scripts/make-boot98-test-image.sh build/boot98/boot98.img
```

Optional environment variables add `BOOT98.CFG`, an ELF kernel, the applet,
or IPLware tests: `BOOT98_CFG`, `BOOT98_KERNEL`, `BOOT98_APPLET`,
`BOOT98_IPLWARE_BIN`, `BOOT98_IPLWARE_COM`, and `BOOT98_FILES`.

QEMU uses `~/qemu-pc98/build-i386-port/qemu-system-i386`, machine
`pc9821`, and ROMs from `~/qemu-pc98/roms/pc98bios`. Always use
`snapshot=on` for user-provided images and terminate only the exact QEMU
instance started by the test.

Validated cases include missing/corrupt Stage 2 fallback, FDD/IDE/SCSI and
secondary-IDE chain loading, non-default logical geometry, CFG execution,
FAT listing, Escape return, applet CRC/entry execution, IPLware type 1 and
type 2 return, and ELF Linux entry through root-device discovery.

The shell's `boot` command has two paths.  After `kernel`, it loads and enters
the selected ELF32/i386 Linux image.  Without a kernel selection, it asks the
real-mode gateway to chain-load the currently selected disk IPL or partition
PBR; this uses the same native partition-table and BIOS work-area handoff as
the Stage 1 upper menu.

The completed QEMU matrix uses snapshot mode and covers:

- primary and secondary IDE, FDD plus IDE, non-default logical H/S geometry,
  and PC-9801-92 SCSI using the preserved real option ROM;
- absent and checksum-invalid `BOOT98.BIN`, both retaining the Stage 1 menu;
- FAT16 CFG/fragmented-file reads, device reprobe, listing and Escape return;
- direct Stage 2-to-PBR chain boot (`BOOT98-CHAIN.CFG.test`);
- applet header/CRC/services/return and IPLware type 1/type 2 register/return
  self-tests;
- Linux ELF segment loading and entry, observed through PC-98 IDE discovery
  and the intentionally configured missing-root panic.

These are emulator results, not a claim of compatibility on every PC-98.
Real-machine testing remains required, particularly for unusual BIOS disk
geometry and option-ROM combinations.
