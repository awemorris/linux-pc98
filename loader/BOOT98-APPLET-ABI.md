BOOT98 applet ABI version 1
===========================

BOOT98 applets are trusted 32-bit i386 images loaded at physical address
`0x50000`. They execute in BOOT98's flat protected mode and return to the
shell. They are distinct from real-mode IPLware modules.

The 36-byte packed header is `struct boot98_applet_header` in
`boot98-abi.h`. Version 1 requires magic `B98A`, ABI version 1, header size
36, an exact file size, and an entry offset within the image. `crc32` is the
IEEE CRC-32 of the complete file while bytes 16-19 (the CRC field) are
treated as zero. The loader rejects an invalid header, size, entry, or CRC.

The entry uses the i386 System V C calling convention:

```c
uint32_t entry(const struct boot98_applet_services *services,
               uint32_t argc, const char *const *argv);
```

Returning zero means success. Version 1 exposes console character/string
output and blocking keyboard input. Service-table growth is append-only;
applets must check both `abi_version` and `size`. Applets are non-resident,
must preserve the normal C callee-saved registers, and may use hardware
directly. The ABI is a corruption/compatibility boundary, not a security
boundary.

`BOOTAPP.BIN` is the in-tree ABI self-test. `patch-boot98-applet.py` patches
its CRC after linking.

