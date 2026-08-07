# Boots M12: Noct submodule migration

Status: **IMPLEMENTED — AWAITING USER REVIEW**

Date: 2026-08-08

## Result

Noct is no longer copied into linux-pc98 as a squashed source subtree.
`third_party/noct` is now a git submodule pinned to:

```text
765df9ed88439eed91d118ab9bdbc6d442524527
```

Upstream: <https://github.com/awemorris/NoctLang.git>

This revision includes the upstream PC98BE target and the subsequent File API
and String fixes. The migration changes dependency transport and the selected
upstream revision; it does not move PC-98 hardware glue into Noct.

## Maintainer interface

```sh
./build.sh noct init
./build.sh noct status
./build.sh noct verify
./build.sh noct update <commit-or-ref>
```

`init` is the only normal setup operation that may access the network.
Ordinary build, test, image, and release commands never fetch or advance Noct.
`update` is explicit, refuses a dirty submodule, checks out a detached
revision, and stages the new gitlink for review.

`verify` rejects all of the following before compilation:

- an uninitialized submodule;
- a dirty Noct worktree;
- a checkout that differs from the staged gitlink;
- a missing zlib `LICENSE` or incomplete source tree;
- generated objects, libraries, CMake caches, or build directories inside the
  submodule.

See `third_party/NOCT-SUBMODULE.md` for clone and update instructions.

## Verification

Noct upstream was configured with the `static` preset and built with 32 jobs.
All 47 syntax programs passed in both interpreter and forced-JIT modes:

```sh
cmake --preset static
cmake --build --preset static -j32
cd tests
NOCT=../build-static/noct ./run-syntax.sh
```

The generated upstream build directory and test output were removed after the
test, leaving the standalone Noct checkout clean.

The complete previously reviewed Boots verification passed after a clean
selected-source rebuild:

```sh
./build.sh noct clean
./build.sh noct verify -j32
```

This includes:

- submodule identity and cleanliness verification;
- heap/libc, lifecycle, soft-float, stdio, filesystem, and utility host tests;
- static Noct, libc, soft-float, glue, and final BOOT.SYS i386 opcode checks;
- forced i386 JIT lifecycle tests;
- IDE and PC-9801-92 SCSI BIOS write/read/restore QEMU tests;
- FAT16 File API and LS.NCT/CP.NCT QEMU tests at 6 MiB.

## Size impact

The size change comes from advancing Noct from the M11 revision to current
upstream; the gitlink itself has no runtime cost.

| Region | M11 | M12 | Delta |
| --- | ---: | ---: | ---: |
| `BOOT.SYS` load image | 238,340 | 242,852 | +4,512 bytes |
| resident `.text` | 229,425 | 233,486 | +4,061 bytes |
| resident `.data` | 8,900 | 9,348 | +448 bytes |
| resident BSS | 31,504 | 31,504 | 0 bytes |

M12 `BOOT.SYS` SHA-256:

```text
f422c8c39d0a23aa4f57bd10a28ced8fee725932086363c028875620b60fcf7b
```

The later TINY milestone must evaluate this increase against the 5 MiB target;
M12 does not hide it or reduce the JIT allocation to compensate.

## Files and repository representation

- `.gitmodules`: records the Noct upstream URL.
- `third_party/noct`: mode-160000 gitlink replacing the formerly tracked
  subtree files.
- `third_party/NOCT-SUBMODULE.md`: maintainer and source-distribution rules.
- `scripts/update-noct.sh`: explicit submodule lifecycle and verification.
- `build.sh`: exposes the Noct submodule commands.
- `bootloader/noct.mk`: records the checked-out submodule commit instead of
  parsing the removed subtree-only `UPSTREAM.md`.
- `bootloader/README.md`: documents Boots, the submodule, and Remacs policy.
- `bootloader/PC98BE-NOCT-IMPLEMENTATION-PLAN.md`: replaces the editor task,
  adds REPL/environment/licensing milestones, and records the submodule model.

Git displays the migration as deletion of the individually tracked subtree
files plus one added mode-160000 gitlink. That is the expected atomic
representation; the Noct files remain available through the pinned submodule.

## Known tradeoff

A plain non-recursive clone does not contain Noct source until `noct init` is
run. Release-source generation must vendor the initialized submodule content
or prominently require `git clone --recurse-submodules`. This is accepted in
exchange for exact upstream history and low-friction updates while Noct is
changing rapidly.
