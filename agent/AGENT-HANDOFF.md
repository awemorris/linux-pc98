# Linux/PC-98 Agent Handoff

Status date: 2026-08-06 (JST)

This document is the entry point for agents continuing the Linux/PC-98 work.
Read it before changing files. The repository contains several years' worth of
work compressed into a small number of large commits, and many apparently
unusual choices are deliberate compatibility decisions.

## Urgent hardware handoff: PC-9801-55/92 SCSI

Before changing the Linux or QEMU SCSI implementations, read
[`SCSI-55-92-HANDOFF.md`](SCSI-55-92-HANDOFF.md).  A real WINnote98/55-compatible
adapter already detects its BlueSCSI disk and NEC98 partitions, but fails after
attachment on a `WRITE(10)`/disconnect transition.  DMA, asynchronous PIO,
negotiation, disconnect and IRQ-drain experiments have not fixed it.  The
specific handoff records the dirty files, fixed test image, QEMU-only success,
failed experiments and the bounded trace required for the next investigation.

## 1. Mission and current milestone

The project has restored NEC PC-9800 support in modern Linux and restored the
i386 and i486 kernel targets in Linux 7.1. The following combinations have
booted on qemu-pc98 and physical PC-98 hardware:

| Target | Kernel | Userland | Tested minimum RAM | Status |
| --- | --- | --- | ---: | --- |
| i386SX/DX | Linux 7.1 PC-98/i386 | static musl/BusyBox | 5 MiB | released |
| i486SX | Linux 7.1 PC-98/i486 + soft float | static musl/BusyBox | 5 MiB | released |
| i486DX/Pentium/Pentium MMX | Linux 7.1 PC-98/i486 | Debian 13/i486DX + glibc 2.41 | 64 MiB | image released; package archive is the next milestone |
| Pentium II/P6 | Linux 7.1 PC-98/i686 | official Debian 13 i686 userland | 64 MiB | released |

The immediate distribution milestone is:

> Publish and validate a Debian 13/i486 APT repository that can independently
> supply a debootstrap root filesystem.

Do not reopen exact-i386 Debian as a distribution target. Exact i386 remains a
research target; the supported custom Debian ABI is i486 with an x87 FPU.

## 2. Authoritative repositories and state

Primary checkout on the build server:

```text
/home/awe/linux-pc98
```

Git remotes:

```text
origin  git@github.com:awemorris/linux-pc98-mirai98.git
salsa   git@salsa.debian.org:awemorris/linux-pc98.git
```

Current root commit at handoff:

```text
fbc04a9169b37e2f219f1d13c991804b30b0a55d  Debian 13
```

Maintained submodules at this commit:

| Path | Commit | Purpose |
| --- | --- | --- |
| `debian-i486` | `8dc3730e861690a90b19d34f2bb13006546f3b6b` | Debian package-port database and archive workers |
| `qemu-pc98` | `35483719f37b135cab612125cea3b7c9a4441eee` | i386/PC-98 validation emulator |
| `toolchain/gcc` | `a0e5e713daa14252912d420a2aaa3107fc874a80` | maintained GCC source |
| `toolchain/glibc` | `cddb5b71c3ee05561cc78ed9c7f8f1e04f703d68` | glibc 2.41 i386/i486 port |
| `toolchain/musl` | `78c0972e439e7473f8660655fed5c24db5a929d5` | static i386 BusyBox toolchain support |

There is one pre-existing untracked file in the root checkout:

```text
README.md~
```

Treat it as a user-owned backup. Do not add, modify, or delete it.

## 3. Published release

GitHub Release:

```text
https://github.com/awemorris/linux-pc98/releases/tag/v0.3.0
```

It contains exactly these four image assets and no checksum assets:

| Asset | Contents | RAM |
| --- | --- | ---: |
| `linux-7.1-pc98-i386-busybox.img.xz` | i386 BusyBox, 32 MiB swap | 5 MiB |
| `linux-7.1-pc98-i486-busybox.img.xz` | i486 kernel + shared i386 BusyBox, 32 MiB swap | 5 MiB |
| `debian13-pc98-i486-live-cfcard.img.xz` | full i486 kernel, Debian 13/i486DX, 128 MiB swap | 64 MiB |
| `debian13-pc98-i686-live-cfcard.img.xz` | full i686 kernel, official Debian 13 packages | 64 MiB |

