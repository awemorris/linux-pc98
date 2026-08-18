/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_X86_SWAB_H
#define _ASM_X86_SWAB_H

#include <linux/types.h>
#include <linux/compiler.h>

/*
 * BSWAP was introduced with the i486.  UAPI headers must not depend on a
 * kernel CONFIG_* symbol, because that would make an installed header select
 * code according to the kernel build rather than the userspace target ISA.
 * GCC and Clang define the processor-specific macros below when the matching
 * -march target (or a later one) is selected.  With an exact i386 target,
 * leave __arch_swab* undefined so linux/swab.h uses its portable fallback.
 */
#if !defined(__i386__) || defined(__i486__) || defined(__i586__) || \
	defined(__i686__)
static inline __attribute_const__ __u32 __arch_swab32(__u32 val)
{
	asm("bswapl %0" : "=r" (val) : "0" (val));
	return val;
}
#define __arch_swab32 __arch_swab32

static inline __attribute_const__ __u64 __arch_swab64(__u64 val)
{
#ifdef __i386__
	union {
		struct {
			__u32 a;
			__u32 b;
		} s;
		__u64 u;
	} v;
	v.u = val;
	asm("bswapl %0 ; bswapl %1 ; xchgl %0,%1"
	    : "=r" (v.s.a), "=r" (v.s.b)
	    : "0" (v.s.a), "1" (v.s.b));
	return v.u;
#else /* __i386__ */
	asm("bswapq %0" : "=r" (val) : "0" (val));
	return val;
#endif
}
#define __arch_swab64 __arch_swab64
#endif /* bswap-capable userspace ISA */

#endif /* _ASM_X86_SWAB_H */
