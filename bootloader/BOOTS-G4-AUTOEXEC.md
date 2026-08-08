# Boots G4: AUTOEXEC graphical action handoff

G4 connects the graphical Noct startup program to the existing Boots command
dispatcher.  It does not jump from a live Noct VM into an operating-system
kernel or another application.

After fixed-disk probing and the three-second uncancelled timeout, Stage 2
mounts the selected `BOOT` partition.  If `AUTOEXEC.NCT` exists, Boots creates
one profiled Noct VM and runs that file.  The script may initialize BeUI and
present a keyboard-operated menu.  A selection is returned by writing one
bounded command line to the persistent environment variable `BOOT_ACTION`.

On script return Boots performs these operations in order:

1. destroy the Noct VM and its arena;
2. force BeUI closed and restore the text console;
3. copy and unset `BOOT_ACTION`;
4. reject empty strings, control characters, and overlong command lines;
5. execute the single line with the ordinary Boots command dispatcher.

`bootloader/AUTOEXEC.NCT` supplies the current Cockpit-inspired menu.  Its
Emacs entry writes `emacs EDIT.TXT`, returns, and therefore launches the
packaged `CMD/REMACS.NB` only after graphics and the menu VM have ended.  The
Linux entry writes `source BOOT.CFG`; the shell entry returns with a harmless
`echo` command.

Compatibility is preserved: a BOOT volume without `AUTOEXEC.NCT` continues
to execute `BOOT.CFG` exactly as before.  A present but failed script or an
invalid/missing action reports the error and falls back to the text shell.

The QEMU integration test is:

```sh
make -C bootloader boot98-autoexec-remacs-qemu-test
```

It boots through the real automatic timeout, selects Emacs in BeUI, edits and
saves a UTF-8 Japanese FAT16 file in Remacs, verifies both screenshots, and
checks the saved file.  `test-boot98-term-japanese.sh` separately verifies the
actual PC-98 text VRAM words for `Term.write("日本語")`.
