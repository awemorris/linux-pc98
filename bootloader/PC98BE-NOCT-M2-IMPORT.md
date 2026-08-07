# PC-98 Bootstrap Environment: M2 Noct Import

Status: **M2 IMPLEMENTED — AWAITING USER REVIEW**

Recorded on 2026-08-07 for M2 of
`PC98BE-NOCT-IMPLEMENTATION-PLAN.md`.  This milestone imports the approved
Noct source and establishes its offline object build.  It does not link Noct
into `BOOT.SYS`, add a runtime heap, or execute Noct code.

## 1. Imported source identity

| Item | Value |
|---|---|
| linux-pc98 starting commit | `3113921e402cbb30f3d5dc3b0266c9c0c4c78906` |
| Noct origin | `https://github.com/awemorris/NoctLang.git` |
| Approved Noct commit | `86079e47b8430a9fce4c67fab584499a3531658e` |
| Commit subject | `Add PC98BE target` |
| Imported path | `third_party/noct` |
| Files from upstream | 280 |
| Snapshot content SHA-256 | `974632b64a40a9128270a37fff13fef7f1d8f8ffd598da53b5014913cfb4671a` |
| License | zlib, retained as `third_party/noct/LICENSE` |

The source was read from the exact Git object fetched from the official
origin.  `diff -qr`, excluding `.git` and the linux-pc98-owned
`UPSTREAM.md`, reports no difference from `~/NoctLang` at the approved
commit.  `scripts/update-noct.sh verify` independently recomputes the
content hash, checks the license and metadata, and rejects generated build
files inside the imported directory.

The initial tree was placed in the review index without making an automatic
commit.  The user-created M2 commit must carry these trailers, which are the
metadata used by later `git subtree pull --squash` operations:

```text
git-subtree-dir: third_party/noct
git-subtree-split: 86079e47b8430a9fce4c67fab584499a3531658e
```

Future imports and updates are explicit maintainer operations through
`scripts/update-noct.sh`.  Ordinary bootloader, image, test, and release
builds never fetch Noct from the network.

## 2. Object-build boundary

`bootloader/noct.mk` names the same 13 core translation units used by the
approved upstream PC98BE object preset:

```text
lexer.yy.c       parser.tab.c      ast.c
hir.c            lir.c             noct.c
runtime.c        interpreter.c     jit.c (includes jit-x86.c)
execution.c      gc.c              intrinsics.c
objectmodel-st.c
```

CLI, REPL, i18n, optional backends, multithreaded object model, and standard
System/Console/File APIs are not compiled.  Generated lexer and parser C
sources are consumed directly, so the normal object build does not require
Flex or Bison.  Objects are written only below `build/bootloader/noct/`,
which is already ignored by Git.

The object compile uses `NOCT_TARGET_PC98BE`, `NOCT_MEMORY_SMALL`, and the
i386 JIT.  It uses the freestanding i386, no-x87, no-MMX, and no-SSE flags
specified by the plan.  `-fno-strict-aliasing` is also required because the
approved LIR source intentionally reads floating-point bit representations
through integer pointer types.

The Debian host dependency inventory now includes `gcc-multilib` and
`libc6-dev-i386`.  These headers are sufficient for the object-only M2
compile; they are not the BE C runtime.  M3 supplies the freestanding headers
and implementations used for the final link.

## 3. Verification results

| Check | Result |
|---|---|
| Shell syntax for `build.sh` and `update-noct.sh` | PASS |
| Snapshot metadata, zlib license, and source hash | PASS |
| Exact source comparison with approved Noct tree | PASS |
| Clean object build in an isolated network namespace | PASS |
| All 13 outputs are ELF32 Intel i386 relocatables | PASS |
| i486+/x87/MMX/SSE object-code rejection scan | PASS |
| Generated objects under `third_party/noct` | none |
| Existing BOOT98 FAT16 host test | PASS |
| Existing `BOOT.SYS` SHA-256 before and after M2 | unchanged |

The offline test was performed after deleting the object output and entering
a new user and network namespace:

```sh
./build.sh noct clean
unshare --user --map-root-user --net ./build.sh noct verify
```

The object total reported by GNU `size --totals` is:

```text
   text    data     bss     dec     hex
 128454    6380   20172  155006   25d7e
```

This is not a final linked image size.  No M2 target adds these objects to
`STAGE2_OBJS`.  The existing BOOT.SYS remained byte-for-byte equal to the M0
baseline:

```text
b4ea7d4e43b1442eb48cbb423454f25606ff275d49b68aadc0dabc4caa2dd81b
```

## 4. Upstream warning inventory before M3

The approved source has six warnings under the plan's release flags plus
`-Wall -Wextra -Werror`.  M2 keeps `-Werror` for all other diagnostics and
leaves these exact warnings visible through file-local `-Wno-error` options.
It does not suppress their text or modify the imported source.

| File | Diagnostic |
|---|---|
| `noct.c` | unused public-API `env` parameter |
| `runtime.c` | packed-copy `size` may be uninitialized |
| `jit-x86.c` via `jit.c` | unused `env` parameter |
| `jit-x86.c` via `jit.c` | signed/unsigned loop comparison |
| `intrinsics.c` | negative-start comparison is ineffective with 32-bit `size_t` |
| `objectmodel-st.c` | dictionary `is_insertion` may be uninitialized |

The latter runtime, intrinsic, and object-model diagnostics may identify real
edge-case defects, not merely style.  They must be fixed and tested in the
upstream Noct repository before M3 links the core into BOOT.SYS.  The
file-local exceptions are therefore an M2 source-immutability bridge, not a
permanent warning policy.

The imported upstream tree also contains pre-existing whitespace diagnostics
reported by `git diff --cached --check`.  They are retained intentionally so
the vendor snapshot remains byte-for-byte identical.  Integration files
outside `third_party/noct` pass `git diff --check`.

## 5. Review and commit boundary

Useful review commands are:

```sh
cd ~/linux-pc98
./scripts/update-noct.sh status
./scripts/update-noct.sh verify
./build.sh noct clean
./build.sh noct verify
diff -qr --exclude=.git --exclude=UPSTREAM.md ~/NoctLang third_party/noct
git diff --cached --stat
git diff -- . ':(exclude)third_party/noct'
```

After review, include the subtree trailers when creating the M2 commit.  Do
not begin M3 until this milestone and the warning-resolution direction have
been accepted.
