/* Linux/x86 32-bit boot protocol loader for the NEC PC-9800. */
	.code16
	.section .text
	.globl _start

	.set LOAD_BASE,  0x1FC00
	.set DISK_BOOT,  0x0584
	/* A buffer at 0x10000 would overlap the IPL at 0x1fc00. */
	.set BOUNCE_SEG, 0x3000
	.set BP_ADDR,    0x20000
	.set CMDLINE,    0x21000
	.set KDEST,      0x100000
	.set BOOT_CS,    0x10
	.set BOOT_DS,    0x18

_start:
	/*
	 * NP2kai enters sector 0 unconditionally, while the free qemu-pc98 IDE
	 * BIOS requires the four-byte "IPL1" marker at offset 4.  Keep the entry
	 * point at offset 0 and jump over a header accepted by both firmware
	 * implementations.
	 */
	.byte	0xeb, 0x06
	.byte	0x90, 0x90
	.ascii	"IPL1"

	cli
	cld
	movw	%cs, %ax
	movw	%ax, %ds
	xorw	%ax, %ax
	movw	%ax, %ss
	movw	$0x7000, %sp
	sti

	movw	$0, %di
	movb	$0x45, %dl
	call	rm_bar

	/* A20 on (PC-9800), let DMA reach >1MB. Same sequence Linux/98 used. */
	outb	%al, $0xf2
	movb	$0x02, %al
	outb	%al, $0xf6
	movw	$0x439, %dx
	inb	%dx, %al
	andb	$0xfb, %al
	outb	%al, %dx

	xorb	%al, %al
	outb	%al, $0xf8
	outb	%al, $0x5f		/* the machine's I/O delay */

	xorl	%eax, %eax
	movw	%ds, %ax
	shll	$4, %eax
	addl	$gdt, %eax
	movl	%eax, gdt_desc+2
	lgdt	gdt_desc

	/* No IDT is loaded while PE is set to latch a flat FS. */
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

	movw	$320, %di
	movb	$0x85, %dl
	call	rm_bar

	movl	$1024, cur_off
	movl	$2048, chunk
	call	read_chunk
	jc	read_fail

	movzbw	%es:0x1F1, %ax
	testw	%ax, %ax
	jnz	1f
	movw	$4, %ax
1:
	movw	%ax, ss_var
	movzbw	%es:0x201, %ax
	addw	$0x202, %ax
	movw	%ax, header_end
	movl	%es:0x1F4, %eax
	shll	$4, %eax
	movl	%eax, remaining
	movzwl	ss_var, %eax
	incl	%eax
	shll	$9, %eax
	addl	$1024, %eax
	movl	%eax, cur_off
	movl	$KDEST, %eax
	movl	%eax, cur_dest

	movl	$BP_ADDR, %edi
	movl	$1024, %ecx
2:
	movl	$0, %fs:(%edi)
	addl	$4, %edi
	decl	%ecx
	jnz	2b

	movw	$0x1F1, %si
	movl	$(BP_ADDR + 0x1F1), %edi
	movw	header_end, %cx
	subw	$0x1F1, %cx
3:
	movb	%es:(%si), %al
	movb	%al, %fs:(%edi)
	incw	%si
	incl	%edi
	decw	%cx
	jnz	3b

copy_next:
	movl	remaining, %eax
	testl	%eax, %eax
	jz	copy_done
	/* Never cross a floppy cylinder (16KB): the FD BIOS stops there and
	 * reports an error for the rest. Harmless for the HDD path. */
	movl	cur_off, %edx
	andl	$0x3fff, %edx
	movl	$0x4000, %ecx
	subl	%edx, %ecx
	cmpl	%ecx, %eax
	jbe	4f
	movl	%ecx, %eax
4:
	movl	%eax, chunk
	call	read_chunk
	jc	read_fail

	movl	cur_dest, %edi
	xorl	%esi, %esi
	movl	chunk, %ecx
	shrl	$2, %ecx
5:
	movl	%es:(%esi), %eax
	movl	%eax, %fs:(%edi)
	addl	$4, %esi
	addl	$4, %edi
	decl	%ecx
	jnz	5b

	movl	chunk, %eax
	addl	%eax, cur_dest
	addl	%eax, cur_off
	subl	%eax, remaining
	jmp	copy_next
