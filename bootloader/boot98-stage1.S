/*
 * PC-9800 Bootloader minimal real-mode system loader
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * The FAT16 PBR loads this file at 1000:0000 and supplies a versioned
 * bootstrap handoff at 0000:0700.  IO.SYS validates that handoff, loads
 * BOOT.SYS from the already selected BOOT volume, installs the BIOS gateway,
 * and enters 32-bit protected mode.  Device discovery and all normal UI are
 * intentionally owned by BOOT.SYS.
 */

	.code16
	.section .text
	.globl _start

	.set HANDOFF,             0x0700
	.set HANDOFF_MAGIC,       0x4839384c /* "L98H" */
	.set HANDOFF_VERSION,     1
	.set HANDOFF_SIZE,        24
	.set HO_MAGIC,            0
	.set HO_VERSION,          4
	.set HO_SIZE,             6
	.set HO_PARTITION_LBA,    8
	.set HO_BOOT_BIOS_ID,    12
	.set HO_HEADS,           13
	.set HO_SECTORS,         14
	.set HO_FILESYSTEM_HINT, 15
	.set HO_SECTOR_SIZE,     16
	.set HO_FLAGS,           18
	.set HO_SAVED_SI,        20
	.set HO_SAVED_DI,        22

	.set HO_FS_FAT16,         1
	.set HO_FLAG_GEOMETRY,    1

	.set TABLE_SEG,           0x0800
	.set TEXT_SEG,            0xa000
	.set ATTR_SEG,            0xa200
	.set STACK_SEG,           0x9000
	.set STAGE2_SEG,          0x2000
	.set STAGE2_MAX_SIZE,     0x40000

	.set DEV_CLASS_IDE,       2
	.set DEV_CLASS_SCSI,      3
	.set DEV_FLAG_PRESENT,    1
	.set DEV_FLAG_GEOMETRY,   2
	.set DEV_FLAG_BOOT,       4
	.set DEV_DESC_SIZE,      16

	.set DEV_CLASS,           0
	.set DEV_INDEX,           1
	.set DEV_BIOS_ID,         2
	.set DEV_FLAGS,           3
	.set DEV_SECTOR_SIZE,     4
	.set DEV_CYLINDERS,       6
	.set DEV_HEADS,           8
	.set DEV_SECTORS,         9

	/* Private descriptor used by the common BIOS read/chain helpers. */
	.set MENU_SIZE,          16
	.set MENU_TYPE,           0
	.set MENU_BIOS_ID,        1
	.set MENU_CLASS,          2
	.set MENU_PART_INDEX,     4
	.set MENU_HEADS,          5
	.set MENU_SECTORS,        6
	.set MENU_LBA,            8

_start:
	jmp	stage1_main

stage1_main:
	cli
	cld
	movw	$STACK_SEG, %ax
	movw	%ax, %ss
	movw	$0xfffe, %sp
	pushw	%cs
	popw	%ds
	sti

	call	validate_bootstrap_handoff
	jc	fatal_handoff
	call	initialize_boot_device
	call	load_stage2
	jc	fatal_stage2
	jmp	enter_stage2

fatal_handoff:
	movw	$fatal_handoff_message, %si
	jmp	fatal
fatal_stage2:
	movw	$fatal_stage2_message, %si
fatal:
	/* Fatal diagnostics are the only display output retained in IO.SYS. */
	movb	$0x42, %ah
	movb	$0xc0, %ch
	int	$0x18
	movb	$0x40, %ah
	int	$0x18
	pushw	%cs
	popw	%ds
	movw	$TEXT_SEG, %ax
	movw	%ax, %es
	movw	$ATTR_SEG, %ax
	movw	%ax, %gs
	xorw	%di, %di
1:
	lodsb
	testb	%al, %al
	jz	fatal_stop
	movb	%al, %es:(%di)
	movb	$0, %es:1(%di)
	movb	$0xe1, %gs:(%di)
	addw	$2, %di
	jmp	1b
fatal_stop:
	cli
1:
	hlt
	jmp	1b

validate_bootstrap_handoff:
	pushw	%ds
	xorw	%ax, %ax
	movw	%ax, %ds
	cmpl	$HANDOFF_MAGIC, HANDOFF+HO_MAGIC
	jne	1f
	cmpw	$HANDOFF_VERSION, HANDOFF+HO_VERSION
	jne	1f
	cmpw	$HANDOFF_SIZE, HANDOFF+HO_SIZE
	jb	1f
	cmpb	$HO_FS_FAT16, HANDOFF+HO_FILESYSTEM_HINT
	jne	1f
	cmpw	$512, HANDOFF+HO_SECTOR_SIZE
	jne	1f
	testw	$HO_FLAG_GEOMETRY, HANDOFF+HO_FLAGS
	jz	1f
	cmpb	$0, HANDOFF+HO_HEADS
	je	1f
	cmpb	$0, HANDOFF+HO_SECTORS
	je	1f
	popw	%ds
	clc
	ret
