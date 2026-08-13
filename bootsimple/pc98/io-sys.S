/* SPDX-License-Identifier: Zlib */
/*
 * bootsimple FAT16-aware Linux/x86 boot-protocol loader for NEC PC-9800.
 *
 * The partition PBR loads this ordinary FAT file at 1000:0000. It follows
 * the FAT16 cluster chain of VMLINUX, prepares boot_params, loads ELF32
 * PT_LOAD segments directly to their physical addresses and enters Linux.
 */

	.code16
	.section .text
	.globl _start

#include "bootstrap.inc"
#include "linux-boot.inc"

	.set BOUNCE_SEG,	0x3000
	.set FAT_SEG,	0x2800
	.set BOOT_CS,	0x10
	.set BOOT_DS,	0x18

_start:
	cli
	cld
	xorw	%ax, %ax
	movw	%ax, %ss
	movw	$0x7000, %sp
	sti
	pushw	%cs
	popw	%ds

	movw	$0, progress_sectors
	call	clear_boot_screen
	call	draw_boot_screen
	movw	$phase_handoff_message, %si
	call	show_status

	/*
	 * Make the 15-16 MiB region ordinary RAM.  Port 043Bh bit 2 disables
	 * the PC-98 system-space mirror; values 00h and 01h both leave the
	 * mirror enabled.  Do this explicitly because the BIOS setup choice
	 * and the hardware latch are not guaranteed to agree at boot time.
	 */
	movw	$0x043b, %dx
	movb	$0x04, %al
	outb	%al, %dx
	outb	%al, $0x5f

	xorw	%ax, %ax
	movw	%ax, %es
	cmpl	$HANDOFF_MAGIC, %es:HANDOFF+HO_MAGIC
	jne	loader_failure
	cmpw	$HANDOFF_VERSION, %es:HANDOFF+HO_VERSION
	jne	loader_failure
	cmpw	$HANDOFF_SIZE, %es:HANDOFF+HO_SIZE
	jb	loader_failure
	cmpb	$HO_FS_FAT16, %es:HANDOFF+HO_FILESYSTEM_HINT
	jne	loader_failure
	cmpw	$512, %es:HANDOFF+HO_SECTOR_SIZE
	jne	loader_failure
	testw	$HO_FLAG_GEOMETRY, %es:HANDOFF+HO_FLAGS
	jz	loader_failure
	movl	%es:HANDOFF+HO_PARTITION_LBA, %eax
	movl	%eax, partition_lba
	movb	%es:HANDOFF+HO_BOOT_BIOS_ID, %al
	movb	%al, boot_drive
	movb	%es:HANDOFF+HO_HEADS, %al
	testb	%al, %al
	jz	loader_failure
	movb	%al, bios_heads
	movb	%es:HANDOFF+HO_SECTORS, %al
	testb	%al, %al
	jz	loader_failure
	movb	%al, bios_sectors
	movw	%es:HANDOFF+HO_SAVED_SI, %ax
	movw	%ax, bios_si
	movw	%es:HANDOFF+HO_SAVED_DI, %ax
	movw	%ax, bios_di
	movw	$phase_sense_message, %si
	call	show_status
	movb	$PC98_BOOT_DISK_F_BOOT, bios_flags
	movb	boot_drive, %al
	call	bios_sense_device
	jc	loader_failure
	movb	sense_heads, %al
	cmpb	bios_heads, %al
	jne	loader_failure
	movb	sense_sectors, %al
	cmpb	bios_sectors, %al
	jne	loader_failure
	call	initialize_device_table
	call	enumerate_ide_devices
	movw	$phase_partition_message, %si
	call	show_status
	call	validate_boot_partition
	jc	loader_failure

	/* Read the FAT16 BPB. */
	movw	$phase_fat_message, %si
	call	show_status
	movw	$FAT_SEG, %ax
	movw	%ax, %es
	xorw	%bp, %bp
	movw	$512, %bx
	movl	partition_lba, %eax
	call	read_disk
	jc	loader_failure
	movw	%es:0x0b, %ax
	cmpw	$512, %ax
	je	1f
	cmpw	$1024, %ax
	jne	loader_failure
	movb	$2, sector_scale
	jmp	2f
1:
	movb	$1, sector_scale
2:

	movzbl	%es:0x0d, %eax
	testl	%eax, %eax
	jz	loader_failure
	movzbl	sector_scale, %ecx
	imull	%ecx, %eax
	cmpl	$255, %eax
	ja	loader_failure
	movb	%al, sectors_per_cluster
	movl	partition_lba, %eax
	movzwl	%es:0x0e, %ecx
	movzbl	sector_scale, %edx
	imull	%edx, %ecx
	addl	%ecx, %eax
	movl	%eax, fat_lba
	movzbl	%es:0x10, %ecx
	movzwl	%es:0x16, %edx
	movzbl	sector_scale, %ebx
	imull	%ebx, %edx
	imull	%edx, %ecx
	addl	%ecx, %eax
	movl	%eax, root_lba
	movzwl	%es:0x11, %ecx
	movw	%cx, root_entries
	shll	$5, %ecx
	movl	%ecx, %edx
	addl	$511, %edx
	shrl	$9, %edx
	addl	%edx, %eax
	movl	%eax, data_lba

	/* Locate the one supported kernel name, VMLINUX, in the FAT root. */
	movw	$phase_vmlinux_message, %si
	call	show_status
	movw	$BOUNCE_SEG, %ax
	movw	%ax, %es
	xorw	%bp, %bp
	movw	root_entries, %bx
	shlw	$5, %bx
	movl	root_lba, %eax
	call	read_disk
	jc	loader_failure
	xorw	%di, %di
	movw	root_entries, %cx
