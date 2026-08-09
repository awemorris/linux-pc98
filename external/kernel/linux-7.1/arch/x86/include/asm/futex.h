/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_FUTEX_H
#define _ASM_X86_FUTEX_H

#ifdef __KERNEL__

#include <linux/futex.h>
#include <linux/uaccess.h>

#include <asm/asm.h>
#include <asm/errno.h>
#include <asm/processor.h>
#include <asm/smap.h>
#ifdef CONFIG_X86_USER_ATOMIC_386
#include <asm/i386_user_atomic.h>
#include <uapi/asm/i386_atomic.h>
#endif

#define unsafe_atomic_op1(insn, oval, uaddr, oparg, label)	\
do {								\
	int oldval = 0, ret;					\
	asm volatile("1:\t" insn "\n"				\
		     "2:\n"					\
		     _ASM_EXTABLE_TYPE_REG(1b, 2b, EX_TYPE_EFAULT_REG, %1) \
		     : "=r" (oldval), "=r" (ret), "+m" (*uaddr)	\
		     : "0" (oparg), "1" (0));	\
	if (ret)						\
		goto label;					\
	*oval = oldval;						\
} while(0)


#define unsafe_atomic_op2(insn, oval, uaddr, oparg, label)	\
do {								\
	int oldval = 0, ret, tem;				\
	asm volatile("1:\tmovl	%2, %0\n"			\
		     "2:\tmovl\t%0, %3\n"			\
		     "\t" insn "\n"				\
		     "3:\t" LOCK_PREFIX "cmpxchgl %3, %2\n"	\
		     "\tjnz\t2b\n"				\
		     "4:\n"					\
		     _ASM_EXTABLE_TYPE_REG(1b, 4b, EX_TYPE_EFAULT_REG, %1) \
		     _ASM_EXTABLE_TYPE_REG(3b, 4b, EX_TYPE_EFAULT_REG, %1) \
		     : "=&a" (oldval), "=&r" (ret),		\
		       "+m" (*uaddr), "=&r" (tem)		\
		     : "r" (oparg), "1" (0));			\
	if (ret)						\
		goto label;					\
	*oval = oldval;						\
} while(0)

static __always_inline int arch_futex_atomic_op_inuser(int op, int oparg, int *oval,
						       u32 __user *uaddr)
{
#ifdef CONFIG_X86_USER_ATOMIC_386
	u32 atomic_op;
	u32 value = oparg;
	u32 observed;
	int ret;

	switch (op) {
	case FUTEX_OP_SET:
		atomic_op = I386_ATOMIC_XCHG;
		break;
	case FUTEX_OP_ADD:
		atomic_op = I386_ATOMIC_XADD;
		break;
	case FUTEX_OP_OR:
		atomic_op = I386_ATOMIC_OR;
		break;
	case FUTEX_OP_ANDN:
		atomic_op = I386_ATOMIC_AND;
		value = ~oparg;
		break;
	case FUTEX_OP_XOR:
		atomic_op = I386_ATOMIC_XOR;
		break;
	default:
		return -ENOSYS;
	}

	ret = i386_user_atomic_op_inuser(atomic_op, uaddr, 0, value,
					  sizeof(*uaddr), &observed);
	if (!ret)
		*oval = observed;
	return ret;
#else
	scoped_user_rw_access(uaddr, Efault) {
		switch (op) {
		case FUTEX_OP_SET:
			unsafe_atomic_op1("xchgl %0, %2", oval, uaddr, oparg, Efault);
			break;
		case FUTEX_OP_ADD:
			unsafe_atomic_op1(LOCK_PREFIX "xaddl %0, %2", oval, uaddr, oparg, Efault);
			break;
		case FUTEX_OP_OR:
			unsafe_atomic_op2("orl %4, %3", oval, uaddr, oparg, Efault);
			break;
		case FUTEX_OP_ANDN:
			unsafe_atomic_op2("andl %4, %3", oval, uaddr, ~oparg, Efault);
			break;
		case FUTEX_OP_XOR:
			unsafe_atomic_op2("xorl %4, %3", oval, uaddr, oparg, Efault);
			break;
		default:
			return -ENOSYS;
		}
	}
	return 0;
Efault:
	return -EFAULT;
#endif
}

static inline int futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr,
						u32 oldval, u32 newval)
{
#ifdef CONFIG_X86_USER_ATOMIC_386
	return i386_user_atomic_op_inuser(I386_ATOMIC_CMPXCHG, uaddr,
					  oldval, newval, sizeof(*uaddr),
					  uval);
#else
	int ret = 0;

	scoped_user_rw_access(uaddr, Efault) {
		asm_inline volatile("\n"
				    "1:\t" LOCK_PREFIX "cmpxchgl %3, %2\n"
				    "2:\n"
				    _ASM_EXTABLE_TYPE_REG(1b, 2b, EX_TYPE_EFAULT_REG, %0)
				    : "+r" (ret), "=a" (oldval), "+m" (*uaddr)
				    : "r" (newval), "1" (oldval)
				    : "memory");
		*uval = oldval;
	}
	return ret;
Efault:
	return -EFAULT;
#endif
}

#endif
#endif /* _ASM_X86_FUTEX_H */
