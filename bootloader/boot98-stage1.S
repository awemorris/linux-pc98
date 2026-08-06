/*
 * PC-9800 Bootloader
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * BOOT98 FAT16 system loader (IO.SYS).
 *
 * Stage 0 currently reuses disk-ipl.S.  This stage stays in real mode,
 * enumerates the bounded BIOS device namespace with INT 1Bh SENSE, and
 * records one descriptor per detected device for the future C Stage 2.
 *
 * Control flow:
 *
 *   FAT16 PBR -> load contiguous IO.SYS from the FAT filesystem
 *   -> probe BIOS devices -> locate FAT16 BOOT/BOOT.SYS
 *   -> validate Stage 2 -> show menu
 *   -> chain-load an IPL/PBR or enter 32-bit Stage 2.
 *
 * The fallback menu must remain usable when Stage 2 is absent or corrupt.
 * All disk geometry in descriptors comes from INT 1Bh SENSE; no fixed H=8
 * assumption is permitted in partition parsing.  BIOS calls are centralized
 * below so the protected-mode gateway and real-mode menu use identical disk
 * semantics.
 *
 * Low-memory ownership:
 *
 *   0000:0700  Stage 0 handoff supplied by disk-ipl.S
 *   CS-local   Stage 1 code, tables, bounce buffer, and GDT
 *   5000:0000  Stage 2 load buffer / applet load area
 *   9000:fffe  Stage 1 real-mode stack
 */

	.code16
	.section .text
	.globl _start

	.set HANDOFF,          0x0700
	.set HANDOFF_MAGIC,    0x4839384c /* "L98H", current Stage 0 ABI */
	.set TABLE_SEG,        0x0800
	.set TEXT_SEG,         0xa000
	.set ATTR_SEG,         0xa200
	.set STACK_SEG,        0x9000

	.set DEV_CLASS_FDD,    1
	.set DEV_CLASS_IDE,    2
	.set DEV_CLASS_SCSI,   3
	.set DEV_FLAG_PRESENT, 1
	.set DEV_FLAG_GEOMETRY, 2
	.set DEV_DESC_SIZE,    16
	.set DEV_MAX,          16

	.set DEV_CLASS,        0
	.set DEV_INDEX,        1
	.set DEV_BIOS_ID,      2
	.set DEV_FLAGS,        3
	.set DEV_SECTOR_SIZE,  4
	.set DEV_CYLINDERS,    6
	.set DEV_HEADS,        8
	.set DEV_SECTORS,      9
	.set MENU_TYPE_DISK,   1
	.set MENU_TYPE_PART,   2
	.set MENU_TYPE_SHELL,  3
	.set MENU_SIZE,        16
	.set MENU_MAX,         16
	.set MENU_TYPE,        0
	.set MENU_BIOS_ID,     1
	.set MENU_CLASS,       2
	.set MENU_DEV_INDEX,   3
	.set MENU_PART_INDEX,  4
	.set MENU_HEADS,       5
	.set MENU_SECTORS,     6
	.set MENU_LBA,         8
	.set MENU_DATA_LBA,    12

_start:
	/*
	 * Keep the old raw partition-loader entry as a compatibility path.  The
	 * normal FAT16 PBR now loads this complete IO.SYS at 1000:0000, which
	 * immediately branches to stage1_main below.
	 */
	jmp	bootstrap_entry
	nop
	.org	0x003e

bootstrap_entry:
	/*
	 * A fixed-disk menu loads only this partition IPL sector at the normal
	 * firmware address 1fc0:0000.  Do not depend on its private register
	 * contract: recover the boot drive from the BIOS work area, find BOOT in
	 * the native partition table, and reload the complete IO.SYS image from
	 * that partition's IPL-start CHS.  Development media may still preload the
	 * image at 1000:0000 and enter it directly.
	 */
	movw	%cs, %ax
	cmpw	$0x1000, %ax
	je	stage1_main
	cli
	cld
	pushw	%cs
	popw	%ds
	movw	%si, bootstrap_saved_si
	movw	%di, bootstrap_saved_di
	movw	$STACK_SEG, %ax
	movw	%ax, %ss
	movw	$0xfffe, %sp
	sti

	pushw	%ds
	xorw	%ax, %ax
	movw	%ax, %ds
	movb	0x0584, %al
	popw	%ds
	andb	$0x7f, %al
	orb	$0x80, %al
	movb	%al, bootstrap_boot_id
	call	bootstrap_write_handoff

	/* Read the PC-98 partition table from physical LBA 1 (CHS 0/0/1). */
	movw	$TABLE_SEG, %ax
	movw	%ax, %es
	xorw	%bp, %bp
	xorw	%cx, %cx
	movw	$0x0001, %dx
	call	bootstrap_read
	jc	bootstrap_failure

	movw	$TABLE_SEG, %ax
	movw	%ax, %ds
	xorw	%si, %si
	movw	$16, %cx
bootstrap_find_partition:
	cmpb	$0, (%si)
	je	bootstrap_next_partition
	pushw	%cx
	pushw	%si
	addw	$16, %si
	xorw	%bx, %bx
	movw	$16, %cx
bootstrap_compare_name:
	lodsb
	cmpb	%cs:bootstrap_boot_name(%bx), %al
	jne	bootstrap_name_mismatch
	incw	%bx
	loop	bootstrap_compare_name
	popw	%si
	popw	%cx
	movb	4(%si), %al
	movb	%al, %cs:bootstrap_ipl_sector
	movb	5(%si), %al
	movb	%al, %cs:bootstrap_ipl_head
	movw	6(%si), %ax
	movw	%ax, %cs:bootstrap_ipl_cylinder
	jmp	bootstrap_partition_found
bootstrap_name_mismatch:
	popw	%si
	popw	%cx
bootstrap_next_partition:
	addw	$32, %si
	loop	bootstrap_find_partition
	jmp	bootstrap_failure

bootstrap_partition_found:
	pushw	%cs
	popw	%ds
	movb	$14, bootstrap_remaining
	movw	$0, %cs:bootstrap_offset
bootstrap_load:
	movw	$0x1000, %ax
	movw	%ax, %es
	movw	%cs:bootstrap_offset, %bp
	movw	%cs:bootstrap_ipl_cylinder, %cx
	movb	%cs:bootstrap_ipl_head, %dh
	movb	%cs:bootstrap_ipl_sector, %dl
	call	bootstrap_read
	jc	bootstrap_failure
	addw	$512, %cs:bootstrap_offset
	incb	%cs:bootstrap_ipl_sector
	decb	%cs:bootstrap_remaining
	jnz	bootstrap_load
	ljmp	$0x1000, $stage1_main

bootstrap_write_handoff:
	pushw	%es
	xorw	%ax, %ax
	movw	%ax, %es
	movl	$0, %es:HANDOFF
	movl	$0, %es:HANDOFF+4
	movl	$0, %es:HANDOFF+8
	movl	$0, %es:HANDOFF+12
	movl	$HANDOFF_MAGIC, %es:HANDOFF
	movb	%cs:bootstrap_boot_id, %al
	movb	%al, %es:HANDOFF+8
	movb	$1, %es:HANDOFF+11
	movw	%cs:bootstrap_saved_si, %ax
	movw	%ax, %es:HANDOFF+12
	movw	%cs:bootstrap_saved_di, %ax
	movw	%ax, %es:HANDOFF+14
	popw	%es
	ret

bootstrap_read:
	pushw	%ds
	xorw	%ax, %ax
	movw	%ax, %ds
	movb	%cs:bootstrap_boot_id, %al
	movb	$0x06, %ah
	movw	$512, %bx
	movw	%cs:bootstrap_saved_si, %si
	movw	%cs:bootstrap_saved_di, %di
	int	$0x1b
	pushfw
	popw	%ax
	movw	%ax, %cs:bootstrap_read_flags
	popw	%ds
	pushw	%cs:bootstrap_read_flags
	popfw
	ret