find_kernel:
	cmpb	$0, %es:(%di)
	je	loader_failure
	cmpb	$0xe5, %es:(%di)
	je	next_entry
	cmpb	$0x0f, %es:0x0b(%di)
	je	next_entry
	pushw	%cx
	pushw	%di
	movw	$vmlinux_name, %si
	movw	$11, %cx
	repe cmpsb
	popw	%di
	popw	%cx
	je	kernel_found
next_entry:
	addw	$32, %di
	loop	find_kernel
	jmp	loader_failure

kernel_found:
	movw	$phase_found_message, %si
	call	show_status
	movw	%es:0x1a(%di), %ax
	cmpw	$2, %ax
	jb	loader_failure
	movw	%ax, first_cluster
	movl	%es:0x1c(%di), %eax
	cmpl	$1024, %eax
	jb	loader_failure
	movl	%eax, file_size
	/* bootsimple deliberately has no logo or menu. */

	/* A20 and unreal-mode FS for copying above 1 MiB. */
	xorb	%al, %al
	outb	%al, $0xf2
	movb	$0x02, %al
	outb	%al, $0xf6
	movw	$0x439, %dx
	inb	%dx, %al
	andb	$0xfb, %al
	outb	%al, %dx
	xorb	%al, %al
	outb	%al, $0xf8
	outb	%al, $0x5f

	xorl	%eax, %eax
	movw	%ds, %ax
	shll	$4, %eax
	addl	$gdt, %eax
	movl	%eax, gdt_desc+2
	lgdt	gdt_desc
	cli
	movl	%cr0, %eax
	orl	$1, %eax
	movl	%eax, %cr0
	movw	$BOOT_DS, %ax
	movw	%ax, %fs
	movl	%cr0, %eax
	andl	$0xfffffffe, %eax
	movl	%eax, %cr0
	sti

	call	stream_reset
	movw	$BOUNCE_SEG, %ax
	movw	%ax, %es
	xorw	%bp, %bp
	call	stream_read_sector
	jc	loader_failure
	movw	$512, %bp
	call	stream_read_sector
	jc	loader_failure

	cmpl	$0x464c457f, %es:0	/* ELF magic */
	jne	loader_failure
	jmp	load_elf

prepare_boot_params:
	movw	$phase_params_message, %si
	call	show_status
	movl	$BP_ADDR, %edi
	movb	$0xff, %fs:0x210(%edi)
	movl	$CMDLINE, %fs:0x228(%edi)
	movb	$2, %fs:0x1e8(%edi)

	/* Pass each INT 1Bh logical geometry independently of ATA IDENTIFY. */
	movl	$0, %fs:0x254(%edi)
	movzbl	device_count, %ecx
	testl	%ecx, %ecx
	jz	loader_failure
	movl	$PC98_DATA, %fs:0x250(%edi)
	movl	$PC98_DATA, %ebx
	xorw	%si, %si
1:
	cmpl	$1, %ecx
	je	2f
	leal	32(%ebx), %eax
	jmp	3f
2:
	xorl	%eax, %eax
3:
	movl	%eax, %fs:(%ebx)
	movl	$0, %fs:4(%ebx)
	movl	$SETUP_PC98_DISK, %fs:8(%ebx)
	movl	$PC98_BOOT_DISK_SIZE, %fs:12(%ebx)
	movl	$PC98_BOOT_DISK_MAGIC, %fs:16(%ebx)
	movw	$PC98_BOOT_DISK_VERSION, %fs:20(%ebx)
	movw	$PC98_BOOT_DISK_SIZE, %fs:22(%ebx)
	movb	device_bios_ids(%si), %al
	movb	%al, %fs:24(%ebx)
	movb	device_heads(%si), %al
	movb	%al, %fs:25(%ebx)
	movb	device_sectors(%si), %al
	movb	%al, %fs:26(%ebx)
	movb	device_flags(%si), %al
	movb	%al, %fs:27(%ebx)
	movl	$0, %fs:28(%ebx)
	addl	$32, %ebx
	incw	%si
	decl	%ecx
	jnz	1b

	/* e820 conventional and extended RAM from the PC-98 BIOS work area. */
	movl	$0x501, %ebx
	movzbl	%fs:(%ebx), %eax
	andl	$7, %eax
	incl	%eax
	shll	$17, %eax
	movl	$0, %fs:0x2d0(%edi)
	movl	$0, %fs:0x2d4(%edi)
	movl	%eax, %fs:0x2d8(%edi)
	movl	$0, %fs:0x2dc(%edi)
	movl	$1, %fs:0x2e0(%edi)
	movl	$0x401, %ebx
	movzbl	%fs:(%ebx), %eax
	testl	%eax, %eax
	jz	loader_failure
	shll	$17, %eax
	movl	$0x100000, %fs:0x2e4(%edi)
	movl	$0, %fs:0x2e8(%edi)
	movl	%eax, %fs:0x2ec(%edi)
	movl	$0, %fs:0x2f0(%edi)
	movl	$1, %fs:0x2f4(%edi)
	movl	$0x594, %ebx
	movzwl	%fs:(%ebx), %eax
	testl	%eax, %eax
	jz	6f
	shll	$20, %eax
	movl	$0x1000000, %fs:0x2f8(%edi)
	movl	$0, %fs:0x2fc(%edi)
	movl	%eax, %fs:0x300(%edi)
	movl	$0, %fs:0x304(%edi)
	movl	$1, %fs:0x308(%edi)
	movb	$3, %fs:0x1e8(%edi)
