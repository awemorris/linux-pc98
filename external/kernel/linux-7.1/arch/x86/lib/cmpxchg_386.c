// SPDX-License-Identifier: GPL-2.0
/*
 * UP-only CMPXCHG and XADD substitutes for Intel 80386.
 */
#include <linux/export.h>
#include <linux/irqflags.h>
#include <linux/types.h>

unsigned long cmpxchg_386(volatile void *ptr, unsigned long old,
			  unsigned long new, int size)
{
	unsigned long flags;
	unsigned long observed;

	raw_local_irq_save(flags);
	switch (size) {
	case 1:
		observed = *(volatile u8 *)ptr;
		if ((u8)observed == (u8)old)
			*(volatile u8 *)ptr = (u8)new;
		break;
	case 2:
		observed = *(volatile u16 *)ptr;
		if ((u16)observed == (u16)old)
			*(volatile u16 *)ptr = (u16)new;
		break;
	case 4:
		observed = *(volatile u32 *)ptr;
		if ((u32)observed == (u32)old)
			*(volatile u32 *)ptr = (u32)new;
		break;
	default:
		__builtin_trap();
	}
	raw_local_irq_restore(flags);

	return observed;
}
EXPORT_SYMBOL(cmpxchg_386);

unsigned long xadd_386(volatile void *ptr, unsigned long inc, int size)
{
	unsigned long flags;
	unsigned long old;

	raw_local_irq_save(flags);
	switch (size) {
	case 1:
		old = *(volatile u8 *)ptr;
		*(volatile u8 *)ptr = (u8)(old + inc);
		break;
	case 2:
		old = *(volatile u16 *)ptr;
		*(volatile u16 *)ptr = (u16)(old + inc);
		break;
	case 4:
		old = *(volatile u32 *)ptr;
		*(volatile u32 *)ptr = (u32)(old + inc);
		break;
	default:
		__builtin_trap();
	}
	raw_local_irq_restore(flags);

	return old;
}
EXPORT_SYMBOL(xadd_386);
