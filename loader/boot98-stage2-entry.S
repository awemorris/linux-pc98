/*
 * PC-9800 Bootloader
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

	.code32
	.section .header, "a"
	.globl __image_start
__image_start:
	.ascii "B98S"
	.word 1
	.word 20
	.long __image_end - __image_start
	.long _start32 - __image_start
	.long 0		/* payload byte-sum, patched after objcopy */

	.section .text.entry, "ax"
	.globl _start32
	.extern boot98_main
	.extern __bss_start
	.extern __bss_end
_start32:
	cld
	movl $0x0008f000, %esp
	xorl %ebp, %ebp
	xorl %eax, %eax
	movl $__bss_start, %edi
	movl $__bss_end, %ecx
	subl %edi, %ecx
	rep stosb
	pushl %ebx		/* Stage 1 handoff pointer */
	call boot98_main
1:
	cli
	hlt
	jmp 1b

	.section .note.GNU-stack, "", @progbits