6:
	call	copy_command_line

	movw	$starting_direct_message, %si
	call	show_status

	cli
	movb	$0xff, %al
	outb	%al, $0x0a
	outb	%al, $0x5f
	movb	$0x7f, %al
	outb	%al, $0x02
	outb	%al, $0x5f
	outb	%al, $0x50
	movl	$BP_ADDR, %esi
	xorl	%ebp, %ebp
	xorl	%edi, %edi
	xorl	%ebx, %ebx
	movl	%cr0, %eax
	orl	$1, %eax
	movl	%eax, %cr0
	movw	$BOOT_DS, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %ss
	ljmpl	*%cs:kernel_entry_ptr

/*
 * Load an ELF32/i386 vmlinux without keeping a compressed input image and
 * decompression workspace in guest RAM.  Program headers must fit in the
 * first 1024 bytes already held in the bounce buffer.  Up to four PT_LOAD
 * segments are copied directly to their physical addresses.
 */
load_elf:
	movw	$phase_elf_message, %si
	call	show_status
	cmpb	$1, %es:4		/* ELFCLASS32 */
	jne	loader_failure
	cmpb	$1, %es:5		/* ELFDATA2LSB */
	jne	loader_failure
	cmpw	$3, %es:0x12		/* EM_386 */
	jne	loader_failure
	cmpw	$32, %es:0x2a		/* sizeof(Elf32_Phdr) */
	jne	loader_failure
	movzwl	%es:0x2c, %ecx
	testl	%ecx, %ecx
	jz	loader_failure
	cmpl	$16, %ecx
	ja	loader_failure
	movl	%es:0x1c, %eax
	movl	%eax, %edx
	shll	$5, %ecx
	addl	%ecx, %edx
	cmpl	$1024, %edx
	ja	loader_failure
	movw	%ax, %di
	movzwl	%es:0x2c, %ecx
	movb	$0, elf_segment_count
	movb	$0, elf_entry_valid
	movl	$0, elf_total_bytes

elf_parse_phdr:
	cmpl	$1, %es:(%di)		/* PT_LOAD */
	jne	elf_next_phdr
	cmpb	$4, elf_segment_count
	jae	loader_failure
	movl	%es:0x10(%di), %eax	/* p_filesz */
	cmpl	%es:0x14(%di), %eax	/* p_memsz */
	ja	loader_failure
	movl	%es:4(%di), %edx	/* p_offset */
	addl	%eax, %edx
	jc	loader_failure
	cmpl	file_size, %edx
	ja	loader_failure
	movzbl	elf_segment_count, %ebx
	shll	$4, %ebx
	addw	$elf_segments, %bx
	movl	%es:4(%di), %eax
	movl	%eax, (%bx)
	movl	%es:0x0c(%di), %eax	/* p_paddr */
	cmpl	$0x00100000, %eax
	jb	loader_failure
	movl	%eax, %edx
	addl	%es:0x14(%di), %edx	/* p_paddr + p_memsz */
	jc	loader_failure
	pushl	%ecx
	movl	%es:0x18, %ecx		/* e_entry */
	cmpl	%eax, %ecx
	jb	1f
	cmpl	%edx, %ecx
	jae	1f
	movb	$1, elf_entry_valid
1:
	popl	%ecx
	movl	%eax, 4(%bx)
	movl	%es:0x10(%di), %eax
	movl	%eax, 8(%bx)
	addl	%eax, elf_total_bytes
	movl	%es:0x14(%di), %eax
	movl	%eax, 12(%bx)
	incb	elf_segment_count
elf_next_phdr:
	addw	$32, %di
	decl	%ecx
	jz	elf_parse_done
	jmp	elf_parse_phdr

elf_parse_done:
	cmpb	$0, elf_segment_count
	je	loader_failure
	cmpb	$0, elf_entry_valid
	je	loader_failure
	movl	%es:0x18, %eax		/* e_entry is a physical address */
	movl	%eax, kernel_entry_ptr
	call	clear_boot_params
	movl	$0, loaded_bytes
	movl	file_size, %eax
	call	bytes_to_kb
	movw	$kernel_size_field, %di
	call	format_decimal6
	movl	elf_segments+8, %eax
	movl	%eax, elf_code_bytes
	call	bytes_to_kb
	movw	$code_total_field, %di
	call	format_decimal6
	movl	$0, elf_data_bytes
	cmpb	$2, elf_segment_count
	jb	1f
	movl	elf_segments+24, %eax
	movl	%eax, elf_data_bytes
	call	bytes_to_kb
	movw	$data_total_field, %di
	call	format_decimal6
1:
	call	update_transfer_display
	movw	$elf_segments, elf_segment_ptr
	movb	elf_segment_count, %al
	movb	%al, elf_segments_left
	movl	$0, elf_stream_pos
	call	stream_reset

elf_load_segment:
	cmpb	$0, elf_segments_left
	je	elf_load_done
	movw	elf_segment_ptr, %si
	movl	(%si), %eax		/* p_offset */
	testl	$511, %eax
	jnz	loader_failure
	cmpl	elf_stream_pos, %eax
	jb	loader_failure
	movl	8(%si), %edx		/* p_filesz */
	movl	4(%si), %ecx
	movl	%ecx, cur_dest
	movl	%edx, remaining
	call	update_transfer_display
	testl	%edx, %edx
	jz	elf_zero_segment
	movl	(%si), %ecx
	subl	elf_stream_pos, %ecx
	testl	$511, %ecx
	jnz	loader_failure
	shrl	$9, %ecx
