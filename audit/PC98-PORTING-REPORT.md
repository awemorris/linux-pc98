# Linux 7.1 PC-98 clean port: implementation and provenance report

## 1. Purpose and policy

This report records where every substantial module and function group in the
clean Linux 7.1 PC-98 port came from.  It distinguishes historical Linux code,
BSD-derived hardware quirks, current upstream API patterns, and implementation
written specifically for the Awe Morris port.

The source baselines are:

| Role | Revision |
|---|---|
| Vanilla target | Linux v7.1, commit `8cd9520d35a6c38db6567e97dd93b1f11f185dc6` |
| Last official Linux PC-98 source | commit `b429f3b3c68296611626c926a78f6d5fe3760226`, tree `566f0a0b5d388d0cd6cf9a796378470e7ad602b5` |
| i386 historical specification reference | Linux v3.7 |
| i386 older implementation reference | Linux v3.4 and the pre-removal i386 code discussed in `i386-port-plan-a.md` |
| NetBSD keyboard reference | NetBSD/pc98 `sys/arch/i386/isa/vsc/kbd.c`, NecBSD revision shown in that file |
| Permitted project keyboard | `awemorris/linux-pc98` commit `fb253cfe61de8b72b6bacacb00137f434014f778` |

Copyright policy for this reconstruction is:

1. Preserve the notice or author credit from the official Linux 2.6.7 PC-98
   source whenever code or a substantial algorithm is carried forward.
2. Preserve applicable BSD notices when BSD implementation is adapted rather
   than merely consulting a public hardware fact.
3. For a new file or a substantially divergent Linux 7.1 implementation,
   retain the historical notice first and add
   `Copyright (C) 2026 Awe Morris` below it.
4. For a substantial addition inside an existing upstream file, mark the
   block `Added in the Awe Morris's port.` instead of claiming the entire
   upstream file.
5. Small Kconfig, Makefile, declaration, and call-site hooks are documented
   here, but do not receive a file-wide project copyright claim.

`PC98-CODE-PROVENANCE-AUDIT.md` records the negative audit against the excluded
Linux 6.12 implementation.  That tree is an audit target, not an implementation
source.

## 2. PC-98 platform core

| Target module/function group | Implementation source | Linux 7.1 adaptation | Notice retained |
|---|---|---|---|
| `arch/x86/kernel/pc9800.c`: BIOS work-area constants and `pc9800_init_platform()` | Official 2.6.7 `include/asm-i386/pc9800.h`, `pc9800_sca.h`, and `arch/i386/mach-pc9800/setup.c` | New aggregation using `x86_platform`, `x86_init`, and `machine_ops` | Historical Osamu Tomita credit plus Awe Morris |
| `pc9800_standard_io_resources[]`, `pc9800_reserve_standard_io_resources()` | Official 2.6.7 `arch/i386/mach-pc9800/std_resources.c` | Resource table translated to current `struct resource`; keyboard ports deliberately left to the input driver | `Written by Osamu Tomita` plus Awe Morris |
| `pc98_rtc_delay()`, `pc98_rtc_output_data_clock()`, `pc98_rtc_output_data()`, `pc98_rtc_command()`, raw read/write helpers, wall-clock callbacks | Official 2.6.7 `mach-pc9800/mach_time.h` and the uPD4990A protocol used there | New `timespec64` and `x86_platform` callback integration | Osamu Tomita credit plus Awe Morris |
| `pc9800_restart()`, `pc9800_emergency_restart()` | Official 2.6.7 `mach-pc9800/mach_reboot.h` | Connected to current restart hooks | Osamu Tomita credit plus Awe Morris |
| PIC port definitions and PC-98 branches in `i8259.h`, `i8259.c`, `irqinit.c` | Official 2.6.7 PC-98 PIC code and `mach-pc9800/io_ports.h` | Minimal branches in the current i8259 implementation; cascade IRQ 7 preserved | Existing upstream file; ledger attribution only |
| PIT ports/rate in `i8253.h`, `timex.h`, and `kaslr.c` | Official 2.6.7 `mach_timer.h`, `timex.h`, and BIOS work-area family flag | Current PIT and KASLR call sites use PC-98 ports 0x71/0x77 and runtime 1.9968/2.4576 MHz selection | Existing upstream file; ledger attribution only |
| `platform-quirks.c` PC-98 legacy-device selection | Official PC-98 machine topology plus Linux 7.1 platform API | New short integration hunk disabling PC/AT i8042/RTC/PNP assumptions | Project-new integration; ledger attribution only |
| `setup.c`, `Kconfig`, Makefiles, public declarations | Official 2.6.7 machine selection plus Linux 7.1 build/setup structure | Short current-API hooks | Project-new integration; ledger attribution only |

