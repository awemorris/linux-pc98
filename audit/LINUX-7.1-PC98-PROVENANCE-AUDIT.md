# Linux 7.1 PC-98 current-tree provenance audit

Audit date: 2026-08-03

## Conclusion

The verified clean reconstruction existed at
`/home/awe/linux-pc98-clean-reconstruction`, commit
`f6391fdb0a70c85f2f69c6e37581605ae3ce43ab`, but an earlier consolidation did
not merge all of its replacement files into the current repository.  This was
an integration omission, not a failure to produce the clean reconstruction.

The omitted clean files have now been integrated into the working tree based
on `awemorris/linux-pc98` commit
`a693ad52887bd75831c63ea6948467cd3893f0df`.  Project changes made after the
clean reconstruction (loader-provided disk geometry, physical-GDC I/O waits,
color attributes, and current Kconfig symbol names) were then reapplied from
their allowed sources.

An exact normalized scan found **zero matching blocks of three or more code
lines** from the seven prohibited candidate implementation patches.  It found
91 normalized matching lines in the explicitly permitted keyboard patches.
Those keyboard matches are expected: the implementation was supplied by Awe
Morris and intentionally preserves the NetBSD/pc98 notice and undocumented
uPD8251 recovery behavior.

This is a technical provenance review, not legal advice.

## Reproducible inputs

| Input | Exact revision or location | Role |
|---|---|---|
| Vanilla Linux 7.1 | `8cd9520d35a6c38db6567e97dd93b1f11f185dc6` | modern API baseline |
| Last official historical PC-98 tree | tree `566f0a0b5d388d0cd6cf9a796378470e7ad602b5` (Linux 2.6.7 era) | historical implementation source |
| Verified clean reconstruction | `f6391fdb0a70c85f2f69c6e37581605ae3ce43ab` | clean integration source |
| Current repository base | `a693ad52887bd75831c63ea6948467cd3893f0df` | integration target |
| Audited Linux 6.12 tree | `/home/awe/work/linux-v6.12-hayao-audit`, `b716545d0...` | comparison target only |
| Official Linux 6.12 baseline | `adc218676eef25575469234709c2d87185ca223a` | identifies additions in the audited tree |
| Permitted keyboard implementation | project commit `fb253cfe61de8b72b6bacacb00137f434014f778` | explicit owner-supplied exception |

## Prohibited candidate-set check

The candidate set is defined in `PC98-CODE-PROVENANCE-AUDIT.md`.  It is the
conservative 1,000-added-line set that was not immediately explained as an
official 2.6.7 backport when the separate Linux 6.12 repository was audited.
Hardware constants and API boilerplate in that upper bound are not assumed to
be protectable; nevertheless the current tree was checked against the whole
set.

| Audited candidate commit | Area | Current exact block of 3+ normalized code lines |
|---|---|---:|
| `e1c3f4247` | early PC-98 console | 0 |
| `7f25ed79d` | modern uPD8251 serial | 0 |
| `a28cc9290` | low-1-MiB workaround | 0; workaround absent |
| `23a4b8c32` | modern GDC `consw` | 0 |
| `a51b4854a` | modern libata frontend | 0 |
| `7180536ff` | GDC cursor implementation | 0 |
| `a10442563` | CPUID-less microcode workaround | 0; workaround removed |
| **Total** | | **0** |

| Explicitly permitted commit | Area | Matching normalized lines |
|---|---|---:|
| `5d96a282a` | keyboard supplied by Awe Morris | included in the 91-line total |
| `b716545d0` | lost-IRQ/USART recovery supplied by Awe Morris | included in the 91-line total |
| **Total** | | **91** |

Reproduce this result from the repository root with:

```sh
python3 audit/tools/scan-candidate-blocks.py \
  --audit-tree /home/awe/work/linux-v6.12-hayao-audit \
  --current-tree /home/awe/linux-pc98/linux-7.1
```

The test intentionally removes whitespace and comments before matching, but
does not perform semantic similarity inference.  The source ledger below is
the complementary manual semantic review.

`linux-7.1-pc98.patch` preserves the complete reviewable delta, while
`PC98-CURRENT-HUNK-MANIFEST.tsv` contains one row for each of its 72 unified
diff hunks.  Generate both again with:

