# musl 1.2.6 legacy-i386 patch set

Apply to the pristine `upstream-1.2.6` snapshot in this order:

1. `0001-i386-implement-internal-locks-with-test-and-set.patch`
   - Replaces internal lock acquisition with a three-state test-and-set
     lock based on memory `LOCK XCHG`.
   - Uses futex wait/wake under contention.
   - Removes the post-i386 `PAUSE`, `CMPXCHG`, and `XADD` dependency from
     the internal lock path.
   - Implements locking directly; it is not a CAS emulation.
2. `0002-i386-add-static-low-memory-profile.patch`
   - Adds the explicit `MUSL_I386_SINGLE_THREAD` profile.
   - Adds `MUSL_NO_FLOAT` guards so unused floating stdio code and x87
     instructions are not linked into the low-memory BusyBox.
   - Preserves stdio ownership and recursion behavior through the internal
     TAS lock.
   - Does not claim complete pthread support on an Intel 80386.
3. `0003-Document-patch-export-location.patch`
   - Updates maintenance documentation only.
   - Records that this directory in the parent repository contains the
     portable patch exports; it makes no functional source change.

Validated output:

- static ELF32/i386 BusyBox;
- 28 `LOCK XCHG` sites;
- zero `CMPXCHG`, `XADD`, x87, SIMD, or post-i386 instruction;
- 16-thread TAS contention test: 4,000,000 expected and observed updates;
- qemu-pc98 `-M pc9801 -cpu 386 -m 4736K`;
- ext4 root mount, 32 MiB swap activation, and BusyBox shell.
