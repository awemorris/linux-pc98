/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_X86_I386_ATOMIC_H
#define _UAPI_ASM_X86_I386_ATOMIC_H

#include <linux/types.h>

#define I386_ATOMIC_ABI_VERSION 1

#define I386_ATOMIC_CMPXCHG 0
#define I386_ATOMIC_XADD    1
#define I386_ATOMIC_XCHG    2
#define I386_ATOMIC_AND     3
#define I386_ATOMIC_OR      4
#define I386_ATOMIC_XOR     5

/*
 * Versioned request for the genuine-80386 atomic service.  ADDRESS is a
 * 32-bit userspace address and WIDTH is 1, 2, or 4 bytes.  On success,
 * OBSERVED contains the value that was in ADDRESS before the operation.
 */
struct i386_atomic_args {
	__u32 version;
	__u32 operation;
	__u32 width;
	__u32 address;
	__u32 expected;
	__u32 value;
	__u32 observed;
	__u32 reserved;
};

#endif /* _UAPI_ASM_X86_I386_ATOMIC_H */
