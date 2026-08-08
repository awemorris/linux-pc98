# Boots M14 environment and convenience functions

Status: implemented and verified; awaiting user review.

## Persistent environment

Boots owns one `struct boot98_environment` for its complete lifetime. It is
outside the Noct arena and is initialized once when `boot98_main()` starts.
Destroying a file-script VM or REPL VM therefore cannot invalidate or erase an
environment value.

The store is a packed sequence of `NAME\0VALUE\0` records with these bounds:

- 4096 bytes of record storage;
- 32 entries;
- 31-byte names matching `[A-Za-z_][A-Za-z0-9_]*`;
- 255-byte values.

An insertion or replacement that exceeds a bound fails before modifying the
old store. No Noct-owned pointer is retained.

## Shell and BOOT.CFG

```text
env
set NAME VALUE
unset NAME
```

`env` prints insertion-ordered `NAME=VALUE` lines. `set` accepts the existing
shell quoting syntax, so an empty or space-containing value can be quoted.
`unset` is idempotent for a valid name.

## Noct API

```text
System.getEnv(name)          # copied string; "" when absent
System.setEnv(name, value)   # 0 on success, runtime error on rejection
System.unsetEnv(name)        # 0; absent valid names are accepted
System.listEnv()             # new dictionary containing copied strings
```

The same environment pointer is supplied to ordinary scripts and the REPL.
Every value crossing into Noct is copied into the current VM.

## Convenience globals

Boots now has a table-driven intrinsic-style global registration pass:

```text
print(value)     # same implementation as Console.print(value)
gets()           # same implementation as Console.gets()
```

`gets()` is not the unsafe C library function. It accepts at most 255 printable
ASCII bytes, echoes input, handles Backspace, returns on Enter, and returns an
empty string after Ctrl-C. `Console.gets()` exposes the identical function.

## Verification

- `boot98-env-host-test` covers create, replace, delete, enumeration, invalid
  names, value bounds, 32-entry exhaustion, 4 KiB exhaustion, and atomic failed
  replacement.
- `boot98-noct-host-test` covers `print()`, `gets()`, cursor visibility, all
  four `System` environment calls, copied dictionaries, and persistence across
  two separately created VMs.
- `test-boot98-noct-env.sh` boots QEMU with `-cpu 386`, executes shell `set`,
  `unset`, and `env` from BOOT.CFG, observes those values in two separate Noct
  VMs, writes a FAT16 marker, and verifies the marker from the host.
- The normal final i386 opcode audit and M4-M15 regression chain remain part of
  `./build.sh noct verify`.