bootstrap_failure:
	movb	$0x06, %al
	outb	%al, $0x37
1:
	hlt
	jmp	1b

bootstrap_boot_name:      .ascii "BOOT            "
bootstrap_saved_si:       .word 0
bootstrap_saved_di:       .word 0
bootstrap_read_flags:     .word 0
bootstrap_offset:         .word 0
bootstrap_ipl_cylinder:   .word 0
bootstrap_boot_id:        .byte 0
bootstrap_ipl_head:       .byte 0
bootstrap_ipl_sector:     .byte 0
bootstrap_remaining:      .byte 0

	/* Keep the complete partition-IPL bootstrap in its first 512-byte sector. */
	.org	0x01fe
	.word	0xaa55
	.org	512

stage1_main:
	cli
	cld
	movw	$STACK_SEG, %ax
	movw	%ax, %ss
	movw	$0xfffe, %sp
	pushw	%cs
	popw	%ds
	sti

	/* Select and start the normal PC-98 640x400 display. */
	movb	$0x42, %ah
	movb	$0xc0, %ch
	int	$0x18
	movb	$0x40, %ah
	int	$0x18

	call	clear_upper
	call	draw_banner

	pushw	%ds
	xorw	%ax, %ax
	movw	%ax, %ds
	cmpl	$HANDOFF_MAGIC, HANDOFF
	popw	%ds
	jne	bad_handoff

	movw	$probing_label, %si
	movw	$(6 * 160), %di
	call	print_string
	call	probe_all
	pushw	%cs
	popw	%ds
	movw	$found_label, %si
	movw	$(7 * 160), %di
	call	print_string
	movb	device_count, %al
	call	print_hex8
	call	build_menu
	pushw	%cs
	popw	%ds
	call	load_stage2
	pushw	%cs
	popw	%ds
	cmpb	$1, stage2_valid
	je	enter_stage2
	/* Disk option ROMs need not preserve display state or extended segments. */
	movb	$0x42, %ah
	movb	$0xc0, %ch
	int	$0x18
	movb	$0x40, %ah
	int	$0x18
	pushw	%cs
	popw	%ds
	call	clear_upper
	call	draw_banner
	call	show_menu
	call	menu_initial_input
	jnc	menu_key
	/* BOOT without a valid Stage 2 still has a useful recovery PBR. */
	movw	boot_menu_entry, %si
	testw	%si, %si
	jnz	menu_timeout_boot
	movw	active_menu_entry, %si
	testw	%si, %si
	jz	menu_input
menu_timeout_boot:
	subw	$menu_table, %si
	shrw	$4, %si
	movw	%si, %ax
	movb	%al, selected_menu
	call	chain_selected
	jmp	menu_input

bad_handoff:
	movw	$handoff_error, %si
	movw	$(6 * 160), %di
	call	print_string

stop:
	hlt
	jmp	stop

menu_input:
	xorb	%ah, %ah
	int	$0x18
menu_key:
	cmpb	$'1', %al
	jb	menu_input
	cmpb	$'9', %al
	ja	menu_input
	subb	$'1', %al
	cmpb	menu_count, %al
	jae	menu_input
	movb	%al, selected_menu
	movzbl	%al, %esi
	shlw	$4, %si
	addw	$menu_table, %si
	cmpb	$MENU_TYPE_SHELL, MENU_TYPE(%si)
	je	enter_stage2
	call	chain_selected
	jmp	menu_input

/* Wait up to three wall-clock seconds for the first menu key.  The PC-98
 * calendar service is used instead of a CPU-speed-dependent delay loop. */
menu_initial_input:
	call	read_clock_second
	movb	%al, %cs:timeout_start
	movw	$0x2000, %cs:timeout_budget
1:
	movb	$0x01, %ah
	int	$0x18
	testb	%bh, %bh
	jnz	3f
	call	read_clock_second
	cmpb	$0xff, %al
	je	2f
	cmpb	$0xff, %cs:timeout_start
	je	2f
	subb	%cs:timeout_start, %al
	jnc	4f
	addb	$60, %al
4:
	cmpb	$3, %al
	jae	5f
2:
	decw	%cs:timeout_budget
	jnz	1b
5:
	stc
	ret
3:
	xorb	%ah, %ah
	int	$0x18
	clc
	ret

/* Return the RTC second as binary 0..59, or ffh for invalid BCD. */
read_clock_second:
	pushw	%bx
	pushw	%cx
	pushw	%dx
	pushw	%si
	pushw	%di
	pushw	%ds
	pushw	%es
	pushw	%cs
	popw	%es
	movw	$timeout_calendar, %bx
	xorb	%ah, %ah
	int	$0x1c
	movb	%es:5(%bx), %al
	movb	%al, %dl
	andb	$0x0f, %dl
	cmpb	$9, %dl
	ja	1f
	andb	$0xf0, %al
	shrb	$4, %al
	cmpb	$5, %al
	ja	1f
	movb	$10, %cl
	mulb	%cl
	addb	%dl, %al
	jmp	2f
1:
	movb	$0xff, %al
2:
	popw	%es
	popw	%ds
	popw	%di
	popw	%si
	popw	%dx
	popw	%cx
	popw	%bx
	ret

/* Probe 1.44 MiB FDD 0-3 (DA/UA 30h-33h). */
probe_all:
	/* Some original SCSI ROMs do not return from absent-media FDD reads. */
	pushw	%ds
	xorw	%ax, %ax
	movw	%ax, %ds
	cmpb	$0, 0x04b2
	popw	%ds
	jne	probe_ide
	movb	$DEV_CLASS_FDD, current_class
	movb	$0x30, current_bios_id
	movb	$0, current_index
1:
	movb	current_bios_id, %al
	call	bios_read_fdd_boot
	pushw	%cs
	popw	%ds
	jc	2f
	cmpb	$0x20, read_status
	jae	2f
	call	append_fdd
2:
	incb	current_bios_id
	incb	current_index
	cmpb	$4, current_index
	jb	1b

probe_ide:
	/* IDE uses the dense BIOS-visible fixed-disk namespace 80h-83h. */
	movb	$DEV_CLASS_IDE, current_class
	movb	$0x80, current_bios_id
	movb	$0, current_index
3:
	movb	current_bios_id, %al
	call	bios_sense
	pushw	%cs
	popw	%ds
	jc	4f
	cmpb	$0x20, sense_status
	jae	4f
	call	validate_fixed_sense
	jc	4f
	movb	current_bios_id, %al
	call	bios_read_lba0
	pushw	%cs
	popw	%ds
	jc	4f
	cmpb	$0x20, read_status
	jae	4f
	call	append_fixed
4:
	incb	current_bios_id
	incb	current_index
	cmpb	$4, current_index
	jb	3b

	/* PC-9801-92 and compatible option ROMs accept the A0h-A7h aliases. */
	movb	$DEV_CLASS_SCSI, current_class
	movb	$0xa0, current_bios_id
	movb	$0, current_index
5:
	/* PC-9801-92 publishes the successfully probed target bitmap at 0482h. */
	pushw	%ds
	xorw	%ax, %ax
	movw	%ax, %ds
	movb	0x0482, %al
	popw	%ds
	movb	current_index, %cl
	shrb	%cl, %al
	testb	$1, %al
	jz	6f
	movb	current_bios_id, %al
	call	bios_sense
	pushw	%cs
	popw	%ds
	jc	6f
	cmpb	$0x20, sense_status
	jae	6f
	call	validate_fixed_sense
	jc	6f
	call	append_fixed