elf_seek_segment:
	testl	%ecx, %ecx
	jz	elf_begin_segment
	pushl	%ecx
	movw	$BOUNCE_SEG, %ax
	movw	%ax, %es
	xorw	%bp, %bp
	call	stream_read_sector
	popl	%ecx
	jc	loader_failure
	addl	$512, elf_stream_pos
	incw	seek_progress_sectors
	testb	$0xff, seek_progress_sectors
	jnz	1f
	call	update_transfer_display
1:
	decl	%ecx
	jmp	elf_seek_segment

elf_begin_segment:
	movw	elf_segment_ptr, %si
	movl	4(%si), %eax
	movl	%eax, cur_dest
	movl	8(%si), %eax
	movl	%eax, remaining
	call	update_transfer_display
elf_copy_next:
	cmpl	$0, remaining
	je	elf_zero_segment
	movw	$BOUNCE_SEG, %ax
	movw	%ax, %es
	xorw	%bp, %bp
	call	stream_read_sector
	jc	loader_failure
	addl	$512, elf_stream_pos
	movl	$512, %edx
	cmpl	%edx, remaining
	jae	elf_copy_size_ready
	movl	remaining, %edx
elf_copy_size_ready:
	movl	cur_dest, %edi
	xorl	%esi, %esi
	movl	%edx, %ecx
	shrl	$2, %ecx
	jz	elf_copy_tail
elf_copy_dwords:
	movl	%es:(%si), %eax
	movl	%eax, %fs:(%edi)
	addl	$4, %esi
	addl	$4, %edi
	decl	%ecx
	jnz	elf_copy_dwords
elf_copy_tail:
	movl	%edx, %ecx
	andl	$3, %ecx
	jz	elf_copy_account
elf_copy_bytes:
	movb	%es:(%si), %al
	movb	%al, %fs:(%edi)
	incl	%esi
	incl	%edi
	decl	%ecx
	jnz	elf_copy_bytes
elf_copy_account:
	addl	%edx, cur_dest
	subl	%edx, remaining
	addl	%edx, loaded_bytes
	incw	progress_sectors
	testb	$0xff, progress_sectors
	jnz	elf_copy_next
	call	update_transfer_display
	jmp	elf_copy_next

elf_zero_segment:
	call	update_transfer_display
	movw	elf_segment_ptr, %si
	movl	12(%si), %ecx		/* p_memsz - p_filesz */
	subl	8(%si), %ecx
	movl	4(%si), %edi
	addl	8(%si), %edi
	movl	%ecx, %edx
	shrl	$2, %ecx
	jz	elf_zero_tail
	xorl	%eax, %eax
elf_zero_dwords:
	movl	%eax, %fs:(%edi)
	addl	$4, %edi
	decl	%ecx
	jnz	elf_zero_dwords
elf_zero_tail:
	movl	%edx, %ecx
	andl	$3, %ecx
	jz	elf_segment_done
elf_zero_bytes:
	movb	$0, %fs:(%edi)
	incl	%edi
	decl	%ecx
	jnz	elf_zero_bytes
elf_segment_done:
	call	update_transfer_display
	addw	$16, elf_segment_ptr
	decb	elf_segments_left
	jmp	elf_load_segment

elf_load_done:
	call	update_transfer_display
	jmp	prepare_boot_params

clear_boot_params:
	pushl	%ecx
	pushl	%edi
	movl	$BP_ADDR, %edi
	movl	$1024, %ecx
1:
	movl	$0, %fs:(%edi)
	addl	$4, %edi
	decl	%ecx
	jnz	1b
	popl	%edi
	popl	%ecx
	ret

/* Clear all text VRAM and the standard GDC B/R/G/I graphics planes. */
clear_boot_screen:
	pushl	%eax
	pushw	%cx
	pushw	%di
	pushw	%dx
	pushw	%es

	/*
	 * Do not depend on the graphics state inherited from POST.  NEC ROMs
	 * may hand control to the IPL with the slave GDC stopped, while the
	 * compatible BIOS normally leaves it configured.  AH=42h selects the
	 * 640x400 ALL area in colour mode, bank 0; AH=40h enables scan-out.
	 */
	pushw	%bx
	pushw	%si
	pushw	%ds
	movb	$0xc0, %ch		/* ALL, colour, display bank 0 */
	movb	$0x42, %ah
	int	$0x18
	movb	$0x40, %ah
	int	$0x18
	popw	%ds
	popw	%si
	popw	%bx

	/* Standard PC-98 digital palette, including colour 7 = white. */
	movb	$0x37, %al
	outb	%al, $0xa8
	movb	$0x15, %al
	outb	%al, $0xaa
	movb	$0x26, %al
	outb	%al, $0xac
	movb	$0x04, %al
	outb	%al, $0xae

	/* Select graphics display/draw bank 0 and enable the I plane. */
	xorb	%al, %al
	outb	%al, $0xa4
	outb	%al, $0xa6
	outb	%al, $0x7c		/* disable GRCG/EGC interception */
	outb	%al, $0x5f
	movb	$0x07, %al		/* enable extended-mode writes */
	outb	%al, $0x6a
	movb	$0x20, %al		/* disable 256-colour PEGC mode */
	outb	%al, $0x6a
	movb	$0x04, %al		/* disable EGC mode */
	outb	%al, $0x6a
	movb	$0x06, %al		/* close extended-mode writes */
	outb	%al, $0x6a
	movb	$0x01, %al		/* mode2[0]: 16-colour plane access */
	outb	%al, $0x6a
	outb	%al, $0x5f

	/* Character and attribute planes are each 8 KiB. */
	movw	$0xa000, %ax
	movw	%ax, %es
	xorw	%di, %di
	movw	$0x0020, %ax
	movw	$4096, %cx
	rep stosw
	movw	$0xa200, %ax
	movw	%ax, %es
	xorw	%di, %di
	movw	$0x00e1, %ax
	movw	$4096, %cx
	rep stosw

	/* 640 x 400 planar graphics: 80 bytes x 400 lines per plane. */
	xorl	%eax, %eax
	movw	$0xa800, %dx
