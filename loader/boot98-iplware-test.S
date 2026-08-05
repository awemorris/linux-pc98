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
	movb	%al, 0x00f2
	movw	%cs, %ax
	cmpw	$0x6000, %ax
	jne	bad1
	movw	%ds, %ax
	cmpw	$0x6000, %ax
	jne	bad2
	movw	%es, %ax
	cmpw	$0x6000, %ax
	jne	bad3
	movw	%ss, %ax
	cmpw	$0x6000, %ax
	jne	bad4
	cmpw	$0x6000, %bp
	jne	bad5
	testw	%di, %di
	jnz	bad6
	cmpw	$0x0393, %si
	jne	bad7
	cmpb	$0x00, 0x00f2
	jne	bad8
	cmpw	$512, %bx
	jb	bad9
	movb	$0x5a, 0x00f0
	lret
bad1: movb $1,0x00f0; lret
bad2: movb $2,0x00f0; lret
bad3: movb $3,0x00f0; lret
bad4: movb $4,0x00f0; lret
bad5: movb $5,0x00f0; lret
bad6: movb $6,0x00f0; lret
bad7: movb $7,0x00f0; lret
bad8: movb $8,0x00f0; lret
bad9: movb $9,0x00f0; lret