1:
	popw	%ds
	stc
	ret

/* Construct the one boot-origin descriptor needed before incremental probing
 * is added in the next refactoring step. */
initialize_boot_device:
	pushw	%es
	pushw	%ds
	popw	%es
	movw	$device_table, %di
	xorw	%ax, %ax
	movw	$DEV_DESC_SIZE, %cx
	rep stosb
	popw	%es

	pushw	%ds
	xorw	%ax, %ax
	movw	%ax, %ds
	movb	HANDOFF+HO_BOOT_BIOS_ID, %al
	movb	HANDOFF+HO_HEADS, %ch
	movb	HANDOFF+HO_SECTORS, %cl
	movl	HANDOFF+HO_PARTITION_LBA, %edx
	popw	%ds

	movb	%al, device_table+DEV_BIOS_ID
	movb	%al, gateway_menu+MENU_BIOS_ID
	movb	%ch, device_table+DEV_HEADS
	movb	%ch, gateway_menu+MENU_HEADS
	movb	%cl, device_table+DEV_SECTORS
	movb	%cl, gateway_menu+MENU_SECTORS
	movw	$512, device_table+DEV_SECTOR_SIZE
	movb	$(DEV_FLAG_PRESENT | DEV_FLAG_GEOMETRY | DEV_FLAG_BOOT), device_table+DEV_FLAGS
	movb	$DEV_CLASS_IDE, %bl
	movb	%al, %ah
	andb	$0xf0, %ah
	cmpb	$0xa0, %ah
	jne	1f
	movb	$DEV_CLASS_SCSI, %bl
1:
	movb	%bl, device_table+DEV_CLASS
	movb	%bl, gateway_menu+MENU_CLASS
	movl	%edx, fat_partition_lba
	movl	%edx, gateway_menu+MENU_LBA
	movw	$gateway_menu, chain_entry
	movb	$1, device_count
	ret

/* Load and validate BOOT.SYS from the handoff BOOT FAT16 volume. */
load_stage2:
	movw	$0x0800, %ax
	movw	%ax, %es
	xorw	%bp, %bp
	movl	fat_partition_lba, %eax
	call	read_fixed_lba
	jc	load_stage2_error
	pushw	%cs
	popw	%ds
	movw	%es:0x0b, %ax
	cmpw	$512, %ax
	je	1f
	cmpw	$1024, %ax
	jne	load_stage2_error
	movb	$2, fat_sector_scale
	jmp	2f
1:
	movb	$1, fat_sector_scale
2:
	movzbl	%es:0x0d, %eax
	movzbl	fat_sector_scale, %ecx
	imull	%ecx, %eax
	testl	%eax, %eax
	jz	load_stage2_error
	cmpl	$510, %eax
	ja	load_stage2_error
	movw	%ax, fat_spc
	movl	fat_partition_lba, %eax
	movzwl	%es:0x0e, %ecx
	testl	%ecx, %ecx
	jz	load_stage2_error
	movzbl	fat_sector_scale, %edx
	imull	%edx, %ecx
	addl	%ecx, %eax
	movl	%eax, fat_lba
	movzbl	%es:0x10, %ecx
	testl	%ecx, %ecx
	jz	load_stage2_error
	movzwl	%es:0x16, %edx
	testl	%edx, %edx
	jz	load_stage2_error
	movzbl	fat_sector_scale, %ebx
	imull	%ebx, %edx
	imull	%edx, %ecx
	addl	%ecx, %eax
	movl	%eax, fat_root_lba
	movzwl	%es:0x11, %ecx
	testl	%ecx, %ecx
	jz	load_stage2_error
	movw	%cx, fat_root_entries
	shll	$5, %ecx
	addl	$511, %ecx
	shrl	$9, %ecx
	movw	%cx, fat_root_sectors
	addl	%ecx, %eax
	movl	%eax, fat_data_lba

	/* Search the FAT16 fixed root one physical sector at a time. */
	xorw	%dx, %dx
3:
	cmpw	fat_root_sectors, %dx
	jae	load_stage2_error
	movl	fat_root_lba, %eax
	movzwl	%dx, %ecx
	addl	%ecx, %eax
	xorw	%bp, %bp
	call	read_fixed_lba
	jc	load_stage2_error
	pushw	%cs
	popw	%ds
	xorw	%di, %di
	movw	$16, %cx
4:
	cmpb	$0, %es:(%di)
	je	load_stage2_error
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
	jb	load_stage2_error
	movw	%ax, fat_cluster
	movl	%es:0x1c(%di), %eax
	cmpl	$20, %eax
	jb	load_stage2_error
	cmpl	$STAGE2_MAX_SIZE, %eax
	ja	load_stage2_error
	movl	%eax, stage2_file_size
	movl	%eax, stage2_remaining
	movw	$STAGE2_SEG, stage2_segment
	movw	$0, stage2_offset

