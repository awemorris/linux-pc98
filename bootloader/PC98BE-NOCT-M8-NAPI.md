# PC98BE Noct M8 — safe native APIs

## 1. Scope

M8 adds the first useful, read-only native API surface for Noct programs in
the PC-98 Bootstrap Environment.  Scripts can now write terminal output, draw
at bounded GDC text positions, read normalized keys, inspect the selected BOOT
filesystem, import another source file, and inspect their bounded arena use.

M8 remains deliberately read-only.  It does not provide stdio, file mutation,
raw sectors, BIOS calls, physical memory, or port I/O.  Those operations remain
behind the M9 and M10 review gates.

## 2. Target abstraction

`boot98-noct-napi.[ch]` contains the target-independent Noct bindings.  It sees
only an injected `boot98_noct_services` table.  The real target adapter in
`boot98-noct-platform.c` maps that table to:

- the centralized GDC text console in `boot98-console.c`;
- the `IO.SYS` real-mode BIOS keyboard gateway;
- `boot98_filesystem`, whose registered implementation is currently FAT16.

The 32-bit host test injects deterministic screen, keyboard, file, and
directory services instead.  This keeps hardware access out of the VM glue
and makes every safe API testable without QEMU.

## 3. Native API contract

| Module | Operation | Result |
| --- | --- | --- |
| `Console` | `print(value)` | bounded serialization followed by newline |
| | `write(text)` | raw text with no implicit newline |
| `Screen` | `getWidth()`, `getHeight()` | `80`, `25` |
| | `clear()`, `clearRow(row)` | zero on success |
| | `put(row, column, text, attr)` | text cells written |
| | `setCursor(row, column)` | zero on success |
| | `showCursor(visible)` | zero on success |
| `Keyboard` | `poll()` | key code, or `-1` when empty |
| | `read()` | blocking key code |
| | `isPrintable(code)` | one for ASCII or half-width kana bytes |
| `Directory` | `list(path)` | array of entry dictionaries |
| | `stat(path)` | one entry dictionary |
| `System` | `getOSName()` | `"PC98BE"` |
| | `import(path)` | registers source in the current VM |
| | `memoryUsage()` | `{current, peak, arenaSize}` |

Directory entries contain `name`, `size`, `attributes`, and `directory`.
FAT16 currently accepts only the root path (`/` or the empty string), but the
NAPI is path-based for future filesystem drivers.  A directory listing is
bounded at 256 entries.  Source imports retain both path and source bytes
until VM destruction and reject files over 256 KiB.

`Console.print` is limited to 4096 emitted bytes, four container levels, and
64 items per container.  It streams output instead of placing the upstream
console serializer's large automatic buffer on the BE stack.

## 4. Screen and cursor rules

Positional output reuses the existing Shift-JIS-to-PC-98 character conversion.
Coordinates, attributes, and strings are checked before VRAM access.  A
double-byte character is never split at column 79.

Before a script runs, the platform records terminal state.  After every normal
or error return it selects terminal mode, chooses a valid output row no earlier
than the saved terminal position, moves to a new line when required, programs
GDC CSRFORM/CSRW, and makes the cursor visible.  This prevents an ordinary
script's output from being overwritten by the next prompt while still allowing
a full-screen program to return to a usable shell.

## 5. Stable keyboard namespace

The old gateway returned only BIOS `AL`, so cursor and function keys all became
zero.  M8 preserves ordinary byte values and maps `AL == 0` to `0x100 | AH`.
Both blocking read and polling use the same rule.

| Keys | Codes |
| --- | --- |
| Escape, Backspace, Enter | `0x1b`, `0x08`, `0x0d` |
| PageUp, PageDown | `0x136`, `0x137` |
| Insert, Delete | `0x138`, `0x139` |
| Up, Left, Right, Down | `0x13a`, `0x13b`, `0x13c`, `0x13d` |
| Home, End | `0x13e`, `0x13f` |
| F1 through F10 | `0x162` through `0x16b` |

