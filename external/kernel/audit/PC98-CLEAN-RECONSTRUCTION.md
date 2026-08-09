# PC-98 clean reconstruction for Linux 7.1

## Purpose

This branch reconstructs PC-9800 support while excluding the independently
authored implementation residue in the Hayao0819/linux-pc98 Linux 6.12 patch
set.  The implementation inputs are deliberately limited so that every
PC-98-specific line can be traced to an upstream source, an openly documented
hardware fact, an explicitly permitted project patch, or a new implementation
in this project.

This is a technical provenance record.  It is not legal advice.

## Fixed source baselines

### Modern kernel

- Repository: `https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git`
- Tag: `v7.1`
- Commit: `8cd9520d35a6c38db6567e97dd93b1f11f185dc6`

### Historical PC-9800 implementation

- Repository: `https://git.kernel.org/pub/scm/linux/kernel/git/history/history.git`
- Commit: `b429f3b3c68296611626c926a78f6d5fe3760226`
- Tree: `566f0a0b5d388d0cd6cf9a796378470e7ad602b5`
- `git archive --format=tar` SHA-256:
  `0a6712a4681e30fb222ed22cf826e57c56df30f4fe39189a656b3ffa70a5cc9a`

The historical commit is the parent of the first upstream commit that removed
PC-9800 support.  It is therefore the last coherent PC-9800 implementation in
the official pre-Git Linux history.

## Allowed implementation inputs

1. The two official Linux source baselines above.
2. Public hardware specifications and programming manuals.
3. Publicly available BSD PC-98 implementations when their licence permits
   reuse; copied material must retain its required notice.
4. qemu-pc98 as a hardware-behaviour specification and test target.  QEMU code
   is not copied into the kernel.
5. Independently authored code and test results from this project, provided
   their provenance is recorded before inclusion.
6. The project's complete, real-hardware-tested keyboard driver at commit
   `fb253cfe61de8b72b6bacacb00137f434014f778` is explicitly permitted and is
   copied without functional changes.  Its NetBSD/pc98 and Naofumi HONDA
   notices and BSD conditions are retained, followed by the 2026 Awe Morris
   notice.  The project owner identified its uPD8251 reset
   sequence and recovery polling as NetBSD/pc98- and FreeBSD/pc98-derived.
7. The excluded repository's patch 18, "recover the keyboard from lost
   interrupts and USART errors", is explicitly permitted: the project owner
   authored that recovery change and supplied it to the other repository.

## Prohibited implementation inputs

- The audit first computes the complete difference between official Linux
  v6.12 (`adc218676eef25575469234709c2d87185ca223a`) and the other
  repository's patched 6.12 tree.
- From that difference it subtracts code mechanically attributable to the
  official Linux 2.6.7 PC-98 tree and the explicitly permitted keyboard
  recovery patch.
- The remaining independently authored/LLM-generated implementation is the
  prohibited set.  It must not be copied or used as an implementation source.
- The excluded repository may be inspected only to construct and verify this
  finite prohibited set.  Every non-trivial similarity must be traced to a
  common allowed source, explicit permission, or removed.

## Per-change provenance classes

Every PC-98 patch must name one or more of these classes in its commit message
and in the subsystem ledger below.

- `UPSTREAM-7.1`: adaptation of an API or pattern already in official v7.1.
- `HISTORICAL-2.6.7`: machine fact or algorithm from the official historical
  PC-9800 implementation.
- `SPEC`: implementation from a cited public hardware specification.
- `BSD`: permitted reuse from a specifically identified BSD source.
- `PROJECT-NEW`: newly designed and implemented in this project.

Substantial `PROJECT-NEW` source files carry the notice
`Copyright (C) 2026 Awe Morris`.  A substantial PC-98-specific block added to
an existing upstream file instead carries the source note
`Added in the Awe Morris's port.`.  Existing upstream and historical notices
are retained; short integration-only hunks do not claim a new file-wide
copyright.

## Subsystem ledger

