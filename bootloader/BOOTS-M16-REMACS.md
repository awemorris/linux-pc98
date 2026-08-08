# Boots M16: Remacs integration

M16 integrates the separately maintained Remacs editor as a single Noct
bytecode application. Boots does not import or duplicate the Remacs C
launcher. The editor sources remain in the pinned `third_party/remacs`
submodule and the release image contains only `CMD/REMACS.NB`.

## Pinned inputs

| Project | Revision | Purpose |
| --- | --- | --- |
| NoctLang | `887bf89` | target-neutral Term/directory backends and bytecode path fix |
| Remacs | `0084533` | single-file bytecode bundle plus current editor sources |

The Remacs nested Noct submodule is not initialized by the Boots build.
`third_party/noct` is the only VM/compiler revision used. Before advancing
either pin, fetch both upstream repositories, confirm that Remacs's requested
Noct revision is an ancestor of the selected Noct revision, rebuild the
bundle, and run the tests below. NoctLang changes are committed and pushed in
NoctLang before the linux-pc98 gitlink is advanced.

## Implemented portability surface

- Noct exposes a target-neutral callback `Term` backend. Boots implements
  terminal open/close, 80x25 size, cursor positioning and visibility, style,
  UTF-8 output, flush, normalized PC-98 keys, timed reads, and pending input.
- Unicode output is decoded once and mapped to PC-98 JIS glyph codes while
  ASCII and half-width katakana retain the console path.
- Noct exposes target-neutral directory enumeration. Boots maps it to the
  selected filesystem and FAT16 implements nested path traversal.
- Freestanding stdio and `FileUtil` provide the read/write/check/list calls
  used by Remacs. FAT16 can create, truncate, extend, reopen, and enumerate
  files in `CMD` and the BOOT root.
- Boots accepts Noct source or bytecode under one profiled VM lifecycle.
  `emacs [FILE]` loads `CMD/REMACS.NB`, passes the optional file argument,
  restores the console on return/error, and destroys the complete arena.
- Generic unknown commands resolve to `CMD/NAME.NCT`, with the BOOT root as a
  compatibility fallback. The editor remains bytecode-only.

Remacs's optional `M-x shell` and GUD commands use Noct `Process.*` to spawn
and control host OS processes. Boots has no process model, executable loader,
or child-process isolation, so registering a fake Process API would be
misleading and unsafe. These optional commands are intentionally unavailable;
they are not required for normal editing, file completion, search, replace,
or save/exit operation.

## Reproducible build and image layout

```sh
git submodule update --init third_party/noct third_party/remacs
./build.sh remacs
./build.sh bootloader-dist
```

`./build.sh remacs` builds the host Noct compiler from the pinned source,
invokes Remacs `tools/build-nb.sh`, verifies the portable `Noct Bytecode`
header, and atomically writes:

```text
build/bootloader/remacs/REMACS.NB
```

Image installation creates `CMD` and copies `CP.NCT`, `LS.NCT`, and
`REMACS.NB` there. `build/releases/bootloader.zip` carries the same layout.

## Memory result

The current bundle is 187,612 bytes. All adaptive profiles permit a bounded
256 KiB source/bytecode input, including the 5 MiB profile. That input buffer
exists only during application loading and does not enlarge the fixed GC or
JIT spaces. Remacs reached its full-screen mode, edited a file, and returned
under `-M pc9801 -cpu 386 -m 5 -accel tcg`.

## Verification

```sh
make -C bootloader noct-m17-verify -j32
BOOT98_TEST_MEMORY_MIB=5 ./build.sh remacs-test
BOOT98_TEST_MEMORY_MIB=16 ./build.sh remacs-test
./build.sh bootloader-dist
unzip -t build/releases/bootloader.zip
```

The Remacs smoke test installs the current BOOT.SYS and bundle into a copied
canonical image, launches the editor, inserts text, invokes save and exit
through `M-x`, verifies the FAT16 file contents, and detects the reverse-video
mode line in a QEMU screenshot. QEMU's synthetic PC-98 input path does not
currently deliver `C-x` reliably, so `M-x` reaches the identical commands;
the target-neutral Term host test separately covers Ctrl-key translation.

