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
	movw	$loading_message, %si
	call	show_status

	xorw	%ax, %ax
	movw	%ax, %es
	cmpl	$0x4839384c, %es:HANDOFF
	jne	loader_failure
	movl	%es:HANDOFF+4, %eax
	movl	%eax, partition_lba

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
	movw	$load_count_field, %di
	xorl	%eax, %eax
	call	format_decimal8
	movl	elf_total_bytes, %eax
	movw	$load_total_field, %di
	call	format_decimal8
	movw	$load_status, %si
	call	show_status
	movw	$elf_segments, elf_segment_ptr
	movb	elf_segment_count, %al
	movb	%al, elf_segments_left

elf_load_segment:
	cmpb	$0, elf_segments_left
	je	elf_load_done
	movw	elf_segment_ptr, %si
	movl	(%si), %eax		/* p_offset */
	testl	$511, %eax
	jnz	loader_failure
	movl	8(%si), %edx		/* p_filesz */
	testl	%edx, %edx
	jz	elf_zero_segment
	call	stream_reset
	movl	(%si), %ecx
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
	decl	%ecx
	jmp	elf_seek_segment

elf_begin_segment:
	movw	elf_segment_ptr, %si
	movl	4(%si), %eax
	movl	%eax, cur_dest
	movl	8(%si), %eax
	movl	%eax, remaining
elf_copy_next:
	cmpl	$0, remaining
	je	elf_zero_segment
	movw	$BOUNCE_SEG, %ax
	movw	%ax, %es
	xorw	%bp, %bp
	call	stream_read_sector
	jc	loader_failure
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
	call	update_load_status
	jmp	elf_copy_next

elf_zero_segment:
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
	addw	$16, elf_segment_ptr
	decb	elf_segments_left
	jmp	elf_load_segment

elf_load_done:
	call	update_load_status
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
	movb	%ss:0x0584, %al
	orb	$0x80, %al
	movb	$0x06, %ah
	movw	%ss:HANDOFF+10, %si
	movw	%ss:HANDOFF+12, %di
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
sector_in_cluster:
	.byte	0
progress_sectors:
	.word	0
progress_column:
	.byte	0
loaded_bytes:
	.long	0
kernel_entry_ptr:
	.long	KDEST
	.word	BOOT_CS
starting_message_ptr:
	.word	starting_message
elf_total_bytes:
	.long	0
elf_segment_ptr:
	.word	0
elf_segment_count:
	.byte	0
elf_segments_left:
	.byte	0
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
