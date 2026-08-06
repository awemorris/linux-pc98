# BOOT98 Boot Environment Design

Status: replaceable disk IPL and BOOT partition IPL implemented and QEMU-tested; cursor-key
menu selection and hardware coverage remain future work.

This document records the agreed design for a new PC-9800 boot environment.
The design deliberately separates a small, firmware-facing boot core from a
full shell and kernel loader stored in a FAT16 partition.  It is intended to
support Linux, existing DOS installations, legacy IPLware modules, and future
boot extensions without making the disk IPL difficult to maintain.

## Goals

- Enumerate BIOS-accessible FDD 0-3, IDE 0-3, and SCSI 0-7 devices.
- Present a boot menu for disks and their PC-98 partitions.
- Chain-load an existing disk IPL or partition PBR, including DOS partitions.
- Locate a FAT16 partition named `BOOT` and load `boot.bin` from it.
- Display the boot menu and Escape-to-shell hint only after `boot.bin` has
  loaded successfully.
- Execute `boot.cfg` only when the user selects `Auto`.
- Load an uncompressed Linux kernel and pass its command line.
- Execute existing IPLware modules through the published IPLware ABI.
- Load BOOT98 applets from the `BOOT` partition on demand.
- Permit future BOOTP/TFTP and serial boot applets without growing the small
  on-disk IPL core.
- Keep all ordinary filesystem access read-only in the first implementation.

## Non-goals for the first implementation

- A general-purpose command language with variables, loops, pipelines, or
  redirection.
- FAT filesystem writes, formatting, copying, or deleting files.
- initrd loading.  Current PC-98 images use a kernel with the required root
  device and filesystem drivers built in.
- Dynamic menu-entry definition from `boot.cfg`.
- LBA48, IDE DMA, or controller-specific disk access in the boot environment.
  The initial implementation uses PC-98 BIOS services.
- Reimplementation or redistribution of third-party IPLware modules.

## Existing loader and size constraints

The current tree contains these working loaders:

| File | Current size | Purpose |
|---|---:|---|
| `ipl-lba0.bin` | 512 bytes | PC-98 `IPL1` disk record |
| `ipl-lba2.bin` | 7,168 bytes | Replaceable LBA 2–15 BOOT selector |
| `ipl-part.img` | 1,024 bytes | FAT16 PBR in one reserved logical sector |
| `IO.SYS` | currently 6,352 bytes | Contiguous FAT16 system-loader file |
| `boot.bin` | unconstrained by IPL area | FAT16-hosted 32-bit loader |
| `fat-loader.bin` | 4,106 bytes | FAT16 and Linux ELF loader |
| `boot2.bin` | 984 bytes | Earlier two-sector loader |
| `dos/linux98.exe` | 16,260 bytes | DOS command-line Linux loader |

Thirty 512-byte sectors provide 15 KiB; 32 sectors provide 16 KiB.  More
importantly, a disk prepared with the NEC FORMAT utility provides roughly 14
sectors (about 7 KiB) for the IPL and fixed-disk menu implementation.  The
current Linux loader fits near that limit, but a menu, shell, applet ABI, and
IPLware compatibility would leave no useful maintenance margin.

The full BOOT98 environment therefore must not depend on fitting in the
traditional IPL/menu area.

## Boot architecture

### Stage 0: generic disk IPL (`ipl-lba0.bin`)

- One 512-byte `IPL1` record.
- Uses PC-98 BIOS disk services.
- Does not alter the display or wait for keyboard input.
- Loads exactly LBA 2 of the current fixed disk at `1fc0:0000`.
- Has no BOOT98-specific sector count, filesystem code, or next-stage format.
  Another project may install its own one-sector bootstrap at LBA 2.

### Stage 1: BOOT partition selector (`ipl-lba2.bin`)

- Occupies the replaceable LBA 2–15 area; only its first sector contains code.
- Reads LBA 1, finds the partition named `BOOT`, and chain-loads its IPL-start
  CHS. The NEC fixed-disk menu may replace this complete stage.

### Stage 2: BOOT partition PBR and `IO.SYS`

