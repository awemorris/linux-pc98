/*
 * BOOT98 FAT16 partition boot record.
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PC-98 FAT16 uses a 1024-byte logical reserved sector.  Firmware need only
 * load its first physical 512 bytes, so that half explicitly reads the second
 * half before parsing FAT.  IO.SYS is an ordinary contiguous FAT root file;
 * INST.EXE and the image builder enforce the historical SYS-file constraint.
 */

	.code16
	.section .text
	.globl _start

	.set LOAD_SEG,          0x1000
	.set TABLE_SEG,         0x0800
	.set STACK_SEG,         0x9000
	.set HANDOFF,           0x0700
	.set HANDOFF_MAGIC,     0x4839384c /* "L98H" */
	.set HANDOFF_VERSION,   1
	.set HANDOFF_SIZE,      24
	.set HANDOFF_FS_FAT16,  1
	.set HANDOFF_GEOMETRY,  1

_start:
	jmp	pbr_start
	nop
	.ascii	"NEC  5.0"
	.fill	0x003e - (. - _start), 1, 0

pbr_start:
	cli
	cld
	movw	%cs, %ax
	movw	%ax, %ds
	movw	%si, saved_si
	movw	%di, saved_di
	movw	$STACK_SEG, %ax
	movw	%ax, %ss
	movw	$0xfffe, %sp
	sti
	movl	0x001c, %eax
	movl	%eax, partition_lba

	pushw	%ds
	xorw	%dx, %dx
	movw	%dx, %ds
	movb	0x0584, %al
	popw	%ds
	andb	$0x7f, %al
	orb	$0x80, %al
	movb	%al, boot_drive
	call	bios_sense
	jnc	1f
	movb	$8, disk_heads
	movb	$17, disk_sectors
	movb	$1, disk_flags
1:
	/* Firmware may load only 512 bytes; fetch the rest of the 1 KiB PBR. */
	movl	partition_lba, %eax
	incl	%eax
	movw	%cs, %dx
	movw	%dx, %es
	movw	$0x0200, %bp
	call	bios_read
	jc	boot_failure
	pushw	%cs
	pushw	$pbr_continue
	lret

bios_sense:
	pushw	%ds
	xorw	%dx, %dx
	movw	%dx, %ds
	movb	%cs:boot_drive, %al
	movb	$0x84, %ah
	movw	$0x0100, %bx
	int	$0x1b
	popw	%ds
	jc	1f
	cmpw	$512, %bx
	jne	1f
	testb	%dh, %dh
	jz	1f
	testb	%dl, %dl
	jz	1f
	movb	%dh, disk_heads
	movb	%dl, disk_sectors
	movb	$0, disk_flags
	clc
	ret
1:
	stc
	ret

/* Read physical LBA EAX to ES:BP through PC-98 INT 1Bh. */
bios_read:
	pushl	%eax
	pushw	%bx
	pushw	%cx
	pushw	%dx
	pushw	%si
	pushw	%di
	pushw	%ds
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
	movb	%cs:boot_drive, %al
	movb	$0x06, %ah
	movw	$512, %bx
	movw	%cs:saved_si, %si
	movw	%cs:saved_di, %di
	int	$0x1b
	pushfw
	popw	%ax
	movw	%ax, %cs:read_flags
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

partition_lba:   .long 0
saved_si:        .word 0
saved_di:        .word 0
read_flags:      .word 0
boot_drive:      .byte 0
disk_heads:      .byte 0
disk_sectors:    .byte 0
disk_flags:      .byte 0

	.org	0x01fe
	.word	0xaa55

	/* Second half of the one-logical-sector (1024-byte) PBR. */
	.org	0x0200
pbr_continue:
	call	write_handoff
	cmpw	$1024, 0x000b
	jne	boot_failure
	cmpw	$1, 0x000e
	jb	boot_failure

	/* Convert the 1024-byte FAT BPB layout to physical 512-byte LBAs. */
	movl	partition_lba, %eax
	movzwl	0x000e, %ecx
	shll	$1, %ecx
	addl	%ecx, %eax
	movzbl	0x0010, %ecx
	movzwl	0x0016, %edx
	shll	$1, %edx
	imull	%edx, %ecx
	addl	%ecx, %eax
	movl	%eax, root_lba
	movzwl	0x0011, %ecx
	movw	%cx, root_entries
	shll	$5, %ecx
	addl	$511, %ecx
	shrl	$9, %ecx
	movw	%cx, root_sectors
	addl	%ecx, %eax
	movl	%eax, data_lba

	/* Locate IO.SYS in the FAT16 root directory. */
	movl	root_lba, %eax
	movw	root_sectors, %dx
root_sector_loop:
	testw	%dx, %dx
	jz	boot_failure
	movw	$TABLE_SEG, %cx
	movw	%cx, %es
	xorw	%bp, %bp
	call	bios_read
	jc	boot_failure
	xorw	%di, %di
	movw	$16, %cx
root_entry_loop:
	cmpb	$0, %es:(%di)
	je	boot_failure
	cmpb	$0xe5, %es:(%di)
	je	next_root_entry
	cmpb	$0x0f, %es:0x0b(%di)
	je	next_root_entry
	pushw	%cx
	pushw	%di
	movw	$io_name, %si
	movw	$11, %cx
	repe cmpsb
	popw	%di
	popw	%cx
	je	io_found
next_root_entry:
	addw	$32, %di
	loop	root_entry_loop
	incl	%eax
	decw	%dx
	jmp	root_sector_loop

io_found:
	movzwl	%es:0x1a(%di), %eax
	cmpl	$2, %eax
	jb	boot_failure
	subl	$2, %eax
	movzbl	0x000d, %ecx
	shll	$1, %ecx
	imull	%ecx, %eax
	addl	data_lba, %eax
	movl	%es:0x1c(%di), %ecx
	addl	$511, %ecx
	shrl	$9, %ecx
	jz	boot_failure
	cmpw	$127, %cx
	ja	boot_failure

	/* IO.SYS is contiguous; load it in full and enter its file entry point. */
	movw	$LOAD_SEG, %dx
	movw	%dx, %es
	xorw	%bp, %bp
load_io:
	call	bios_read
	jc	boot_failure
	incl	%eax
	addw	$512, %bp
	loop	load_io
	ljmp	$LOAD_SEG, $0

write_handoff:
	pushw	%es
	xorw	%ax, %ax
	movw	%ax, %es
	movl	$HANDOFF_MAGIC, %es:HANDOFF
	movw	$HANDOFF_VERSION, %es:HANDOFF+4
	movw	$HANDOFF_SIZE, %es:HANDOFF+6
	movl	partition_lba, %eax
	movl	%eax, %es:HANDOFF+8
	movb	boot_drive, %al
	movb	%al, %es:HANDOFF+12
	movb	disk_heads, %al
	movb	%al, %es:HANDOFF+13
	movb	disk_sectors, %al
	movb	%al, %es:HANDOFF+14
	movb	$HANDOFF_FS_FAT16, %es:HANDOFF+15
	movw	$512, %es:HANDOFF+16
	movw	$HANDOFF_GEOMETRY, %es:HANDOFF+18
	movw	saved_si, %ax
	movw	%ax, %es:HANDOFF+20
	movw	saved_di, %ax
	movw	%ax, %es:HANDOFF+22
	popw	%es
	ret

io_name:         .ascii "IO      SYS"
root_lba:        .long 0
data_lba:        .long 0
root_entries:    .word 0
root_sectors:    .word 0

	.org	0x03fe
	.word	0xaa55