1:
	movw	%dx, %es
	xorw	%di, %di
	movw	$8192, %cx
	rep stosl
	addw	$0x0800, %dx
	cmpw	$0xc000, %dx
	jne	1b
	/* Plane I is mapped separately at E0000 in 16-colour mode. */
	movw	$0xe000, %ax
	movw	%ax, %es
	xorw	%di, %di
	movw	$8192, %cx
	xorl	%eax, %eax
	rep stosl

	/* Start the slave (graphics) GDC so the optional bitmap is visible. */
	movw	$0xffff, %cx
2:
	inb	$0xa0, %al
	testb	$0x02, %al
	jz	3f
	xorb	%al, %al
	outb	%al, $0x5f
	loop	2b
	jmp	4f
3:
	movb	$0x6b, %al		/* uPD7220 START */
	outb	%al, $0xa2
	xorb	%al, %al
	outb	%al, $0x5f
	outb	%al, $0x5f
4:

	popw	%es
	popw	%dx
	popw	%di
	popw	%cx
	popl	%eax
	ret

/* Draw the fixed boot-loader layout.  Row 0 and rows 2/5/9 stay blank. */
draw_boot_screen:
	pushw	%di
	pushw	%si
	movw	$160, %di		/* row 1 */
	movw	$banner_machine, %si
	call	draw_mixed_string
	movw	$480, %di		/* row 3 */
	movw	$banner_loader, %si
	call	draw_mixed_string
	movw	$640, %di		/* row 4 */
	movw	$banner_copyright, %si
	call	draw_mixed_string
	call	draw_progress_lines
	popw	%si
	popw	%di
	ret

draw_progress_lines:
	pushw	%di
	pushw	%si
	movw	$960, %di		/* row 6 */
	movw	$kernel_size_line, %si
	call	draw_mixed_string
	movw	$1120, %di		/* row 7 */
	movw	$code_progress_line, %si
	call	draw_mixed_string
	movw	$1280, %di		/* row 8 */
	movw	$data_progress_line, %si
	call	draw_mixed_string
	popw	%si
	popw	%di
	ret

/*
 * Draw a mixed PC-98 text string at text-VRAM byte offset DI.
 * Ordinary nonzero bytes are ANK codes.  Marker 01h is followed by a
 * two-byte JIS code (row, cell); the word is duplicated for the two cells
 * expected by real PC-98 character generators.  A zero byte terminates.
 */
draw_mixed_string:
	pushw	%ax
	pushw	%si
	pushw	%di
	pushw	%es
	movw	$0xa000, %ax
	movw	%ax, %es
1:
	lodsb
	testb	%al, %al
	jz	3f
	cmpb	$0x01, %al
	je	2f
	xorb	%ah, %ah
	stosw
	jmp	1b
2:
	lodsw				/* JIS row in AL, cell in AH */
	stosw
	stosw
	jmp	1b
3:
	popw	%es
	popw	%di
	popw	%si
	popw	%ax
	ret

/* Convert a byte count in EAX to a rounded-up KiB count. */
bytes_to_kb:
	addl	$1023, %eax
	shrl	$10, %eax
	ret

/* Write EAX right-aligned in a six-character decimal field at DS:DI. */
format_decimal6:
	pushl	%eax
	pushl	%ebx
	pushl	%edx
	pushw	%cx
	pushw	%di
	movw	$6, %cx
	movb	$' ', %dl
1:
	movb	%dl, (%di)
	incw	%di
	loop	1b
	subw	$1, %di
	movl	$10, %ebx
	testl	%eax, %eax
	jnz	2f
	movb	$'0', (%di)
	jmp	4f
2:
	xorl	%edx, %edx
	divl	%ebx
	addb	$'0', %dl
	movb	%dl, (%di)
	testl	%eax, %eax
	jz	4f
	decw	%di
	jmp	2b
4:
	popw	%di
	popw	%cx
	popl	%edx
	popl	%ebx
	popl	%eax
	ret

/* Update the code/data transfer fields from the cumulative ELF byte count. */
update_transfer_display:
	pushl	%eax
	pushl	%ebx
	pushw	%di

	movl	loaded_bytes, %eax
	movl	elf_code_bytes, %ebx
	cmpl	%ebx, %eax
	jbe	1f
	movl	%ebx, %eax
1:
	call	bytes_to_kb
	movw	$code_done_field, %di
	call	format_decimal6

	movl	loaded_bytes, %eax
	subl	elf_code_bytes, %eax
	jnc	2f
	xorl	%eax, %eax
2:
	movl	elf_data_bytes, %ebx
	cmpl	%ebx, %eax
	jbe	3f
	movl	%ebx, %eax