- `ipl-part.img` occupies one 1024-byte reserved FAT logical sector. The
  first physical 512-byte half explicitly reads the second half, so it does
  not assume that firmware loaded 1024 bytes.
- The PBR locates root-directory file `IO.SYS` and loads it at `1000:0000`.
  `IO.SYS` is an ordinary contiguous FAT16 file; none of it is copied into
  the reserved sector.
- The image builder copies `IO.SYS` first on a fresh volume. `INST /PART`
  copies it with DOS file I/O and rejects a fragmented FAT chain.
- Neither PBR nor `IO.SYS` relies on private Stage 1 registers.
- Enumerates BIOS-visible devices.
- Reads PC-98 partition tables.
- Displays the basic device and partition menu.
- Chain-loads a disk IPL or partition PBR.
- Locates a partition whose PC-98 partition name is `BOOT`.
- Reads FAT16 just far enough to load `boot.bin`.
- Continues to provide device/PBR booting when `boot.bin` is absent or
  invalid.
- Does not contain the interactive shell.

### Stage 3: `boot.bin`

- Stored as a normal file in the FAT16 `BOOT` partition.
- Loaded only after its header, size, and checksum have been validated.
- Displays `Auto`, `FDD 1`, `FDD 2`, `HDD 1`, and `HDD 2`; the `Auto` entry
  identifies the selected HDD, partition, and configuration file.
- `Auto` executes `boot.cfg`; Escape bypasses it and opens the prompt.
- Waits three seconds for the first selection and chooses `Auto` on timeout.
- Implements the lower-half-screen interactive shell.
- Implements FAT16 file access, `boot.cfg`, the Linux loader, BOOT98
  applets, and IPLware compatibility.
- May grow independently of the traditional system-area size.

If multiple `BOOT` partitions exist, the search order is deterministic:

1. The disk from which Stage 0 was started.
2. Other IDE devices in BIOS-visible order.
3. SCSI targets in BIOS-visible order.

The selected source must be displayed so that an unexpected duplicate
`BOOT` partition is not silent.

## User interface

The current Stage 3 menu uses number keys.  Cursor-key selection is planned
but is not part of the first implementation.  Escape at the menu enters the
shell.  Pressing Escape from the shell returns to the menu; there is no `menu`
command.

The prompt includes the current disk and partition and ends with the OpenBoot
style `ok` marker:

```text
ide0:BOOT ok
```

Selecting `Auto` runs `source BOOT.CFG`.  Entering the shell with Escape
never executes the file.  If automatic execution fails, the user is left in
the interactive shell with the error visible.

## Device names and aliases

The user-visible namespace is independent of the raw PC-98 DA/UA values and
of the IDE bank/unit mapping:

- `fd0` through `fd3`
- `ide0` through `ide3`
- `scsi0` through `scsi7`
- `boot`, referring to the original boot device

`ideN` is a stable entry from the BIOS-visible IDE enumeration.  Probe output
may additionally show primary/secondary and master/slave when known.  The
internal device object retains the BIOS DA/UA, device type, sector size,
logical geometry, and any controller-specific location needed for subsequent
BIOS calls.

`devalias` initially lists built-in and detected aliases.  User-defined alias
creation is deferred.

Example output:

```text
ok devalias
fd0       floppy 0        BIOS 90h
ide0      IDE HDD         BIOS 80h
ide1      IDE HDD         BIOS 81h
ide2      ATAPI CD-ROM    BIOS 16h
scsi0     SCSI HDD        target 0
boot      ide0
```

## Shell state

The shell retains four main pieces of selection state:

1. Current disk.
2. Current partition, or no partition.
3. Current kernel filename, or no kernel.
4. Current kernel command line.

State changes are intentionally conservative:

- Selecting a new disk clears partition, kernel, and argument state.
- Selecting a new partition clears kernel and argument state.
- Selecting a new kernel clears the old argument state.
- Returning from an IPLware module reprobes devices and invalidates any disk
  or partition state whose BIOS geometry or identity changed.

`boot.cfg` is read completely into memory before its first command is
executed.  This allows an IPLware module to change disk BIOS geometry without
invalidating the file that is currently being interpreted.

