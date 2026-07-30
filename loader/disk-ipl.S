/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * PC-98 fixed-disk IPL for the Mirai98 Linux disk.
 *
 * The firmware loads LBA 0 and LBA 1 at 1fc0:0000.  LBA 1 contains the
 * sixteen-entry NEC98 partition table.  The gap at LBA 2..135 contains a
 * small FAT-aware second-stage loader; load it and transfer control.
 */

	.code16
	.section .text
	.globl _start

	.set STAGE_SEG,        0x1000
	.set HANDOFF,          0x0500
	.set HANDOFF_MAGIC,    0x4839384c	/* "L98H" */
	.set LOADER_SECTORS,   0x0008

_start:
	jmp	ipl_start
	nop
	nop
	.ascii	"IPL1"
	.word	0			/* patched second-stage sector count */
	.byte	0
	.byte	0x1e

ipl_start:
	cli
	xorw	%ax, %ax
	movw	%ax, %ss
	movw	$0x0400, %sp
	sti
	pushw	%cs
	popw	%ds

	/* LBA = cylinder * (8 * 17) + head * 17 + sector. */
	movzwl	0x0206, %eax
	imull	$136, %eax, %eax
	movzbl	0x0205, %ecx
	imull	$17, %ecx, %ecx
	addl	%ecx, %eax
	movzbl	0x0204, %ecx
	addl	%ecx, %eax

	movl	%eax, %ss:HANDOFF+4
	movb	%ss:0x0584, %dl
	andb	$0x7f, %dl
	movb	%dl, %ss:HANDOFF+8
	movl	$HANDOFF_MAGIC, %ss:HANDOFF

	movl	$2, %eax
	movw	LOADER_SECTORS, %si
	movw	$STAGE_SEG, %dx
	movw	%dx, %es
	xorw	%bp, %bp

load_stage:
	testw	%si, %si
	jz	stage_loaded
	movw	$512, %bx
	call	read_disk
	jc	boot_failure
	incl	%eax
	addw	$512, %bp
	decw	%si
	jmp	load_stage

stage_loaded:
	ljmp	$STAGE_SEG, $0

read_disk:
	pushl	%eax
	pushw	%bx
	pushw	%cx
	pushw	%dx
	pushw	%si
	pushw	%di
	pushw	%ds
	pushw	%es
	pushw	%bp
	movw	%ax, %cx
	shrl	$16, %eax
	movb	%al, %dl
	movb	%ss:0x0584, %al
	andb	$0x7f, %al
	movb	$0x06, %ah
	int	$0x1b
	pushfw
	popw	%ax
	movw	%ax, %cs:read_flags
	popw	%bp
	popw	%es
	popw	%ds
	popw	%di
	popw	%si
	popw	%dx
	popw	%cx
	popw	%bx
	popl	%eax
	pushw	%cs:read_flags
	popfw
	ret

boot_failure:
	movb	$0x06, %al
	outb	%al, $0x37
1:
	hlt
	jmp	1b

read_flags:
	.word	0

	.org	0x01f4
	.word	9
	.word	0xaa55
	.org	0x01fc
	.word	9
	.word	0xaa55