6:
	incb	current_bios_id
	incb	current_index
	cmpb	$8, current_index
	jb	5b
	ret

validate_fixed_sense:
	cmpw	$512, sense_bytes
	jne	1f
	testb	$0xff, sense_heads
	jz	1f
	testb	$0xff, sense_sectors
	jz	1f
	clc
	ret
1:
	stc
	ret

append_fdd:
	call	allocate_descriptor
	jc	1f
	movb	$DEV_FLAG_PRESENT, DEV_FLAGS(%di)
	movw	$0, DEV_SECTOR_SIZE(%di)
	movw	$0, DEV_CYLINDERS(%di)
	movb	$0, DEV_HEADS(%di)
	movb	$0, DEV_SECTORS(%di)
1:
	ret

append_fixed:
	call	allocate_descriptor
	jc	1f
	movb	$(DEV_FLAG_PRESENT | DEV_FLAG_GEOMETRY), DEV_FLAGS(%di)
	movw	sense_bytes, %ax
	movw	%ax, DEV_SECTOR_SIZE(%di)
	movw	sense_cylinders, %ax
	movw	%ax, DEV_CYLINDERS(%di)
	movb	sense_heads, %al
	movb	%al, DEV_HEADS(%di)
	movb	sense_sectors, %al
	movb	%al, DEV_SECTORS(%di)
	movb	sense_status, %al
	movb	%al, 10(%di)
1:
	ret

/* Return DS:DI for a zeroed descriptor, or carry when the table is full. */
allocate_descriptor:
	movzbl	device_count, %edi
	cmpw	$DEV_MAX, %di
	jae	2f
	shlw	$4, %di
	addw	$device_table, %di
	pushw	%di
	movw	$DEV_DESC_SIZE, %cx
	xorb	%al, %al
1:
	movb	%al, (%di)
	incw	%di
	loop	1b
	popw	%di
	movb	current_class, %al
	movb	%al, DEV_CLASS(%di)
	movb	current_index, %al
	movb	%al, DEV_INDEX(%di)
	movb	current_bios_id, %al
	movb	%al, DEV_BIOS_ID(%di)
	incb	device_count
	clc
	ret
2:
	stc
	ret

/*
 * Invoke INT 1Bh AH=84h without trusting it to preserve general registers.
 * The documented fixed-disk result is BX=bytes, CX=cylinders, DH=heads,
 * DL=sectors.  Carry is restored for the caller.
 */
bios_sense:
	pushw	%ax
	pushw	%bx
	pushw	%cx
	pushw	%dx
	pushw	%si
	pushw	%di
	pushw	%ds
	pushw	%es
	pushw	%bp
	pushw	%ds
	xorw	%bx, %bx
	movw	%bx, %ds
	movw	HANDOFF+12, %si
	movw	HANDOFF+14, %di
	popw	%ds
	movw	$0x0100, %bx
	movb	$0x84, %ah
	int	$0x1b
	movb	%ah, %cs:sense_status
	movw	%bx, %cs:sense_bytes
	movw	%cx, %cs:sense_cylinders
	movb	%dh, %cs:sense_heads
	movb	%dl, %cs:sense_sectors
	pushfw
	popw	%ax
	movw	%ax, %cs:sense_flags
	popw	%bp
	popw	%es
	popw	%ds
	popw	%di
	popw	%si
	popw	%dx
	popw	%cx
	popw	%bx
	popw	%ax
	pushw	%cs:sense_flags
	popfw
	ret

/*
 * Confirm a fixed-disk SENSE result with one read-only sector-zero access.
 * Some option BIOSes report plausible geometry for an absent target.
 */
bios_read_lba0:
	pushw	%ax
	pushw	%bx
	pushw	%cx
	pushw	%dx
	pushw	%si
	pushw	%di
	pushw	%ds
	pushw	%es
	pushw	%bp
	pushw	%ds
	xorw	%bx, %bx
	movw	%bx, %ds
	movw	HANDOFF+12, %si
	movw	HANDOFF+14, %di
	popw	%ds
	movw	$0x0800, %bx
	movw	%bx, %es
	xorw	%bp, %bp
	movw	$512, %bx
	xorw	%cx, %cx
	xorw	%dx, %dx
	movb	$0x06, %ah
	int	$0x1b
	movb	%ah, %cs:read_status
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
	popw	%ax
	pushw	%cs:read_flags
	popfw
	ret

/* Confirm FDD media with a read of C=0/H=0/R=1/N=2. */
bios_read_fdd_boot:
	pushw	%ax
	pushw	%bx
	pushw	%cx
	pushw	%dx
	pushw	%si
	pushw	%di
	pushw	%ds
	pushw	%es
	pushw	%bp
	movw	$0x0800, %bx
	movw	%bx, %es
	xorw	%bp, %bp
	movw	$512, %bx
	movw	$0x0200, %cx
	movw	$0x0001, %dx
	movb	$0x56, %ah
	int	$0x1b
	movb	%ah, %cs:read_status
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
	popw	%ax
	pushw	%cs:read_flags
	popfw
	ret

/* Read the native PC-98 partition table (physical LBA 1) into our segment. */
bios_read_partition_table:
	pushw	%ax
	pushw	%bx
	pushw	%cx
	pushw	%dx
	pushw	%si
	pushw	%di
	pushw	%ds
	pushw	%es
	pushw	%bp
	pushw	%ds
	xorw	%bx, %bx
	movw	%bx, %ds
	movw	HANDOFF+12, %si
	movw	HANDOFF+14, %di
	popw	%ds
	movw	$0x0800, %bx
	movw	%bx, %es
	xorw	%bp, %bp
	movw	$512, %bx
	xorw	%cx, %cx
	movw	$1, %dx
	movb	$0x06, %ah
	int	$0x1b
	movb	%ah, %cs:read_status
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
	popw	%ax
	pushw	%cs:read_flags
	popfw
	ret

/* Build stable menu records for every detected device and partition. */
build_menu:
	movb	$0, menu_count
	movw	$0, boot_menu_entry
	movw	$0, active_menu_entry
	xorw	%bx, %bx
1:
	cmpb	device_count, %bl
	jae	9f
	movw	%bx, %si
	shlw	$4, %si
	addw	$device_table, %si
	movw	%si, current_descriptor
	movb	$MENU_TYPE_DISK, current_menu_type
	movb	$0xff, current_partition
	movl	$0, current_lba
	movl	$0, current_data_lba
	call	append_menu
	testb	$DEV_FLAG_GEOMETRY, DEV_FLAGS(%si)
	jz	8f
	movb	DEV_BIOS_ID(%si), %al
	call	bios_read_partition_table
	pushw	%cs
	popw	%ds
	jc	8f
	cmpb	$0x20, read_status
	jae	8f
	call	copy_partition_bounce
	xorw	%dx, %dx
2:
	cmpw	$16, %dx
	jae	8f
	movw	%dx, %si
	shlw	$5, %si
	addw	$partition_table, %si
	cmpb	$0, (%si)
	je	7f
	movw	current_descriptor, %di
	/* IPL CHS fields are sector@4, head@5, cylinder@6. */
	movzwl	6(%si), %eax
	movzbl	DEV_HEADS(%di), %ecx
	imull	%ecx, %eax
	movzbl	5(%si), %ecx
	addl	%ecx, %eax
	movzbl	DEV_SECTORS(%di), %ecx
	imull	%ecx, %eax
	movzbl	4(%si), %ecx
	addl	%ecx, %eax
	movl	%eax, current_lba
	/* Data-start CHS fields are sector@8, head@9, cylinder@10. */
	movzwl	10(%si), %eax
	movzbl	DEV_HEADS(%di), %ecx
	imull	%ecx, %eax
	movzbl	9(%si), %ecx
	addl	%ecx, %eax
	movzbl	DEV_SECTORS(%di), %ecx
	imull	%ecx, %eax
	movzbl	8(%si), %ecx
	addl	%ecx, %eax
	movl	%eax, current_data_lba
	movb	$MENU_TYPE_PART, current_menu_type
	movb	%dl, current_partition
	call	append_menu
	call	consider_active_partition
	call	consider_boot_partition