3:
	call	bytes_to_kb
	movw	$data_done_field, %di
	call	format_decimal6
	call	draw_progress_lines

	popw	%di
	popl	%ebx
	popl	%eax
	ret

/*
 * Leave visible progress on the first 80-column GDC text row while the
 * compressed kernel is running without a console. DS:SI is a NUL-terminated
 * ANK string. Character and attribute planes use a two-byte cell stride.
 */
show_status:
	pushw	%ax
	pushw	%bx
	pushw	%cx
	pushw	%si
	pushw	%di
	pushw	%es
	movw	%si, %bx

	movw	$0xa000, %ax
	movw	%ax, %es
	xorw	%di, %di
	movw	$0x0020, %ax
	movw	$80, %cx
	rep stosw
	xorw	%di, %di
	movw	%bx, %si
1:
	lodsb
	testb	%al, %al
	jz	2f
	xorb	%ah, %ah
	stosw
	jmp	1b
2:
	movw	$0xa200, %ax
	movw	%ax, %es
	xorw	%di, %di
	movw	$0x00e1, %ax
	movw	$80, %cx
	rep stosw

	popw	%es
	popw	%di
	popw	%si
	popw	%cx
	popw	%bx
	popw	%ax
	ret

/*
 * Re-run PC-98 INT 1Bh/AH=84h SENSE for the boot-origin disk.  The PBR
 * needed geometry to load us, but Linux must receive a value independently
 * confirmed by IO.SYS.  Genuine ROMs do not promise to preserve registers.
 */
bios_sense_device:
	movb	%al, %cs:sense_drive
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
	movb	%cs:sense_drive, %al
	movb	$0x84, %ah
	movw	$0x0100, %bx
	movw	%cs:bios_si, %si
	movw	%cs:bios_di, %di
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
	jc	1f
	cmpb	$0x20, %cs:sense_status
	jae	1f
	cmpw	$512, %cs:sense_bytes
	jne	1f
	cmpb	$0, %cs:sense_heads
	je	1f
	cmpb	$0, %cs:sense_sectors
	je	1f
	clc
	ret
1:
	stc
	ret

/* Record the already validated boot-origin geometry in slot zero. */
initialize_device_table:
	movb	boot_drive, %al
	movb	%al, device_bios_ids
	movb	bios_heads, %al
	movb	%al, device_heads
	movb	bios_sectors, %al
	movb	%al, device_sectors
	movb	$PC98_BOOT_DISK_F_BOOT, device_flags
	movb	$1, device_count
	ret

/*
 * Probe only IDE units whose BIOS work-area bitmap bit is set.  Some real
 * ROMs do not return from SENSE on an absent unit, so a blind 80h..83h scan
 * is forbidden.
 */
enumerate_ide_devices:
	pushw	%ax
	pushw	%bx
	pushw	%cx
	pushw	%dx
	pushw	%di
	pushw	%ds
	xorw	%ax, %ax
	movw	%ax, %ds
	movb	0x055d, %bl
	popw	%ds
	andb	$0x0f, %bl
	movb	$0x80, %al
	movw	$4, %cx
1:
	testb	$1, %bl
	jz	3f
	cmpb	boot_drive, %al
	je	3f
	call	bios_sense_device
	jc	3f
	call	bios_probe_device
	jc	3f
	xorw	%di, %di
	movb	device_count, %dl
	xorb	%dh, %dh
	movw	%dx, %di
	cmpw	$12, %di
	jae	4f
	movb	%al, device_bios_ids(%di)
	movb	sense_heads, %dl
	movb	%dl, device_heads(%di)
	movb	sense_sectors, %dl
	movb	%dl, device_sectors(%di)
	movb	$0, device_flags(%di)
	incb	device_count
3:
	shrb	$1, %bl
	incb	%al
	loop	1b
4:
	popw	%di
	popw	%dx
	popw	%cx
	popw	%bx
	popw	%ax
	ret

/* Confirm a present IDE unit with one read-only CHS 0/0/0 access. */
bios_probe_device:
	movb	%al, %cs:probe_drive
	pushw	%ax
	pushw	%bx
	pushw	%cx
	pushw	%dx
	pushw	%si
	pushw	%di
	pushw	%ds
	pushw	%es
	pushw	%bp
	movw	$BOUNCE_SEG, %ax
	movw	%ax, %es
	xorw	%bp, %bp
	xorw	%ax, %ax
	movw	%ax, %ds
	movb	%cs:probe_drive, %al
	movb	$0x06, %ah
	movw	$512, %bx
	xorw	%cx, %cx
	xorw	%dx, %dx
	movw	%cs:bios_si, %si
	movw	%cs:bios_di, %di
	int	$0x1b
	movb	%ah, %cs:probe_status
	pushfw
	popw	%ax
	movw	%ax, %cs:probe_flags
	popw	%bp
	popw	%es
	popw	%ds
	popw	%di
	popw	%si
	popw	%dx
	popw	%cx
	popw	%bx
	popw	%ax
	pushw	%cs:probe_flags
	popfw
	jc	1f
	cmpb	$0x20, %cs:probe_status
	jae	1f
	clc
	ret
1:
	stc
	ret

/* Copy the build-profile command line into the Linux low-memory buffer. */
copy_command_line:
	pushw	%si
	pushl	%edi
	pushw	%cx
	movw	$embedded_command_line, %si
	movl	$CMDLINE, %edi
	movw	$(embedded_command_line_end - embedded_command_line), %cx
