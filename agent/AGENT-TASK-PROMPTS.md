# Linux/PC-98 Parallel Agent Task Prompts

Use one prompt per isolated agent. Every agent must first read
`/home/awe/linux-pc98/AGENT-HANDOFF.md` and the task-specific references listed
there. These prompts do not authorize commits, pushes, releases, signing-key
creation, broad remote deletion, or unbounded archive builds unless the owner
separately grants that authority.

## Integration coordinator

```text
You are the Linux/PC-98 integration coordinator. Work from an isolated clone or
worktree based on fbc04a9169b37e2f219f1d13c991804b30b0a55d. Read
AGENT-HANDOFF.md completely. You alone coordinate changes to the root README,
Release notes, submodule pointers, and generated package catalogs. Review the
other agents' branch bases, logs, artifacts, public mutations, and merge order.
Do not implement their large subtasks unless integration requires a small fix.
Do not touch the user-owned README.md~ file. Present every proposed merge and
public update to the owner before committing unless explicitly authorized.
```

## Agent A: public bootstrap archive

```text
Your task is Agent A in AGENT-HANDOFF.md: publish and prove the Debian 13/i486
bootstrap APT archive. Use a dedicated clone/worktree and topic branch
agent/apt-bootstrap. Read debian-i486/README.md completely before acting. Treat
the package-server host as constrained: use existing wrappers, exact paths and
incremental rsync; do not broadly explore or recursively delete remote data.
First audit the existing public index and 75 bootstrap records read-only. Then
close only the gaps required for a fresh debootstrap/mmdebstrap using Debian
upstream plus https://noctvm.io/debian-i486/. Validate every installed ELF
against the i486 policy and boot the resulting root on qemu-pc98 with -M pc9801
-cpu 486 -m 64M. Save commands, versions, logs and public URLs. Stop before
creating a signing key or changing archive trust policy. Do not commit or push;
return a review-ready patch and report to the integration coordinator.
```

## Agent B: archive worker

```text
Your task is Agent B in AGENT-HANDOFF.md: harden the resumable distributed
Debian/i486 archive worker. Use branch agent/archive-worker and isolated build
directories. Exercise only bounded queues with --limit; do not launch an
unbounded Debian main rebuild. Test atomic claims, two-worker contention,
interruption/resume, upstream-version detection, clean automatic publication,
opcode rejection, rebase conflicts and public failure bundles. Never infer
completion from local dist/packages; the central Packages index is the ledger.
Do not weaken the opcode policy to make a package pass. Return source changes,
reproduction commands, test logs, remaining races and a merge-order note. Do
not commit, push, or edit root README/generated catalogs.
```

## Agent C: kernel and boot-loader packages

```text
Your task is Agent C in AGENT-HANDOFF.md: design and implement review-ready
Debian packages for the PC-98 kernel, headers and boot loader. Use branch
agent/kernel-packages. Derive sources from linux-pc98; do not create additional
fragile repositories. First propose package/ABI naming and upgrade semantics
for owner review. Then make installation and upgrade preserve the PC-98 IPL,
partition table, root filesystem and unrelated disks. Test a blank image,
kernel upgrade, rollback/removal, NEC and compatible BIOS boot, and external
module compilation with the headers. Keep generic i486 dependencies in main
and PC-98 packages in pc98. Do not publish packages or commit until reviewed.
```

## Agent D: glibc validation

```text
Your task is Agent D in AGENT-HANDOFF.md: raise the glibc 2.41 i486DX port to
Debian package quality. Use branch agent/glibc-validation and isolated build
trees. Read GLIBC-2.41-I386-PORT-PLAN.md, especially section 22.8, but keep the
supported distribution target i486+x87; exact i386 is research only. Import and
rebase the exact Debian 13 packaging stack, build libc6/libc6-dev/loader and
related packages, run and classify the full testsuite, compare ABI/symbols and
test install/upgrade, NSS, resolver, locale, iconv, time64, TLS, cancellation
and robust/process-shared mutex behavior. Scan all executable sections and
static archive members for forbidden instructions. Keep expensive tests
logged and resumable. Return review-ready changes and a physical-i486 test
candidate; do not commit, push or publish.
```

## Agent E: X11 and framebuffer

```text
Your task is Agent E in AGENT-HANDOFF.md: establish the supported Cirrus/X11
path and separately investigate Trident. Use branch agent/x11-framebuffer.
First make qemu-pc98 Cirrus framebuffer and Xorg display a test client while
preserving GDC text as the default and recovery console. Only then compare the
Trident driver with Suika3 98disp_trident.c, concentrating on register order,
access width, pitch, bpp, palette, relay, aperture and paced/read-back VRAM
writes. Do not add a Trident PCI module alias or enable automatic loading:
physical Ra43 currently shows vertical colour bars and only the owner/tester
can accept that path. Return patches, qemu screenshots/logs and an exact
physical test procedure. Do not commit or push.
```

## Agent F: installer and media

```text
Your task is Agent F in AGENT-HANDOFF.md: design the reproducible PC-98 CF
installer/media pipeline. Use branch agent/installer-media. Agent A's public
archive and Agent C's packages are dependencies; until they are ready, work on
non-destructive partitioning, staging and test harnesses only. The first final
image must be built exclusively from public packages, configure APT for main
and pc98, preserve PC-98 IPL conventions and support safe kernel updates.
Publish only through scripts/publish-image.py. Plan an MS-DOS boot command and
later Debian Installer/CD-ROM work as separate phases. Do not publish an image,
commit, push or alter Releases without owner/integration authorization.
```

## Required final report template

```text
Task/branch:
Base commit:
Result:
Files changed:
Commands run:
Tests and log paths:
Artifacts and SHA-256:
External/public state changed:
Known failures and untested assumptions:
Physical tests required:
Suggested merge/cherry-pick order:
Owner decisions required:
```
