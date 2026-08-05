/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Generic PC-98 disk IPL selector.
 * Copyright (C) 2026 Awe Morris
 *
 * The ROM loads this 512-byte sector at 1fc0:0000.  It relocates itself and,
 * without changing the display or waiting for input, loads exactly one
 * sector from LBA 2 of the current fixed disk back at the firmware load
 * address.  Code installed at LBA 2 is therefore independent of BOOT98 and
 * may implement its own continuation-sector format.
 */

	.code16
	.section .text
	.globl _start

	.set RELOC_SEG,       0x9000
	.set LOAD_SEG,        0x1fc0
	.set HANDOFF,         0x0700
	.set HANDOFF_MAGIC,   0x4839384c /* "L98H" */

_start:
	jmp	relocate
	nop
	nop
	.ascii	"IPL1"
	.byte	0, 0, 0
	.byte	0x1e

relocate:
	cli
	cld
	pushw	%cs
	popw	%ds
	movw	%si, saved_si
	movw	%di, saved_di
	movw	$RELOC_SEG, %ax
	movw	%ax, %es
	xorw	%si, %si
	xorw	%di, %di
	movw	$256, %cx
	rep movsw
	ljmp	$RELOC_SEG, $relocated

relocated:
	pushw	%cs
	popw	%ds
	sti
	/* The fixed-disk IPL has one job: enter the installed LBA 2 loader. */
	pushw	%ds
	xorw	%ax, %ax
	movw	%ax, %ds
	movb	0x0584, %al
	popw	%ds
	andb	$0x7f, %al
	orb	$0x80, %al
	movb	%al, boot_id
	call	write_handoff
	call	prepare_target
	xorw	%ax, %ax
	movw	%ax, %ds
	movb	%cs:boot_id, %al
	movb	$0x06, %ah
	movw	$512, %bx
	xorw	%cx, %cx
	movw	$0x0002, %dx /* C=0, H=0, zero-based sector 2 */
	movw	%cs:saved_si, %si
	movw	%cs:saved_di, %di
	int	$0x1b
	jc	boot_failure

enter_target:
	xorw	%ax, %ax
	movw	%ax, %ds
	movb	%cs:boot_id, %al
	movb	%al, 0x0584
	movw	%cs:saved_si, %si
	movw	%cs:saved_di, %di
	sti
	ljmp	$LOAD_SEG, $0

prepare_target:
	movw	$LOAD_SEG, %ax
	movw	%ax, %es
	xorw	%bp, %bp
	ret

write_handoff:
	pushw	%es
	xorw	%ax, %ax
	movw	%ax, %es
	movl	$0, %es:HANDOFF
	movl	$0, %es:HANDOFF+4
	movl	$0, %es:HANDOFF+8
	movl	$0, %es:HANDOFF+12
	movl	$HANDOFF_MAGIC, %es:HANDOFF
	movb	%cs:boot_id, %al
	movb	%al, %es:HANDOFF+8
	movb	$1, %es:HANDOFF+11
	movw	%cs:saved_si, %ax
	movw	%ax, %es:HANDOFF+12
	movw	%cs:saved_di, %ax
	movw	%ax, %es:HANDOFF+14
	popw	%es
	ret

boot_failure:
	movb	$0x06, %al
	outb	%al, $0x37
1:
	hlt
	jmp	1b

saved_si:
	.word	0
saved_di:
	.word	0
boot_id:
	.byte	0

	.org	0x01fc
	.word	9
	.word	0xaa55