7:
	incw	%dx
	jmp	2b
8:
	incw	%bx
	jmp	1b
9:
	ret

copy_partition_bounce:
	pushw	%ds
	pushw	%es
	pushw	%si
	pushw	%di
	pushw	%cx
	movw	$0x0800, %ax
	movw	%ax, %ds
	pushw	%cs
	popw	%es
	xorw	%si, %si
	movw	$partition_table, %di
	movw	$256, %cx
	rep movsw
	popw	%cx
	popw	%di
	popw	%si
	popw	%es
	popw	%ds
	ret

append_menu:
	pushw	%si
	cmpb	$MENU_MAX, menu_count
	jae	2f
	movzbl	menu_count, %edi
	shlw	$4, %di
	addw	$menu_table, %di
	pushw	%di
	movw	$MENU_SIZE, %cx
	xorb	%al, %al
1:
	movb	%al, (%di)
	incw	%di
	loop	1b
	popw	%di
	movw	current_descriptor, %si
	movb	current_menu_type, %al
	movb	%al, MENU_TYPE(%di)
	movb	DEV_BIOS_ID(%si), %al
	movb	%al, MENU_BIOS_ID(%di)
	movb	DEV_CLASS(%si), %al
	movb	%al, MENU_CLASS(%di)
	movb	DEV_INDEX(%si), %al
	movb	%al, MENU_DEV_INDEX(%di)
	movb	current_partition, %al
	movb	%al, MENU_PART_INDEX(%di)
	movb	DEV_HEADS(%si), %al
	movb	%al, MENU_HEADS(%di)
	movb	DEV_SECTORS(%si), %al
	movb	%al, MENU_SECTORS(%di)
	movl	current_lba, %eax
	movl	%eax, MENU_LBA(%di)
	movl	current_data_lba, %eax
	movl	%eax, MENU_DATA_LBA(%di)
	incb	menu_count
2:
	popw	%si
	ret

consider_boot_partition:
	pushw	%ax
	pushw	%bx
	pushw	%cx
	pushw	%si
	pushw	%di
	movw	%si, %bx
	addw	$16, %bx
	xorw	%di, %di
	movw	$16, %cx
1:
	movb	(%bx), %al
	cmpb	bootstrap_boot_name(%di), %al
	jne	2f
	incw	%bx
	incw	%di
	loop	1b
	movzbl	menu_count, %edi
	decw	%di
	shlw	$4, %di
	addw	$menu_table, %di
	cmpw	$0, boot_menu_entry
	je	3f
	movw	current_descriptor, %si
	movb	DEV_BIOS_ID(%si), %al
	andb	$0x7f, %al
	pushw	%ds
	xorw	%bx, %bx
	movw	%bx, %ds
	cmpb	HANDOFF+8, %al
	popw	%ds
	jne	2f
3:
	movw	%di, boot_menu_entry
2:
	popw	%di
	popw	%si
	popw	%cx
	popw	%bx
	popw	%ax
	ret

/* MID bit 7 is the PC-98 active/bootable flag.  Retain the first active
 * partition in stable probe order for unattended fallback boot. */
consider_active_partition:
	pushw	%ax
	pushw	%di
	testb	$0x80, (%si)
	jz	1f
	cmpw	$0, active_menu_entry
	jne	1f
	movzbl	menu_count, %edi
	decw	%di
	shlw	$4, %di
	addw	$menu_table, %di
	movw	%di, active_menu_entry
1:
	popw	%di
	popw	%ax
	ret

/* Load and validate BOOT.SYS from the selected BOOT FAT16 partition. */
load_stage2:
	cmpw	$0, boot_menu_entry
	je	9f
	movw	boot_menu_entry, %si
	movw	%si, chain_entry
	movl	MENU_DATA_LBA(%si), %eax
	movl	%eax, fat_partition_lba
	movw	$0x0800, %ax
	movw	%ax, %es
	xorw	%bp, %bp
	movl	fat_partition_lba, %eax
	call	read_fixed_lba
	jc	9f
	pushw	%cs
	popw	%ds
	movw	%es:0x0b, %ax
	cmpw	$512, %ax
	je	1f
	cmpw	$1024, %ax
	jne	9f
	movb	$2, fat_sector_scale
	jmp	2f
1:
	movb	$1, fat_sector_scale
2:
	movzbl	%es:0x0d, %eax
	movzbl	fat_sector_scale, %ecx
	imull	%ecx, %eax
	testl	%eax, %eax
	jz	9f
	cmpl	$128, %eax
	ja	9f
	movb	%al, fat_spc
	movl	fat_partition_lba, %eax
	movzwl	%es:0x0e, %ecx
	movzbl	fat_sector_scale, %edx
	imull	%edx, %ecx
	addl	%ecx, %eax
	movl	%eax, fat_lba
	movzbl	%es:0x10, %ecx
	movzwl	%es:0x16, %edx
	testl	%edx, %edx
	jz	9f
	movzbl	fat_sector_scale, %ebx
	imull	%ebx, %edx
	imull	%edx, %ecx
	addl	%ecx, %eax
	movl	%eax, fat_root_lba
	movzwl	%es:0x11, %ecx
	movw	%cx, fat_root_entries
	shll	$5, %ecx
	addl	$511, %ecx
	shrl	$9, %ecx
	movw	%cx, fat_root_sectors
	addl	%ecx, %eax
	movl	%eax, fat_data_lba

	/* Search the root one physical sector at a time. */
	xorw	%dx, %dx
3:
	cmpw	fat_root_sectors, %dx
	jae	9f
	movl	fat_root_lba, %eax
	movzwl	%dx, %ecx
	addl	%ecx, %eax
	xorw	%bp, %bp
	call	read_fixed_lba
	jc	9f
	pushw	%cs
	popw	%ds
	xorw	%di, %di
	movw	$16, %cx
4:
	cmpb	$0, %es:(%di)
	je	9f
	cmpb	$0xe5, %es:(%di)
	je	5f
	cmpb	$0x0f, %es:0x0b(%di)
	je	5f
	pushw	%cx
	pushw	%di
	movw	$stage2_name, %si
	movw	$11, %cx
	repe cmpsb
	popw	%di
	popw	%cx
	je	stage2_found
5:
	addw	$32, %di
	loop	4b
	incw	%dx
	jmp	3b

stage2_found:
	movw	%es:0x1a(%di), %ax
	cmpw	$2, %ax
	jb	9f
	movw	%ax, fat_cluster
	movl	%es:0x1c(%di), %eax
	cmpl	$20, %eax
	jb	9f
	cmpl	$0xf000, %eax
	ja	9f
	movl	%eax, stage2_file_size
	movl	%eax, stage2_remaining
	movw	$0, stage2_offset

stage2_cluster_loop:
	movb	$0, fat_sector_in_cluster
stage2_sector_loop:
	movzwl	fat_cluster, %eax
	subl	$2, %eax
	movzbl	fat_spc, %ecx
	imull	%ecx, %eax
	movzbl	fat_sector_in_cluster, %ecx
	addl	%ecx, %eax
	addl	fat_data_lba, %eax
	movw	$0x2000, %cx
	movw	%cx, %es
	movw	stage2_offset, %bp
	call	read_fixed_lba
	jc	9f
	pushw	%cs
	popw	%ds
	addw	$512, stage2_offset
	cmpl	$512, stage2_remaining
	jbe	stage2_loaded
	subl	$512, stage2_remaining
	incb	fat_sector_in_cluster
	movb	fat_sector_in_cluster, %al
	cmpb	fat_spc, %al
	jb	stage2_sector_loop
	movw	fat_cluster, %ax
	call	stage2_fat_next
	cmpw	$0xfff8, %ax
	jae	9f
	cmpw	$2, %ax
	jb	9f
	movw	%ax, fat_cluster
	jmp	stage2_cluster_loop

