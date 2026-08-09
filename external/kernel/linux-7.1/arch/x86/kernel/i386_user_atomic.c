// SPDX-License-Identifier: GPL-2.0
/*
 * Fault-contained userspace atomics for a genuine uniprocessor Intel 80386.
 *
 * Memory XCHG remains available to userspace, but CMPXCHG and XADD do not.
 * This service keeps modern glibc/NPTL state machines and their public ABI
 * intact without emulating arbitrary faulting instructions.
 */

#include <linux/errno.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>

#include <asm/i386_user_atomic.h>
#include <uapi/asm/i386_atomic.h>

#ifndef CONFIG_X86_USER_ATOMIC_386

SYSCALL_DEFINE1(i386_atomic, struct i386_atomic_args __user *, uargs)
{
	return -ENOSYS;
}

#else

static DEFINE_RAW_SPINLOCK(i386_user_atomic_lock);

static bool i386_atomic_operation_valid(u32 operation)
{
	return operation <= I386_ATOMIC_XOR;
}

static bool i386_atomic_width_valid(u32 width)
{
	return width == 1 || width == 2 || width == 4;
}

static u32 i386_atomic_mask(u32 width)
{
	if (width == 1)
		return 0xff;
	if (width == 2)
		return 0xffff;
	return 0xffffffff;
}

static int i386_atomic_calculate(u32 operation, u32 old, u32 expected,
				 u32 value, u32 width, u32 *new,
				 bool *store)
{
	u32 mask = i386_atomic_mask(width);

	old &= mask;
	expected &= mask;
	value &= mask;
	*store = true;

	switch (operation) {
	case I386_ATOMIC_CMPXCHG:
		*store = old == expected;
		*new = value;
		break;
	case I386_ATOMIC_XADD:
		*new = old + value;
		break;
	case I386_ATOMIC_XCHG:
		*new = value;
		break;
	case I386_ATOMIC_AND:
		*new = old & value;
		break;
	case I386_ATOMIC_OR:
		*new = old | value;
		break;
	case I386_ATOMIC_XOR:
		*new = old ^ value;
		break;
	default:
		return -EINVAL;
	}

	*new &= mask;
	return 0;
}

static u32 i386_atomic_read_kernel(const void *address, u32 width)
{
	if (width == 1)
		return READ_ONCE(*(const u8 *)address);
	if (width == 2)
		return READ_ONCE(*(const u16 *)address);
	return READ_ONCE(*(const u32 *)address);
}

static void i386_atomic_write_kernel(void *address, u32 width, u32 value)
{
	if (width == 1)
		WRITE_ONCE(*(u8 *)address, (u8)value);
	else if (width == 2)
		WRITE_ONCE(*(u16 *)address, (u16)value);
	else
		WRITE_ONCE(*(u32 *)address, value);
}

static bool i386_atomic_ranges_overlap(unsigned long first, size_t first_size,
				       unsigned long second,
				       size_t second_size)
{
	u64 first_start = first;
	u64 first_end = first_start + first_size;
	u64 second_start = second;
	u64 second_end = second_start + second_size;

	return first_start < second_end && second_start < first_end;
}

int i386_user_atomic_op_inuser(u32 operation, void __user *uaddr,
			       u32 expected, u32 value, u32 width,
			       u32 *observed)
{
	struct page *page;
	unsigned long flags;
	u32 old, new;
	bool store;
	int ret;

	if (!i386_atomic_operation_valid(operation) ||
	    !i386_atomic_width_valid(width) ||
	    !IS_ALIGNED((unsigned long)uaddr, width))
		return -EINVAL;
	if (!current->mm || !access_ok(uaddr, width))
		return -EFAULT;

	/* This helper runs with page faults disabled from the futex core.  Fast
	 * GUP verifies that the current PTE is writable (including completed COW)
	 * without falling back to a sleeping fault.  A miss is returned as EFAULT;
	 * the futex core faults the page writable and retries.  On this UP-only
	 * configuration no other task can replace the PTE between the successful
	 * lookup and the raw-spinlocked user access.  Writing through the user VA
	 * also lets the CPU set the PTE dirty bit normally.
	 */
	ret = get_user_pages_fast_only((unsigned long)uaddr, 1, FOLL_WRITE,
				       &page);
	if (ret != 1)
		return -EFAULT;

	raw_spin_lock_irqsave(&i386_user_atomic_lock, flags);
	old = i386_atomic_read_kernel((__force const void *)uaddr, width);
	ret = i386_atomic_calculate(operation, old, expected, value,
				    width, &new, &store);
	if (!ret && store)
		i386_atomic_write_kernel((__force void *)uaddr, width, new);
	raw_spin_unlock_irqrestore(&i386_user_atomic_lock, flags);
	put_page(page);

	if (!ret)
		*observed = old;
	return ret;
}

