// SPDX-License-Identifier: GPL-2.0
/* Tests for the PC-98 Linux genuine-i386 atomic syscall ABI. */

#define _GNU_SOURCE
#include <asm/i386_atomic.h>
#include <asm/unistd_32.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define THREADS 4
#define THREAD_LOOPS 2000
#define FORK_LOOPS 1000

static int test_number;
static volatile uint32_t threaded_value;

static long
atomic_call(volatile void *address, uint32_t width, uint32_t operation,
	    uint32_t expected, uint32_t value, uint32_t *observed)
{
	struct i386_atomic_args args = {
		.version = I386_ATOMIC_ABI_VERSION,
		.operation = operation,
		.width = width,
		.address = (uint32_t)(uintptr_t)address,
		.expected = expected,
		.value = value,
	};
	long ret = syscall(__NR_i386_atomic, &args);

	if (!ret && observed)
		*observed = args.observed;
	return ret;
}

static void
result(int pass, const char *name)
{
	printf("%s %d - %s\n", pass ? "ok" : "not ok", ++test_number, name);
	if (!pass)
		exit(1);
}

static void *
xadd_thread(void *unused)
{
	unsigned int i;

	(void)unused;
	for (i = 0; i < THREAD_LOOPS; i++)
		if (atomic_call(&threaded_value, 4, I386_ATOMIC_XADD,
				0, 1, NULL))
			return (void *)1;
	return NULL;
}

int
main(void)
{
	union {
		uint32_t word;
		uint16_t half[2];
		uint8_t byte[4];
	} target = { .word = 0x12345678 };
	uint32_t observed;
	pthread_t threads[THREADS];
	void *thread_result;
	volatile uint32_t *shared;
	volatile uint32_t *readonly;
	volatile uint32_t *private_word;
	void *none;
	pid_t child;
	int status;
	unsigned int i;
	struct i386_atomic_args overlap = {
		.version = I386_ATOMIC_ABI_VERSION,
		.operation = I386_ATOMIC_XADD,
		.width = 4,
		.value = 1,
	};
	unsigned char unaligned_request[sizeof(overlap) + 1]
		__attribute__((aligned(4)));

	puts("TAP version 13");
	puts("1..12");

	result(!atomic_call(&target.word, 4, I386_ATOMIC_CMPXCHG,
			    0x12345678, 0x89abcdef, &observed) &&
	       observed == 0x12345678 && target.word == 0x89abcdef,
	       "32-bit compare-and-exchange success");
	result(!atomic_call(&target.word, 4, I386_ATOMIC_CMPXCHG,
			    1, 2, &observed) &&
	       observed == 0x89abcdef && target.word == 0x89abcdef,
	       "compare-and-exchange mismatch is non-destructive");
	result(!atomic_call(&target.half[0], 2, I386_ATOMIC_XADD,
			    0, 3, &observed) &&
	       observed == 0xcdef && target.half[0] == (uint16_t)0xcdf2,
	       "16-bit exchange-and-add");
	result(!atomic_call(&target.byte[0], 1, I386_ATOMIC_XOR,
			    0, 0xff, &observed) &&
	       observed == 0xf2 && target.byte[0] == 0x0d,
	       "8-bit logical operation");

	errno = 0;
	result(atomic_call((char *)&target.word + 1, 4, I386_ATOMIC_XADD,
			   0, 1, NULL) == -1 && errno == EINVAL,
	       "misaligned target is rejected");

	none = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (none == MAP_FAILED)
		return 1;
	errno = 0;
	result(atomic_call(none, 4, I386_ATOMIC_XADD, 0, 1, NULL) == -1 &&
	       errno == EFAULT, "inaccessible target is fault-contained");
	munmap(none, 4096);

	readonly = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (readonly == MAP_FAILED)
		return 1;
	*readonly = 0x11223344;
	if (mprotect((void *)readonly, 4096, PROT_READ))
		return 1;
	errno = 0;
	result(atomic_call(readonly, 4, I386_ATOMIC_XADD, 0, 1, NULL) == -1 &&
	       errno == EFAULT && *readonly == 0x11223344,
	       "read-only target is rejected without modification");
	munmap((void *)readonly, 4096);

	private_word = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (private_word == MAP_FAILED)
		return 1;
	*private_word = 41;
	child = fork();
	if (child < 0)
		return 1;
	if (!child) {
		if (atomic_call(private_word, 4, I386_ATOMIC_XADD,
				0, 1, &observed) || observed != 41 ||
		    *private_word != 42)
			_exit(1);
		_exit(0);
	}
	if (waitpid(child, &status, 0) != child)
		return 1;
	result(WIFEXITED(status) && !WEXITSTATUS(status) &&
	       *private_word == 41,
	       "private mapping honors fork copy-on-write");
	munmap((void *)private_word, 4096);

	threaded_value = 0;
	for (i = 0; i < THREADS; i++)
		if (pthread_create(&threads[i], NULL, xadd_thread, NULL))
			return 1;
	for (i = 0; i < THREADS; i++) {
		if (pthread_join(threads[i], &thread_result) || thread_result)
			return 1;
	}
	result(threaded_value == THREADS * THREAD_LOOPS,
	       "threaded exchange-and-add is serialized");

	shared = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
		      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (shared == MAP_FAILED)
		return 1;
	*shared = 0;
	child = fork();
	if (child < 0)
		return 1;
	for (i = 0; i < FORK_LOOPS; i++)
		if (atomic_call(shared, 4, I386_ATOMIC_XADD, 0, 1, NULL))
			return 1;
	if (!child)
		_exit(0);
	if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
	    WEXITSTATUS(status))
		return 1;
	result(*shared == FORK_LOOPS * 2,
	       "process-shared exchange-and-add is serialized");
	munmap((void *)shared, 4096);

	/* The result field must not alias the target: this keeps an EFAULT from
	 * being reported after a target update has already become visible. */
	overlap.address = (uint32_t)(uintptr_t)&overlap.observed;
	errno = 0;
	result(syscall(__NR_i386_atomic, &overlap) == -1 && errno == EINVAL,
	       "request and target overlap is rejected");

	/* A naturally aligned result field cannot cross a page boundary.  The
	 * kernel rejects a deliberately unaligned request rather than pinning
	 * one page and writing a four-byte result into the next one. */
	memcpy(unaligned_request + 1, &overlap, sizeof(overlap));
	errno = 0;
	result(syscall(__NR_i386_atomic, unaligned_request + 1) == -1 &&
	       errno == EINVAL, "unaligned request structure is rejected");

	return 0;
}
