/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * FAT16-aware Linux/x86 boot-protocol loader for NEC PC-9800.
 *
 * The partition PBR loads this stage from the FAT reserved area at
 * 1000:0000.  It follows the FAT16 cluster chain of VMLINUX or BZIMAGE,
 * prepares boot_params, loads the protected-mode kernel and enters it.
 */

	.code16
	.section .text
	.globl _start

	.set HANDOFF,	0x0700
	.set BOUNCE_SEG,	0x3000
	.set FAT_SEG,	0x2800
	.set BP_ADDR,	0x20000
	.set CMDLINE,	0x21000
	.set PC98_DATA,	0x22000
	.set KDEST,	0x100000
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
	movb	$(load_dot_field - load_status), progress_column
	call	clear_boot_screen
	call	draw_boot_screen

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
	cmpl	$0x4839384c, %es:HANDOFF
	jne	loader_failure
	movl	%es:HANDOFF+4, %eax
	movl	%eax, partition_lba
	movb	%es:HANDOFF+8, %al
	movb	%al, boot_drive
	cmpb	$1, %es:HANDOFF+11
	jne	loader_failure
	movb	%es:HANDOFF+9, %al
	testb	%al, %al
	jz	loader_failure
	movb	%al, bios_heads
	movb	%es:HANDOFF+10, %al
	testb	%al, %al
	jz	loader_failure
	movb	%al, bios_sectors
	movw	%es:HANDOFF+12, %ax
	movw	%ax, bios_si
	movw	%es:HANDOFF+14, %ax
	movw	%ax, bios_di
	movb	%es:HANDOFF+16, %al
	movb	%al, bios_flags
	call	validate_boot_partition
	jc	loader_failure

	/* Read the FAT16 BPB. */
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

	/* Locate BZIMAGE or VMLINUX in the FAT root directory. */
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
	movw	$kernel_name, %si
	movw	$11, %cx
	repe cmpsb
	popw	%di
	popw	%cx
	je	kernel_found
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
	movw	%es:0x1a(%di), %ax
	cmpw	$2, %ax
	jb	loader_failure
	movw	%ax, first_cluster
	movl	%es:0x1c(%di), %eax
	cmpl	$1024, %eax
	jb	loader_failure
	movl	%eax, file_size
	call	find_and_load_logo

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
	je	load_elf

	cmpw	$0xaa55, %es:0x1fe
	jne	loader_failure
	cmpl	$0x53726448, %es:0x202	/* "HdrS" */
	jne	loader_failure
	testb	$1, %es:0x211		/* LOADED_HIGH */
	jz	loader_failure

	movzbw	%es:0x1f1, %ax
	testw	%ax, %ax
	jnz	1f
	movw	$4, %ax
1:
	movw	%ax, setup_sects
	movzbw	%es:0x201, %ax
	addw	$0x202, %ax
	movw	%ax, header_end
	movl	%es:0x1f4, %eax
	shll	$4, %eax
	movl	%eax, remaining
	movzwl	setup_sects, %eax
	incl	%eax
	shll	$9, %eax
	movl	%eax, payload_offset
	addl	remaining, %eax
	cmpl	file_size, %eax
	ja	loader_failure
	movl	payload_offset, %eax
	movl	%eax, loaded_bytes
	movw	$load_count_field, %di
	call	format_decimal8
	movl	file_size, %eax
	movw	$load_total_field, %di
	call	format_decimal8
	movw	$load_status, %si
	call	show_status

	/* Clear boot_params and copy the setup header. */
	call	clear_boot_params
	movw	$0x1f1, %si
	movl	$(BP_ADDR + 0x1f1), %edi
	movw	header_end, %cx
	subw	$0x1f1, %cx
3:
	movb	%es:(%si), %al
	movb	%al, %fs:(%edi)
	incw	%si
	incl	%edi
	decw	%cx
	jnz	3b

	/* Seek to the protected-mode payload by walking the FAT stream. */
	call	stream_reset
	movl	payload_offset, %ecx
	shrl	$9, %ecx
seek_payload:
	testl	%ecx, %ecx
	jz	load_payload
	pushl	%ecx
	movw	$BOUNCE_SEG, %ax
	movw	%ax, %es
	xorw	%bp, %bp
	call	stream_read_sector
	popl	%ecx
	jc	loader_failure
	decl	%ecx
	jmp	seek_payload

load_payload:
	movl	$KDEST, cur_dest
