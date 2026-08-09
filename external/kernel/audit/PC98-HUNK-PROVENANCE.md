# PC-98 clean-port hunk provenance ledger

This ledger maps each substantial PC-98-specific source hunk in the Linux 7.1
clean reconstruction to allowed implementation inputs.  It supplements
`PC98-CODE-PROVENANCE-AUDIT.md`; the excluded Linux 6.12 implementation is an
audit target only and is not an implementation source.

The historical paths below refer to official Linux tree
`566f0a0b5d388d0cd6cf9a796378470e7ad602b5`.  Modern paths refer to official
Linux v7.1 commit `8cd9520d35a6c38db6567e97dd93b1f11f185dc6`.

## Platform and interrupt/timer integration

| Clean target or hunk | Allowed historical source | Modern API/pattern | Class and adaptation |
|---|---|---|---|
| `arch/x86/Kconfig`, `arch/x86/Kconfig.cpu`, `arch/x86/Makefile_32.cpu` PC-98/i486 selections | `arch/i386/Kconfig`, historical CPU selections | v7.1 x86 Kconfig and build flags; official v6.14 i486 restoration | `HISTORICAL-2.6.7`, `UPSTREAM-7.1`; modern selection syntax only |
| `arch/x86/include/asm/i8259.h` PC-98 ports and `arch/x86/kernel/i8259.c`, `irqinit.c` branches | `include/asm-i386/mach-pc9800/io_ports.h`, historical `i8259.c` | v7.1 legacy PIC implementation | `HISTORICAL-2.6.7`; port constants and PC-98 cascade IRQ 7 applied to the unchanged modern PIC core |
| `include/linux/i8253.h`, `include/linux/timex.h` PC-98 branches | `mach_timer.h`, historical `timex.h` | v7.1 PIT clocksource | `HISTORICAL-2.6.7`; PC-98 channel ports and runtime 1.9968/2.4576 MHz selection |
| `arch/x86/kernel/setup.c`, `platform-quirks.c`, `kaslr.c` short PC-98 hooks | historical PC-98 machine selection and memory facts | v7.1 `x86_init` and setup ordering | `UPSTREAM-7.1`, `HISTORICAL-2.6.7`; short integration-only hunks |

## `arch/x86/kernel/pc9800.c`

| Symbol/hunk | Allowed source | Class and adaptation |
|---|---|---|
| BIOS work-area constants and `pc9800_init_platform()` platform setup | historical `include/asm-i386/pc9800.h`, `pc9800_sca.h`, `mach-pc9800/setup.c` | `HISTORICAL-2.6.7`, `UPSTREAM-7.1`; reads the documented BIOS work area and installs v7.1 hooks |
| `pc9800_standard_io_resources[]`, `pc9800_reserve_standard_io_resources()` | `arch/i386/mach-pc9800/std_resources.c` by Osamu Tomita | `HISTORICAL-2.6.7`; keyboard ports are intentionally omitted so the modern input driver owns 0x41/0x43 |
| `pc98_rtc_*`, `pc98_get_wallclock()`, `pc98_set_wallclock()` | `include/asm-i386/mach-pc9800/mach_time.h`; uPD4990A protocol | `HISTORICAL-2.6.7`; translated to v7.1 `x86_platform.get_wallclock`/`set_wallclock` and `timespec64` |
| `pc9800_restart()`, `pc9800_emergency_restart()` | `include/asm-i386/mach-pc9800/mach_reboot.h` | `HISTORICAL-2.6.7`; reset-port behavior connected to v7.1 restart hooks |

This is a substantial project-created aggregation file.  It preserves the
historical `Written by Osamu Tomita <tomita@cinet.co.jp>` credit and then
carries `Copyright (C) 2026 Awe Morris`.

## Text consoles

| Clean target or symbol | Allowed source | Class and adaptation |
|---|---|---|
| `early_pc9800_scroll()`, `early_pc9800_write()`, early-console registration hunk | `arch/i386/boot98/compressed/misc.c` text/attribute plane layout | `HISTORICAL-2.6.7`, `UPSTREAM-7.1`, `PROJECT-NEW`; independently written bounded writer/scroll logic using v7.1 early-console registration |
| `pc98con_startup()` through `pc98con_invert_region()` | PC-98 text/attribute VRAM layout from official boot code and public GDC documentation | `SPEC`, `UPSTREAM-7.1`, `PROJECT-NEW`; new v7.1 `consw` implementation using generic console-cell semantics |
| `pc98con_cursor()` | cursor-position source supplied directly by Awe Morris from an independently developed kernel | `PROJECT-NEW`; waits for GDC FIFO bit, sends CSRW 0x49, then the low/high address; it does not implement CSRFORM |
| `pc98_con` and `pc98_console_init()` | official v7.1 `consw` initializers and console registration patterns | `UPSTREAM-7.1`; interface wiring |

The PC-98 block in the existing `early_printk.c` is marked
`Added in the Awe Morris's port.`.  The new `pc98con.c` file carries the 2026
Awe Morris copyright notice.

## Keyboard

