# Boots BeUI G2a: PC-98 GDC and BMP image path

This review gate makes `BeUI.init()` useful on every supported PC-98 by adding
the 640x400 16-color GDC fallback. It also introduces the target-independent
image object and a BMP-only decoder. Cirrus, Trident, the font service, bus
mouse, and WSS remain later G2 gates.

## Source boundary and provenance

StratoHAL is **not** a linux-pc98 submodule and its full source tree is not
vendored. The implementation was reviewed from a separate work clone:

- repository: `https://github.com/awemorris/StratoHAL`
- reviewed commit: `76e909577bdf4629f11e473539b446a948fef830`
- consulted file: `src/98disp_gdc.c`
- consulted behavior: BIOS GDC mode setup, RGBI plane addresses, text display
  disable/enable commands, plane clearing, and RGB-to-PC-98 color thresholds

`boot98-beui-pc98.c` is a Boots-specific freestanding adaptation. It does not
copy the StratoHAL DOS/4G runtime, game loop, full-frame redraw policy, PNG
loader, or other display/sound backends. Its header preserves the applicable
copyright notices for Awe Morris and Keiichi Tabata and identifies the exact
reviewed source revision.

## Image representation

BeUI exposes two storage formats independent of the target framebuffer:

| Format | In-memory pixels | Source BMP |
| --- | --- | --- |
| `INDEX8` | one palette index byte per pixel plus up to 256 RGB entries | 1, 4, or 8bpp BI_RGB |
| `RGB24` | tightly packed R, G, B bytes | 24bpp BI_RGB |

The decoder accepts bottom-up and top-down Windows BMP with a DIB header of at
least 40 bytes. It rejects compression, unsupported depths, invalid palettes,
integer overflow, truncated rows, and invalid offsets. It performs no
allocation and has no PNG, zlib, or gzip dependency.

The display backend converts at draw time. GDC maps RGB to its B/R/G/I planes
using the reviewed StratoHAL thresholds. Future Cirrus and Trident backends
may copy or convert the same image object to 8/15/16/24/32bpp without changing
Noct or the BMP decoder.

## Noct API

```text
BeUI.fill(x, y, width, height, rgb)
BeUI.line(x0, y0, x1, y1, rgb)
BeUI.patternFill(x, y, width, height, rgb, pattern)
BeUI.loadImage(path)                 -> integer handle
BeUI.drawImage(handle, x, y)
BeUI.drawImagePattern(handle, x, y, pattern)
BeUI.destroyImage(handle)
```

Colors use `0x00RRGGBB`. Image handles are owned by one Noct VM. Explicit
destroy is supported; VM cleanup also releases every remaining image and
restores text mode after normal return or an exception. Source and decoded
pixel allocations are each limited to 2 MiB, and ordinary allocation failure
is reported to the script.

The low-memory drawing primitives write directly to the selected framebuffer;
they do not allocate a backing image. A pattern is a Noct 64-bit `long`
containing an 8x8 one-bit mask. The low byte is row 0 and bit 7 is the leftmost
pixel of each row. Mask zero preserves the destination; mask one draws the
solid color or source-image pixel. Coordinates repeat the pattern every eight
pixels. `drawImage()` is the all-bits-set form of `drawImagePattern()`.

## PC-98 integration

Stage 2 binds one GDC HAL but does not touch graphics while showing the text
boot menu or merely creating a Noct VM. `BeUI.init()` invokes the existing
real-mode BIOS display-reset gateway, selects 16-color GDC access, clears the
four graphics planes, and hides text display. `BeUI.close()` invokes
INT 18h/AH=41h through the real-mode gateway, clears graphics, and re-enables
the preserved text VRAM. Draw and fill operations update GDC
VRAM directly, so `flush()` is intentionally a no-op.

## Verification

```sh
make -C bootloader beui-g2a-verify -j32
```

This gate verifies:

- malformed and valid 1/4/8/24bpp BMP decoding on the host;
- GDC color conversion, plane masks, fill, image drawing, and text-mode
  restoration with an injectable in-memory backend;
- Noct image-handle creation/use/destruction and error cleanup;
- final BOOT.SYS i386 opcode policy;
- QEMU `pc9801`, `-cpu 386`, 6 MiB: FAT16 8bpp BMP load, visible GDC drawing,
  screenshot color validation, key input, text restoration, and FAT marker.

At this gate `BOOT.SYS` is 295,220 bytes.