Before upload, all four XZ streams passed `xz -t`. The BusyBox images reached
the interactive console at 5 MiB in qemu-pc98. Both Debian images reached the
login prompt at 64 MiB. Public images use the screen and PC-98 keyboard as the
console. The Debian root password is `pc98`.

Do not replace release assets without recording the source commit, validating
the uncompressed image, running `xz -t`, and booting the exact compressed
artifact after decompression.

## 4. Essential design decisions

### CPU and userland baselines

- The small i386 image targets i386SX and therefore also runs on i386DX.
- The small i486SX image uses kernel software floating point.
- Debian/i486 requires i486DX and x87. Pentium and Pentium MMX use this ABI.
- Official Debian 13 i386 packages use an i686/P6 baseline and are only used
  by the Pentium II/P6 image.
- Exact-i386 glibc exists as a validated research path using a kernel-assisted
  atomic ABI. It is not the Debian distribution baseline.

### Boot and disk layout

- PC-98 IPL1 and partition conventions are required; a PC/AT MBR is not enough.
- The first FAT16 partition contains an uncompressed ELF named `VMLINUX`.
- The second partition contains the ext4 root filesystem.
- BusyBox images include a third 32 MiB swap partition. The released i486
  Debian image includes a 128 MiB swap partition.
- bzImage and a root initramfs were deliberately removed from public images to
  avoid wasting scarce RAM during decompression and early boot.
- The loader clears text/graphics VRAM, shows kernel/code/data transfer
  progress, and draws an optional 80 x 120 1-bpp logo at the lower-right.
- The loader initializes the GDC through the PC-98 BIOS interface so its screen
  works with both NEC ROM BIOS and the compatible BIOS.

### Kernel/device scope for low-memory i386

- No PCI exists on the intended 386 PC-98 models.
- Use GDC text, PC-98 keyboard, FDD, PC-98 IDE, and optionally LGY-98 Ethernet.
- No Cirrus, Trident, sound, generic SCSI, or unnecessary PC/AT serial devices
  in the small profile.
- The low-memory kernel uses the project-specific PC-98 IDE block driver to
  avoid the generic SCSI/libata memory cost.
- ext4 is the supported root filesystem.

### Networking and display

- LGY-98 is supported through a small PC-98 wrapper around the NE2000 core.
- The Ra43 onboard Intel PRO/100 (`8086:1229`, subsystem `1033:8000`) is
  recognized by the normal `e100` driver; no subsystem table entry is needed.
- GDC 80x25 text is the supported console.
- `pc98cirrusfb` exists for qemu-pc98.
- `pc98tridentfb` is not production-ready on physical Ra43 hardware. Explicit
  loading still produces vertical colour bars. It intentionally has no PCI
  module alias, so Debian must not auto-switch away from the GDC console.

## 5. Build and test environment

Main server:

```text
ssh awe@10.0.10.101
```

It is a Debian 13 Proxmox VM backed by a dual Xeon Gold 6130 server
(32 physical cores / 64 threads). CPU-heavy builds may use roughly 28-32
parallel jobs. Leave a few threads free when practical.

Important execution policy:

- Do not use nested KVM for performance measurements. Use TCG on this server.
- Never kill QEMU processes by name or with a broad pattern. Other agents use
  the server. Record and terminate only the exact PID created by the agent.
- Use GNU Screen or a detached, logged process for long jobs and unstable SSH.
- Keep build logs and exact commands. Long glibc/GCC tests must be resumable.
- The host `10.0.10.25` is a physical Alder Lake laptop and may be useful for
  native KVM execution, but compile on the main server.

Known current validation QEMU:

```text
/home/awe/qemu-pc98/build-i386-port/qemu-system-i386
```