```sh
python3 audit/tools/generate-pc98-patch.py \
  --vanilla-tree /path/to/vanilla-linux-7.1 \
  --current-tree linux-7.1 \
  --patch audit/linux-7.1-pc98.patch \
  --manifest audit/PC98-CURRENT-HUNK-MANIFEST.tsv
```

The generator deliberately includes adjacent genuine-i386 selection hunks in
shared Kconfig files, so the patch can reconstruct the actual buildable PC-98
tree rather than a syntactically incomplete fragment.  Their detailed source
history is the project-owned genuine-i386 port, not the audited PC-98 candidate
set.

## Hunk-level provenance ledger

“Hunk” below means a cohesive PC-98-specific change block or symbol group.  A
row covering a new file enumerates its internal functional groups so that the
source is not hidden behind a file-level label.

### Architecture selection, platform setup, PIC, PIT, RTC, and reset

| Current file and hunk/symbol group | Provenance | Copyright/credit action |
|---|---|---|
| `arch/x86/Kconfig`: `X86_PC9800` | official 2.6.7 `arch/i386/Kconfig`; expressed in vanilla 7.1 Kconfig syntax | historical attribution in this ledger; short existing-file integration |
| `arch/x86/Kconfig.cpu`, `arch/x86/Makefile_32.cpu`: i386/i486 selections | vanilla 7.1 plus official later-upstream i486 restoration and project i386 work | substantial project blocks are documented as Awe Morris port additions |
| `arch/x86/include/asm/i8259.h`: PC-98 PIC ports | official 2.6.7 `mach-pc9800/io_ports.h` | historical hardware constants |
| `include/linux/i8253.h`, `include/linux/timex.h`: PIT ports and runtime rate | official 2.6.7 `mach_timer.h` and `timex.h` | historical algorithm/constants |
| `arch/x86/kernel/pc9800.c`: BIOS flags and platform hook setup | official 2.6.7 PC-98 headers/setup plus vanilla 7.1 `x86_init` API | Osamu Tomita source credit and `Copyright (C) 2026 Awe Morris` in file |
| same: standard I/O resources | official 2.6.7 `arch/i386/mach-pc9800/std_resources.c`, written by Osamu Tomita | explicit in-file source credit; keyboard ports intentionally owned by input driver |
| same: `pc98_rtc_*`, wall-clock hooks | official 2.6.7 `mach_time.h`; public uPD4990A protocol; vanilla 7.1 `timespec64` hooks | historical behavior, modern integration by project |
| same: restart functions | official 2.6.7 `mach_reboot.h`; vanilla 7.1 restart hooks | historical reset behavior, modern integration by project |
| same: loader disk-geometry state and getter/export | project boot protocol and Linux 7.1 integration | project-new, `Copyright (C) 2026 Awe Morris` |
| `arch/x86/kernel/setup.c`, `platform-quirks.c`, `arch/x86/lib/kaslr.c` | short vanilla-7.1 integration hooks plus historical PC-98 platform facts | marked/documented as project port integration |
| `arch/x86/include/uapi/asm/setup_data.h`: PC-98 boot setup record | project boot protocol using vanilla setup-data conventions | project-new interface |
| `arch/x86/kernel/head32.c` | restored to vanilla 7.1 | prohibited four-line CPUID/initrd workaround is absent |
| `arch/x86/include/asm/microcode.h`: no-microcode platform-ID stub | project genuine-i386 compatibility code; `M386` disables microcode while `early_init_intel()` still records the platform ID | project-new and distinct from the audited `head32.c` candidate |

### Early and VT text consoles