stage2_cluster_loop:
	movw	$0, fat_sector_in_cluster
stage2_sector_loop:
	movzwl	fat_cluster, %eax
	subl	$2, %eax
	movzwl	fat_spc, %ecx
	imull	%ecx, %eax
	movzwl	fat_sector_in_cluster, %ecx
	addl	%ecx, %eax
	addl	fat_data_lba, %eax
	movw	stage2_segment, %cx
	movw	%cx, %es
	movw	stage2_offset, %bp
	call	read_fixed_lba
	jc	load_stage2_error
	pushw	%cs
	popw	%ds
	addw	$512, stage2_offset
	jnc	6f
	addw	$0x1000, stage2_segment
6:
	cmpl	$512, stage2_remaining
	jbe	stage2_loaded
	subl	$512, stage2_remaining
	incw	fat_sector_in_cluster
	movw	fat_sector_in_cluster, %ax
	cmpw	fat_spc, %ax
	jb	stage2_sector_loop
	movw	fat_cluster, %ax
	call	stage2_fat_next
	cmpw	$0xfff8, %ax
	jae	load_stage2_error
	cmpw	$2, %ax
	jb	load_stage2_error
	movw	%ax, fat_cluster
	jmp	stage2_cluster_loop

stage2_loaded:
	call	validate_stage2
	ret
load_stage2_error:
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
	movw	$TABLE_SEG, %bx
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
	movw	$STAGE2_SEG, %ax
	movw	%ax, %es
	cmpl	$0x53383942, %es:0
	jne	validate_stage2_error
	cmpw	$1, %es:4
	jne	validate_stage2_error
	cmpw	$20, %es:6
	jne	validate_stage2_error
	movl	%es:8, %ecx
	cmpl	stage2_file_size, %ecx
	jne	validate_stage2_error
	cmpl	$20, %ecx
	jb	validate_stage2_error
	cmpl	$STAGE2_MAX_SIZE, %ecx
	ja	validate_stage2_error
	movl	%es:12, %eax
	movzwl	%es:6, %edx
	cmpl	%edx, %eax
	jb	validate_stage2_error
	cmpl	%ecx, %eax
	jae	validate_stage2_error
	movl	%eax, stage2_entry_offset

	/* Sum the payload while advancing ES for every 64 KiB offset wrap. */
	movzwl	%es:6, %eax
	subl	%eax, %ecx
	movw	$STAGE2_SEG, %ax
	movw	%ax, %es
	movw	$20, %si
	xorl	%ebx, %ebx
1:
	testl	%ecx, %ecx
	jz	3f
	movzbl	%es:(%si), %eax
	addl	%eax, %ebx
	incw	%si
	jnz	2f
	movw	%es, %ax
	addw	$0x1000, %ax
	movw	%ax, %es
2:
	decl	%ecx
	jmp	1b
3:
	movw	$STAGE2_SEG, %ax
	movw	%ax, %es
	cmpl	%es:16, %ebx
	jne	validate_stage2_error
	clc
	ret
validate_stage2_error:
	stc
	ret

/* Fixed-disk one-sector read. EAX=LBA, ES:BP=destination. */
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
	jz	read_fixed_lba_invalid
	divl	%edi
	cmpl	$0xffff, %eax
	ja	read_fixed_lba_invalid
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
	movw	HANDOFF+HO_SAVED_SI, %si
	movw	HANDOFF+HO_SAVED_DI, %di
	int	$0x1b
	pushfw
	popw	%ax
	movw	%ax, %cs:disk_flags
	jmp	read_fixed_lba_done
read_fixed_lba_invalid:
	movw	$1, %cs:disk_flags
read_fixed_lba_done:
	popw	%bp
	popw	%es
	popw	%ds
	popw	%di
	popw	%si
	popw	%dx
	popw	%cx
	popw	%bx
	popl	%eax
	pushw	%cs:disk_flags
	popfw
	ret

/* Return RTC seconds as binary 0..59, or ffh for invalid BCD. */
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
	movw	$clock_calendar, %bx
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
	movw	$STACK_SEG, %ax
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

/* BOOT.SYS now owns its menu. These legacy service numbers remain stable. */
gateway_return_menu:
gateway_reprobe:
	movl	$0, %cs:gateway_result
	jmp	gateway_real_return

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
	movw	$STACK_SEG, %ax
	movw	%ax, %ss
	movl	$0x0000eff0, %esp
	cmpb	$0x5a, %es:0x00f0
	jne	3f
	movl	$0, gateway_result
	jmp	gateway_real_return
3:
	movl	$0x0006ffff, gateway_result
	jmp	gateway_real_return