SYSCALL_DEFINE1(i386_atomic, struct i386_atomic_args __user *, uargs)
{
	struct i386_atomic_args args;
	struct page *target_page;
	struct page *result_page;
	void __user *uaddr;
	void *target_mapping;
	void *result_mapping;
	void *target;
	u32 *result;
	unsigned long flags;
	u32 old, new;
	bool store;
	int ret;

	if (copy_from_user(&args, uargs, sizeof(args)))
		return -EFAULT;
	if (args.version != I386_ATOMIC_ABI_VERSION || args.reserved ||
	    !i386_atomic_operation_valid(args.operation) ||
	    !i386_atomic_width_valid(args.width))
		return -EINVAL;

	uaddr = (void __user *)(unsigned long)args.address;
	if (!IS_ALIGNED((unsigned long)uaddr, args.width) ||
	    !IS_ALIGNED((unsigned long)&uargs->observed,
			sizeof(uargs->observed)) ||
	    !access_ok(uaddr, args.width) ||
	    !access_ok(&uargs->observed, sizeof(uargs->observed)) ||
	    i386_atomic_ranges_overlap((unsigned long)uaddr, args.width,
				       (unsigned long)uargs, sizeof(*uargs)))
		return -EINVAL;

	/* An 80386 ignores CR0.WP for supervisor writes.  Pin both writable
	 * userspace pages and update their kernel mappings, just like the i386
	 * copy_to_user slow path.  This preserves COW and read-only protection
	 * while the raw spinlock supplies the missing CPU atomic operation.
	 */
	ret = get_user_pages_fast((unsigned long)uaddr, 1, FOLL_WRITE,
				  &target_page);
	if (ret != 1) {
		ret = ret < 0 ? ret : -EFAULT;
		return ret;
	}
	ret = get_user_pages_fast((unsigned long)&uargs->observed, 1, FOLL_WRITE,
				  &result_page);
	if (ret != 1) {
		ret = ret < 0 ? ret : -EFAULT;
		goto out_put_target;
	}

	target_mapping = kmap_local_page(target_page);
	result_mapping = kmap_local_page(result_page);
	target = target_mapping + offset_in_page((unsigned long)uaddr);
	result = result_mapping +
		offset_in_page((unsigned long)&uargs->observed);

	raw_spin_lock_irqsave(&i386_user_atomic_lock, flags);
	old = i386_atomic_read_kernel(target, args.width);
	ret = i386_atomic_calculate(args.operation, old, args.expected,
				    args.value, args.width, &new, &store);
	if (!ret) {
		/* Publish the old value before changing the target. */
		WRITE_ONCE(*result, old);
		if (store)
			i386_atomic_write_kernel(target, args.width, new);
	}
	raw_spin_unlock_irqrestore(&i386_user_atomic_lock, flags);

	kunmap_local(result_mapping);
	kunmap_local(target_mapping);
	if (!ret) {
		set_page_dirty_lock(result_page);
		if (store)
			set_page_dirty_lock(target_page);
	}
	put_page(result_page);
out_put_target:
	put_page(target_page);
	return ret;
}

#endif /* CONFIG_X86_USER_ATOMIC_386 */
