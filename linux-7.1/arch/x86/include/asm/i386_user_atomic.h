/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_I386_USER_ATOMIC_H
#define _ASM_X86_I386_USER_ATOMIC_H

#include <linux/compiler_types.h>
#include <linux/types.h>

int i386_user_atomic_op_inuser(u32 operation, void __user *uaddr,
			       u32 expected, u32 value, u32 width,
			       u32 *observed);

#endif /* _ASM_X86_I386_USER_ATOMIC_H */
