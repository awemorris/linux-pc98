# Boots BeUI G2c: CGROM text and keyboard menu

G2c adds text rendering to BeUI and verifies a keyboard-only graphical menu
on both the PC-9821 Core-Graph/Cirrus path and the 4bpp GDC safe-mode path.
The test menu uses a compact Cockpit-inspired dark layout: header accent,
navigation sidebar, main action cards, and a persistent key legend. It also
reserves an Emacs entry for the Remacs bytecode command planned for a later
milestone.

## Font source and hardware sequence

The glyph backend reads the PC-98 character-generator ROM. Its hardware
sequence follows the proven implementation reviewed from StratoHAL commit
`76e909577bdf4629f11e473539b446a948fef830`, file `src/98glyph.c`:

1. wait for vertical blank at port 60h;
2. select font access with port 68h command 0Bh;
3. program the A1h/A3h/A5h glyph selector, including the special row banks;
4. copy the glyph through the A4000h character-generator window;
5. restore display access with port 68h command 0Ah.

This sequence is retained for real hardware rather than inferred from QEMU.
The backend shares Noct's JIS X 0208 table for Unicode-to-JIS lookup and keeps
a 64-entry glyph cache. Repeated menu redraws therefore do not repeatedly
stop at vertical blank for the same character.

## API and drawing behavior

Noct exposes `BeUI.textWidth`, `BeUI.textHeight`, and `BeUI.drawText`.
`drawText` accepts UTF-8, advances by 8 pixels for single-byte glyphs and 16
pixels for double-byte glyphs, and substitutes `?` for an unsupported code
point. No heap allocation is required while decoding a string.

For the 4bpp GDC path, RGB colors are converted to native B/R/G/I planes.
Each component uses a half-range threshold. Intensity uses the integer-only,
green-weighted luminance `(R + 2G + B) / 4`, also at half range. This improves
readability over testing the bitwise OR of the components. Neutral dark gray
still becomes black in this fixed RGBI mapping; the menu remains readable
without forcing the whole design to monochrome.

## Incremental-build safety

The glyph cache enlarges `struct boot98_beui_pc98_auto`. `boot98-stage2.o`
therefore explicitly depends on `boot98-beui-pc98-glyph.h`. Without that
dependency, an incremental build could link a caller compiled with the old
structure size to a callee compiled with the new size and corrupt adjacent
global state during initialization.

## Verification

Run the complete gate with:

```sh
make -C bootloader beui-g2c-verify -j32
```

The two final QEMU workloads use `-cpu 386`, 6 MiB RAM, FAT16 Noct source,
and PC-98 CGROM glyphs. Each workload draws the menu, sends the Down key,
captures the selected action, sends Enter, and checks a completion marker in
the BOOT volume. The Core-Graph run validates the 640x480 RGB332 layout; the
PC-9801 run validates the 640x400 RGBI-quantized fallback.