stage2_loaded:
	call	validate_stage2
	jc	9f
	call	append_shell_menu
	clc
	ret
9:
	stc
	ret

stage2_fat_next:
	pushw	%bx
	pushw	%dx
	pushw	%es
	pushw	%bp
	movzwl	%ax, %edx
	shll	$1, %edx
	movl	%edx, %eax
	shrl	$9, %eax
	addl	fat_lba, %eax
	andw	$511, %dx
	movw	$0x0800, %bx
	movw	%bx, %es
	xorw	%bp, %bp
	call	read_fixed_lba
	jc	1f
	movw	%dx, %bx
	movw	%es:(%bx), %ax
	jmp	2f
1:
	movw	$0xffff, %ax
2:
	pushw	%cs
	popw	%ds
	popw	%bp
	popw	%es
	popw	%dx
	popw	%bx
	ret

validate_stage2:
	movw	$0x2000, %ax
	movw	%ax, %es
	cmpl	$0x53383942, %es:0
	jne	8f
	cmpw	$1, %es:4
	jne	8f
	cmpw	$20, %es:6
	jne	8f
	movl	%es:8, %ecx
	cmpl	stage2_file_size, %ecx
	jne	8f
	movl	%es:12, %eax
	cmpl	$20, %eax
	jb	8f
	cmpl	%ecx, %eax
	jae	8f
	movl	%eax, stage2_entry_offset
	subl	$20, %ecx
	movw	$20, %si
	xorl	%ebx, %ebx
1:
	testl	%ecx, %ecx
	jz	2f
	movzbl	%es:(%si), %eax
	addl	%eax, %ebx
	incw	%si
	decl	%ecx
	jmp	1b
2:
	cmpl	%es:16, %ebx
	jne	8f
	clc
	ret
8:
	stc
	ret

append_shell_menu:
	cmpb	$MENU_MAX, menu_count
	jae	2f
	pushw	%es
	pushw	%ds
	popw	%es
	movw	boot_menu_entry, %si
	movzbl	menu_count, %edi
	shlw	$4, %di
	addw	$menu_table, %di
	movw	$8, %cx
	rep movsw
	subw	$MENU_SIZE, %di
	movb	$MENU_TYPE_SHELL, MENU_TYPE(%di)
	incb	menu_count
	movb	$1, stage2_valid
	popw	%es
2:
	ret

show_menu:
	movw	$probing_label, %si
	movw	$(6 * 160), %di
	call	print_string
	movw	$found_label, %si
	movw	$(7 * 160), %di
	call	print_string
	movb	device_count, %al
	call	print_hex8
	movw	$menu_label, %si
	movw	$(8 * 160), %di
	call	print_string
	xorw	%bx, %bx
1:
	cmpb	menu_count, %bl
	jae	7f
	cmpb	$9, %bl
	jae	7f
	movw	%bx, %si
	shlw	$4, %si
	addw	$menu_table, %si
	movw	%bx, %di
	addw	$9, %di
	imulw	$160, %di, %di
	movb	%bl, %al
	addb	$'1', %al
	call	print_char
	movb	$')', %al
	call	print_char
	movb	$' ', %al
	call	print_char
	movb	MENU_CLASS(%si), %al
	cmpb	$DEV_CLASS_FDD, %al
	jne	2f
	pushw	%si
	movw	$fd_name, %si
	call	print_string
	popw	%si
	jmp	4f
2:
	cmpb	$DEV_CLASS_IDE, %al
	jne	3f
	pushw	%si
	movw	$ide_name, %si
	call	print_string
	popw	%si
	jmp	4f
3:
	pushw	%si
	movw	$scsi_name, %si
	call	print_string
	popw	%si
4:
	movb	MENU_DEV_INDEX(%si), %al
	call	print_hex8
	cmpb	$MENU_TYPE_PART, MENU_TYPE(%si)
	je	4f
	cmpb	$MENU_TYPE_SHELL, MENU_TYPE(%si)
	jne	5f
	pushw	%si
	movw	$shell_label, %si
	call	print_string
	popw	%si
	jmp	6f
4:
	pushw	%si
	movw	$part_label, %si
	call	print_string
	popw	%si
	movb	MENU_PART_INDEX(%si), %al
	call	print_hex8
	pushw	%si
	movw	$partition_lba_label, %si
	call	print_string
	popw	%si
	movl	MENU_LBA(%si), %eax
	call	print_hex32
	jmp	6f
5:
	pushw	%si
	movw	$disk_ipl_label, %si
	call	print_string
	popw	%si
6:
	incw	%bx
	jmp	1b
7:
	movw	$select_label, %si
	movw	%bx, %di
	addw	$9, %di
	imulw	$160, %di, %di
	call	print_string
	ret

chain_selected:
	movzbl	selected_menu, %esi
	shlw	$4, %si
	addw	$menu_table, %si
	movw	%si, chain_entry
	movw	$0x1fc0, %ax
	movw	%ax, %es
	cmpb	$DEV_CLASS_FDD, MENU_CLASS(%si)
	jne	1f
	xorw	%bp, %bp
	call	read_fdd_boot
	jc	chain_failed
	jmp	4f
1:
	/* Preserve the native table next to the target IPL/PBR. */
	movl	$1, %eax
	movw	$0x0200, %bp
	call	read_fixed_lba
	jc	chain_failed
	movw	chain_entry, %si
	movl	MENU_LBA(%si), %eax
	xorw	%bp, %bp
	call	read_fixed_lba
	jc	chain_failed
	movw	chain_entry, %si
	cmpb	$MENU_TYPE_PART, MENU_TYPE(%si)
	jne	4f
	/* Put the selected entry first, matching the firmware IPL convention. */
	movzbl	MENU_PART_INDEX(%si), %eax
	shlw	$5, %ax
	addw	$0x0200, %ax
	pushw	%ds
	pushw	%es
	popw	%ds
	movw	%ax, %si
	movw	$0x0200, %di
	movw	$16, %cx
	rep movsw
	popw	%ds
4:
	cli
	movw	chain_entry, %si
	movb	MENU_BIOS_ID(%si), %al
	andb	$0x7f, %al
	pushw	%ds
	xorw	%dx, %dx
	movw	%dx, %ds
	movb	%al, 0x0584
	movw	HANDOFF+12, %si
	movw	HANDOFF+14, %di
	popw	%ds
	sti
	ljmp	$0x1fc0, $0

chain_failed:
	/* BIOS calls may disturb the GDC, so rebuild the complete menu. */
	movb	$0x42, %ah
	movb	$0xc0, %ch
	int	$0x18
	movb	$0x40, %ah
	int	$0x18
	call	clear_upper
	call	draw_banner
	movw	$chain_error, %si
	movw	$(5 * 160), %di
	call	print_string
	call	show_menu
	ret