copy_next:
	cmpl	$0, remaining
	je	copy_done
	movw	$BOUNCE_SEG, %ax
	movw	%ax, %es
	xorw	%bp, %bp
	call	stream_read_sector
	jc	loader_failure
	movl	$512, %ecx
	cmpl	%ecx, remaining
	jae	4f
	movl	remaining, %ecx
4:
	movl	cur_dest, %edi
	xorl	%esi, %esi
	movl	%ecx, %edx
	shrl	$2, %ecx
5:
	movl	%es:(%si), %eax
	movl	%eax, %fs:(%edi)
	addl	$4, %esi
	addl	$4, %edi
	decl	%ecx
	jnz	5b
	addl	%edx, cur_dest
	subl	%edx, remaining
	addl	%edx, loaded_bytes
	incw	progress_sectors
	testb	$0xff, progress_sectors
	jnz	copy_next
	call	update_load_status
	jmp	copy_next

copy_done:
	call	update_load_status
prepare_boot_params:
	movl	$BP_ADDR, %edi
	movb	$0xff, %fs:0x210(%edi)
	movl	$CMDLINE, %fs:0x228(%edi)
	movb	$2, %fs:0x1e8(%edi)

	/* Pass the INT 1Bh logical geometry independently of ATA IDENTIFY. */
	movl	$PC98_DATA, %fs:0x250(%edi)
	movl	$0, %fs:0x254(%edi)
	movl	$PC98_DATA, %ebx
	movl	$0, %fs:(%ebx)
	movl	$0, %fs:4(%ebx)
	movl	$11, %fs:8(%ebx)	/* SETUP_PC98_DISK */
	movl	$12, %fs:12(%ebx)
	movl	$0x44383950, %fs:16(%ebx)	/* "P98D" */
	movw	$1, %fs:20(%ebx)
	movw	$12, %fs:22(%ebx)
	movb	boot_drive, %al
	movb	%al, %fs:24(%ebx)
	movb	bios_heads, %al
	movb	%al, %fs:25(%ebx)
	movb	bios_sectors, %al
	movb	%al, %fs:26(%ebx)
	movb	bios_flags, %al
	movb	%al, %fs:27(%ebx)

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
	movl	$CMDLINE, %ebx
	movb	$0, %fs:(%ebx)

	movw	starting_message_ptr, %si
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
	movl	%eax, 4(%bx)
	movl	%es:0x10(%di), %eax
	movl	%eax, 8(%bx)
	addl	%eax, elf_total_bytes
	movl	%es:0x14(%di), %eax
	movl	%eax, 12(%bx)
	incb	elf_segment_count
elf_next_phdr:
	addw	$32, %di
	loop	elf_parse_phdr

	cmpb	$0, elf_segment_count
	je	loader_failure
	movl	%es:0x18, %eax		/* e_entry is a physical address */
	movl	%eax, kernel_entry_ptr
	movw	$starting_direct_message, starting_message_ptr
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

/* Find optional LOGO.RAW (80 x 120, packed 1bpp) in the FAT16 root. */
find_and_load_logo:
	pushw	%ax
	pushw	%bx
	pushw	%cx
	pushw	%di
	pushw	%si
	pushw	%es
	movw	$BOUNCE_SEG, %ax
	movw	%ax, %es
	xorw	%di, %di
	movw	root_entries, %cx
1:
	cmpb	$0, %es:(%di)
	je	4f
	cmpb	$0xe5, %es:(%di)
	je	3f
	cmpb	$0x0f, %es:0x0b(%di)
	je	3f
	pushw	%cx
	pushw	%di
	movw	$logo_name, %si
	movw	$11, %cx
	repe cmpsb
	popw	%di
	popw	%cx
	jne	3f
	cmpw	$1200, %es:0x1c(%di)
	jne	4f
	cmpw	$0, %es:0x1e(%di)	/* upper word of file size */
	jne	4f
	movw	%es:0x1a(%di), %ax
	cmpw	$2, %ax
	jb	4f
	movw	%ax, logo_cluster
	call	load_logo_file
	jmp	4f
3:
	addw	$32, %di
	loop	1b
4:
	popw	%es
	popw	%si
	popw	%di
	popw	%cx
	popw	%bx
	popw	%ax
	ret

/* Read the three sectors containing the optional 1200-byte logo. */
load_logo_file:
	pushw	%ax
	pushw	%cx
	pushw	%bp
	pushw	%es
	movw	first_cluster, %ax
	pushw	%ax
	movw	logo_cluster, %ax
	movw	%ax, first_cluster
	call	stream_reset
	movw	$BOUNCE_SEG, %ax
	movw	%ax, %es
	xorw	%bp, %bp
	movw	$3, %cx
