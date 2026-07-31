/* Runtime smoke test for the glibc 2.41 i386/i486 PC-98 port. */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <gnu/libc-version.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define THREADS 3
#define LOOPS 200

static pthread_mutex_t thread_lock = PTHREAD_MUTEX_INITIALIZER;
static int thread_counter;
static __thread int tls_value;

struct shared_state {
	pthread_mutex_t lock;
	int value;
};

static void *
thread_main(void *argument)
{
	int id = (int)(intptr_t)argument;
	int i;
	char *allocation;

	tls_value = 0x1200 + id;
	allocation = malloc(257 + id);
	if (!allocation)
		return (void *)1;
	memset(allocation, id, 257 + id);
	for (i = 0; i < LOOPS; i++) {
		if (pthread_mutex_lock(&thread_lock))
			return (void *)2;
		thread_counter++;
		if (pthread_mutex_unlock(&thread_lock))
			return (void *)3;
	}
	free(allocation);
	return tls_value == 0x1200 + id ? NULL : (void *)4;
}

static int
fail(const char *what, int code)
{
	fprintf(stderr, "GLIBC_SMOKE_FAIL: %s (%d)\n", what, code);
	return 1;
}

int
main(void)
{
	pthread_t threads[THREADS];
	pthread_mutexattr_t attributes;
	struct shared_state *shared;
	struct timespec now;
	void *thread_result;
	void *libm;
	double (*cos_function)(double);
	pid_t child;
	int status;
	int rc;
	int i;

	printf("glibc=%s pid=%ld\n", gnu_get_libc_version(), (long)getpid());
	if (clock_gettime(CLOCK_MONOTONIC, &now))
		return fail("clock_gettime", errno);

	for (i = 0; i < THREADS; i++) {
		rc = pthread_create(&threads[i], NULL, thread_main,
				    (void *)(intptr_t)(i + 1));
		if (rc)
			return fail("pthread_create", rc);
	}
	for (i = 0; i < THREADS; i++) {
		rc = pthread_join(threads[i], &thread_result);
		if (rc || thread_result)
			return fail("pthread_join", rc ? rc : (int)(intptr_t)thread_result);
	}
	if (thread_counter != THREADS * LOOPS)
		return fail("pthread mutex counter", thread_counter);

	libm = dlopen("libm.so.6", RTLD_NOW | RTLD_LOCAL);
	if (!libm)
		return fail("dlopen libm", 0);
	*(void **)(&cos_function) = dlsym(libm, "cos");
	if (!cos_function || cos_function(0.0) != 1.0)
		return fail("dlsym cos", 0);
	dlclose(libm);

	shared = mmap(NULL, sizeof(*shared), PROT_READ | PROT_WRITE,
		      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (shared == MAP_FAILED)
		return fail("mmap", errno);
	memset(shared, 0, sizeof(*shared));
	if ((rc = pthread_mutexattr_init(&attributes)) ||
	    (rc = pthread_mutexattr_setpshared(&attributes,
					PTHREAD_PROCESS_SHARED)) ||
	    (rc = pthread_mutexattr_setrobust(&attributes,
				       PTHREAD_MUTEX_ROBUST)) ||
	    (rc = pthread_mutex_init(&shared->lock, &attributes)))
		return fail("process-shared robust mutex setup", rc);
	pthread_mutexattr_destroy(&attributes);

	child = fork();
	if (child < 0)
		return fail("fork", errno);
	if (!child) {
		if (pthread_mutex_lock(&shared->lock))
			_exit(2);
		shared->value = 0x386;
		_exit(0); /* Deliberately leave the robust mutex owned. */
	}
	if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
	    WEXITSTATUS(status))
		return fail("child wait", status);
	rc = pthread_mutex_lock(&shared->lock);
	if (rc != EOWNERDEAD)
		return fail("robust owner death", rc);
	if (shared->value != 0x386)
		return fail("process-shared data", shared->value);
	if ((rc = pthread_mutex_consistent(&shared->lock)) ||
	    (rc = pthread_mutex_unlock(&shared->lock)))
		return fail("robust mutex recovery", rc);
	pthread_mutex_destroy(&shared->lock);
	munmap(shared, sizeof(*shared));

	puts("GLIBC_SMOKE_OK");
	return 0;
}