## Built-in command set

Names are provisional and may be adjusted before implementation.

| Command | Behavior |
|---|---|
| `help [command]` | List commands or show command help. |
| `echo text...` | Display text; especially useful in `boot.cfg`. |
| `pause [text...]` | Display optional text and wait for a key. |
| `wait seconds` | Wait, allowing a key to interrupt the delay. |
| `devalias` | List detected device aliases. |
| `probe-ide` | Reprobe IDE and display type, BIOS number, location, and logical CHS. |
| `probe-scsi` | Reprobe SCSI IDs 0-7 and display type and logical CHS. |
| `probe-fd` | Reprobe FDD units 0-3; optional for the first version. |
| `disk [class index]` | Select a disk; with no argument, display the current disk. |
| `part [number-or-name]` | Select a partition; with no argument, display the selection and partition list. |
| `ls [path]` | List files on the selected FAT16 partition. |
| `cat file` | Display a text file. |
| `source file` | Execute a text file as shell commands. |
| `kernel [file]` | Select or display the kernel file. |
| `arg [text...]` | Replace or display the complete kernel command line. |
| `boot` | Boot from the current state. |
| `linux file [arguments...]` | Convenience command: `kernel`, `arg`, then `boot`. |
| `run applet [arguments...]` | Load and execute a BOOT98 applet. |
| `iplware file` | Execute a legacy IPLware module using its published ABI. |
| `loadbios file [address]` | Load a raw extension BIOS; deferred until its formats are specified. |
| `reboot` | Reset the machine; `reset-all` is a possible OpenBoot-style name. |
| `halt` | Stop the machine. |

There are no built-in `clear`, `version`, `current`, `geometry`, `initrd`, or
`menu` commands.  Geometry is reported by the relevant probe command.  Current
state is reported by invoking `disk`, `part`, `kernel`, or `arg` without an
argument.

## Stateful boot behavior

`boot` takes no arguments in the first version.

| Current state | Action |
|---|---|
| Disk only | Chain-load that disk's IPL. |
| HDD plus partition | Chain-load the selected partition PBR. |
| FDD | Chain-load the FDD IPL. |
| CD-ROM | Use the future PC-98 CD boot convention. |
| Kernel selected | Load the kernel from the selected filesystem and boot it. |

DOS example:

```text
ok disk ide 0
ide0 ok part 1
ide0:MS-DOS ok boot
```

Linux example:

```text
ok disk ide 0
ide0 ok part BOOT
ide0:BOOT ok kernel VMLINUX
ide0:BOOT ok arg root=PARTLABEL=LINUXROOT rw console=tty0
ide0:BOOT ok boot
```

The one-shot form remains available:

```text
ide0:BOOT ok linux VMLINUX root=PARTLABEL=LINUXROOT rw
```

`kernel` is intentionally not Linux-specific.  The first implementation may
accept only the already supported ELF32/i386 Linux image, while leaving room
for later BSD or other kernel formats.

## `boot.cfg`

The configuration language is the same line-oriented language used by the
interactive shell.

Initial grammar:

- ASCII command names.
- Space- or tab-separated arguments.
- Quoted strings may be added because Stage 3 is not limited to the IPL area.
- CRLF and LF line endings.
- Empty lines.
- Lines beginning with `#` or `;` are comments.
- Suggested maximum file size: 4 or 8 KiB.
- Suggested maximum line length: 255 bytes.
- No variables, conditionals, loops, pipelines, or redirection initially.

Minimal configuration:

```text
echo Starting Linux...
disk ide 0
part BOOT
kernel VMLINUX
arg root=PARTLABEL=LINUXROOT rw console=tty0
boot
```

Configuration using an IDE BIOS extension:

```text
echo Installing IDE BIOS extension...
disk ide 0
part BOOT
iplware LBA_IDE.BIN

# IPLware return triggers automatic IDE/SCSI reprobe.
disk ide 0
part BOOT
kernel VMLINUX
arg root=PARTLABEL=LINUXROOT rw
boot
```

Dynamic menu-entry commands are deferred until the core menu and stateful
shell have been proven on QEMU and real machines.

