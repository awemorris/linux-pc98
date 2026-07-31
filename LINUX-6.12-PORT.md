# Linux 6.12 PC-98 Port: Origin and Reconstruction Notes

## Purpose and status

`linux-6.12/` is the first modern PC-98 kernel in this repository and is the
source from which the Linux 7.0 and 7.1 ports were derived. This document
records:

1. the exact historical Linux/PC-98 source used as architectural reference;
2. why the old implementation could not be applied as a normal patch;
3. how each old subsystem maps to the Linux 6.12 implementation;
4. the actual reconstruction sequence and later fixes;
5. what was validated, what remains intentionally unported, and where the
   current design still needs improvement.

This is a reconstruction record, not a claim that every Linux 2.6.7 PC-98
feature was restored.

## Source baselines

### Modern baseline

`linux-6.12/` is based on official Linux v6.12:

- Commit: `adc218676eef25575469234709c2d87185ca223a`
- Tree: `ac4266ccaf1cf79e8fb22ad3e0d86deac358ffb9`
- Release date: 2024-11-17

Relative to that commit, the current PC-98 tree changes 34 files with 3,028
insertions and 7 deletions. The complete reproducible delta is
`patches/linux-6.12-pc98/current-complete.patch`.

### Historical reference

`linux-2.6.7-pc98-original/` is a source snapshot from the official Linux
history repository:

- Commit: `b429f3b3c68296611626c926a78f6d5fe3760226`
- Tree: `566f0a0b5d388d0cd6cf9a796378470e7ad602b5`
- Description: `v2.6.7-33-gb429f3b3c6`
- Date: 2004-06-17
- Archive SHA-256:
  `0a6712a4681e30fb222ed22cf826e57c56df30f4fe39189a656b3ffa70a5cc9a`

It is the parent of
`5e018f7e60c98df93ff39246c3132dbc985aae8e`, the first upstream commit
named `[PATCH] Remove PC9800 support`. The following removal commits first
disconnected Kconfig and Makefiles, then deleted 14,234 lines in 46 orphaned
platform and driver files. The separate `boot98` decompressor disappeared
last in `df13449018c3ae8119cf1daae1fffda5b47231f3` on 2004-07-28.

This point in history was selected deliberately: it is a coherent upstream
tree immediately before removal, not a third-party tarball of uncertain
revision and not a partially removed intermediate state. Its license is
preserved in `linux-2.6.7-pc98-original/COPYING`; exact reproduction details
are in `linux-2.6.7-pc98-original/SOURCE-PROVENANCE.md`.

The still earlier Linux/98 project ported Linux 2.1.57 and later 2.2/2.3
kernels. Its work established the PC-9800 machine differences and eventually
fed the early-2.6 upstream subarchitecture. It remains useful historical
context, while the checked-in 2.6.7 snapshot is the practical code reference.

## Why this is a reconstruction, not a cherry-pick

Twenty years of x86 and driver evolution removed nearly every API boundary
used by the old port:

- `arch/i386` became `arch/x86`, and the old `mach-pc9800` compile-time
  subarchitecture framework disappeared.
- The separate `arch/i386/boot98` build and setup assembly no longer fit the
  modern x86 boot protocol, decompressor, or Kbuild layout.
- Legacy IDE was removed; modern kernels use libata and the SCSI midlayer.
- The input/serio, UART, console, timekeeping, IRQ, resource, and framebuffer
  APIs all changed.
- The old port relied heavily on BIOS work-area flags and compile-time header
  replacement. Linux 6.12 expects platform hooks and subsystem-owned device
  state.
- Some old drivers were broad hardware collections. The present project
  initially needed only the devices implemented by qemu-pc98 and available on
  the target physical machines.

The old tree was therefore used to recover machine facts—ports, interrupt
wiring, clock rates, register sequencing, BIOS-work-area meanings, and device
identity—while the Linux 6.12 code was written against current subsystem
interfaces. NetBSD/pc98, FreeBSD/pc98, qemu-pc98, and the Suika3 hardware
drivers were also consulted where the old Linux code was incomplete or real
hardware exposed timing problems.

## Architecture mapping