| Clean target | Allowed source | Class and adaptation |
|---|---|---|
| all functional code in `drivers/input/keyboard/pc98kbd.c` | exact project file at commit `fb253cfe61de8b72b6bacacb00137f434014f778`, explicitly permitted by the owner | `BSD`, explicit project permission; copied without functional changes |
| uPD8251 reset and recovery polling within that file | NetBSD/pc98 and FreeBSD/pc98 behavior identified by the owner; owner-supplied lost-interrupt patch | `BSD`, explicit project permission; undocumented hardware quirks are intentionally preserved |

The clean copy retains the applicable NetBSD/pc98 and Naofumi HONDA notices
and BSD conditions for the adapted reset sequence, followed by
`Copyright (C) 2026 Awe Morris`.  It is not represented as a
specification-only rewrite.

## PATA and partitions

| Clean target or symbol | Allowed source | Class and adaptation |
|---|---|---|
| PATA port resources and 0x432 bank select in `pata_pc9800.c` | `drivers/ide/legacy/pc9800.c` | `HISTORICAL-2.6.7`; preserves two-byte task-file spacing, control port, IRQ, and bank 0 selection |
| `pc98_pata_probe()`, driver/device registration | official v7.1 `drivers/ata/pata_platform.c` and platform-driver APIs | `UPSTREAM-7.1`, `PROJECT-NEW`; a small clean frontend delegates transfer/error handling to upstream libata |
| `pc98_pata_bios_param()` | PC-98 logical geometry requirement and v7.1 libata SCSI-host callback API | `HISTORICAL-2.6.7`, `UPSTREAM-7.1`; reports boot geometry without changing ATA LBA I/O |
| `nec98_table_valid()`, `nec98_partition()` | official `fs/partitions/nec98.c` | `HISTORICAL-2.6.7`; translated to v7.1 `parsed_partitions`, sector access, and bounds APIs |

Both new files carry `Copyright (C) 2026 Awe Morris`.  `pata_pc9800.c` also
retains the 1997-2000 Linux/98 project and Kyoto University Microcomputer Club
notice, and `nec98.c` retains the 1999 Kyoto University notice.

## Genuine i386 support

The i386 implementation is documented function-by-function in
`PC98-PORTING-REPORT.md`.  Linux v3.4 and v3.7 are historical behavior and
design references; the Linux 7.1 implementation comes from the owner-authored
project commits `787c10955` and `538c7e9c3`.  New substantive files carry
`Copyright (C) 2026 Awe Morris`, while substantial M386 blocks in existing
upstream files are marked `Added in the Awe Morris's port.`.

## Standard onboard uPD8251 serial

| Clean symbol group | Allowed source | Class and adaptation |
|---|---|---|
| register/command definitions, `pc9800_8251_command()`, `set_mode()` | official `drivers/serial/serial98.c`; uPD8251/PC-98 hardware specification | `HISTORICAL-2.6.7`, `SPEC`; reset/mode command sequence and fixed I/O map |
| TX/RX/IRQ functions | official `serial98.c` hardware state machine; v7.1 `serial_core.c` and kfifo UART drivers | `HISTORICAL-2.6.7`, `UPSTREAM-7.1`, `PROJECT-NEW`; modern `uart_fifo_get`, tty flip buffer, and serial-core locking |
| modem-control and lifecycle functions | official `serial98.c` modem polarity, enable registers, and startup/shutdown order | `HISTORICAL-2.6.7`; expressed through v7.1 `uart_ops` |
| `pc9800_8251_set_termios()` | official mode-byte and PIT-divisor algorithm; v7.1 baud/timeout APIs | `HISTORICAL-2.6.7`, `UPSTREAM-7.1`; standard non-FIFO port only |
| console wait/write/setup | official `serial98.c` interrupt preservation and transmitter-empty behavior; v7.1 `uart_console_write`, `uart_set_options` | `HISTORICAL-2.6.7`, `UPSTREAM-7.1`, `PROJECT-NEW`; registered as `ttyPC0` to avoid PC/AT `ttyS` ambiguity |
| parent platform-device registration | v7.1 serial-base requirement that `uart_port.dev` be valid | `UPSTREAM-7.1`; fixes a NULL `__dev_fwnode()` found during QEMU validation |

The new driver carries the historical Osamu Tomita notice and
`Copyright (C) 2026 Awe Morris`.  A normalized scan found no matching block of
three or more code lines with the excluded modern serial implementation.

## Validation attached to this ledger

- i486 and M686 `vmlinux` link with all listed objects built in.
- i486 and M686 mount the test image's ext4 root and reach BusyBox under
  qemu-pc98.
- PC-98 keyboard Enter activates the graphical `tty0` shell.
- `ttyPC0` transmitted a 17,235-byte `dmesg` and received `RX-8251-OK` through
  QEMU file/TCP chardev tests.
- `console=ttyPC0,9600` emitted 16,169 bytes of automatic kernel and BusyBox
  console output.
- `git diff --check` and strict checkpatch of the new serial/PATA files report
  no style errors, warnings, or checks.  The server lacks Python `ply`, so
  checkpatch prints an unrelated `spdxcheck.py` import traceback before its
  normal result for some files.