| Current file and hunk/symbol group | Provenance | Copyright/credit action |
|---|---|---|
| `arch/x86/kernel/early_printk.c`: text/attribute mapping | official 2.6.7 `boot98/compressed/misc.c` | substantial block identified as added in Awe Morris port |
| same: bounded writer, scroll, console registration | vanilla 7.1 early-console API plus independent project implementation | no prohibited candidate block matched |
| `drivers/video/console/pc98con.c`: mapping, cells, clear, putc/putcs, scroll, invert | public GDC/text-VRAM format, official boot console facts, vanilla 7.1 `consw`; independently written | new file carries `Copyright (C) 2026 Awe Morris` |
| same: color attribute conversion | public PC-98 attribute-bit layout; project implementation | project-new |
| same: cursor position | independent kernel snippet supplied by Awe Morris: wait for FIFO and issue CSRW `0x49` | project-owned source; no copied CSRFORM/show-hide candidate helper |
| same: 0x5f waits between parameter bytes | project Suika3/physical-machine investigation and commit `40349759f`; required by real i686 hardware | project-new hardware fix |
| console Kconfig/Makefile and `include/linux/console.h` | vanilla 7.1 interface wiring | short mechanical integration |

### Keyboard and mouse

| Current file and hunk/symbol group | Provenance | Copyright/credit action |
|---|---|---|
| `drivers/input/keyboard/pc98kbd.c`: complete functional implementation | owner-approved project file at `fb253cfe6`; reset/recovery behavior adapted from NetBSD/pc98 and FreeBSD/pc98 | full NetBSD/pc98/Naofumi HONDA BSD notice retained; `Copyright (C) 2026 Awe Morris` retained |
| same: lost-interrupt recovery | patch supplied by Awe Morris to the audited repository | explicitly permitted; exact behavior retained because a specification-only rewrite failed on hardware |
| keyboard Kconfig/Makefile | vanilla 7.1 input interfaces | mechanical integration |
| `drivers/input/mouse/pc98busmouse.c`: PPI protocol and IRQ path | official 2.6.7 `98busmouse.c` by Osamu Tomita, adapted to vanilla 7.1 input APIs | Osamu Tomita and original contributor list retained; `Copyright (C) 2026 Awe Morris` added |
| mouse Kconfig/Makefile | vanilla 7.1 input interfaces | mechanical integration |

### Disk drivers and NEC98 partitions

| Current file and hunk/symbol group | Provenance | Copyright/credit action |
|---|---|---|
| `drivers/ata/pata_pc9800.c`: task-file mapping, bank select, IRQ | official 2.6.7 `drivers/ide/legacy/pc9800.c` | Linux/98 project and Kyoto University Microcomputer Club 1997-2000 notice retained |
| same: libata resources/probe | vanilla 7.1 `pata_platform`/libata interfaces; independently integrated | `Copyright (C) 2026 Awe Morris`; no candidate block matched |
| same: `bios_param` | project loader geometry protocol plus vanilla SCSI-host callback; ATA access remains LBA-first | project-new current-tree addition |
| `drivers/block/pc98_ide.c`: ports, ATA commands, CHS fallback | official Linux/98 PC-9800 IDE mapping and public ATA specification | historical notice plus `Copyright (C) 2026 Awe Morris` |
| same: minimal synchronous blk-mq implementation | vanilla 7.1 block APIs, project implementation for memory-constrained i386 | project-new |
| `block/partitions/nec98.c`: on-disk layout and CHS conversion | official 2.6.7 `fs/partitions/nec98.c` | Kyoto University Microcomputer Club 1999 notice retained |
| same: parser bounds and loader-geometry use | vanilla 7.1 partition APIs and project boot protocol | `Copyright (C) 2026 Awe Morris` |
| related Kconfig/Makefile/check/core hooks | vanilla 7.1 interface integration | short mechanical integration |

### Serial

| Current file and hunk/symbol group | Provenance | Copyright/credit action |
|---|---|---|
| `drivers/tty/serial/pc98_8251.c`: register definitions, reset/mode sequence, baud divisor | official 2.6.7 `drivers/serial/serial98.c` by Osamu Tomita and public uPD8251 behavior | historical Osamu Tomita notice retained |
| same: RX/TX, IRQ, termios, startup/shutdown | historical state machine expressed through vanilla 7.1 `serial_core`, tty flip, and kfifo APIs | independently integrated; no candidate block matched |
| same: console setup/write | official serial98 interrupt-preservation behavior plus vanilla 7.1 console APIs | project-new integration, `Copyright (C) 2026 Awe Morris` |
| serial Kconfig/Makefile and `include/uapi/linux/serial_core.h` identifier | vanilla 7.1 wiring; inevitable public type constant | mechanical integration |