| Area | Linux 2.6.7 implementation | Linux 6.12 implementation | Result |
| --- | --- | --- | --- |
| Platform selection | `CONFIG_X86_PC9800`, `arch/i386/mach-pc9800`, machine-specific header substitution | `CONFIG_X86_PC9800` under `X86_EXTENDED_PLATFORM`; PC-98-only 32-bit, UP kernel | Recreated with a smaller compile-time platform surface |
| Boot | Separate `arch/i386/boot98` boot sector, setup, video, and decompressor | Repository IPL/PBR/FAT16 loader supplies the kernel; standard x86 bzImage layout; `earlyprintk=pc9800` writes text VRAM | Replaced rather than ported |
| Early machine setup | `setup_arch_pre/post` headers and BIOS work-area macros | `pc9800_init_platform()` called from `setup_arch()` plus explicit x86 platform hooks | Rewritten |
| Memory | Old BIOS-specific setup and reservations | Loader-provided map plus a guard that prevents the low 1 MiB from being freed by the top-down PMD probe | Minimal required behavior restored |
| PIC | PC-98 8259 ports and slave cascade on master IR7 | Conditional PIC register definitions; `native_init_IRQ()` requests `PIC_CASCADE_IR` rather than hard-coded IRQ2 | Restored and adapted |
| PIT | Machine header selected counters `0x71/0x73/0x75`, control `0x77`, and machine clock | Conditional i8253 ports; run-time 5 MHz/8 MHz family tick rate selected from BIOS work area | Restored, but global-header leakage remains |
| RTC | uPD4990A bit-serial read in `mach_time.h`; separate RTC driver existed | `x86_platform.get_wallclock`; explicitly selects 48-bit uPD4990A format before shifting BCD fields | Read path restored; write path absent |
| Reset | `outb(0, 0xf0)` in machine reboot header | PC-98 machine restart and emergency-restart hooks use I/O `0xf0` | Restored |
| Text console | Old boot video plus firmware console assumptions | Early direct text-VRAM console and a modern `consw` driver for 80x25 GDC text | Rewritten |
| GDC cursor/timing | Firmware setup and old console paths | CSRW/CSRFORM cursor control; BIOS raster height; FIFO-full polling and fixed-delay port `0x5f` between writes | Rewritten and real-hardware hardened |
| Keyboard | `98kbd-io.c` serio frontend plus `98kbd.c` input driver | Single input driver owns uPD8251 ports `0x41/0x43`, initializes hardware, consumes IRQ1, and retains a 10 Hz recovery poll | Rewritten; repeat/overrun behavior fixed |
| Serial | Large `serial98.c` UART driver with FIFO/model detection | Small PC-98 uPD8251 UART/console at `0x30/0x32`; TxRDY-paced output | Rewritten; receive is still polling-oriented |
| IDE | Legacy IDE `hd98.c` and PC-9821 bank selector | libata platform PATA driver with 2-byte taskfile spacing, 16-bit data, bank port `0x432`, IRQ9 | Rewritten for one built-in interface |
| Partitions | `fs/partitions/nec98.c` | `block/partitions/nec98.c`, parsing the 16 entries at LBA1 for project IPL images | Rewritten; current geometry/signature rules are deliberately narrow |
| Cirrus graphics | Not a standalone old PC-98 fbdev in the preserved tree | `pc98cirrusfb`, using the qemu-pc98 PC-98 Cirrus mapping and initialization | New project driver |
| Trident graphics | No equivalent standalone driver in the preserved tree | `pc98tridentfb`, derived from documented PC-98/Suika3 initialization | Experimental; physical Ra43 still shows colour bars |
| PCI/APIC/SMP | Old subarchitecture carried PCI and SMP hook headers | Linux 6.12 PC-98 Kconfig is `!SMP`; generic PCI is used only when enabled for PC-9821 | Partial, intentionally conservative |
| FDD/DMA | Dedicated 4,682-line `floppy98.c` and PC-98 DMA assumptions | Not ported | Open |
| C-bus Ethernet | `ne2k_cbus` plus PC-98 variants of older ISA drivers | Not in the 6.12 tree; the dedicated LGY-98 frontend was implemented later in Linux 7.1 | Deferred in 6.12 |
| SCSI | PC-9801-55 and generic PC-98 SCSI glue | Not ported | Open |
| Sound | PC-98 CS423x/WSS and PC-9801-118 support | Not ported | Open |
| Printer, bus mouse, speaker, APM | Dedicated old drivers and machine headers | Not ported | Open |

## Linux 6.12 implementation files

The current delta is intentionally concentrated in a small set of files.

| Subsystem | Principal files | Responsibility |
| --- | --- | --- |
| Kconfig/platform | `arch/x86/Kconfig`, `arch/x86/kernel/pc9800.c`, `arch/x86/include/asm/pc9800.h` | Platform selection, family clock, RTC, reset, absence of CMOS/8042 |
| Early boot | `arch/x86/kernel/early_printk.c`, `arch/x86/kernel/setup.c`, `arch/x86/mm/init.c` | Earliest visible diagnostics, platform initialization, low-memory safety |
| Interrupts | `arch/x86/include/asm/i8259.h`, `arch/x86/kernel/irqinit.c` | PC-98 PIC ports and IR7 cascade |
| Timer | `include/linux/i8253.h`, `include/linux/timex.h` | PC-98 PIT ports and tick rate |
| Storage | `drivers/ata/pata_pc9800.c` | Built-in IDE taskfile window and IRQ |
| Partitions | `block/partitions/nec98.c` and partition core/Kconfig glue | Native PC-98 partition discovery |
| Input | `drivers/input/keyboard/pc98kbd.c` | uPD8251 keyboard initialization, IRQ, mapping, recovery |
| Serial | `drivers/tty/serial/pc98_8251.c` | tty/console support for the standard serial port |
| Console | `drivers/video/console/pc98con.c` | GDC text VRAM, attributes, scrolling, and hardware cursor |
| Graphics | `drivers/video/fbdev/pc98cirrusfb.c`, `pc98tridentfb.c` | PC-98-specific graphics initialization |