## BOOT98 applets

BOOT98 applets are loaded from the `BOOT` partition on demand.  They are not
kept resident unless a later ABI explicitly supports residency.  This keeps
conventional memory use low and avoids relocation conflicts.

Potential applets include:

- BOOTP/TFTP network boot.
- NIC support for LGY-98, PC-9801-108, and PCI Ethernet devices.
- Serial terminal and serial file/kernel transfer.
- IDE, SCSI, PCI, and memory diagnostics.
- Platform-specific firmware initialization.

The preliminary BOOT98 applet header is:

```c
struct boot98_applet_header {
    char magic[4];        /* "B98A" */
    uint16_t abi_version;
    uint16_t header_size;
    uint32_t image_size;
    uint16_t entry_offset;
    uint16_t flags;
    uint32_t crc32;
    char name[16];
};
```

The exact register ABI and service table remain to be specified.  The service
interface should eventually include console I/O, keyboard input, FAT16 file
reads, BIOS disk calls, temporary memory, device reprobe, and entry into the
central kernel loader.  Applets are trusted real-mode code with unrestricted
hardware access.  CRC32 detects corruption; it is not an authenticity or
security boundary.

Network and serial applets should reuse BOOT98's kernel loader rather than
each containing another ELF loader.  A later streaming or image-sink service
will be needed for downloaded kernels.

## IPLware compatibility

IPLware modules execute in real mode; protected mode is not required.
Published IPLware behavior includes:

- Load address `6000:0100`.
- `CS=DS=ES=SS=6000h`.
- A stack near the top of the same 64 KiB segment.
- Type 1 modules return with `RETF`.
- Type 2 COM-style modules return with `RET`.
- Entry registers describe module type, source DA/UA, source disk LBA, module
  size, and IPLware loader version.

BOOT98 must leave unreal mode and establish ordinary real-mode segment limits,
stack state, and interrupt behavior before calling an IPLware module.  It then
restores its own state and reprobes disks after the module returns.

Files loaded from FAT16 may be fragmented, while some self-modifying IPLware
modules assume a contiguous on-disk module beginning at the supplied LBA.  The
first implementation should:

- Verify that an IPLware file has a contiguous FAT chain.
- Refuse fragmented modules with an explicit error.
- Document self-modifying/write-back modules as unsupported until their disk
  write behavior is deliberately implemented and tested.
- Never bundle third-party modules without confirming their distribution
  terms.

Primary specification reference:

- https://www7b.biglobe.ne.jp/~marimo9821/iplware/iplwares.html

Compatibility testing should include independently authored modules where
their terms permit local testing, particularly a module from the IPLware
author, drachen6jp's `LBA_IDE`, and a simk98 DOS/IPLware-compatible utility.

## Error behavior

- A missing or invalid `boot.bin` leaves the partition-IPL fallback device/PBR
  menu usable.
- A missing `boot.cfg` selected through `Auto` reports the error and enters
  the shell; entering the shell with Escape is always independent of the file.
- A syntax or command failure in automatic configuration stops execution and
  leaves the user in the shell with the failing line number displayed.
- Disk reads are bounded and checked for BIOS carry/error status.
- Selecting a device or partition that disappeared after reprobe clears that
  selection rather than retaining a stale BIOS number.
- Chain loading restores the register and work-area conventions required by
  the target PC-98 IPL/PBR.

## Implementation phases

### Phase 1: partition IPL foundation (implemented)

1. Define an internal BIOS device descriptor.
2. Enumerate FDD, IDE, and SCSI through INT 1Bh SENSE.
3. Parse PC-98 partition tables using per-disk BIOS logical geometry.
4. Implement the upper menu and PBR/disk-IPL chain loading.
5. Verify primary and secondary IDE and PC-9801-92 SCSI on QEMU.

### Phase 2: 32-bit loader loading (implemented)

1. Find a `BOOT` partition.
2. Implement the minimal FAT16 path needed to find `boot.bin`.
3. Define and validate the `boot.bin` header and checksum.
4. Load and enter `boot.bin` while retaining the device table and boot origin.
5. Add `Shell` only after successful entry.