## 3. Text console and early output

| Target function group | Implementation source | Classification |
|---|---|---|
| `early_pc9800_scroll()`, `early_pc9800_write()`, early console registration | Text/attribute plane layout from official 2.6.7 `arch/i386/boot98/compressed/misc.c`; registration model from Linux 7.1 | Independently written bounded Linux 7.1 early console; the block in `early_printk.c` is marked as added by the Awe Morris port |
| `pc98con_init()`, `deinit()`, `attribute()`, `putcs()`, `clear()`, `scroll()` | Official PC-98 VRAM layout and public GDC behavior; Linux 7.1 `consw` API | New Linux 7.1 implementation, not copied from the excluded modern console |
| `pc98con_cursor()` | Cursor-position routine supplied by Awe Morris from an independently developed kernel | Project-owned adaptation: wait for FIFO, issue CSRW 0x49, write low/high address; CSRFORM remains outside this function |
| `pc98con_switch()`, `blank()`, `build_attr()`, `invert_region()`, `pc98_con` | Linux 7.1 console-cell and `consw` contracts | Project-new implementation |
| `pc98con_register_screen()`, `pc98_console_init()`, `setup_arch()` hook | Linux 7.1 console selection/initialization pattern | Project-new integration selecting the GDC console early and avoiding a PC/AT vgacon handoff |

The new `pc98con.c` carries `Copyright (C) 2026 Awe Morris`.  No 2.6.7
module-level notice existed for this new `consw` implementation; historical
layout sources remain identified in its header and in this report.

## 4. Keyboard

| Target function group | Implementation source | Classification and notice |
|---|---|---|
| `pc98kbd_ctl_write()`, `pc98kbd_hw_init()` | NetBSD/pc98 `kbd_reset()`, including the three zero writes, internal reset, mode/command bytes, and 5 ms/50 ms settling delays | BSD-derived hardware quirk. NetBSD/pc98 porting staff and Naofumi HONDA notices and the BSD conditions are retained in the source |
| `pc98kbd_keycode[]`, input-device setup, make/break reporting | Permitted working project file at `fb253cfe...`; Linux input API | Project-owned working mapping/integration; Awe Morris notice retained |
| `pc98kbd_release_all()`, error recovery in `pc98kbd_drain()` | Owner-supplied recovery patch and working project file | Explicitly permitted project implementation |
| `pc98kbd_poll()` | BSD PC-98 lost-interrupt recovery behavior and the permitted project file | Behavior is documented as BSD-derived; Linux timer/input expression is in the owner-permitted file |
| IRQ/init/exit plumbing | Permitted project file and Linux 7.1 input/IRQ APIs | Project-owned current API integration |

The keyboard is intentionally not an IC-specification rewrite.  Real hardware
requires undocumented behavior captured by the BSD-derived sequence.  This is
also why its origin is recorded more strictly than a normal register-level
driver.

## 5. Storage

| Target function group | Implementation source | Linux 7.1 adaptation | Notice retained |
|---|---|---|---|
| `pata_pc9800.c` task-file/control ports, two-byte spacing, IRQ and port 0x432 bank selection | Official 2.6.7 `drivers/ide/legacy/pc9800.c` | Hardware frontend is new; transfer, error handling, and LBA/CHS policy are delegated to Linux 7.1 libata | Linux/98 project and Kyoto University Microcomputer Club 1997-2000, then Awe Morris |
| `pc98_pata_bios_param()` | Official PC-98 logical geometry requirement plus current SCSI-host callback contract | New callback expression for current libata | Same module notices |
| `pc98_pata_probe()`, platform device/driver registration | Linux 7.1 `pata_platform.c` and platform APIs | Independently written small frontend | Same module notices |
| `nec98_table_valid()`, `nec98_partition()` | Official 2.6.7 `fs/partitions/nec98.c` | Parser translated to Linux 7.1 `parsed_partitions`, sector accessor, and bounds APIs | Kyoto University Microcomputer Club 1999, then Awe Morris |
| partition registration in `block/partitions/*` | Linux 7.1 partition parser registry | Short integration hooks | Existing upstream files; ledger attribution only |
| `pc980155.c`, `pc980155.h`: board detection/configuration, interrupt gate, bus reset and ISA DMA callbacks | Official Linux/98 2.6.7 `drivers/scsi/pc980155.c` and `.h` by Tomoharu Ugawa and Kyoto University Microcomputer Club | Direct port to current `scsi_host_alloc()`, IRQ and error-handler APIs; the removed `unchecked_isa_dma` facility is replaced by a 64 KiB low-memory bounce buffer, including short-transfer residual handling | Original 1997-2003 Linux/98 notices retained; `Copyright (C) 2026 Awe Morris` added for the Linux 7.1 adaptation |
| `wd33c93.c`: `CONFIG_WD33C93_PIO` register accessor path | Official Linux/98 2.6.7 WD33C93 core I/O-port accessors | Restored behind a dedicated Kconfig selection so the ordinary memory-mapped upstream path remains unchanged | Existing upstream notices remain; provenance recorded here |
| PC-9801-55/92 profiles, per-disk boot geometry, Kconfig and Makefile hooks | Project boot protocol and Linux 7.1 build/SCSI interfaces | Adds `pc9801_scsi=55/92,irq=,dma=,clock=` selection and 55 H=8/S=17 versus 92 H=8/S=32 fallback without replacing the historical controller state machine | `Copyright (C) 2026 Awe Morris` in the ported driver; short build hooks attributed in this ledger |