The same values are exposed in the Noct `Key` dictionary.  Host tests cover
ordinary, known special, and full eight-bit scan values.

## 6. Ownership and cleanup

Only one Noct execution may own the native service state at a time, matching
the existing single-VM lifecycle.  Cleanup order is:

1. release pinned Noct values and JIT resources;
2. destroy the VM;
3. release imported source buffers that the VM could reference;
4. clear the active NAPI service table;
5. sample heap accounting and reset the complete script arena;
6. restore terminal mode and a visible cursor.

No imported source, dictionary, service pointer, or JIT allocation survives a
script invocation.

## 7. Verification

`./build.sh noct verify` passes the libc tests, 100-run interpreter/JIT
lifecycle corpus, M7 argument/signature corpus, M8 injected-service API script,
soft-float checks, final linked i386 opcode audit, and generated-JIT opcode
audit.

The M8 host script checks serialized values, every Screen call, poll/read and
printability, the `Key.Left` value, root listing/stat, `System.import`, imported
function execution, and a 6 MiB injected arena.  Separate negative cases check
that an out-of-range screen position, an unsupported subdirectory, and a
missing import become runtime errors and still leave an empty, valid heap.

Two QEMU tests used the same profile:

```text
qemu-pc98/build/qemu-system-i386
  -M pc9801 -cpu 386 -m 6 -accel tcg
  -L qemu-pc98/pc-bios -nic none
  -drive if=ide,bus=0,unit=0,format=raw,file=IMAGE,snapshot=on
  -display none -serial none -no-reboot
```

`BOOT-M8-QEMU.CFG` ran `M8-API.NCT` from FAT16 and produced:

```text
System.import PASS
arena=5176544
PC98BE-M8-QEMU-PASS
```

The separate `M8-KEY.NCT` blocked in `Keyboard.read()`.  QEMU monitor command
`sendkey left` passed through the PC-98 BIOS gateway as decimal `315`
(`0x13b`) and produced `PC98BE-M8-KEY-PASS`.  Both scripts returned to the BE
shell with output intact and the hardware cursor visible.  Each dedicated QEMU
instance was stopped through its own monitor; no unrelated process was
signalled.

## 8. Size and memory result

| Measurement | M8 value | Change from recorded M7 |
| --- | ---: | ---: |
| BOOT.SYS file/load-image | 225,648 bytes | +8,160 bytes |
| resident `.text` | 218,518 bytes | +7,460 bytes |
| resident `.data` | 7,120 bytes | +704 bytes |
| resident BSS | 30,928 bytes | +32 bytes |
| `__image_end` | `0x57170` | +`0x1fe0` |
| bytes before `0x60000` | 36,496 | -8,160 |
| `__bss_end` | `0x5ea50` | +`0x2000` |
| bytes before `0x70000` | 71,088 | -8,192 |
| SHA-256 of BOOT.SYS | `d0f995ea783ea1646b23d5484e0729cbe0c5e05cf1f138e8bcb9c62d87928d1a` | — |

The 256 KiB BOOT.SYS load window, low-memory BSS ceiling, 6 MiB QEMU profile,
and 192 KiB JIT code arena remain valid.

## 9. Source provenance and review boundary

The M8 binding and target adapter are new code in the Awe Morris PC-98 port,
with the copyright header recorded in each substantial new file.  They use
Noct's public embedding/NAPI functions but do not link upstream host
`api-console.c`, `api-system.c`, or host operating-system adapters.  Screen
conversion extends the preexisting PC98BE console implementation; directory
access uses the preexisting generic filesystem API; the keyboard transport is
the preexisting BIOS gateway with the loss of `AH` corrected.

M8 is ready for user review and commit.  The next milestone is M9: reviewed,
temporary-image-only BIOS and generic filesystem sector writes.  FAT metadata
writes and stdio remain M10 and must not be pulled into M9.
