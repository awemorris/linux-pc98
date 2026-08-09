# Linux 6.12 PC-98 provenance audit

## Scope

This audit isolates the implementation that must not be reused from
`Hayao0819/linux-pc98`.  It does not treat all PC-98 code in that repository
as prohibited.  The prohibited set is only the part of its Linux 6.12 delta
which cannot be explained by the last official Linux PC-98 implementation or
by an explicit permission from this project's owner.

This document records a technical source-code provenance audit, not legal
advice.

## Reproducible inputs

| Input | Revision |
|---|---|
| Official Linux 6.12 | tag `v6.12`, commit `adc218676eef25575469234709c2d87185ca223a` |
| Audited repository | `Hayao0819/linux-pc98`, commit `1a135c017979090c913b1e6e7484f305b93cd9fc` |
| Historical official PC-98 | commit `b429f3b3c68296611626c926a78f6d5fe3760226`, tree `566f0a0b5d388d0cd6cf9a796378470e7ad602b5` |
| Current clean target | official Linux v7.1, commit `8cd9520d35a6c38db6567e97dd93b1f11f185dc6` |
| Permitted keyboard implementation | `awemorris/linux-pc98` commit `fb253cfe61de8b72b6bacacb00137f434014f778` |

The audited 18-patch series applies cleanly to official v6.12 and changes 30
files, adding about 1,600 lines.  The reconstructed audit tree is kept outside
the clean source tree at `/home/awe/work/linux-v6.12-hayao-audit`.

### Size of the independently implemented candidate set

The complete final delta is 1,606 added lines and 7 deleted lines.  At patch
granularity, the changes not immediately explained as a mechanical 2.6.7
backport total exactly 1,000 added lines:

| Area | Added lines |
|---|---:|
| Early PC-98 console | 64 |
| Modern uPD8251 serial/console driver | 376 |
| Low-1-MiB memory workaround | 14 |
| Modern GDC `consw` implementation | 213 |
| Initial modern keyboard implementation | 170 |
| Modern libata frontend | 103 |
| GDC hardware cursor | 56 |
| CPUID-less microcode workaround | 4 |
| **Total candidate set** | **1,000** |

This is a conservative upper bound, not a claim that every one of those lines
is independently copyrightable.  It still contains hardware constants, BSD
or old-Linux initialization facts, and modern API boilerplate.  After those
are removed, the substantively distinctive implementation residue is
estimated at roughly 800-900 lines.  The owner-supplied keyboard recovery
patch (+164/-25 at patch-series granularity) is excluded from this 1,000-line
candidate set.

## Subtraction procedure

1. Apply the audited repository's 18 patches to the exact official v6.12
   commit.
2. Enumerate the resulting v6.12 delta without consulting the clean target.
3. For each hunk, identify an exact historical official file, a modern
   upstream API pattern, a public hardware fact, or explicit owner permission.
4. Mark only the unexplained implementation expression as prohibited.
5. Compare the clean v7.1 implementation against that residue.  Short API
   declarations and hardware constants are not accepted merely because they
   are inevitable; their allowed source is recorded as well.

## Patch classification

| Patch | Subject (abbreviated) | Historical/allowed basis | Classification and clean-tree action |
|---:|---|---|---|
| 01 | platform Kconfig | official 2.6.7 `arch/i386/Kconfig`; v7.1 Kconfig syntax | Interface plumbing; independently expressed |
| 02 | early text console | official `boot98/compressed/misc.c` text/attribute planes | Other implementation expression prohibited; clean writer and scroll code independently implemented |
| 03 | boot decompressor GNU mode | generic compiler workaround, not PC-98 | Not present in clean tree |
| 04 | PC-98 8259 ports | official `i8259.c` and `mach-pc9800/io_ports.h` | Historical backport; allowed constants/algorithm |
| 05 | PC-98 8253 ports/rate | official `mach_timer.h` and `timex.h` | Historical backport; allowed |
| 06 | uPD8251 serial | official `serial98.c` supplies the hardware state machine, registers, delays, divisor calculation, and console interrupt preservation; v7.1 supplies serial-core/console patterns | Audited modern expression prohibited; clean `pc9800_8251.c` was newly integrated from the two allowed upstream generations and supports the standard non-FIFO TTY and console first |
| 07 | preserve low 1 MiB | no demonstrated PC-98 historical counterpart for this modern MM path | Not present in clean tree |
| 08 | GDC `consw` text console | display facts are historical; modern `consw` body is new | Audited implementation expression prohibited; clean `consw` independently written and intentionally omits cursor programming |
| 09 | initial keyboard driver | official `98kbd.c`/`98kbd-io.c`, BSD PC-98 behavior | Clean tree uses the explicitly permitted project driver, not a reconstruction from this patch |
| 10 | PIC cascade line | official PC-98 i8259 cascade IRQ 7 | Historical backport; allowed |
| 11 | libata PATA frontend | ports/bank behavior from official `drivers/ide/legacy/pc9800.c`; libata API from upstream | Audited wrapper expression prohibited; clean wrapper independently built from those two allowed sources |
| 12 | reset port 0xf0 | official `mach_reboot.h` | Historical backport; allowed |
| 13 | GDC hardware cursor | project owner's independent kernel supplies the CSRW position sequence | Audited implementation expression prohibited; clean console implements position only and leaves CSRFORM/show-hide to firmware |
| 14 | uPD4990A RTC | official `mach_time.h`; uPD4990A protocol | Historical algorithm adapted to v7.1 platform hooks |
| 15 | PIT family selection | official BIOS work-area flag and timer code | Historical backport; allowed |
| 16 | CPUID-less microcode guard | modern workaround, not shown to derive from 2.6.7 PC-98 | Not present in clean tree |
| 17 | NEC98 partitions | official `fs/partitions/nec98.c` | Historical parser adapted to the modern partition API |
| 18 | keyboard lost-IRQ/USART recovery | authored by this project's owner and supplied to the audited repository; BSD behavior | Explicitly permitted; retained in the exact working keyboard driver |