## 6. Standard onboard uPD8251 serial port

| Target function group | Implementation source | Linux 7.1 adaptation |
|---|---|---|
| register/command definitions, `pc9800_8251_command()`, `pc9800_8251_set_mode()` | Official 2.6.7 `drivers/serial/serial98.c` and uPD8251 hardware behavior | Names and types adapted to the new driver |
| TX/RX and IRQ functions | Official `serial98.c` hardware state machine | New serial-core/kfifo/tty-flip-buffer integration |
| modem-control and startup/shutdown functions | Official `serial98.c` polarity, enable registers, and operation order | Expressed through Linux 7.1 `uart_ops` |
| `pc9800_8251_set_termios()` | Official mode-byte and PIT-divisor algorithm | Current baud and timeout APIs; standard non-FIFO onboard port only |
| console wait/write/setup | Official transmitter-empty and interrupt-preservation behavior; Linux 7.1 console helpers | New `ttyPC0` console integration |
| platform parent registration | Linux 7.1 serial-core requirement for a valid `uart_port.dev` | Independent compatibility fix found during QEMU testing |

The source retains the original `Copyright (C) 2002 Osamu Tomita`, the
historical “Based on” credits for Russell King, Linus Torvalds, and Theodore
Ts'o, followed by `Copyright (C) 2026 Awe Morris` for the divergent current
implementation.

## 7. Genuine i386 support

The i386 implementation was introduced in the owner-authored project commits
`787c10955` and `538c7e9c3`.  Linux 3.4/3.7 is used as a behavioral and design
reference, not as a drop-in patch.  Current interfaces and most implementation
expression are new to the Awe Morris Linux 7.1 port.

| Target file/function group | Historical or current reference | Actual change origin |
|---|---|---|
| `Kconfig.cpu`, `Kconfig`, `Makefile_32.cpu`: `M386` and related capability selections | Linux v3.7 M386 model; Linux 7.1 Kconfig | New current-Kconfig translation; substantial block marked as added in the Awe Morris port |
| `head_32.S`: `EARLY_CR0_STATE`, AC-bit 386/486 detection, 386 CR0 selection | Linux v3.4/v3.7 early i386 detection and architectural EFLAGS/CR0 behavior | Reimplemented in the Linux 7.1 startup sequence; block marked as added in the Awe Morris port |
| `cmpxchg_386()`, `xadd_386()` | Pre-removal Linux i386 IRQ-exclusion semantics; current `atomic64_386_32.S` as the in-tree IF-save precedent | New out-of-line UP-only implementation; Awe Morris copyright |
| `cmpxchg.h` M386 routing and try-cmpxchg/xadd wrappers | Linux 7.1 primitive contracts | New current-API integration; marked as added in the Awe Morris port |
| `atomic.h` M386 `arch_atomic_*` implementation | Linux v3.x UP intent; Linux 7.1 generated atomic API | New IRQ-save implementation for current API; marked as added in the Awe Morris port |
| `local.h`, `percpu.h` M386 fallbacks | Linux 7.1 asm-generic UP contracts and current x86 macros | Project-new current-API fallbacks; substantial per-CPU block marked as added |
| `atomic64_386_32.S` | Existing vanilla Linux 7.1 386/486 implementation | Reused unchanged; no project claim |
| `tlb.h:invlpg()` | Linux v3.x i386 full-TLB-flush behavior and Intel 386 architecture | New Linux 7.1 conditional CR3 reload |
| `swab.h` | Absence of BSWAP on 386; Linux UAPI portable fallback | Project-new target-ISA gate so userspace headers do not depend on kernel config |
| `usercopy_32.c:__copy_to_user_386()`, `clear_user_386()` | Linux 3.6 broken-WP slow-path requirement; current GUP/kmap/dirty-page APIs | New Linux 7.1 implementation; substantial block marked as added in the Awe Morris port |
| `uaccess.h`, `uaccess_32.h` M386 routes | Linux 7.1 put/copy/unsafe-put contracts | New routing to the software-validated write path; substantial block marked as added |
| `init_32.c:test_wp_bit()` | Linux v3.x `X86_WP_WORKS_OK` semantics and Linux 7.1 test | Current test cleanup plus M386 continuation only after the software write path is linked |
| `i386_user_atomic_op_inuser()` and `sys_i386_atomic` ABI | Modern glibc/NPTL need for atomic userspace state changes; no adequate old-Linux ABI donor | Independent project design and implementation; Awe Morris copyright |
| `futex.h` M386 operations | Linux v3.7 conservative futex behavior as a risk reference; new i386 atomic service | New Linux 7.1 bridge to the project atomic service; marked as added |
| `hw_breakpoint_386.c`, non-perf ptrace path | Linux 7.1 architectural DR6/DR7 contracts | New minimal non-perf implementation for the M386 research configuration; Awe Morris copyright |
| `perf_event.h`, `cpu_entry_area.c` minimal-build fixes | Linux 7.1 type and configuration contracts | Independent compile-configuration corrections; no historical PC-98 donor |
| `common.c` M386 admission and CR pinning selection | Linux v3.x CPU admission and Linux 7.1 hardening | New conditional integration; does not claim that CR0.WP works on 386 |
| `vermagic.h` processor labels | Historical Linux processor-family labels and Linux 7.1 module ABI string | New restoration of missing family labels |
| small include fixes in `alternative.c`, `process_32.c`, `fault.c` | Linux 7.1 declarations | Independent build fixes, not PC-98 algorithms |

