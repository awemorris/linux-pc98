# Boots BeUI G1: lifecycle and HAL boundary

This review gate introduces the opt-in graphical-environment boundary. It does
not import or execute PC-98 display-register sequences.

## Result

- `boot98-beui.[ch]` owns one allocation-free lifecycle.
- Merely creating a Noct VM only binds a HAL pointer; it does not probe or
  change graphics, pointer, or audio hardware.
- `BeUI.init()` enters the display and then starts the pointer backend.
- Initialization failure and every close path unwind in reverse order.
- `boot98_noct_napi_cleanup()` forces BeUI closed after normal return, runtime
  error, source error, or an omitted script-level `BeUI.close()`.
- Repeated `init` and `close` calls are idempotent and do not invoke a backend
  twice.
- Audio is represented in the ABI but is not started by the G1 default. WSS
  policy belongs to G2.

## HAL boundary

The root `boot98_beui_hal` contains separate display, glyph, pointer, clock,
and audio interfaces. The display backend owns mode save/restore through
`enter` and `leave`. Widgets and Noct code must not access PC-98 registers.
Dirty-rectangle display flushing is represented now so G2/G3 need not break
the lifecycle ABI.

StratoHAL is the intended hardware provenance. G2 uses a separate work clone
and imports only the reviewed register-level behavior needed by each backend;
StratoHAL is not a submodule and its complete tree is not vendored. G1 itself
contains no copied StratoHAL code, DOS/4G runtime, or game-loop policy.

## Noct API

```text
BeUI.init()
BeUI.close()
BeUI.isOpen()
BeUI.getWidth()
BeUI.getHeight()
BeUI.poll()
BeUI.flush()
```

Without a bound display backend, `BeUI.init()` raises a clear unavailable
error. This is the expected PC-98 behavior until G2 is reviewed.

## Verification

```sh
make -C bootloader beui-g1-verify -j32
```

The dedicated C test covers absent HAL, idempotency, dimensions, poll/flush,
normal close, and partial-initialization unwind. The Noct lifecycle test covers
explicit close, omitted close, and runtime-error cleanup under interpreter and
JIT operation. The final BOOT.SYS opcode audit remains i386-clean.

At this gate `BOOT.SYS` is 288,692 bytes, below its 320 KiB limit.

## Next gate

G2 records the exact StratoHAL work-clone revision, audits every file-level
adaptation, and implements GDC safe mode before Cirrus. Trident, shared
Unicode/JIS glyph drawing, the polled PC-98 bus mouse, and WSS then complete
the hardware HAL.