## Reconstruction sequence

The recovered numbered patches are kept in
`patches/linux-6.12-pc98/`. They replay cleanly with `git am` on official
v6.12 and pass `git diff --check`.

| Patch | Change | Dependency or reason |
| --- | --- | --- |
| 0001 | Add `CONFIG_X86_PC9800` | Establish a compile-time machine boundary |
| 0002 | Add early PC-98 text-VRAM output | Make failures before console init visible |
| 0003 | Force GNU11 for the boot decompressor | GCC 16 build compatibility; not hardware-specific |
| 0004 | Move 8259 PICs to PC-98 ports | Interrupt delivery prerequisite |
| 0005 | Move/configure the 8253 PIT and clock | Scheduler and calibration prerequisite |
| 0006 | Add uPD8251 serial console | Independent diagnostic channel |
| 0007 | Preserve low 1 MiB on failed top-down PMD probe | Prevent early memory-map corruption |
| 0008 | Pace serial transmit on TxRDY | Required by physical UART timing |
| 0009 | Add GDC text console | Normal VT output |
| 0010 | Add PC-98 keyboard input | Interactive local console |
| 0011 | Unmask the actual IR7 PIC cascade | Enable slave-PIC IRQs |
| 0012 | Add built-in PC-98 IDE via libata | Root-disk access |
| 0013 | Install reset through I/O `0xf0` | Reliable reboot |
| 0014 | Add GDC hardware cursor | Usable text console |
| 0015 | Read uPD4990A wall clock | Correct system time |
| 0016 | Select timer rate from the machine family | 5 MHz/8 MHz correctness |
| 0017 | Select uPD4990A 48-bit RTC format | Avoid nibble-shifted dates on later firmware |
| 0018 | Skip the PC/AT standard I/O reservations | Avoid collisions with PC-98 keyboard and reset ports |
| 0019 | Move keyboard receive to IRQ1 | Remove firmware-dependent timer-only input |
| 0020 | Add PC-98 Cirrus and Trident fbdev drivers | Graphics/X11 groundwork |

The numbered sequence captures the original enablement history, but it is
not the complete current tree. The following work was integrated afterwards:

- a Linux 6.12 NEC98 partition parser;
- explicit keyboard USART initialization and keyboard-side reset;
- make/break/repeat and overrun recovery fixes, retaining a safety poll after
  moving normal input to IRQ1;
- GDC CSRFORM FIFO checks and CPU-independent waits through port `0x5f`,
  which fixed row corruption on a fast physical PC-9821;
- further Trident initialization and safety changes;
- narrower and better documented device ownership.

Use `current-complete.patch`, not only patches 0001-0020, when reconstructing
the checked-in `linux-6.12/` state.

## Important design decisions

### Keep a PC-98-only kernel

PC/AT and PC-98 assign different meanings to overlapping I/O ports. Examples
include the PC-98 keyboard at `0x41/0x43`, PIT counter at `0x71`, text GDC at
`0x60`, and reset at `0xf0`. Run-time probing after generic PC/AT setup would
already have touched the wrong hardware. `CONFIG_X86_PC9800` is therefore a
compile-time selection, and a PC-98 kernel is not expected to boot a PC/AT.

### Use an external PC-98 loader

Porting `boot98` line-for-line would permanently fork the modern x86
decompressor and setup code. This repository instead owns the machine IPL,
partition PBR, and FAT16-aware second-stage loader. The kernel receives the
modern boot protocol it expects, while machine-specific disk boot behavior
stays in the loader. `earlyprintk=pc9800` closes the visibility gap after the
loader jumps to the kernel.

### Rewrite drivers against current cores

IDE is a libata platform driver rather than restored legacy IDE. Keyboard is
a current input device rather than an obsolete serio stack. Serial uses the
current UART core. The GDC console implements `consw`. These choices reduce
generic upstream modifications and make forward-porting to Linux 7.x
tractable.

### Treat real hardware timing as part of the ABI

QEMU accepts I/O sequences faster than an i486 or GDC necessarily can.
Conversely, a fast Pentium II can overrun hardware that appeared stable on an
i486. Fixed-delay port `0x5f`, status polling with timeouts, UART TxRDY, and
keyboard recovery delays are therefore correctness mechanisms, not cosmetic
delays.