New substantive i386 files carry `Copyright (C) 2026 Awe Morris`.  Substantial
M386 blocks in existing upstream hot headers carry the local block marker.
Short selectors and declarations are attributed in this report without adding
a misleading file-wide copyright claim.

## 8. Excluded implementation audit

The audited modern Linux 6.12 PC-98 delta added 1,606 lines.  A conservative
candidate set of 1,000 lines was isolated after separating obvious historical
backports.  The clean reconstruction was compared against added blocks only.
No unexplained matching block of three or more normalized code lines was found
outside the explicitly permitted keyboard and ordinary historical/API facts.

The clean PATA and serial frontends are deliberately structured differently
from the excluded implementations.  The audit procedure and per-patch result
are in `PC98-CODE-PROVENANCE-AUDIT.md`.

## 9. Validation status

At the time this report was prepared:

- Final i386, i486, and i686 configurations all produce 32-bit linked
  `vmlinux` images from the same clean tree.
- The final i386 kernel boots under qemu-pc98 with `-cpu 386`, identifies a
  family-3 CPU, registers the PC-98 text console exactly once, initializes
  `pata_pc9800`, mounts the ext4 `sda2` root read/write, starts BusyBox, accepts
  keyboard Enter, and runs a command from the interactive shell.
- The current i486 and M686 configurations link successfully.  Earlier boot
  validation of the same clean platform/device implementation reached BusyBox
  on both CPU configurations.
- `ttyPC0` transmitted a 17,235-byte `dmesg`, received `RX-8251-OK`, and
  emitted an automatic serial-console boot log.
- The PC-9801-92 driver enumerated the qemu-pc98 SCSI disk, parsed both NEC98
  partitions, mounted the SCSI `sda2` ext4 filesystem as root, reached the
  BusyBox shell, and completed traced READ(10) and WRITE(10) commands with
  zero target status and residual.  An IDE-only boot also reached the shell
  with the driver built in and no PC-9801-92 device present.
- `git diff --check` is clean.
- Strict checkpatch reports no issues for the platform, PATA, GDC console,
  keyboard, serial, i386 cmpxchg, and minimal debug-register files.  The NEC98
  parser has only the existing kernel `Sector` type CamelCase check.  The
  disabled-configuration syscall stub has the intentional `-ENOSYS` warning.

## 10. Single vanilla-delta patch

The deliverable is one patch against the exact vanilla Linux v7.1 commit
listed in section 1.  It includes tracked modifications and all new source
files, but does not include itself.  It is generated with a temporary Git
index so the review worktree does not need to be committed or staged:

```sh
index=$(mktemp)
GIT_INDEX_FILE="$index" git read-tree HEAD
GIT_INDEX_FILE="$index" git add -A
GIT_INDEX_FILE="$index" git diff --cached --binary HEAD \
    > /tmp/linux-7.1-pc98-clean.patch
rm -f "$index"
```

The generated repository artifact is named `linux-7.1-pc98-clean.patch`.
Its byte size and SHA-256 digest are reported alongside the review handoff so
the report itself remains part of the hashed patch content without creating a
self-referential digest.