/* Chain a fixed-disk IPL or partition PBR without a Stage-1 menu table. */
gateway_chain_boot:
	pushw	%cs
	popw	%ds
	movw	%es:2(%bx), %ax
	movw	%ax, gateway_module_type
	movb	%es:4(%bx), %al
	movb	%al, gateway_menu+MENU_BIOS_ID
	movb	%es:5(%bx), %al
	movb	%al, gateway_menu+MENU_HEADS
	movb	%es:6(%bx), %al
	movb	%al, gateway_menu+MENU_SECTORS
	movl	%es:8(%bx), %eax
	movl	%eax, gateway_lba
	movl	%eax, gateway_menu+MENU_LBA
	movw	$gateway_menu, chain_entry
	movw	$0x1fc0, %ax
	movw	%ax, %es
	movl	$1, %eax
	movw	$0x0200, %bp
	call	read_fixed_lba
	jc	gateway_chain_error
	movl	gateway_lba, %eax
	xorw	%bp, %bp
	call	read_fixed_lba
	jc	gateway_chain_error
	cmpw	$0, gateway_module_type
	je	gateway_chain_jump

	/* Locate the selected native entry by its IPL LBA and move it first. */
	xorw	%si, %si
	movw	$16, %cx
gateway_chain_find_partition:
	cmpb	$0, %es:0x0200(%si)
	je	gateway_chain_next_partition
	movzwl	%es:0x0206(%si), %eax
	movzbl	gateway_menu+MENU_HEADS, %edx
	imull	%edx, %eax
	movzbl	%es:0x0205(%si), %edx
	addl	%edx, %eax
	movzbl	gateway_menu+MENU_SECTORS, %edx
	imull	%edx, %eax
	movzbl	%es:0x0204(%si), %edx
	addl	%edx, %eax
	cmpl	gateway_lba, %eax
	je	gateway_chain_partition_found
gateway_chain_next_partition:
	addw	$32, %si
	loop	gateway_chain_find_partition
	jmp	gateway_chain_error

gateway_chain_partition_found:
	addw	$0x0200, %si
	pushw	%ds
	pushw	%es
	popw	%ds
	movw	$0x0200, %di
	movw	$16, %cx
	rep movsw
	popw	%ds

gateway_chain_jump:
	cli
	movb	gateway_menu+MENU_BIOS_ID, %al
	andb	$0x7f, %al
	pushw	%ds
	xorw	%dx, %dx
	movw	%dx, %ds
	movb	%al, 0x0584
	movw	HANDOFF+HO_SAVED_SI, %si
	movw	HANDOFF+HO_SAVED_DI, %di
	popw	%ds
	sti
	ljmp	$0x1fc0, $0

gateway_chain_error:
	pushw	%cs
	popw	%ds
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
	movw	$TABLE_SEG, %ax
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
	movl	$0x48323842, stage2_handoff
	movw	$1, stage2_handoff+4
	movw	$20, stage2_handoff+6
	movb	device_count, %al
	movb	%al, stage2_handoff+8
	pushw	%ds
	xorw	%ax, %ax
	movw	%ax, %ds
	movb	HANDOFF+HO_BOOT_BIOS_ID, %al
	popw	%ds
	movb	%al, stage2_handoff+9
	movl	$(0x10000 + device_table), stage2_handoff+12
	movl	$(0x10000 + bios_gateway32), stage2_handoff+16
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

fatal_handoff_message: .asciz "BOOT98: invalid PBR handoff"
fatal_stage2_message:  .asciz "BOOT98: cannot load BOOT.SYS"
stage2_name:           .ascii "BOOT    SYS"

	.balign 2
disk_flags:            .word 0
chain_entry:           .word 0
clock_calendar:        .space 6
device_count:          .byte 0
fat_sector_scale:      .byte 0
	.balign 2
fat_root_entries:      .word 0
fat_root_sectors:      .word 0
fat_cluster:           .word 0
fat_spc:               .word 0
fat_sector_in_cluster: .word 0
stage2_segment:        .word 0
stage2_offset:         .word 0
	.balign 4
fat_partition_lba:     .long 0
fat_lba:               .long 0
fat_root_lba:          .long 0
fat_data_lba:          .long 0
stage2_file_size:      .long 0
stage2_remaining:      .long 0
stage2_entry_offset:   .long 0

	.balign 4
stage2_handoff:        .space 20
pm_jump:               .long 0
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
gateway_request:        .long 0
gateway_saved_esp:      .long 0
gateway_result:         .long 0
gateway_active_service: .byte 0
gateway_bios_id:        .byte 0
	.balign 4
gateway_lba:            .long 0
gateway_buffer:         .long 0
gateway_module_type:    .word 0
	.balign 16
gateway_menu:           .space MENU_SIZE
	.balign 16
device_table:           .space DEV_DESC_SIZE