1:
	movb	(%si), %al
	movb	%al, %fs:(%edi)
	incw	%si
	incl	%edi
	loop	1b
	popw	%cx
	popl	%edi
	popw	%si
	ret

stream_reset:
	movw	first_cluster, %ax
	movw	%ax, current_cluster
	movb	$0, sector_in_cluster
	ret

/*
 * Read and interpret the NEC98 partition table with the BIOS logical CHS.
 * Accept a handoff LBA only when it matches an active table entry's IPL or
 * partition-start CHS.  This also validates a genuine IPL -> PBR handoff.
 */
validate_boot_partition:
	pushl	%eax
	pushl	%ebx
	pushl	%ecx
	pushl	%edx
	pushw	%di
	pushw	%es
	pushw	%bp
	movw	$FAT_SEG, %ax
	movw	%ax, %es
	xorw	%bp, %bp
	movl	$1, %eax
	movw	$512, %bx
	call	read_disk
	jc	8f
	xorw	%di, %di
	movw	$16, %cx
1:
	movw	%es:(%di), %ax
	testw	%ax, %ax
	jz	7f
	pushw	%cx
	movb	%es:4(%di), %dl
	movb	%es:5(%di), %dh
	movw	%es:6(%di), %ax
	call	chs_to_lba
	cmp	partition_lba, %eax
	je	6f
	movb	%es:8(%di), %dl
	movb	%es:9(%di), %dh
	movw	%es:10(%di), %ax
	call	chs_to_lba
	cmp	partition_lba, %eax
	je	6f
	popw	%cx
7:
	addw	$32, %di
	loop	1b
8:
	movw	$1, read_flags
	jmp	9f
6:
	popw	%cx
	movw	$0, read_flags
9:
	popw	%bp
	popw	%es
	popw	%di
	popl	%edx
	popl	%ecx
	popl	%ebx
	popl	%eax
	cmpw	$0, read_flags
	jne	10f
	clc
	ret
10:
	stc
	ret

/* AX=cylinder, DH=head, DL=zero-based sector; return EAX=absolute LBA. */
chs_to_lba:
	movzwl	%ax, %eax
	movzbl	bios_heads, %ecx
	imull	%ecx, %eax
	movzbl	%dh, %ecx
	addl	%ecx, %eax
	movzbl	bios_sectors, %ecx
	imull	%ecx, %eax
	movzbl	%dl, %ecx
	addl	%ecx, %eax
	ret

/* Read the next 512-byte file sector to ES:BP and advance the FAT stream. */
stream_read_sector:
	movzwl	current_cluster, %eax
	cmpl	$2, %eax
	jb	9f
	subl	$2, %eax
	movzbl	sectors_per_cluster, %ecx
	imull	%ecx, %eax
	movzbl	sector_in_cluster, %ecx
	addl	%ecx, %eax
	addl	data_lba, %eax
	movw	$512, %bx
	call	read_disk
	jc	9f
	incb	sector_in_cluster
	movb	sector_in_cluster, %al
	cmpb	sectors_per_cluster, %al
	jb	8f
	movb	$0, sector_in_cluster
	movw	current_cluster, %ax
	call	fat_next
	cmpw	$0xfff8, %ax
	jb	7f
	movw	$0xffff, current_cluster
	jmp	8f
7:
	movw	%ax, current_cluster
8:
	clc
	ret
9:
	stc
	ret

/* AX=current cluster, return AX=next FAT16 cluster. */
fat_next:
	pushw	%bx
	pushw	%cx
	pushw	%dx
	pushw	%es
	pushw	%bp
	movzwl	%ax, %edx
	shll	$1, %edx
	movl	%edx, %eax
	shrl	$9, %eax
	addl	fat_lba, %eax
	andw	$511, %dx
	movw	$FAT_SEG, %cx
	movw	%cx, %es
	xorw	%bp, %bp
	movw	$512, %bx
	call	read_disk
	jc	7f
	movw	%dx, %bx
	movw	%es:(%bx), %ax
	popw	%bp
	popw	%es
	popw	%dx
	popw	%cx
	popw	%bx
	ret
7:
	popw	%bp
	popw	%es
	popw	%dx
	popw	%cx
	popw	%bx
	movw	$0xffff, %ax
	ret

read_disk:
	pushl	%eax
	pushl	%ebx
	pushw	%cx
	pushw	%dx
	pushw	%si
	pushw	%di
	pushw	%ds
	pushw	%es
	pushw	%bp

	/* BX is a byte count; issue conservative one-sector INT 1Bh reads. */
	testw	%bx, %bx
	jz	9f
	testw	$511, %bx
	jnz	9f
	movw	%bx, %cx
	shrw	$9, %cx
	movw	%cx, %cs:pio_count
	movl	%eax, %cs:pio_lba
	movw	%bp, %cs:pio_offset
	movw	%es, %ax
	movw	%ax, %cs:pio_segment

1:
	cmpw	$0, %cs:pio_count
	je	8f
	movl	%cs:pio_lba, %eax
	xorl	%edx, %edx
	movzbl	%cs:bios_heads, %ebx
	movzbl	%cs:bios_sectors, %ecx
	imull	%ecx, %ebx
	divl	%ebx
	cmpl	$0xffff, %eax
	ja	9f
	movw	%ax, %cs:pio_cylinder
	movl	%edx, %eax
	xorl	%edx, %edx
	movzbl	%cs:bios_sectors, %ebx
	divl	%ebx
	movb	%al, %dh
	movw	%cs:pio_cylinder, %cx
	movw	%cs:pio_segment, %ax
	movw	%ax, %es
	movw	%cs:pio_offset, %bp
	xorw	%ax, %ax
	movw	%ax, %ds
	movb	%cs:boot_drive, %al
	orb	$0x80, %al
	movb	$0x06, %ah
	movw	$512, %bx
	movw	%cs:bios_si, %si
	movw	%cs:bios_di, %di
	int	$0x1b
	jc	9f
	incl	%cs:pio_lba
	addw	$512, %cs:pio_offset
	decw	%cs:pio_count
	jmp	1b

