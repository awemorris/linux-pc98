/*
 * PC-9800 Bootloader
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

	.code16
	.section .text
	.globl _start
_start:
	movw	%cs, %ax
	cmpw	$0x6000, %ax
	jne	bad
	cmpw	$0x6000, %bp
	jne	bad
	testw	%di, %di
	jnz	bad
	cmpw	$0x0393, %si
	jne	bad
	movb	$0x5a, 0x00f0
	ret
bad:
	movb	$0xee, 0x00f0
	ret