/* Fixed-disk one-sector read.  EAX=LBA, ES:BP=destination. */
read_fixed_lba:
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
	movw	%cs:chain_entry, %si
	movzbl	%cs:MENU_HEADS(%si), %edi
	movzbl	%cs:MENU_SECTORS(%si), %ecx
	imull	%ecx, %edi
	testl	%edi, %edi
	jz	8f
	divl	%edi
	cmpl	$0xffff, %eax
	ja	8f
	movw	%ax, %cx
	movl	%edx, %eax
	xorl	%edx, %edx
	movzbl	%cs:MENU_SECTORS(%si), %edi
	divl	%edi
	movb	%al, %dh
	xorw	%ax, %ax
	movw	%ax, %ds
	movw	%cs:chain_entry, %si
	movb	%cs:MENU_BIOS_ID(%si), %al
	movb	$0x06, %ah
	movw	$512, %bx
	movw	HANDOFF+12, %si
	movw	HANDOFF+14, %di
	int	$0x1b
	pushfw
	popw	%ax
	movw	%ax, %cs:chain_flags
	jmp	9f
8:
	movw	$1, %cs:chain_flags
9:
	popw	%bp
	popw	%es
	popw	%ds
	popw	%di
	popw	%si
	popw	%dx
	popw	%cx
	popw	%bx
	popl	%eax
	pushw	%cs:chain_flags
	popfw
	ret

/* 1.44 MiB FDD boot sector: C=0, H=0, R=1, N=2. */
read_fdd_boot:
	pushw	%ax
	pushw	%bx
	pushw	%cx
	pushw	%dx
	pushw	%si
	pushw	%di
	pushw	%ds
	pushw	%es
	pushw	%bp
	xorw	%ax, %ax
	movw	%ax, %ds
	movw	%cs:chain_entry, %si
	movb	%cs:MENU_BIOS_ID(%si), %al
	movb	$0x56, %ah
	movw	$512, %bx
	movw	$0x0200, %cx
	movw	$0x0001, %dx
	movw	HANDOFF+12, %si
	movw	HANDOFF+14, %di
	int	$0x1b
	pushfw
	popw	%ax
	movw	%ax, %cs:chain_flags
	popw	%bp
	popw	%es
	popw	%ds
	popw	%di
	popw	%si
	popw	%dx
	popw	%cx
	popw	%bx
	popw	%ax
	pushw	%cs:chain_flags
	popfw
	ret

show_devices:
	movw	$found_label, %si
	movw	$160, %di
	call	print_string
	movb	device_count, %al
	call	print_hex8

	xorw	%bx, %bx
1:
	cmpb	device_count, %bl
	jae	5f
	cmpb	$8, %bl
	jae	5f
	movw	%bx, %ax
	shlw	$4, %ax
	movw	%ax, %si
	addw	$device_table, %si
	movw	%bx, %di
	addw	$3, %di
	imulw	$160, %di, %di

	movb	DEV_CLASS(%si), %al
	cmpb	$DEV_CLASS_FDD, %al
	jne	2f
	pushw	%si
	movw	$fd_name, %si
	call	print_string
	popw	%si
	jmp	4f
2:
	cmpb	$DEV_CLASS_IDE, %al
	jne	3f
	pushw	%si
	movw	$ide_name, %si
	call	print_string
	popw	%si
	jmp	4f
3:
	pushw	%si
	movw	$scsi_name, %si
	call	print_string
	popw	%si
4:
	movb	DEV_INDEX(%si), %al
	call	print_hex8
	pushw	%si
	movw	$bios_label, %si
	call	print_string
	popw	%si
	movb	DEV_BIOS_ID(%si), %al
	call	print_hex8
	pushw	%si
	movw	$status_label, %si
	call	print_string
	popw	%si
	movb	10(%si), %al
	call	print_hex8
	testb	$DEV_FLAG_GEOMETRY, DEV_FLAGS(%si)
	jz	6f
	pushw	%si
	movw	$geometry_label, %si
	call	print_string
	popw	%si
	movb	DEV_HEADS(%si), %al
	call	print_hex8
	movb	$'/', %al
	call	print_char
	movb	DEV_SECTORS(%si), %al
	call	print_hex8
6:
	incw	%bx
	jmp	1b
5:
	ret

/* Cache LBA 1 from the first fixed disk before the final GDC redraw. */
load_first_partition_table:
	xorw	%bx, %bx
1:
	cmpb	device_count, %bl
	jae	3f
	movw	%bx, %si
	shlw	$4, %si
	addw	$device_table, %si
	testb	$DEV_FLAG_GEOMETRY, DEV_FLAGS(%si)
	jnz	2f
	incw	%bx
	jmp	1b
2:
	movb	DEV_BIOS_ID(%si), %al
	call	bios_read_partition_table
	jc	3f
	cmpb	$0x20, read_status
	jae	3f
	/* Copy the BIOS bounce buffer only after a successful transfer. */
	pushw	%ds
	pushw	%es
	movw	$0x0800, %ax
	movw	%ax, %ds
	pushw	%cs
	popw	%es
	xorw	%si, %si
	movw	$partition_table, %di
	movw	$256, %cx
	rep movsw
	popw	%es
	popw	%ds
	/* Recover the descriptor pointer clobbered by the copy. */
	movw	%bx, %si
	shlw	$4, %si
	addw	$device_table, %si
	movb	DEV_HEADS(%si), %al
	movb	%al, partition_heads
	movb	DEV_SECTORS(%si), %al
	movb	%al, partition_sectors
	movb	$1, partition_table_valid
3:
	ret

/* Show up to four non-empty entries from the cached table. */
show_partitions:
	cmpb	$1, partition_table_valid
	jne	8f
	movw	$partitions_label, %si
	movw	$(7 * 160), %di
	call	print_string
	xorw	%bx, %bx
	xorw	%dx, %dx
3:
	cmpw	$16, %bx
	jae	9f
	cmpw	$4, %dx
	jae	9f
	movw	%bx, %si
	shlw	$5, %si
	addw	$partition_table, %si
	movb	(%si), %al
	testb	%al, %al
	jz	7f
	movw	%dx, %di
	addw	$8, %di
	imulw	$160, %di, %di
	movb	$'#', %al
	call	print_char
	movb	%bl, %al
	call	print_hex8
	pushw	%si
	movw	$partition_id_label, %si
	call	print_string
	popw	%si
	movb	(%si), %al
	call	print_hex8
	movb	$'/', %al
	call	print_char
	movb	1(%si), %al
	call	print_hex8
	pushw	%si
	movw	$partition_lba_label, %si
	call	print_string
	popw	%si
	movzwl	10(%si), %eax
	movzbl	partition_heads, %ecx
	imull	%ecx, %eax
	movzbl	9(%si), %ecx
	addl	%ecx, %eax
	movzbl	partition_sectors, %ecx
	imull	%ecx, %eax
	movzbl	8(%si), %ecx
	addl	%ecx, %eax
	call	print_hex32
	incw	%dx
7:
	incw	%bx
	jmp	3b
8:
9:
	ret

	.code32
bios_gateway32:
	pushl	%ebp
	pushl	%edi
	pushl	%esi
	pushl	%ebx
	movl	20(%esp), %eax
	movl	%eax, (0x10000 + gateway_request)
	movb	$0, (0x10000 + gateway_active_service)
	movl	%esp, (0x10000 + gateway_saved_esp)
	cli
	ljmp	$0x18, $gateway16_pm

gateway32_return:
	movw	$0x10, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %ss
	movw	%ax, %fs
	movw	%ax, %gs
	cmpb	$1, (0x10000 + gateway_active_service)
	jne	1f
	cmpl	$0, (0x10000 + gateway_result)
	jne	1f
	movl	$0x00008000, %esi
	movl	(0x10000 + gateway_buffer), %edi
	movl	$128, %ecx
	cld
	rep movsl
1:
	movl	(0x10000 + gateway_saved_esp), %esp
	movl	(0x10000 + gateway_result), %eax
	popl	%ebx
	popl	%esi
	popl	%edi
	popl	%ebp
	ret

	.code16
