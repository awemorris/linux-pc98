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
	.set HANDOFF,          0x0700
	.set HANDOFF_MAGIC,    0x4839384c	/* "L98H" */
	.set LOADER_SECTORS,   0x01f0

_start:
	jmp	ipl_start
	nop
	nop
	.ascii	"IPL1"
	.byte	0, 0, 0
	.byte	0x1e

ipl_start:
	cld
	pushw	%cs
	popw	%ds

	/*
	 * Keep the firmware-provided stack.  Use segment zero explicitly for
	 * the BIOS work area and place the handoff block at 0:0700; 0:0500
	 * overlaps genuine IDE BIOS work data and makes AH=06h time out.
	 */
	pushw	%ds
	xorw	%dx, %dx
	movw	%dx, %ds
	movw	%si, HANDOFF+12
	movw	%di, HANDOFF+14
	movb	0x0584, %dl
	andb	$0x7f, %dl
	movb	%dl, HANDOFF+8
	movl	$HANDOFF_MAGIC, HANDOFF

	/* Initialise the selected fixed disk and obtain its sector size. */
	movb	0x0584, %al
	orb	$0x80, %al
	movw	$0x0100, %bx
	movb	$0x84, %ah
	int	$0x1b
	popw	%ds
	jc	1f
	cmpw	$512, %bx
	jne	1f
	testb	%dh, %dh
	jz	1f
	testb	%dl, %dl
	jz	1f
	movb	%dh, %cs:disk_heads
	movb	%dl, %cs:disk_sectors
	movb	$0, %cs:disk_flags
	jmp	2f
1:
	movb	$8, %cs:disk_heads
	movb	$17, %cs:disk_sectors
	movb	$1, %cs:disk_flags
2:
	pushw	%ds
	xorw	%dx, %dx
	movw	%dx, %ds
	movb	%cs:disk_heads, %al
	movb	%al, HANDOFF+9
	movb	%cs:disk_sectors, %al
	movb	%al, HANDOFF+10
	movb	$1, HANDOFF+11
	movb	%cs:disk_flags, %al
	movb	%al, HANDOFF+16
	popw	%ds

	/* Convert the selected partition's BIOS CHS using sensed geometry. */
	movzbl	%cs:disk_heads, %edi
	movzbl	%cs:disk_sectors, %ebx
	imull	%ebx, %edi
	movzwl	0x0206, %eax
	imull	%edi, %eax
	movzbl	0x0205, %ecx
	imull	%ebx, %ecx
	addl	%ecx, %eax
	movzbl	0x0204, %ecx
	addl	%ecx, %eax
	pushw	%ds
	xorw	%dx, %dx
	movw	%dx, %ds
	movl	%eax, HANDOFF+4
	popw	%ds

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
	xorl	%edx, %edx
	movzbl	%cs:disk_heads, %edi
	movzbl	%cs:disk_sectors, %ecx
	imull	%ecx, %edi
	divl	%edi
	movw	%ax, %cx
	movl	%edx, %eax
	xorl	%edx, %edx
	movzbl	%cs:disk_sectors, %edi
	divl	%edi
	movb	%al, %dh
	xorw	%ax, %ax
	movw	%ax, %ds
	movb	0x0584, %al
	orb	$0x80, %al
	movb	$0x06, %ah
	movw	HANDOFF+12, %si
	movw	HANDOFF+14, %di
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
disk_heads:
	.byte	0
disk_sectors:
	.byte	0
disk_flags:
	.byte	0

	.org	LOADER_SECTORS
	.word	0			/* patched second-stage sector count */
	.org	0x01fc
	.word	9
	.word	0xaa55