copy_done:

	movw	$640, %di
	movb	$0x25, %dl
	call	rm_bar

	movl	$BP_ADDR, %edi
	movb	$0xff, %fs:0x210(%edi)
	movl	$CMDLINE, %fs:0x228(%edi)
	movb	$2, %fs:0x1e8(%edi)
	/* e820[0] = {0, conventional RAM}. Its size is in the low three bits of
	 * the BIOS flag byte, in 128KB units. */
	movl	$0x501, %ebx
	movzbl	%fs:(%ebx), %eax
	andl	$7, %eax
	incl	%eax
	shll	$17, %eax		/* * 128KB */
	movl	$0, %fs:0x2d0(%edi)
	movl	$0, %fs:0x2d4(%edi)
	movl	%eax, %fs:0x2d8(%edi)
	movl	$0, %fs:0x2dc(%edi)
	movl	$1, %fs:0x2e0(%edi)
	/* Extended RAM comes from the BIOS work area, so this works on any
	 * machine instead of assuming the emulator's size:
	 *   byte 0x401  = RAM from 1MB up, in 128KB units, capped at 14MB
	 *   word 0x594  = RAM above 16MB, in MB (0 if none)
	 * PC-98 leaves a hole at 15-16MB, hence the two separate entries. */
	movl	$0x401, %ebx
	movzbl	%fs:(%ebx), %eax
	testl	%eax, %eax
	jz	mem_fail
	shll	$17, %eax			/* * 128KB */
	movl	$0x100000, %fs:0x2e4(%edi)
	movl	$0, %fs:0x2e8(%edi)
	movl	%eax, %fs:0x2ec(%edi)
	movl	$0, %fs:0x2f0(%edi)
	movl	$1, %fs:0x2f4(%edi)
	movl	$0x594, %ebx
	movzwl	%fs:(%ebx), %eax
	testl	%eax, %eax
	jz	1f
	shll	$20, %eax			/* * 1MB */
	movl	$0x1000000, %fs:0x2f8(%edi)
	movl	$0, %fs:0x2fc(%edi)
	movl	%eax, %fs:0x300(%edi)
	movl	$0, %fs:0x304(%edi)
	movl	$1, %fs:0x308(%edi)
	movb	$3, %fs:0x1e8(%edi)
1:
	movl	$CMDLINE, %ebx
	movb	$0, %fs:(%ebx)

	cli
	/* The disk BIOS leaves interrupts live and the PICs however the firmware
	 * had them; the kernel does not reprogram them until init_IRQ(). Mask
	 * everything but the cascade line, and hold off NMI, the way Linux/98's
	 * setup did. */
	movb	$0xff, %al
	outb	%al, $0x0a		/* slave: all masked */
	outb	%al, $0x5f
	movb	$0x7f, %al
	outb	%al, $0x02		/* master: all but IR7, the cascade */
	outb	%al, $0x5f
	outb	%al, $0x50		/* NMI off until the kernel is ready */

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
	ljmpl	$BOOT_CS, $KDEST

read_fail:
	movw	$3840, %di		/* row 24, red */
	movb	$0x45, %dl
	call	rm_bar
6:
	hlt
	jmp	6b

/* No extended memory reported: the kernel cannot run from 0x100000. */
mem_fail:
	movw	$3520, %di		/* row 22, red */
	movb	$0x45, %dl
	call	rm_bar
	jmp	6b

read_chunk:
	movw	$BOUNCE_SEG, %ax
	movw	%ax, %es
	movw	chunk, %bx
	xorw	%bp, %bp
	movb	%ss:DISK_BOOT, %al
	testb	$0x10, %al		/* device class: bit4 set = floppy */
	jnz	read_fd

	andb	$0x03, %al
	movb	$0x06, %ah
	movl	cur_off, %ecx
	shrl	$9, %ecx
	xorb	%dl, %dl		/* LBA bits 16-23 = 0 */
	int	$0x1b
	ret

	/* floppy: AH=0xD6 (read | seek | head-cross), CH=N, CL=cyl, DH=head,
	 * DL=sector (1-based). 2HD is 8 x 1024-byte sectors per track. */
read_fd:
	movl	cur_off, %edx
	movl	%edx, %eax
	shrl	$14, %eax
	movb	%al, fd_cyl		/* cylinder = off / 16KB */
	andl	$0x3fff, %edx
	movl	%edx, %eax
	shrl	$13, %eax
	movb	%al, fd_head		/* head = (off % 16KB) / 8KB */
	movl	%edx, %eax
	shrl	$10, %eax
	andb	$0x07, %al
	incb	%al
	movb	%al, fd_sect		/* sector = (off % 8KB) / 1KB + 1 */
	movb	%ss:DISK_BOOT, %al
	movb	$0xd6, %ah
	movb	$3, %ch			/* N=3 -> 1024-byte sectors */
	movb	fd_cyl, %cl
	movb	fd_head, %dh
	movb	fd_sect, %dl
	int	$0x1b
	ret

rm_bar:
	pushw	%es
	pushw	%gs
	pushw	%cx
	pushw	%di
	movw	$0xA000, %ax
	movw	%ax, %es
	movw	$0xA200, %ax
	movw	%ax, %gs
	movw	$60, %cx
1:
	movb	$0x20, %es:(%di)
	movb	$0, %es:1(%di)
	movb	%dl, %gs:(%di)
	addw	$2, %di
	loop	1b
	popw	%di
	popw	%cx
	popw	%gs
	popw	%es
	ret

	.balign 8
gdt:
	.quad	0x0000000000000000
	.quad	0x0000000000000000
	.quad	0x00cf9a000000ffff
	.quad	0x00cf92000000ffff
gdt_end:
gdt_desc:
	.word	gdt_end - gdt - 1
	.long	0

	.balign 4
ss_var:		.word 0
header_end:	.word 0
fd_cyl:		.byte 0
fd_head:	.byte 0
fd_sect:	.byte 0
		.byte 0
cur_off:	.long 0
remaining:	.long 0
cur_dest:	.long 0
chunk:		.long 0