gateway16_pm:
	movw	$0x20, %ax
	movw	%ax, %ss
	movl	$0x0009eff0, %esp
	movl	%cr0, %eax
	andl	$0xfffffffe, %eax
	movl	%eax, %cr0
	ljmp	$0x1000, $gateway_real

gateway_real:
	movw	%cs, %ax
	movw	%ax, %ds
	movw	$0x9000, %ax
	movw	%ax, %ss
	movl	$0x0000eff0, %esp
	sti
	movl	gateway_request, %eax
	movl	%eax, %ebx
	andw	$0x000f, %bx
	shrl	$4, %eax
	movw	%ax, %es
	movw	%es:(%bx), %ax
	cmpw	$9, %ax
	je	gateway_clock_second
	cmpw	$8, %ax
	je	gateway_chain_boot
	cmpw	$7, %ax
	je	gateway_reprobe
	cmpw	$6, %ax
	je	gateway_iplware
	cmpw	$5, %ax
	je	gateway_return_menu
	cmpw	$4, %ax
	je	gateway_display_reset
	cmpw	$3, %ax
	je	gateway_key_poll
	cmpw	$2, %ax
	je	gateway_key_read
	cmpw	$1, %ax
	je	gateway_disk_read
	movl	$0xffffffff, gateway_result
	jmp	gateway_real_return

gateway_clock_second:
	call	read_clock_second
	movzbl	%al, %eax
	movl	%eax, %cs:gateway_result
	jmp	gateway_real_return

gateway_key_read:
	xorb	%ah, %ah
	int	$0x18
	movzbl	%al, %eax
	movl	%eax, %cs:gateway_result
	jmp	gateway_real_return

gateway_key_poll:
	movb	$0x01, %ah
	int	$0x18
	testb	%bh, %bh
	jz	1f
	movzbl	%al, %eax
	movl	%eax, %cs:gateway_result
	jmp	gateway_real_return
1:
	movl	$0xffffffff, %cs:gateway_result
	jmp	gateway_real_return

gateway_display_reset:
	movb	$0x42, %ah
	movb	$0xc0, %ch
	int	$0x18
	movb	$0x40, %ah
	int	$0x18
	movl	$0, %cs:gateway_result
	jmp	gateway_real_return

gateway_return_menu:
	pushw	%cs
	popw	%ds
	movb	$0x42, %ah
	movb	$0xc0, %ch
	int	$0x18
	movb	$0x40, %ah
	int	$0x18
	call	clear_upper
	call	draw_banner
	call	show_menu
	jmp	menu_input

gateway_iplware:
	movw	%es:2(%bx), %ax
	movw	%ax, gateway_module_type
	movb	%es:4(%bx), %al
	andb	$0x7f, %al
	movb	%al, gateway_bios_id
	movl	%es:8(%bx), %eax
	movl	%eax, gateway_lba
	movl	%es:12(%bx), %eax
	movl	%eax, gateway_buffer
	/* Type-2 COM trampoline: near CALL 0100h, then far return. */
	movw	$0x6000, %ax
	movw	%ax, %es
	movb	$0xe8, %es:0
	movw	$0x00fd, %es:1
	movb	$0xcb, %es:3
	movb	$0, %es:0x00f0
	cli
	movw	%ax, %ss
	movl	$0x0000f7fe, %esp
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %bp
	movl	%cs:gateway_lba, %eax
	movw	%ax, %cx
	shrl	$16, %eax
	movw	%ax, %dx
	movl	%cs:gateway_buffer, %eax
	addl	$511, %eax
	andl	$0xfffffe00, %eax
	movw	%ax, %bx
	movw	$0x0393, %si
	xorw	%di, %di
	movb	%cs:gateway_bios_id, %al
	cmpw	$2, %cs:gateway_module_type
	jne	1f
	movb	$2, %ah
	lcall	$0x6000, $0x0000
	jmp	2f
1:
	xorb	%ah, %ah
	lcall	$0x6000, $0x0100
2:
	cli
	movw	%cs, %ax
	movw	%ax, %ds
	movw	$0x9000, %ax
	movw	%ax, %ss
	movl	$0x0000eff0, %esp
	movb	$0, device_count
	call	probe_all
	pushw	%cs
	popw	%ds
	call	build_menu
	movb	device_count, %al
	movb	%al, stage2_handoff+8
	cmpb	$0x5a, %es:0x00f0
	jne	3f
	movl	$0, gateway_result
	jmp	gateway_real_return

gateway_reprobe:
	pushw	%cs
	popw	%ds
	movb	$0, device_count
	call	probe_all
	pushw	%cs
	popw	%ds
	call	build_menu
	movb	device_count, %al
	movb	%al, stage2_handoff+8
	movl	$0, gateway_result
	jmp	gateway_real_return
3:
	movl	$0x0006ffff, gateway_result
	jmp	gateway_real_return

/* Chain a disk IPL or partition PBR selected by Stage 2.  status=0 selects
 * the whole-disk menu entry; status=1 selects the entry whose IPL LBA is
 * supplied in lba.  A successful chain never returns. */
gateway_chain_boot:
	pushw	%cs
	popw	%ds
	movb	%es:4(%bx), %al
	movb	%al, gateway_bios_id
	movw	%es:2(%bx), %ax
	movw	%ax, gateway_module_type
	movl	%es:8(%bx), %eax
	movl	%eax, gateway_lba
	xorw	%bx, %bx
	movw	$menu_table, %si
1:
	cmpb	menu_count, %bl
	jae	3f
	movb	gateway_bios_id, %al
	cmpb	%al, MENU_BIOS_ID(%si)
	jne	2f
	cmpw	$0, gateway_module_type
	jne	4f
	cmpb	$MENU_TYPE_DISK, MENU_TYPE(%si)
	je	5f
	jmp	2f
4:
	cmpb	$MENU_TYPE_PART, MENU_TYPE(%si)
	jne	2f
	movl	gateway_lba, %eax
	cmpl	%eax, MENU_LBA(%si)
	je	5f
2:
	incb	%bl
	addw	$MENU_SIZE, %si
	jmp	1b
5:
	movb	%bl, selected_menu
	call	chain_selected
3:
	movl	$0x0008ffff, gateway_result
	jmp	gateway_real_return

gateway_disk_read:
	movb	$1, gateway_active_service
	movb	%es:4(%bx), %al
	movb	%al, gateway_menu+MENU_BIOS_ID
	movb	%es:5(%bx), %al
	movb	%al, gateway_menu+MENU_HEADS
	movb	%es:6(%bx), %al
	movb	%al, gateway_menu+MENU_SECTORS
	movl	%es:8(%bx), %eax
	movl	%eax, gateway_lba
	movl	%es:12(%bx), %eax
	movl	%eax, gateway_buffer
	xorw	%bp, %bp
	movw	$0x0800, %ax
	movw	%ax, %es
	movw	$gateway_menu, chain_entry
	movl	gateway_lba, %eax
	call	read_fixed_lba
	jc	gateway_disk_error
	pushw	%cs
	popw	%ds
	movl	$0, gateway_result
	jmp	gateway_real_return
gateway_disk_error:
	pushw	%cs
	popw	%ds
	movl	$0x0001ffff, gateway_result

gateway_real_return:
	cli
	movw	%cs, %ax
	movw	%ax, %ds
	lgdt	gdt_desc
	movl	%cr0, %eax
	orl	$1, %eax
	movl	%eax, %cr0
	ljmpl	*gateway_pm_return