| Subsystem | Historical source | Modern API source | Additional source | Status |
|---|---|---|---|---|
| Platform selection | `arch/i386/Kconfig`, `mach-pc9800` | `arch/x86/Kconfig` | none | implemented; builds and boots |
| BIOS work area | `include/asm-i386/pc9800*.h` | x86 early memory access | PC-9800 manuals | partial: PIT family and reset |
| Standard I/O resources | `mach-pc9800/std_resources.c` (Osamu Tomita) | v7.1 resource API | keyboard owns 0x41/0x43 | implemented; fixes PC/AT timer0 collision |
| 8259 PIC | `arch/i386/kernel/i8259.c`, `io_ports.h` | v7.1 i8259 | 8259 specification | implemented; QEMU IRQ boot verified |
| 8253 PIT | `mach_timer.h`, `timex.h` | v7.1 i8253 | PC-9800 manuals | implemented; QEMU timer boot verified |
| RTC/reset | `mach_time.h`, `mach_reboot.h` | v7.1 x86 platform hooks | uPD4990A specification | implemented; QEMU boot verified, RTC value test pending |
| Text console | historical GDC paths | v7.1 `consw` | uPD7220/PC-9800 manuals; project-owner CSRW code | 80x25 console and cursor positioning implemented; QEMU boot verified |
| Keyboard | `98kbd.c`, `98kbd-io.c` | v7.1 input subsystem | exact project commit `fb253cfe`; BSD behavior; owner-supplied recovery patch | implemented; QEMU input verified, hardware retest pending |
| Serial | `serial98.c` | v7.1 `serial_core`, kfifo, tty-port, console, and platform-device APIs | uPD8251 specification | standard non-FIFO onboard TTY and console implemented; QEMU TX/RX/console verified; FIFO/V.Fast and hardware tests pending |
| IDE | `hd98.c`, `pc9800.c` | v7.1 libata/block APIs | ATA specification | PIO frontend and bank-select port 0x432 implemented; QEMU root mount verified |
| NEC98 partitions | `fs/partitions/nec98.c` | v7.1 partition parser APIs | PC-98 partition format | implemented for supplied H=8/S=17 geometry; QEMU verified |
| Low-memory support | official historical x86 | v7.1 memory manager | project tests | pending |
| i486/i386 restoration | Linux v3.4/v3.7 i386 behavior; official v6.14 i486 support | v7.1 x86 | Intel architecture manuals; project-new Linux 7.1 integration | i386, i486, and i686 build; true i386 boots to BusyBox |

## Validation gates

1. `git diff --check` is clean and kernel `checkpatch.pl` has no unexplained
   issue (the historical `Sector` type and syscall-stub `-ENOSYS` diagnostics
   are recorded exceptions).
2. The kernel builds from a fresh official v7.1 checkout by replaying the
   documented patch series.
3. QEMU boots are verified independently for i386, i486, and i686 targets.
4. Hardware tests are repeated for the available PC-9800 systems.
5. A final similarity report classifies every non-trivial match with the
   excluded repository as common upstream, historical upstream, unavoidable
   interface syntax, or a defect to remove.

## Recorded validation

- Linux 7.1 `vmlinux` links successfully with all currently implemented
  PC-9800 objects built in.
- QEMU `-M pc9801 -cpu pentium2,-apic` reaches the BusyBox shell from the
  project's H=8 disk image using the clean libata frontend and NEC98 parser.
- The same run receives Enter through the clean keyboard driver and activates
  the interactive `tty0` shell.
- QEMU `-M pc9801 -cpu 486` boots the i486 kernel, mounts the ext4 root, and
  enters the BusyBox shell after the PC-98 resource table replaces the PC/AT
  `timer0` reservation at 0x40-0x43.
- The M686 configuration rebuilds and passes the same ext4-root and keyboard
  console test.
- The final M386 configuration boots under QEMU `-M pc9801 -cpu 386`, reports
  CPU family 3, registers the PC-98 text console once without a duplicate
  `vtcon0`, initializes `pata_pc9800`, mounts ext4 `sda2` read/write, starts
  BusyBox, accepts Enter, and executes `dmesg` from the interactive shell.
- The standard onboard uPD8251 registers as `ttyPC0`.  Under QEMU, redirecting
  `dmesg` to the port transmitted 17,235 bytes through a file chardev, and a
  TCP chardev receive test delivered `RX-8251-OK` through the receive IRQ and
  tty flip buffer to a guest `cat /dev/ttyPC0` process.
- The uPD8251 validation exposed and fixed a Linux 7.1 integration requirement:
  every `uart_port` needs a non-NULL parent device before
  `uart_add_one_port()`.  The fixed onboard port now has a platform device as
  its serial-core parent.
- With `console=ttyPC0,9600`, the same QEMU uPD8251 emitted 16,169 bytes of
  automatic kernel and BusyBox console output.  The normal test configuration
  was then restored to the public-image default `console=tty0`.
