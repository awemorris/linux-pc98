/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Generic PC-98 disk IPL selector.
 * Copyright (C) 2026 Awe Morris
 *
 * The ROM loads this 512-byte sector at 1fc0:0000.  It relocates itself,
 * then loads either the first floppy boot sector or exactly one sector from
 * LBA 2 of the current fixed disk back at the firmware load address.  Code
 * installed at LBA 2 is therefore independent of BOOT98 and may implement
 * its own continuation-sector format.
 */

	.code16
	.section .text
	.globl _start

	.set RELOC_SEG,       0x9000
	.set LOAD_SEG,        0x1fc0
	.set TEXT_SEG,        0xa000
	.set ATTR_SEG,        0xa200
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
	movb	$0x42, %ah
	movb	$0xc0, %ch
	int	$0x18
	movb	$0x40, %ah
	int	$0x18
	call	clear_menu
	movw	$title_text, %si
	movw	$(2 * 160), %di
	call	print_string
	movw	$fdd_text, %si
	movw	$(4 * 160), %di
	call	print_string
	movw	$hdd_text, %si
	movw	$(5 * 160), %di
	call	print_string

menu_input:
	xorb	%ah, %ah
	int	$0x18
	cmpb	$'1', %al
	je	boot_fdd1
	cmpb	$'2', %al
	je	boot_hdd1
	jmp	menu_input

boot_fdd1:
	movb	$0x30, boot_id
	call	prepare_target
	xorw	%ax, %ax
	movw	%ax, %ds
	movb	$0x30, %al
	movb	$0x56, %ah
	movw	$512, %bx
	movw	$0x0200, %cx
	movw	$0x0001, %dx
	movw	%cs:saved_si, %si
	movw	%cs:saved_di, %di
	int	$0x1b
	jc	boot_failure
	jmp	enter_target

boot_hdd1:
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

clear_menu:
	movw	$TEXT_SEG, %ax
	movw	%ax, %es
	movw	$ATTR_SEG, %ax
	movw	%ax, %gs
	xorw	%di, %di
	movw	$(80 * 10), %cx
1:
	movb	$0x20, %es:(%di)
	movb	$0, %es:1(%di)
	movb	$0xe1, %gs:(%di)
	addw	$2, %di
	loop	1b
	ret

print_string:
	movw	$TEXT_SEG, %ax
	movw	%ax, %es
	movw	$ATTR_SEG, %ax
	movw	%ax, %gs
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
	ret

boot_failure:
	movb	$0x06, %al
	outb	%al, $0x37
1:
	hlt
	jmp	1b

title_text:
	.asciz	"PC-98 IPL"
fdd_text:
	.asciz	"1) FDD 1"
hdd_text:
	.asciz	"2) HDD 1"
saved_si:
	.word	0
saved_di:
	.word	0
boot_id:
	.byte	0

	.org	0x01fc
	.word	9
	.word	0xaa55