enter_stage2:
	cmpb	$1, stage2_valid
	jne	menu_input
	movl	$0x48323842, stage2_handoff
	movw	$1, stage2_handoff+4
	movw	$20, stage2_handoff+6
	movb	device_count, %al
	movb	%al, stage2_handoff+8
	pushw	%ds
	xorw	%ax, %ax
	movw	%ax, %ds
	movb	HANDOFF+8, %al
	popw	%ds
	movb	%al, stage2_handoff+9
	movl	$(0x10000 + device_table), stage2_handoff+12
	movl	$(0x10000 + bios_gateway32), stage2_handoff+16
	movl	stage2_entry_offset, %eax
	addl	$0x20000, %eax
	movl	%eax, stage2_target
	movl	$(0x10000 + protected_entry), %eax
	movl	%eax, pm_jump
	movl	$(0x10000 + gdt), %eax
	movl	%eax, gdt_desc+2
	lgdt	gdt_desc
	cli
	movl	%cr0, %eax
	orl	$1, %eax
	movl	%eax, %cr0
	ljmpl	*pm_jump

	.code32
protected_entry:
	movw	$0x10, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %ss
	movw	%ax, %fs
	movw	%ax, %gs
	movl	$(0x10000 + stage2_handoff), %ebx
	movl	0x0002000c, %eax
	addl	$0x00020000, %eax
	jmp	*%eax
	.code16

clear_upper:
	movw	$TEXT_SEG, %ax
	movw	%ax, %es
	movw	$ATTR_SEG, %ax
	movw	%ax, %gs
	xorw	%di, %di
	movw	$(80 * 20), %cx
1:
	movb	$0x20, %es:(%di)
	movb	$0, %es:1(%di)
	movb	$0xe1, %gs:(%di)
	addw	$2, %di
	loop	1b
	ret

/*
 * Keep the identity shown by the original PC-98 Linux loader.  The kana are
 * PC-98 ANK bytes, not Shift-JIS; writing them as single cells also works on
 * the character generators found in early machines.
 */
draw_banner:
	movw	$banner_machine, %si
	movw	$(1 * 160), %di
	call	print_string
	movw	$banner_loader, %si
	movw	$(3 * 160), %di
	call	print_string
	movw	$banner_copyright, %si
	movw	$(4 * 160), %di
	call	print_string
	ret

print_string:
1:
	lodsb
	testb	%al, %al
	jz	2f
	call	print_char
	jmp	1b
2:
	ret

print_char:
	movb	%al, %es:(%di)
	movb	$0, %es:1(%di)
	movb	$0xe1, %gs:(%di)
	addw	$2, %di
	ret

print_hex8:
	pushw	%ax
	movb	%al, %ah
	shrb	$4, %al
	call	print_nibble
	movb	%ah, %al
	andb	$0x0f, %al
	call	print_nibble
	popw	%ax
	ret

print_hex32:
	pushl	%eax
	rorl	$16, %eax
	call	print_hex16
	rorl	$16, %eax
	call	print_hex16
	popl	%eax
	ret

print_hex16:
	pushw	%ax
	movb	%ah, %al
	call	print_hex8
	popw	%ax
	call	print_hex8
	ret

print_nibble:
	andb	$0x0f, %al
	cmpb	$10, %al
	jb	1f
	addb	$('A' - 10), %al
	jmp	print_char
1:
	addb	$'0', %al
	jmp	print_char

/* PC-98 ANK text retained from the previous Linux loader. */
banner_machine:
	.ascii	"NEC PC-9800 "
	.byte	0xbc, 0xd8, 0xb0, 0xbd, 0xde
	.ascii	" "
	.byte	0xca, 0xdf, 0xb0, 0xbf, 0xc5, 0xd9
	.ascii	" "
	.byte	0xba, 0xdd, 0xcb, 0xdf, 0xad, 0xb0, 0xc0
	.byte	0
banner_loader:
	/* Integrated bootloader, version 1.00. */
	.byte	0xb2, 0xdd, 0xc3, 0xb8, 0xde, 0xda, 0xb0, 0xc3, 0xaf, 0xc4, 0xde
	.ascii	" "
	.byte	0xcc, 0xde, 0xb0, 0xc4, 0xdb, 0xb0, 0xc0, 0xde
	.ascii	" "
	.byte	0xca, 0xde, 0xb0, 0xbc, 0xde, 0xae, 0xdd
	.ascii	" 1.00"
	.byte	0
banner_copyright:
	.asciz	"Copyright (C) 2026 NEC FANS on X.com"
probing_label:   .asciz "Probing disks..."
found_label:     .asciz "Detected devices: "
fd_name:         .asciz "fd"
ide_name:        .asciz "ide"
scsi_name:       .asciz "scsi"
bios_label:      .asciz "  BIOS "
status_label:    .asciz " ST "
geometry_label:  .asciz "  H/S "
partitions_label: .asciz "PC-98 partitions (first fixed disk):"
menu_label:       .asciz "Select boot target:"
shell_label:      .asciz " Shell"
disk_ipl_label:   .asciz " disk IPL"
part_label:       .asciz " part "
select_label:     .asciz "Press 1-9 to chain-load."
chain_error:      .asciz "Chain-load read failed."
stage2_name:      .ascii "BOOT    SYS"
partition_id_label: .asciz " MID/SID "
partition_lba_label: .asciz " start LBA "
partitions_error: .asciz "PC-98 partition table read failed."
handoff_error:   .asciz "Invalid Stage 0 handoff."

	.balign 2
sense_bytes:      .word 0
sense_cylinders:  .word 0
sense_heads:      .byte 0
sense_sectors:    .byte 0
sense_flags:      .word 0
sense_status:     .byte 0
read_flags:       .word 0
read_status:      .byte 0
timeout_start:    .byte 0
timeout_calendar: .space 6
device_count:     .byte 0
current_class:    .byte 0
current_index:    .byte 0
current_bios_id:  .byte 0
menu_count:        .byte 0
selected_menu:     .byte 0
current_menu_type: .byte 0
current_partition: .byte 0
partition_heads:  .byte 0
partition_sectors: .byte 0
partition_table_valid: .byte 0
	.balign 2
current_descriptor: .word 0
chain_entry:       .word 0
chain_flags:       .word 0
	.balign 4
current_lba:       .long 0
current_data_lba:  .long 0
fat_partition_lba: .long 0
fat_lba:           .long 0
fat_root_lba:      .long 0
fat_data_lba:      .long 0
stage2_file_size:  .long 0
stage2_remaining:  .long 0
stage2_entry_offset: .long 0
stage2_target:     .long 0
	.balign 2
boot_menu_entry:   .word 0
active_menu_entry: .word 0
timeout_budget:    .word 0
fat_root_entries:  .word 0
fat_root_sectors:  .word 0
fat_cluster:       .word 0
stage2_offset:     .word 0
fat_sector_scale:  .byte 0
fat_spc:           .byte 0
fat_sector_in_cluster: .byte 0
stage2_valid:      .byte 0
	.balign 4
stage2_handoff:    .space 20
pm_jump:           .long 0
	.word 0x08
	.balign 8
gdt:
	.quad 0
	.quad 0x00cf9a000000ffff
	.quad 0x00cf92000000ffff
	.quad 0x00009a010000ffff
	.quad 0x000092000000ffff
gdt_end:
gdt_desc:
	.word gdt_end - gdt - 1
	.long 0
gateway_pm_return:
	.long 0x10000 + gateway32_return
	.word 0x08
gateway_request:   .long 0
gateway_saved_esp: .long 0
gateway_result:    .long 0
gateway_active_service: .byte 0
gateway_bios_id:   .byte 0
gateway_heads:     .byte 0
gateway_sectors:   .byte 0
	.balign 4
gateway_lba:       .long 0
gateway_buffer:    .long 0
gateway_module_type: .word 0
	.balign 16
gateway_menu:      .space MENU_SIZE
	.balign 16
device_table:     .space DEV_DESC_SIZE * DEV_MAX
	.balign 16
menu_table:       .space MENU_SIZE * MENU_MAX
	.balign 16
partition_table:  .space 512
