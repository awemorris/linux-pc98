# Boots BeUI G2b: PC-9821 Core-Graph / Cirrus

G2b adds the first accelerated-color display backend while preserving GDC as
the deterministic fallback. The reviewed scope is deliberately narrow:
PC-9821 Core-Graph with its internal Cirrus GD5440, 640x480, 8bpp RGB332.
Classic WAB Cirrus and independently enumerated PCI Cirrus laptops remain
separate hardware gates because their wake, aperture, LCD, and cleanup paths
are not interchangeable with Core-Graph.

## Source boundary and provenance

StratoHAL remains a separate work clone; it is not vendored or added as a
submodule. The implementation was reviewed from:

- repository: `https://github.com/awemorris/StratoHAL`
- reviewed commit: `76e909577bdf4629f11e473539b446a948fef830`
- consulted file: `src/98disp_cirrus.c`
- adapted behavior: path-08h ID validation, NEC 68h/6Ah board gate, relocated
  Cirrus register access, the recovered 640x480x8 mode stream, RGB332 DAC,
  linear-aperture selection, BLT reset, blank/unblank, and GDC restoration

`boot98-beui-pc98-cirrus.c` is an altered freestanding implementation. It
does not copy StratoHAL's DOS/4G mapping code, environment-variable
experiments, PCI enumeration, LCD paths, game backbuffer, frame loop, logging,
or CPU-source BLT implementation.

## Detection and fallback

`BeUI.init()` is still the first operation that touches display hardware.
The auto backend tries Core-Graph and then GDC:

1. Read fixed-interface register 00h at 0xFAA/0xFAB.
2. Accept only NEC path-08h IDs 58h through 5Dh.
3. Unlock the relocated sequencer and validate nonzero/non-FFh CR27.
4. Save board-side sleep/window/linear/relay registers.
5. Select the linear page and verify its readback before writing VRAM.
6. If any check fails, leave hardware unchanged and enter 640x400 GDC mode.

The generic BeUI lifecycle sees one display HAL. Its forwarding layer records
which child entered successfully, so draw, flush, and leave cannot be sent to
different adapters.

## Framebuffer and image conversion

Core-Graph register 02h is set to F0h, exposing its one-MiB linear aperture at
physical address F0000000h. Boots uses only the first 307,200 bytes as a
640-byte-stride 640x480 surface. There is no permanent software backbuffer.

The hardware DAC is programmed as RGB332. Every BeUI `0x00RRGGBB` solid color
and every INDEX8/RGB24 source-image pixel is converted when drawn:

```text
RRR GGG BB
```

Fill, Bresenham line, 8x8 pattern fill, image draw, and pattern-masked image
draw therefore share the same target-independent Noct API as GDC. `flush()`
is a no-op because drawing is direct.

## Exit behavior

`BeUI.close()` blanks the Cirrus sequencer, runs the complementary NEC
Core-Graph gate sequence, selects GDC output, restores the saved board-side
registers, and marks the backend inactive. Normal return, source/runtime
errors, and omitted script-level close still pass through the existing
per-VM cleanup path.

## Verification

```sh
make -C bootloader beui-g2b-verify -j32
```

The gate runs:

- a host register mock covering detection, 640x480 entry, framebuffer clear,
  RGB332 fill/line/image conversion, and board-register restoration;
- the complete G2a host and `pc9801` GDC fallback regression;
- the final BOOT.SYS i386 opcode policy;
- QEMU `pc9821`, `-cpu 386`, 6 MiB: the same FAT16 BMP/Noct workload through
  Core-Graph, a 640x480 screenshot with multiple colors, key input, cleanup,
  and a FAT completion marker.

QEMU verification passes. Real Core-Graph hardware remains an explicit
pending test; the backend intentionally does not claim WAB or PCI-laptop
compatibility from the QEMU result.