## Validation record

The Linux 6.12 line has been used as the original Debian-oriented release
target and as the source for the later 7.0/7.1 forward ports. Confirmed work
includes:

- i686 kernel and module builds from a clean out-of-tree directory;
- qemu-pc98 TCG boot with the compatibility BIOS;
- PC-98 IPL and FAT16 second-stage loading;
- GDC early output and normal 80x25 text console;
- keyboard input through IRQ1;
- built-in IDE discovery and ext4 root mounting;
- NEC98 partition discovery for project-created images;
- Debian 13 userland startup;
- uPD4990A time decoding after explicit format selection;
- clean replay of patches 0001-0020 onto official v6.12;
- `git diff --check` for the reconstructed series and complete current delta.

Later physical-machine results on Linux 7.1—successful Ra43 boot, keyboard
repeat correction, and the required GDC command waits—were back-propagated
where the Linux 6.12 source shares the same driver. They should still be
retested explicitly with a Linux 6.12 image before being described as a full
6.12 physical-hardware qualification.

## Known omissions and technical debt

### Priority hardware gaps

1. **PC-98 DMA and FDD:** the generic PC/AT floppy driver is not a substitute.
   A modern PC-98 DMA abstraction and a dedicated FDD adaptation are required.
2. **Serial receive:** the current small driver still contains polling-era
   compromises. It should reserve all ports, own IRQ4 and the system-port
   interrupt gates, and keep polling only for the earliest console writes.
3. **IDE ownership:** bank port `0x432`, multiple interfaces, secondary/slave
   policy, and resource serialization need a complete platform-device model.
4. **RTC write support:** the early wall-clock hook reads time but does not
   expose a full RTC-class device or set-time operation.
5. **Trident fbdev:** physical Ra43 testing still produces vertical colour
   bars. It must remain opt-in until its initialization and write path are
   validated.
6. **SCSI, WSS/PC-9801-118 sound, C-bus Ethernet, printer, bus mouse, speaker,
   and APM:** present in parts of the historical port but not restored in
   Linux 6.12.

### Generic-header leakage

The current implementation conditionally changes generic PIC, PIT, and
timekeeping headers under `CONFIG_X86_PC9800`. This was efficient for the
first bootable port, but explicit platform data or helper functions would
make the delta easier to review and less likely to break unrelated
configurations. In particular, turning a traditionally constant PIT clock
into a run-time value can surprise generic drivers.

### NEC98 parser scope

The current parser requires the project `IPL1` marker, assumes 512-byte Linux
logical sectors and the qemu-pc98 8-head/17-sector geometry. The historical
parser is valuable reference for broader media, but expanding recognition
must be driven by real-disk samples to avoid false positives.

### SMP and APIC

Linux 6.12 PC-98 explicitly depends on `!SMP`. The historical tree contained
SMP hook headers, but that does not establish a correct modern APIC topology
or interrupt-routing implementation. PC-9821 Rv-class SMP support is a
separate platform project, not an untested Kconfig switch.

## Maintenance workflow

1. Keep `linux-2.6.7-pc98-original/` immutable except for its provenance file.
2. Make active changes only in the maintained kernel trees.
3. Build Linux 6.12 out of tree:

   ```sh
   KERNEL_VERSION=6.12 ./build-kernel.sh
   ```

4. Boot both `pc9801` and `pc9821` configurations relevant to the change.
5. Verify `/proc/interrupts` and `/proc/ioports`, not only visible behavior.
6. Test the compatibility BIOS first and genuine ROMs where the subsystem
   depends on firmware state.
7. For timing-sensitive changes, test QEMU and physical hardware.
8. Regenerate `patches/linux-6.12-pc98/current-complete.patch` against
   official v6.12 and update its statistics/checksum in the patch README.
9. Run `git diff --check` before carrying the delta to a newer kernel.

The historical tree answers “how PC-98 hardware was driven.” The maintained
6.12 tree answers “how this project drives it with modern Linux APIs.” Both
are needed; neither should silently replace the other.

## References

- Official historical Linux repository:
  <https://git.kernel.org/pub/scm/linux/kernel/git/history/history.git>
- Linux/98 project:
  <https://www.kmc.gr.jp/projects/linux98/index-english.html>
- 2002 PC-9800 upstream patch series, input portion:
  <https://lkml.iu.edu/0210.2/0986.html>
- The checked-in historical snapshot:
  `linux-2.6.7-pc98-original/`
- The exact Linux 6.12 delta and chronological patch inventory:
  `patches/linux-6.12-pc98/`
- Forward-port records: `LINUX-7.0-PORT.md` and `LINUX-7.1-PORT.md`
