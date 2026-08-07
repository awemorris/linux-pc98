# PC98BE Noct M7 — FAT16 source loading and shell resolution

## 1. Scope

M7 turns the embedded M6 Noct runtime into a command interpreter for `.NCT`
files stored on the currently selected BOOT filesystem.  It adds bounded
source loading, zero- or one-parameter `main`, shell arguments, integer exit
status, explicit `noct` execution, implicit command lookup, and the first
installed example `HELLO.NCT`.

M7 deliberately does not add file APIs, directory APIs, keyboard APIs, screen
APIs, or writable filesystems.  Those remain M8 through M10.  The filesystem
used here is the existing generic `boot98_filesystem` interface; the mounted
implementation is currently FAT16, so later filesystem drivers do not need a
Noct-specific loader.

## 2. Command rules

The shell resolves commands in this order:

1. Existing C built-ins, including a built-in whose arguments are invalid.
2. `noct FILE.NCT [args...]`, which opens the supplied path explicitly.
3. An otherwise unknown unqualified body such as `hello`: ASCII letters are
   uppercased and `.NCT` is appended, producing `HELLO.NCT` in the selected
   BOOT filesystem.
4. The original unknown-command failure.

Names containing `.`, `/`, or `\\` are not candidates for implicit lookup.
The existing shell tokenizer limits a command to 20 tokens, below Noct's
`NOCT_ARG_MAX` of 32.  Script arguments exclude both the shell command name
and the explicit source filename.

Examples:

```text
noct HELLO.NCT alpha beta
hello alpha beta
```

Both invoke `main(args)` with `args == ["alpha", "beta"]`.  A script may
instead define `main()` and ignore shell arguments.  Any `main` with more than
one parameter is rejected before execution.  An integer or long return value
becomes the script status; zero and a non-integer/no explicit return are
successful, while a nonzero integer returns command failure to the shell.

## 3. Source and arena ownership

`boot98_noct_run_file()` performs the target-side operation:

1. Open through `boot98_fs_open()`.
2. Reject a source larger than 256 KiB before allocating or reading it.
3. Enable high memory and calculate the same bounded script arena used by M6.
4. Reserve an aligned source region at the *top* of that arena.
5. Read the complete file, append a NUL byte, and exclude the source region
   from the heap passed to Noct.
6. Run the VM/JIT, destroy it, reset the heap, and restore the visible cursor.

This arrangement keeps the compiler's source pointer valid for the complete
VM lifetime without copying it into a GC allocation or allowing the Noct heap
to overwrite it.  At least 2 MiB must remain for the Noct heap after the source
reservation.  As in M6, the script/JIT arena is discarded wholesale on return.

## 4. Noct entry contract

`boot98_noct_run_args()` preserves `boot98_noct_run()` as a zero-argument
compatibility wrapper and adds the following checked sequence:

1. register the source;
2. resolve global `main` and verify it is a function;
3. query its parameter count;
4. call `main()` for zero parameters, or create one pinned Noct array and call
   `main(args)` for one parameter;
5. reject two or more parameters with `BOOT98_NOCT_SIGNATURE_ERROR`;
6. record an integer/long return in `result.script_status`;
7. unpin argument values before VM destruction and the existing heap reset.

Compiler/runtime diagnostics retain the `file:line: message` format from M6.
Target-side open/read/size errors name the source path and return to the shell.

## 5. Installed example and distribution path

`bootloader/HELLO.NCT` prints its arguments through `Console.write` and returns
zero.  `scripts/install-boot98-image.sh` copies it into every newly installed
BOOT filesystem.  `scripts/build-bootloader-dist.sh` includes it in
`bootloader.zip` beside BOOT.SYS and the existing installation files.

Noct currently preserves backslash escapes in string literals rather than
turning `\\n` into a newline.  `HELLO.NCT` therefore does not fake line
handling in the low-level `Console.write` API.  M8's higher-level console API
will add an explicit print operation.

## 6. Verification

### 6.1 Host lifecycle and API test

`./build.sh noct verify` passes.  The extended 32-bit host test covers:

- the existing 100-run interpreter/JIT parity and heap-reset corpus;
- `main(args)` receiving two strings in order;
- a zero-parameter `main()` safely ignoring supplied shell arguments;
- integer and long return statuses of 7 and 9;
- rejection of `main(first, second)`;
- syntax and runtime errors;
- static and generated i386 opcode audits.

### 6.2 QEMU FAT16 tests

All target tests used:

```text
qemu-pc98/build/qemu-system-i386
  -M pc9801 -cpu 386 -m 6 -accel tcg
  -L qemu-pc98/pc-bios -nic none
  -drive if=ide,bus=0,unit=0,format=raw,file=IMAGE,snapshot=on
  -display none -serial none -no-reboot
```

The success image ran explicit and implicit `HELLO.NCT` commands with
arguments.  A deliberately present `DISK.NCT` was not executed: the built-in
`disk` command won and printed `ide0`.  The final marker was:

```text
PC98BE M7 FAT16 command test
Hello from HELLO.NCT explicit a b
Hello from HELLO.NCT implicit one two
ide0
PC98BE-M7-QEMU-PASS
```

Separate FAT16 images verified all failure paths and shell recovery:

| Case | Target output | Result |
| --- | --- | --- |
| absent source | `Noct: file not found: MISSING.NCT` | BOOT.CFG stops; `ide0:BOOT ok` shell returns |
| 256 KiB + 1 byte | `Noct: source exceeds 256 KiB: LARGE.NCT` | BOOT.CFG stops; shell returns |
| `main(a, b)` | `Noct main error: main must accept zero or one argument` | VM is destroyed; shell returns |

QEMU was terminated only through each instance's own monitor after capturing
its screen; no unrelated process was signalled.

## 7. Size and memory result

| Measurement | M7 value | Change from recorded M6 |
| --- | ---: | ---: |
| BOOT.SYS file/load-image | 217,488 bytes | +1,632 bytes |
| resident `.text` | 211,058 bytes | +1,623 bytes |
| resident `.data` | 6,416 bytes | 0 |
| resident BSS | 30,896 bytes | 0 |
| `__image_end` | `0x55190` | +`0x660` |
| bytes before `0x60000` | 44,656 | -1,632 |
| `__bss_end` | `0x5ca50` | +`0x660` |
| bytes before `0x70000` | 79,280 | -1,632 |
| SHA-256 of BOOT.SYS | `d0611c70ff7102f6fedfeeb9defdf10eceab448d52acc3c4d10140c529b409e1` | — |

The 256 KiB BOOT.SYS load window, low-memory BSS ceiling, 6 MiB runtime test,
and 192 KiB JIT code arena all remain valid.

## 8. Review boundary

M7 is ready for source review and commit.  The next milestone is M8: safe
console/screen/keyboard/directory NAPI.  M8 may replace the demonstration's
low-level write-only presentation, but must preserve M7 command precedence,
source bounds, signature checks, and cleanup behavior.
