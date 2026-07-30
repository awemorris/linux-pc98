/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * PC-98 FAT16 partition boot record for the Mirai98 Linux disk.
 *
 * A genuine PC-98 disk IPL boots a selected partition through its PBR.
 * Load the same second-stage Linux loader used by disk-ipl.S from the
 * pre-partition gap at absolute LBA 2.
 */

	.code16
	.section .text
	.globl _start

	.set STAGE_SEG,		0x1000
	.set HANDOFF,		0x0700
	.set HANDOFF_MAGIC,	0x4839384c	/* "L98H" */
	.set LOADER_SECTORS,	0x0046

_start:
	jmp	pbr_start
	nop
	.ascii	"NEC  5.0"
	.fill	0x003e - (. - _start), 1, 0

	/*
	 * Offsets 0x3e..0x45 are the NEC DOS fixed-disk BPB extension.
	 * The image builder fills them with the absolute partition start,
	 * relative data start, and physical sector size.
	 */
	.fill	0x0046 - (. - _start), 1, 0
	.word	0			/* patched second-stage sector count */

pbr_start:
	cld
	pushw	%cs
	popw	%ds

	/* Pass the physical partition LBA and selected BIOS drive to stage 2. */
	movl	0x001c, %eax
	/*
	 * Keep the genuine IPL's stack and address the BIOS work area and
	 * handoff block explicitly through segment zero.
	 */
	pushw	%ds
	xorw	%dx, %dx
	movw	%dx, %ds
	movw	%si, HANDOFF+10
	movw	%di, HANDOFF+12
	movl	%eax, HANDOFF+4
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
	jc	boot_failure
	cmpw	$512, %bx
	jne	boot_failure

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
	movl	$136, %edi
	divl	%edi
	movw	%ax, %cx
	movl	%edx, %eax
	xorl	%edx, %edx
	movl	$17, %edi
	divl	%edi
	movb	%al, %dh
	xorw	%ax, %ax
	movw	%ax, %ds
	movb	0x0584, %al
	orb	$0x80, %al
	movb	$0x06, %ah
	movw	HANDOFF+10, %si
	movw	HANDOFF+12, %di
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

	.org	0x01fe
	.word	0xaa55
