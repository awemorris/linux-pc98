/*
 * M4 link guards for floating-point operations deferred to M5.
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

	.section .text
	.code32
	.extern boot98_noct_float_unavailable

	.macro FLOAT_GUARD name
	.globl \name
	.type \name, @function
\name:
	jmp boot98_noct_float_unavailable
	.size \name, .-\name
	.endm

	FLOAT_GUARD __adddf3
	FLOAT_GUARD __addsf3
	FLOAT_GUARD cosf
	FLOAT_GUARD __divdf3
	FLOAT_GUARD __divsf3
	FLOAT_GUARD __eqdf2
	FLOAT_GUARD __eqsf2
	FLOAT_GUARD __extendsfdf2
	FLOAT_GUARD __fixdfdi
	FLOAT_GUARD __fixdfsi
	FLOAT_GUARD __fixsfdi
	FLOAT_GUARD __fixsfsi
	FLOAT_GUARD __floatdidf
	FLOAT_GUARD __floatdisf
	FLOAT_GUARD __floatsidf
	FLOAT_GUARD __floatsisf
	FLOAT_GUARD __gedf2
	FLOAT_GUARD __gesf2
	FLOAT_GUARD __gtdf2
	FLOAT_GUARD __gtsf2
	FLOAT_GUARD __ledf2
	FLOAT_GUARD __lesf2
	FLOAT_GUARD __ltdf2
	FLOAT_GUARD __ltsf2
	FLOAT_GUARD __muldf3
	FLOAT_GUARD __mulsf3
	FLOAT_GUARD __nedf2
	FLOAT_GUARD __nesf2
	FLOAT_GUARD sinf
	FLOAT_GUARD sqrtf
	FLOAT_GUARD __subdf3
	FLOAT_GUARD __subsf3
	FLOAT_GUARD tanf
	FLOAT_GUARD __truncdfsf2

	.section .note.GNU-stack,"",@progbits