This build includes the `386` CPU model and permits the 5 MiB PC-98 machine.
Do not accidentally use the older
`/home/awe/qemu-codex/qemu-pc98-dev/build-release-codex/qemu-system-i386`;
it lacks the `386` model and rejects less than 16 MiB.

Representative commands:

```sh
# Small i386 image
qemu-system-i386 -M pc9801 -cpu 386 -m 5M -accel tcg \
  -drive if=ide,bus=0,unit=0,format=raw,file=IMAGE.img

# Debian/i486
qemu-system-i386 -M pc9801 -cpu 486 -m 64M -accel tcg \
  -drive if=ide,bus=0,unit=0,format=raw,file=IMAGE.img

# Official Debian/i686
qemu-system-i386 -M pc9821 -cpu pentium2,-apic -m 64M -accel tcg \
  -drive if=ide,bus=0,unit=0,format=raw,file=IMAGE.img
```

Primary scripts are documented in `README.md`. Particularly important:

```text
build-kernel.sh
build-debian.sh
build-i386-image.sh
build-i386-rootfs.sh
build-glibc.sh
build-glibc-tests.sh
build-glibc-validation-image.sh
test-glibc-qemu.sh
```

## 6. Public Debian archive design

The package publication host is deliberately constrained:

```text
SSH alias:  package-server
SSH root:   www/noctvm.io/debian-i486
HTTPS root: https://noctvm.io/debian-i486/
```

Do not explore broad remote directories. Use the existing wrappers, exact
paths, incremental `rsync`, atomic `mkdir`, exact-file removal, and bounded
SSH timeouts. Never run a recursive remote deletion.

Intended public layout:

```text
packages/
  dists/trixie/main/binary-i386/
  dists/trixie/pc98/binary-i386/
  dists/trixie/fmtowns/binary-i386/
  pool/main/
  pool/pc98/
  pool/fmtowns/
  build-deps/
  failures/
images/
  pc98/
  fmtowns/
```

`main` is the machine-independent i486 index. `pc98` and `fmtowns` contain
only machine-specific packages. A PC-98 client is expected to use:

```text
deb https://noctvm.io/debian-i486/packages trixie main pc98
```

The central `Packages` index is the completion ledger. A worker must not infer
completion from its local `dist/` tree. Package operations must go through
`debian-i486/scripts/artifact-store.py`; callers must not directly copy into
`dist/packages/` or construct public URLs.

Current `debian-i486` state:

- commit `8dc3730` (`Complete Debian 13 i486 bootstrap archive`);
- 75 source-package JSON records;
- framework for claims, resumable queues, opcode scans, package publication,
  upstream rebase, failure bundles, build dependencies, and image publication;
- local `dist/packages/` is not authoritative and may be almost empty.

Read `debian-i486/README.md` completely before operating a worker.

## 7. Parallel work allocation

Agents must use separate clones or worktrees and topic branches. Do not allow
two agents to edit the same root checkout or submodule worktree. Build outputs
must also be distinct (`BUILD_DIR`, `WORK_DIR`, or task-specific `build/`
paths). The integration agent alone updates root `README.md`, release notes,
submodule pointers, and shared generated catalogs.

### Agent A: Bootstrap APT publication and debootstrap proof

Scope:

- `debian-i486/` publication/configuration scripts;
- read-only inspection of `package-server` through existing wrappers;
- publish the already completed bootstrap package set if not present;
- generate atomic `Packages`, `Packages.gz`, and `Release` metadata;
- test a fresh i486 root using only Debian upstream plus the public custom
  archive; no private local `.deb` shortcuts;
- document unsigned-repository handling until a project signing key exists.

Completion gate:

1. `https://noctvm.io/debian-i486/packages/.../Packages.gz` is fetchable.
2. A clean debootstrap/mmdebstrap run resolves all required i486 packages.
3. Every installed executable passes the i486 opcode policy.
4. The root boots with `-M pc9801 -cpu 486 -m 64M` to a login prompt.
5. Commands, package versions, logs, and public URLs are recorded.

Stop and ask before creating a signing key or changing public archive policy.

### Agent B: Distributed full-archive worker hardening

Scope:

- queue, claim, retry, update, rebase, opcode-scan, and failure-bundle paths in
  `debian-i486/scripts/`;
- dry-run and small bounded live runs against a few non-bootstrap packages;
- verify interruption and multi-worker behavior;
- keep expensive compiler tests separate from routine rebuilds.

Completion gate:

1. Two isolated workers cannot corrupt or lose the central index.
2. An interrupted worker safely resumes or expires its exact claim.
3. Clean unmodified packages publish without AI assistance.
4. Build/opcode/rebase failures create collision-resistant public bundles.
5. A Debian upstream version update is detected and either rebased or logged.

Do not start an unbounded all-Debian build. Use `--limit` until the owner
explicitly authorizes continuous operation.

### Agent C: PC-98 kernel and boot-loader Debian packages

Scope:

- Debian package definitions for the PC-98 kernel, headers, and loader;
- derive sources from this repository rather than creating fragile external
  repositories;
- install/update the FAT16 `VMLINUX` file and PC-98 disk sectors safely;
- keep generic i486 packages in `main` and PC-98 packages in `pc98`.

Provisional package naming must be reviewed before publication. The working
model discussed previously was similar to:

```text
linux-image-6.12.90+deb13.1-pc98-i386
linux-headers-6.12.90+deb13.1-pc98-i386
```

Do not blindly adopt that string: Debian architecture remains `i386`, while
CPU baseline, PC-98 machine identity, ABI name, and kernel release suffix must
be kept distinct. Agree on naming and upgrade semantics first.

Completion gate:

1. Install onto a blank two-partition PC-98 image.
2. Upgrade the kernel without rewriting or damaging the root partition.
3. Remove/rollback without destroying the IPL or unrelated partitions.
4. Boot with both compatible BIOS and NEC ROM BIOS under qemu-pc98.
5. Package headers can build a simple external module.

### Agent D: glibc/i486 Debian-quality validation

Scope:

- `toolchain/glibc`, its patch inventory, and isolated glibc build outputs;
- import/rebase the exact Debian 13 packaging patch stack;
- run and classify the full testsuite;
- compare ABI, symbols, structure layouts, loader behavior, and Debian package
  contents;
- test NSS, resolver, locale, iconv, time64, TLS, cancellation, robust mutexes,
  install, and upgrade in a disposable root.

Completion gate:

1. Debian `libc6`, `libc6-dev`, loader, and related packages build for i486.
2. No forbidden post-i486 instructions occur on reachable paths.
3. ABI comparison and every test failure have recorded dispositions.
4. Packages pass an install/upgrade test and boot on qemu-pc98 `-cpu 486`.
5. A physical i486DX run is prepared for the owner/tester.

Do not conflate this with the exact-i386 research target. Read
`GLIBC-2.41-I386-PORT-PLAN.md`, especially section 22.8.

### Agent E: X11 and framebuffer work

Scope:

- reproduce X11 on the supported qemu-pc98 Cirrus path first;
- keep GDC text console as the default and recovery path;
- compare `pc98tridentfb` register order, access widths, banking/linear mode,
  pitch, bpp, palette, relay, and paced VRAM writes with Suika3
  `98disp_trident.c`;
- prepare instrumented builds for physical Ra43 testing.

Completion gate for phase 1:

1. Cirrus framebuffer is stable under qemu-pc98.
2. Xorg starts and displays a test client without corrupting the text console.
3. Boot without explicitly requesting framebuffer remains GDC text.

Trident completion is separate and requires physical confirmation. Do not add
a PCI module alias or auto-load Trident until the owner confirms a correct
screen on Ra43.

### Agent F: Installer and distribution media

This task depends on Agents A and C. It may design/test tooling in parallel,
but must not claim completion before the public archive and packages exist.

Scope:

- PC-98 partitioning support;
- `/etc/apt` configured for `main pc98`;
- reproducible CF image generation from public packages;
- MS-DOS command-line boot loader;
- later, Debian Installer and bootable CD-ROM media;
- publish through `scripts/publish-image.py`, not direct server copies.

Completion gate for the first phase:

1. Build a clean CF image exclusively from published packages.
2. Boot it on qemu-pc98 with NEC and compatible BIOS.
3. Provide a non-destructive install/upgrade path for physical CF media.
4. Record required RAM and CPU prominently.

## 8. Dependency order

```text
Agent D glibc validation ----+
                             +--> Agent A public bootstrap --> Agent F images/installer
Agent C kernel/loader pkgs --+

Agent B worker hardening ----------> full Debian archive expansion

Agent E Cirrus/X11 ----------------> optional graphical image profile
Agent E Trident -------------------> physical-hardware-only follow-up
```

Agents A, B, D, and E can start independently with isolated workspaces.
Agent C can also begin packaging design, but publication integrates through
Agent A. Agent F should wait for A/C artifacts before producing final media.

## 9. Integration and reporting protocol

Each agent must provide:

1. topic branch and exact base commit;
2. files changed and why;
3. commands executed;
4. build/test logs and their paths;
5. exact generated artifacts and SHA-256 values (hashes need not be uploaded
   to GitHub Release unless requested);
6. public state changed, including URLs and package versions;
7. known failures, untested assumptions, and physical tests required;
8. a short cherry-pick or merge order note.

Do not commit generated disk images, package pools, root filesystems, or build
trees. Do not modify unrelated user changes. Do not force-push shared branches.
Do not create or edit a GitHub Release unless assigned by the integration
agent or owner.

Suggested branch names:

```text
agent/apt-bootstrap
agent/archive-worker
agent/kernel-packages
agent/glibc-validation
agent/x11-framebuffer
agent/installer-media
```

Generated catalogs should be regenerated once by the integration agent after
merging source records; agents should avoid competing catalog commits.

## 10. Known deferred or separate QEMU work

The following belongs primarily to qemu-pc98, not the immediate Debian archive
milestone. Preserve it as a separate backlog unless explicitly assigned:

- complete PC-9801-92 SCSI HDD/CD-ROM emulation;
- PC-9801-86 PCM/FIFO interrupt work and Windows-host audio regressions;
- PCI IRQ/MMIO and USB host-controller/libusb validation on Windows 2000;
- Windows-host FDD passthrough;
- Windows 95 DOS-window Alt+Enter cursor/freeze issue;
- savevm/migration support across PC-98 devices;
- compatible BIOS completeness and ROM BASIC follow-up.

Do not mix these changes into Debian/package branches.

## 11. Required reading by task

All agents:

- `README.md`
- this file

Kernel/platform agents:

- `LINUX-6.12-PORT.md`
- `LINUX-7.0-PORT.md`
- `LINUX-7.1-PORT.md`
- `linux-2.6.7-pc98-original/SOURCE-PROVENANCE.md`

Boot/image agents:

- `loader/README.md`
- `QEMU-PC98.md`

Toolchain agents:

- `toolchain/README.md`
- `toolchain/patchsets/README.md`
- `GLIBC-2.41-I386-PORT-PLAN.md`
- `toolchain/glibc/I386-PORT.md`
- `toolchain/musl/PC98-PORT.md`

Archive agents:

- `debian-i486/README.md`
- the schemas and immutable profile under `debian-i486/`

## 12. Final safety reminders

- Physical PC-98 testing is scarce and valuable. Never overwrite a tester's
  only disk image; publish a new, clearly named candidate.
- Avoid assuming PC/AT memory maps, IPL conventions, keyboard behavior, IRQs,
  or BIOS sector-read behavior on PC-98.
- Reproducibility variance is a warning sign. Prefer static review plus logged,
  repeatable tests over a single successful boot.
- Keep screen-console public images separate from serial-console diagnostic
  images.
- When changing low-memory code, test both 5 MiB and a normal-memory control.
- When changing CPU baselines, inspect every executable section and static
  archive member, not only the final main executable.

The project has crossed the research threshold: modern Linux 7.1 boots on
genuine i386/i486 PC-98 systems, and Debian 13 boots on i486DX hardware. The
next work should turn that achievement into a reproducible, maintainable
distribution rather than reopening already validated architecture choices.
