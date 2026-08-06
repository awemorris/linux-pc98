BOOT98 build and test
=====================

Build on the Debian host:

```sh
cd ~/linux-pc98
make -C bootloader
```

The build reports the BOOT partition IPL size against its 7 KiB limit and produces
`bootloader/ipl-lba0.bin`, `bootloader/ipl-lba2.bin`, `bootloader/boot.sys`,
`bootloader/boot.bin`, the applet self-test, and both IPLware return-style
self-tests.

Create a named BusyBox image with the complete three-stage chain:

```sh
./build.sh image busybox-i386-h8 \
  --output build/images/boot98-i386-test.raw
```

The installer writes the generic stub to LBA 0, the replaceable BOOT selector
to LBA 2–15, raw `boot.sys` to the BOOT partition IPL cylinder, and
`boot.bin` plus `boot.cfg` to its FAT16 data area. LBA 0 loads only one LBA 2
sector, so the complete LBA 2–15 image may be replaced by the NEC fixed-disk
menu or another project's compatible bootstrap.

For an existing disk which already contains the NEC menu, recreate only the
selected BOOT partition and preserve disk IPL code at LBA 0 and LBA 2–15:

```sh
./build.sh boot-install --partition 1 disk.img VMLINUX BOOT.CFG
```

Add `--install-disk-stubs` only when creating a distributed-stub image.

Optional environment variables add `BOOT.CFG`, an ELF kernel, the applet,
or IPLware tests: `BOOT98_CFG`, `BOOT98_KERNEL`, `BOOT98_APPLET`,
`BOOT98_IPLWARE_BIN`, `BOOT98_IPLWARE_COM`, and `BOOT98_FILES`.

Run the automated headless smoke test with:

```sh
./build.sh test busybox-i386 \
  --image build/images/boot98-i386-test.raw --timeout 55
```

The fixed-disk IPL silently enters LBA 2, and the third-stage menu selects
`Auto` after its three-second first-key timeout.  The test succeeds only after
the kernel reaches the BusyBox shell; it does not inject keyboard input. QEMU uses
`~/linux-pc98/qemu-pc98/build/qemu-system-i386`, machine `pc9801`, and ROMs
from the submodule's `pc-bios` directory. Always use
`snapshot=on` for user-provided images and terminate only the exact QEMU
instance started by the test.

At the menu, Escape enters the interactive shell.  Kernel loading displays the
ELF file size and live `text` and `data` transferred-size counters.

The 64 MiB Debian/i486 regression test must report substantially more than
16 MiB in `/proc/meminfo`.  BOOT98 builds three E820 RAM entries when BIOS work
area `0:0594h` is nonzero.  A verified 64 MiB QEMU run reported 47,460 KiB of
managed memory after kernel reservations, reached the root login, and used no
swap after login.

Earlier development tests covered missing/corrupt third-stage fallback, FDD/IDE/SCSI and
secondary-IDE chain loading, non-default logical geometry, CFG execution,
FAT listing, Escape return, applet CRC/entry execution, IPLware type 1 and
type 2 return, and ELF Linux entry through root-device discovery.

The shell's `boot` command has two paths.  After `kernel`, it loads and enters
the selected ELF32/i386 Linux image.  Without a kernel selection, it asks the
real-mode gateway to chain-load the currently selected disk IPL or partition
PBR; this uses the same native partition-table and BIOS work-area handoff as
the second-stage fallback menu.

The completed QEMU matrix uses snapshot mode and covers:

- primary and secondary IDE, FDD plus IDE, non-default logical H/S geometry,
  and PC-9801-92 SCSI using the preserved real option ROM;
- absent and checksum-invalid `boot.bin`, both retaining the partition-IPL fallback menu;
- FAT16 CFG/fragmented-file reads, device reprobe, listing and Escape return;
- direct `boot.bin`-to-PBR chain boot (`BOOT98-CHAIN.CFG.test`);
- applet header/CRC/services/return and IPLware type 1/type 2 register/return
  self-tests;
- Linux ELF segment loading and entry, observed through PC-98 IDE discovery
  and the intentionally configured missing-root panic.

These are emulator results, not a claim of compatibility on every PC-98.
Real-machine testing remains required, particularly for unusual BIOS disk
geometry and option-ROM combinations.