8:
	movw	$0, %cs:read_flags
	jmp	10f
9:
	movw	$1, %cs:read_flags
10:
	popw	%bp
	popw	%es
	popw	%ds
	popw	%di
	popw	%si
	popw	%dx
	popw	%cx
	popl	%ebx
	popl	%eax
	cmpw	$0, %cs:read_flags
	jne	11f
	clc
	ret
11:
	stc
	ret

loader_failure:
	movb	$0x06, %al
	outb	%al, $0x37

1:
	hlt
	jmp	1b

	.balign 8
gdt:
	.quad	0
	.quad	0
	.quad	0x00cf9a000000ffff
	.quad	0x00cf92000000ffff
gdt_end:
gdt_desc:
	.word	gdt_end - gdt - 1
	.long	0

vmlinux_name:
	.ascii	"VMLINUX    "
partition_lba:
	.long	0
fat_lba:
	.long	0
root_lba:
	.long	0
data_lba:
	.long	0
file_size:
	.long	0
remaining:
	.long	0
cur_dest:
	.long	0
first_cluster:
	.word	0
current_cluster:
	.word	0
root_entries:
	.word	0
sectors_per_cluster:
	.byte	0
sector_scale:
	.byte	0
boot_drive:
	.byte	0
bios_heads:
	.byte	0
bios_sectors:
	.byte	0
bios_flags:
	.byte	0
sector_in_cluster:
	.byte	0
progress_sectors:
	.word	0
seek_progress_sectors:
	.word	0
	.balign 4
pio_lba:
	.long	0
pio_count:
	.word	0
pio_cylinder:
	.word	0
pio_offset:
	.word	0
pio_segment:
	.word	0
bios_si:
	.word	0
bios_di:
	.word	0
sense_bytes:
	.word	0
sense_cylinders:
	.word	0
sense_flags:
	.word	0
sense_status:
	.byte	0
sense_heads:
	.byte	0
sense_sectors:
	.byte	0
sense_drive:
	.byte	0
probe_drive:
	.byte	0
probe_status:
	.byte	0
probe_flags:
	.word	0
device_count:
	.byte	0
device_bios_ids:
	.fill	12, 1, 0
device_heads:
	.fill	12, 1, 0
device_sectors:
	.fill	12, 1, 0
device_flags:
	.fill	12, 1, 0
loaded_bytes:
	.long	0
kernel_entry_ptr:
	.long	0
	.word	BOOT_CS
elf_total_bytes:
	.long	0
elf_code_bytes:
	.long	0
elf_data_bytes:
	.long	0
elf_segment_ptr:
	.word	0
elf_segment_count:
	.byte	0
elf_entry_valid:
	.byte	0
elf_segments_left:
	.byte	0
	.balign 4
elf_stream_pos:
	.long	0
	.balign 4
elf_segments:
	.fill	4 * 16, 1, 0
read_flags:
	.word	0
	.balign 2
embedded_command_line:
	.incbin "cmdline.bin"
	.byte	0
embedded_command_line_end:
phase_handoff_message:
	.asciz	"01 handoff"
phase_sense_message:
	.asciz	"02 disk SENSE"
phase_partition_message:
	.asciz	"03 BOOT partition"
phase_fat_message:
	.asciz	"04 FAT16"
phase_vmlinux_message:
	.asciz	"05 find VMLINUX"
phase_found_message:
	.asciz	"06 VMLINUX found"
phase_elf_message:
	.asciz	"07 load ELF segments"
phase_params_message:
	.asciz	"08 build Linux boot_params"
starting_direct_message:
	.asciz	"Starting Linux..."

/* PC-98 ANK strings; 01h introduces a two-byte JIS row/cell code. */
banner_machine:
	.ascii	"NEC PC-9800 "
	.byte	0xbc, 0xd8, 0xb0, 0xbd, 0xde
	.ascii	" "
	.byte	0xca, 0xdf, 0xb0, 0xbf, 0xc5, 0xd9
	.ascii	" "
	.byte	0xba, 0xdd, 0xcb, 0xdf, 0xad, 0xb0, 0xc0
	.byte	0
banner_loader:
	.asciz	"linux-pc98 bootsimple 0.1"
banner_copyright:
	.asciz	"Assembly FAT16 VMLINUX loader for PC-98"
kernel_size_line:
	.byte	0xb6, 0xb0, 0xc8, 0xd9, 0xbb, 0xb2, 0xbd, 0xde
	.ascii	" "
kernel_size_field:
	.ascii	"     0"
	.asciz	" KB"
code_progress_line:
	.byte	0xba, 0xb0, 0xc4, 0xde
	.ascii	" "
code_done_field:
	.ascii	"     0"
	.ascii	" / "
code_total_field:
	.ascii	"     0"
	.asciz	" KB"
data_progress_line:
	.byte	0xc3, 0xde, 0xb0, 0xc0
	.ascii	" "
data_done_field:
	.ascii	"     0"
	.ascii	" / "
data_total_field:
	.ascii	"     0"
	.asciz	" KB"