### Phase 3: shell and configuration (implemented baseline)

1. Implement the lower-half text console and Escape return.
2. Implement parser, command dispatch, and selection state.
3. Add `devalias`, probe, `disk`, `part`, `ls`, and `cat`.
4. Load all of `boot.cfg` and execute it only through `Auto`.
5. Integrate the existing ELF Linux loader with `kernel`, `arg`, and `boot`.

### Phase 4: extensions

1. Specify and implement the BOOT98 applet ABI.
2. Implement legacy IPLware execution and reprobe.
3. Test representative third-party IPLware modules without redistributing
   them.
4. Investigate raw extension-BIOS formats before implementing `loadbios`.
5. Implement network and serial applets.

## Unresolved questions

- Exact FDD DA/UA probing rules across early and late PC-98 machines.
- Stable user-visible numbering for IDE ATAPI devices versus dense BIOS HDD
  numbering.
- Complete SCSI target-to-INT-1Bh mapping for different option BIOSes.
- The PC-98 IDE and SCSI CD-ROM boot conventions to support.
- Whether the `BOOT` partition may use subdirectories in the first release.
- The final `boot.bin` load address and low-memory map.
- The BOOT98 applet register ABI and service dispatch format.
- The exact formats covered by `loadbios` and whether address discovery can be
  derived from a file header.
- Safe compatibility rules for IPLware modules that modify their own disk
  image.
- Installation and recovery tooling after NEC FORMAT rewrites the system area.

These questions do not block Phase 1.  They should be resolved with QEMU
tracing and real-machine tests as the corresponding phase is reached.

## Handoff to the next Codex session

This section is an operational handoff, not part of the on-disk BOOT98 ABI.
It records enough current state for another Codex session to continue without
reconstructing the decisions from chat history.

### Workspace and repository state

The canonical working trees are on the shared Debian server:

```text
SSH host:       awe@10.0.10.101
Linux tree:     ~/linux-pc98
QEMU tree:      ~/qemu-pc98
Windows build:  ~/qemu-win64
```

Snapshot taken on 2026-08-04:

```text
~/linux-pc98
  HEAD 9244333eb [pc98] Improve PC-98 IDE support
  ?? bootloader/BOOT98-DESIGN.md

~/qemu-pc98
  HEAD 0f6b74c [pc98] Add a feature to boot from the secondary IDE HDD
  ?? build-i386-port/
  ?? docs/pc98-ide-bios-analysis.md
  ?? roms/pc98bios/*.img
  ?? roms/pc98bios/*.o
```

All existing modified and untracked files belong to the user.  Do not delete,
clean, reset, or overwrite them merely because they are untracked.  In
particular, some ROM `.img` and `.o` files are build products, but their
presence does not authorize a broad cleanup.  Inspect exact status again
before editing.

`bootloader/BOOT98-DESIGN.md` should be added to the Linux repository after user
review.  Do not commit or push implementation changes unless the user asks;
the normal workflow is to prepare a reviewable change and let the user commit.

### Decisions that are already settled

Do not reopen these choices without new hardware evidence:

1. BOOT98 is split into the generic LBA 0 IPL, a self-loading LBA 2 boot core,
   and FAT16-hosted `boot.bin`.  The full shell is not forced into the NEC
   system area.
2. The generic IPL loads exactly one LBA 2 sector and does not know BOOT98's
   format or length.  The BOOT98 second stage retains a fallback
   device/partition/PBR menu when `boot.bin` is missing or damaged.
   The `Auto` menu and Escape-to-shell hint appear only after `boot.bin`
   loads successfully.
3. BIOS-visible devices are presented as FDD 0-3, IDE 0-3, and SCSI 0-7.
4. Disk access in the first boot-environment implementation uses INT 1Bh.
   Per-disk BIOS SENSE geometry is used to interpret each PC-98 partition
   table; fixed `H=8` assumptions must not be reintroduced into the loader.
5. The loader passes the boot drive and BIOS logical CHS to Linux boot
   parameters.  Linux uses that geometry for NEC98 partition interpretation.
   The PC-98 IDE drivers prefer LBA for data I/O and fall back to CHS when LBA
   is unavailable.