## Keyboard-specific record

The clean tree's `drivers/input/keyboard/pc98kbd.c` implementation is
functionally identical to the working project file at commit
`fb253cfe61de8b72b6bacacb00137f434014f778`.  Its header retains the applicable
NetBSD/pc98 and Naofumi HONDA notices and BSD conditions for the adapted reset
sequence, followed by `Copyright (C) 2026 Awe Morris` for the current Linux
driver.  Re-expressing it from the IC specification produced a driver that did
not work on real hardware and is not an acceptable substitute for the
BSD-derived undocumented quirks.

The following provenance is asserted by the project owner and supported by
the available source:

- The uPD8251 reset sequence (three zero writes, reset/mode/command sequence,
  5 ms and 50 ms waits) appears in NetBSD/pc98
  `sys/arch/i386/isa/vsc/kbd.c`.
- Lost-interrupt recovery polling follows the BSD PC-98 behavior.
- Patch 18 in the audited repository was supplied by this project's owner and
  may be used here.

## Current findings

- A direct bug was found outside the audited 6.12 delta: official v7.1's PC/AT
  standard resource table reserves `timer0` at I/O 0x40-0x43, colliding with
  the PC-98 keyboard at 0x41/0x43.
- The replacement PC-98 resource list is adapted from official 2.6.7
  `arch/i386/mach-pc9800/std_resources.c` (Osamu Tomita).  Keyboard ports are
  omitted because the modern input driver requests them itself.
- With that fix, the exact working keyboard driver registers and QEMU HMP
  `sendkey ret` activates the BusyBox console on `-M pc9801 -cpu 486`.
- The M686 configuration also rebuilds, mounts the ext4 root, and accepts
  Enter at the BusyBox console under QEMU.

## Exact-added-block scan

An automated scan compared only lines added by the audited v6.12 patch set
against the clean v7.1 tree.  Unchanged upstream context was excluded.  It
reported every contiguous match of at least three normalized code lines.

The non-keyboard matches fall into these explained classes:

- Historical hardware constants: PC-98 PIC ports, PIT ports/rate hook, and
  their surrounding preprocessor branches.
- Modern kernel interface boilerplate: Kconfig entries, the `consw` member
  initializer, the partition parser function signature, and the early-console
  `struct console` initializer.
- Ordinary shared includes and braces.

The independently implemented PATA frontend had no exact match of three code
lines.  `pc9800.c` had only its three common x86 include directives.  The
NEC98 parser had only its modern entry-point declaration.  The PC-98-specific
early console body had no unexplained exact block.

Because `early_printk.c` is an existing upstream file, its substantial PC-98
block is marked `Added in the Awe Morris's port.` rather than carrying a new
file-wide copyright notice.

The keyboard produced many long exact blocks.  All are covered by the explicit
permission for the byte-identical project driver and the owner-supplied patch
18; they are not being claimed as an independent rewrite.

A separate normalized comparison pairs the differently named excluded
`pc98_8251.c` and clean `pc9800_8251.c` files.  It found no matching block of
three or more code lines.  A comparison against official 2.6.7 `serial98.c`
found only one generic three-line closing/return block because the modern
serial-core integration necessarily has a different structure.  The clean
driver's hardware behavior is nevertheless deliberately traced to that
official implementation; its Linux 7.1 integration is project-new.

No unexplained prohibited block of three or more normalized added code lines
was found in this pass.

## Project-owned GDC cursor source

The clean console's cursor-position implementation is adapted from source
supplied directly by the project owner from their independently developed
kernel.  It waits for text-GDC FIFO-full bit 1 at port 0x60 to clear, sends
CSRW command 0x49 to port 0x62, and writes the low and high cursor-address
bytes to port 0x60.  It deliberately does not copy the audited repository's
three-parameter helper or CSRFORM implementation.  Cursor form and visibility
remain under firmware control until independently sourced show/hide behavior
is added.

## Remaining audit gates

- Keep `PC98-HUNK-PROVENANCE.md` current as implementation hunks change.
- Check all exact or high-similarity matches against the historical official
  source and modern upstream API exemplars.
- Run `git diff --check`, relevant kernel builds, i486/i686 QEMU boot tests,
  and real-hardware keyboard regression testing before merge.
