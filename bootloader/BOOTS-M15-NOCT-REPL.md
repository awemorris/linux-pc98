# Boots M15: Noct REPL integration

## Result

The argument-free `noct` shell command starts an interactive Noct session.
`noct FILE.NCT [args...]` keeps its existing file-script behavior.

The integration uses the reusable REPL API introduced by Noct commit
`3d362396ac23dacab809f102373e2b1aca1ca418`. The complete verification was
run with the refreshed Noct pin
`5b5efba3adfec8d56a2c8955e1797c39d75de942`. One VM, one REPL session, the
safe Boots NAPI, and the File API are created on entry. They are all destroyed
before control returns to the Boots shell.

## Terminal behavior

- `> ` is the primary prompt and `. ` is the continuation prompt.
- Enter submits a physical line.
- Backspace edits the current line.
- ASCII Ctrl-C (`0x03`) exits the REPL, prints `^C`, restores the visible
  cursor, and returns to the Boots shell.
- The input line is limited to 255 bytes and accumulated multiline source is
  limited to 32 KiB.

Function declarations remain available for later submissions in the same
REPL session. Ordinary top-level local variables do not persist between
submissions; persistent Boots values will be supplied by the M14 environment
store.

## Verification

`./build.sh noct verify` runs the historical M4-M11 checks followed by the
M15 host and QEMU tests. The host test covers interpreter and i386 JIT
sessions, multiline definitions, syntax-error recovery, repeated lifecycle
cycles, and complete arena/JIT release. The QEMU test uses `-M pc9801 -cpu
386 -m 6 -accel tcg`, submits input through the PC-98 keyboard, recovers after
a syntax error, exits with Ctrl-C, and proves that BOOT.CFG execution resumed
afterward. A manual GUI-keyboard check covers shifted characters and
multiline entry because QMP qcode injection bypasses QEMU's PC-98 keysym
remapper and cannot currently synthesize shifted text through the compatible
BIOS keyboard service.

The Noct WebApp is intentionally not an acceptance test: it is a
multithreaded application, while Boots is a single-threaded pre-boot
environment.