6. `boot.cfg` is a normal multi-line command file, loaded completely before
   execution.  It is executed by the `Auto` menu entry; Escape-to-shell
   explicitly bypasses it.  It is not restricted to a single
   kernel-command-line record.
7. The shell is stateful.  The intended pattern is:

   ```text
   disk ide 0
   part 2
   kernel VMLINUX
   arg root=PARTLABEL=LINUXROOT rw
   boot
   ```

8. Escape returns from the lower-half shell to the upper menu.  There is no
   `menu` command.  There are also no `clear`, `version`, `current`,
   `geometry`, or `initrd` commands in the initial design.
9. Probe commands report geometry and controller identity.  `devalias` lists
   the stable user-visible device namespace.
10. IPLware compatibility and native BOOT98 applets are separate mechanisms.
    Network BOOTP/TFTP and serial support belong in applets, not `IO.SYS`.
11. Existing DOS `LINUX98.EXE` remains useful for machines whose custom disk
    geometry is established after DOS boot.  BOOT98 does not make that loader
    obsolete.

### Current implementation and next work

The initial chain is implemented and has reached a Linux 7.1 i386 BusyBox
prompt under QEMU:

1. LBA 0 silently enters LBA 2 of the current fixed disk.
2. The LBA 2 bootstrap loads the remainder of sectors 2-15.
3. The boot core finds `BOOT`, validates and loads `boot.bin`.
4. Probe status and the third-stage menu are displayed on consecutive rows.
5. The first menu selection has a three-second timeout.  Timeout executes
   `Auto`; if the 32-bit loader is unavailable, `IO.SYS` instead chain-loads the
   `BOOT` partition PBR or the first active partition PBR.
6. The headless test waits for the automatic selection and verifies the
   BusyBox prompt without injecting keyboard input.

The next work is deliberately hardware-facing:

1. Verify this exact three-stage image on a real PC-98.
2. Test FDD and HDD chain loading with NEC and compatible BIOSes.
3. Extend the third-stage menu to cursor-key selection without changing the
   on-disk stage boundaries.
4. Retain the number-key path as a recovery interface.

Minimum Phase 1 QEMU matrix:

| Case | Required observation |
|---|---|
| FDD plus primary IDE | Both appear; either can be selected. |
| Primary plus secondary IDE | Both HDDs and their partitions remain distinct. |
| IDE disk with non-default logical geometry | Partition entries are decoded with SENSE H/S. |
| PC-9801-92 SCSI HDD | Target is enumerated and its PBR can be selected. |
| Missing or invalid `boot.bin` | The `IO.SYS` menu still chain-loads a bootable device. |

Real-machine testing should follow QEMU tests, especially on early machines
with unusual BIOS geometry.  Do not claim broad hardware compatibility from
QEMU alone.

### Build and test constraints

- Prefer the strong server at `10.0.10.101` for builds and non-KVM tests.
- Do not use nested KVM on that Proxmox guest for performance judgments.
- Other agents and users may be running QEMU on the server.  Never kill QEMU
  processes in bulk; terminate only processes that this task started and can
  identify precisely.
- Preserve user disk images.  Work on copies for tests that can modify a disk.
- Keep `IO.SYS` size visible in every build. The current PBR accepts at most
  127 physical sectors (65,024 bytes); larger functionality still belongs in
  `boot.bin`, not in the real-mode system loader.

### Suggested prompt on another computer

Use the same SSH server and start the next Codex task with:

> Work in `awe@10.0.10.101:~/linux-pc98`. Read
> `bootloader/BOOT98-DESIGN.md` completely, especially the handoff section. Check
> `git status` in both `~/linux-pc98` and `~/qemu-pc98`; preserve all existing
> changes and untracked files. The initial three-stage chain is implemented;
> first inspect the current diff and reproduce the `busybox-i386` headless
> smoke test. Continue with real-machine regression fixes or cursor-key menu
> selection only after reporting the result. Do not commit or push until I
> review it.

If chat handoff is unavailable, the repository, this document, and current
server-side worktree are the authoritative continuation state.