### Framebuffers and network

| Current file and hunk/symbol group | Provenance | Copyright/credit action |
|---|---|---|
| `drivers/video/fbdev/pc98cirrusfb.c` | zlib-licensed StratoHAL `98disp_cirrus.c` by Awe Morris and Keiichi Tabata; generic Linux cirrus timing patterns | source and license provenance stated in file |
| `drivers/video/fbdev/pc98tridentfb.c` | zlib-licensed StratoHAL `98disp_trident.c` by Awe Morris and Keiichi Tabata; vanilla PCI/fbdev APIs | original 1996-2026 notices retained in file |
| fbdev Kconfig/Makefile | vanilla 7.1 wiring | mechanical integration |
| `drivers/net/ethernet/8390/ne2k-lgy98.c`: C-Bus windows and board defaults | public LGY-98/DP8390 hardware behavior and project implementation | new file carries `Copyright (C) 2026 Awe Morris` |
| same: remote-DMA and 8390 callbacks | vanilla 7.1 8390 core conventions | project-new frontend |
| 8390 Kconfig/Makefile | vanilla 7.1 wiring | mechanical integration |

## Copyright-notice policy applied

* Notices from official 2.6.7 PC-98 files are preserved when their algorithms
  or state machines remain recognizable.
* BSD-derived keyboard code retains its complete BSD notice and conditions.
* New substantial project files or substantially reworked ports state
  `Copyright (C) 2026 Awe Morris`.
* Large PC-98 blocks added inside existing upstream files are described as
  additions in the Awe Morris port rather than asserting ownership over the
  surrounding upstream file.
* Hardware register addresses, bit definitions, structure declarations, and
  unavoidable modern API glue are still assigned a provenance class in this
  ledger; they are not silently treated as author-specific implementation.

## Validation gates

The following must remain true before the integration is committed:

1. `tools/scan-candidate-blocks.py` reports zero candidate blocks and only the
   explicitly permitted keyboard matches.
2. `git diff --check` is clean.
3. Linux 7.1 i486 `vmlinux` links with PC-98 PATA, serial, keyboard, and GDC
   console enabled using `ARCH=i386`.
4. Existing i386/i486/i686 configurations continue to configure without
   silently dropping `CONFIG_X86_PC9800`.
5. QEMU and real-hardware regression remain separate runtime gates; the clean
   reconstruction already reached an i486/M686 BusyBox console in qemu-pc98,
   while keyboard and GDC-wait behavior were previously verified on hardware.

### Results for this integration

| Check | Result |
|---|---|
| Candidate normalized-block scan | PASS: 0 prohibited, 91 explicitly permitted keyboard lines |
| `git diff --check` | PASS |
| i486 full link (`ARCH=i386`, `pc9800-i486-7.1.config`) | PASS; ELF32 Intel 80386 `vmlinux`, SHA-256 `52989aca9c9c8e6a4e6de053fbf7cbb602783a28ec4341d1853e883572d84e76` |
| i386 BusyBox config `olddefconfig prepare` | PASS; `X86_PC9800=y`, `M386=y`, `PC98_CONSOLE=y` retained |
| i686 Debian config `olddefconfig prepare` | PASS; `X86_PC9800=y`, `M686=y`, `PATA_PC9800=y`, `PC98_CONSOLE=y` retained |
| Review patch/manifest generation | PASS; 5,562 patch lines and 72 manifest hunks |

The full i486 build emitted one pre-existing conversion warning in generic
`arch/x86/kernel/i8259.c` while forming the cascade-bit probe mask.  It did not
originate in any replaced candidate file and did not prevent linking; it is a
separate cleanup item rather than a provenance failure.

## Scope boundary

This audit covers PC-98 implementation hunks.  The genuine-i386 restoration is
project-owned work documented separately in `PC98-PORTING-REPORT.md` and the
i386 port plans; it was not part of the audited author's PC-98 candidate set.
Build scripts, root filesystems, and boot-loader code likewise have their own
project histories and are outside the 1,000-line PC-98 kernel candidate set.
