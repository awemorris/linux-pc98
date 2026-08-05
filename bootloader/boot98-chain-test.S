/*
 * PC-9800 Bootloader
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/* Test-only PBR proving that BOOT98 Stage 1 transferred control. */

	.code16
	.section .text
	.globl _start
_start:
	cli
	cld
	movw	$0x9000, %ax
	movw	%ax, %ss
	movw	$0xfffe, %sp
	sti
	pushw	%cs
	popw	%ds
	movb	$0x42, %ah
	movb	$0xc0, %ch
	int	$0x18
	movb	$0x40, %ah
	int	$0x18
	movw	$0xa000, %ax
	movw	%ax, %es
	movw	$0xa200, %ax
	movw	%ax, %gs
	xorw	%di, %di
	movw	$message, %si
1:
	lodsb
	testb	%al, %al
	jz	2f
	movb	%al, %es:(%di)
	movb	$0, %es:1(%di)
	movb	$0xe1, %gs:(%di)
	addw	$2, %di
	jmp	1b
2:
	movw	$drive_label, %si
3:
	lodsb
	testb	%al, %al
	jz	4f
	movb	%al, %es:(%di)
	movb	$0, %es:1(%di)
	movb	$0xe1, %gs:(%di)
	addw	$2, %di
	jmp	3b
4:
	pushw	%ds
	xorw	%ax, %ax
	movw	%ax, %ds
	movb	0x0584, %al
	andb	$0x7f, %al
	popw	%ds
	movb	%al, %ah
	shrb	$4, %al
	call	nibble
	movb	%ah, %al
	andb	$0x0f, %al
	call	nibble
7:
	hlt
	jmp	7b
nibble:
	cmpb	$10, %al
	jb	5f
	addb	$('A' - 10), %al
	jmp	6f
5:
	addb	$'0', %al
6:
	movb	%al, %es:(%di)
	movb	$0, %es:1(%di)
	movb	$0xe1, %gs:(%di)
	addw	$2, %di
	ret
message: .asciz "BOOT98 PBR CHAIN OK"
drive_label: .asciz "  DRIVE "
	.org 510
	.word 0xaa55
