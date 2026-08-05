/*
 * PC-9800 Bootloader
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/* BOOT98 1.44 MiB floppy Stage 0: load sectors 3..16 at 1000:0000. */
	.code16
	.section .text
	.globl _start
	.set HANDOFF, 0x0700
	.set HANDOFF_MAGIC, 0x4839384c
_start:
	jmp	start
	nop
	nop
	.ascii	"IPL1"
	.byte	0, 0, 0, 0x1e
start:
	cld
	pushw	%ds
	xorw	%ax, %ax
	movw	%ax, %ds
	movw	%si, HANDOFF+12
	movw	%di, HANDOFF+14
	movl	$HANDOFF_MAGIC, HANDOFF
	movb	0x0584, %al
	andb	$0x0f, %al
	orb	$0x30, %al
	movb	%al, HANDOFF+8
	movb	$2, HANDOFF+9
	movb	$18, HANDOFF+10
	movb	$1, HANDOFF+11
	popw	%ds
	movw	$0x1000, %bx
	movw	%bx, %es
	xorw	%bp, %bp
	movw	$(14 * 512), %bx
	movw	$0x0200, %cx
	movw	$0x0003, %dx
	movb	$0x56, %ah
	int	$0x1b
	jc	failure
	ljmp	$0x1000, $0
failure:
	hlt
	jmp	failure
	.org 510
	.word 0xaa55
