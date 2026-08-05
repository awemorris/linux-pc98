/*
 * PC-9800 Bootloader
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

	.code32
	.section .header,"a"

	.ascii "B98A"
	.word 1
	.word 36
	.long __applet_end - __applet_start
	.word 36
	.word 0
	.long 0
	.ascii "ABI-SELFTEST"
	.space 4

	.section .text,"ax"
	.globl __applet_start
	.globl applet_entry
__applet_start = 0x50000
applet_entry:
	pushl %ebp
	movl %esp,%ebp
	movl 8(%ebp),%eax
	pushl $message
	call *8(%eax)
	addl $4,%esp
	xorl %eax,%eax
	leave
	ret
message:
	.asciz "BOOT98 applet ABI: OK\n"