1:
	pushw	%cx
	call	stream_read_sector
	popw	%cx
	jc	2f
	addw	$512, %bp
	loop	1b
	call	draw_logo
2:
	popw	%ax
	movw	%ax, first_cluster
	popw	%es
	popw	%bp
	popw	%cx
	popw	%ax
	ret

/* Copy LOGO.RAW to x=560, y=280 in all four planar graphics planes. */
draw_logo:
	pushw	%ax
	pushw	%cx
	pushw	%si
	pushw	%di
	pushw	%es
	pushw	%fs
	movw	$BOUNCE_SEG, %ax
	movw	%ax, %fs
	movw	$0, logo_source_offset
	movw	$22470, logo_vram_offset	/* 280 * 80 + 560 / 8 */
	movw	$120, logo_rows_left
1:
	movw	$0xa800, %ax
	call	copy_logo_row
	movw	$0xb000, %ax
	call	copy_logo_row
	movw	$0xb800, %ax
	call	copy_logo_row
	movw	$0xe000, %ax
	call	copy_logo_row
	addw	$10, logo_source_offset
	addw	$80, logo_vram_offset
	decw	logo_rows_left
	jnz	1b
	popw	%fs
	popw	%es
	popw	%di
	popw	%si
	popw	%cx
	popw	%ax
	ret

copy_logo_row:
	movw	%ax, %es
	movw	logo_source_offset, %si
	movw	logo_vram_offset, %di
	movw	$10, %cx
1:
	movb	%fs:(%si), %al
	movb	%al, %es:(%di)
	incw	%si
	incw	%di
	loop	1b
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

/* Refresh the decimal byte count and append one persistent progress dot. */
update_load_status:
	pushl	%eax
	pushw	%bx
	pushw	%di
	pushw	%si
	movl	loaded_bytes, %eax
	movw	$load_count_field, %di
	call	format_decimal8
	xorw	%bx, %bx
	movb	progress_column, %bl
	cmpb	$79, %bl
	jae	1f
	movb	$0x2e, load_status(%bx)
	incb	progress_column
1:
	movw	$load_status, %si
	call	show_status
	popw	%si
	popw	%di
	popw	%bx
	popl	%eax
	ret

/* Write EAX as eight zero-padded decimal digits at DS:DI. */
format_decimal8:
	pushl	%eax
	pushl	%ebx
	pushl	%edx
	pushw	%cx
	pushw	%di
	addw	$7, %di
	movl	$10, %ebx
	movw	$8, %cx
1:
	xorl	%edx, %edx
	divl	%ebx
	addb	$0x30, %dl
	movb	%dl, (%di)
	decw	%di
	loop	1b
	popw	%di
	popw	%cx
	popl	%edx
	popl	%ebx
	popl	%eax
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

kernel_name:
	.ascii	"BZIMAGE    "
vmlinux_name:
	.ascii	"VMLINUX    "
logo_name:
	.ascii	"LOGO    RAW"
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
payload_offset:
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
setup_sects:
	.word	0
header_end:
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
progress_column:
	.byte	0
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
loaded_bytes:
	.long	0
kernel_entry_ptr:
	.long	KDEST
	.word	BOOT_CS
starting_message_ptr:
	.word	starting_message
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
elf_segments_left:
	.byte	0
	.balign 4
elf_stream_pos:
	.long	0
	.balign 2
logo_cluster:
	.word	0
logo_source_offset:
	.word	0
logo_vram_offset:
	.word	0
logo_rows_left:
	.word	0
	.balign 4
elf_segments:
	.fill	4 * 16, 1, 0
read_flags:
	.word	0
loading_message:
	.asciz	"Loading Linux kernel..."
starting_message:
	.asciz	"Decompressing Linux..."
starting_direct_message:
	.asciz	"Starting Linux..."
load_status:
	.ascii	"Loading kernel: "
load_count_field:
	.ascii	"00000000"
	.ascii	" / "
load_total_field:
	.ascii	"00000000"
	.ascii	" bytes"
load_dot_field:
	.fill	24, 1, 0x20
	.byte	0

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
	.ascii	"UNIX "
	.byte	0xb6, 0xb0, 0xc8, 0xd9, 0xdb, 0xb0, 0xc0, 0xde, 0xb0
	.ascii	" "
	.byte	0xca, 0xde, 0xb0, 0xbc, 0xde, 0xae, 0xdd
	.ascii	" 1.00"
	.byte	0
banner_copyright:
	.asciz	"Copyright (C) 2026 NEC FANS on X.com"
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
