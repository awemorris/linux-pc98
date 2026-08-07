/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * Noct Programming Language
 * Copyright (c) 2025, 2026, Awe Morris
 */

/*
 * API: Thread.*
 */

#include <noct/noct.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>

#if defined(NOCT_TARGET_WINDOWS)
#include <fcntl.h>
#elif defined(NOCT_TARGET_POSIX)
#include <pthread.h>
#else
#error "No thread support for this platform."
#endif

#define NEVER_COME_HERE		(0)

/*
 * Forward declaration.
 */

/*
 * [Thread Object]
 *
 * var threadId = Thread.createThread(func, param);
 * Thread.joinThread(threadId);
 */
static bool cfunc_Thread_createThread(NoctEnv *env);
static bool cfunc_Thread_joinThread(NoctEnv *env);

/*
 * [Shared Object] (Lock-Free Dictionary)
 *
 * var shared = Thread.createShared({msg: ""});
 * 
 * func threadA_producer() {
 *     while(true) {
 *         Thread.updateShared(shared, {msg: produce()});
 *     }
 * }
 *
 * func threadB_consumer() {
 *     while(true) {
 *         var snapshot = Thread.snapshotShared(shared);
 *         consume(snapshot.msg);
 *     }
 * }
 */
static bool cfunc_Thread_createShared(NoctEnv *env);
static bool cfunc_Thread_updateShared(NoctEnv *env);
static bool cfunc_Thread_snapshotShared(NoctEnv *env);

/*
 * [Atomic Counter]
 *
 * exitRequest = Thread.createCounter();
 *
 * func threadA_incrementer() {
 *     processA();
 *     Thread.incrementCounter(exitRequest);
 * }
 *
 * func threadB_reader() {
 *     while(true) {
 *         if (Thread.getCounter(exitRequest) > 0)
 *             break;
 *         processB();
 *     }
 * }
 */
static bool cfunc_Thread_createCounter(NoctEnv *env);
static bool cfunc_Thread_incrementCounter(NoctEnv *env);
static bool cfunc_Thread_getCounter(NoctEnv *env);

/*
 * Locked Dictionary
 *
 * storage = Thread.createLocked({data: ""});
 * Thread.withLock(storage, (o) => { o.data = makeData(); });
 */
static bool cfunc_Thread_createLocked(NoctEnv *env);
static bool cfunc_Thread_withLock(NoctEnv *env);

/* FFI table. */
struct ffi_item {
	const char *global_name;
	const char *package_name;
	const char *field_name;
	size_t param_count;
	const char *param[NOCT_ARG_MAX];
	bool (*cfunc)(NoctEnv *env);
};
static struct ffi_item ffi_items[] = {
	{"Thread.createThread",		"Thread",		"createThread",			2,	{"func", "param"},	cfunc_Thread_createThread},
	{"Thread.joinThread",		"Thread",		"joinThread",			1,	{"id"},			cfunc_Thread_joinThread},
	{"Thread.createShared",		"Thread",		"createShared",			1,	{"dict"},		cfunc_Thread_createShared},
	{"Thread.updateShared",		"Thread",		"updateShared",			2,	{"shared", "dict"},	cfunc_Thread_updateShared},
	{"Thread.snapshotShared",	"Thread",		"snapshotShared",		1,	{"shared"},		cfunc_Thread_snapshotShared},
	{"Thread.createCounter",	"Thread",		"createCounter",		0,	{NULL},			cfunc_Thread_createCounter},
	{"Thread.incrementCounter",	"Thread",		"incrementCounter",		1,	{"counter"},		cfunc_Thread_incrementCounter},
	{"Thread.getCounter",		"Thread",		"getCounter",			1,	{"counter"},		cfunc_Thread_getCounter},
	{"Thread.createLocked",		"Thread",		"createLocked",			1,	{"dict"},		cfunc_Thread_createLocked},
	{"Thread.withLock",		"Thread",		"withLock",			2,	{"locked", "func"},	cfunc_Thread_withLock},
};

/*
 * Register "Thread.*" functions.
 */
NOCT_DLL
bool
noct_register_api_thread(
	NoctEnv *env)
{
	NoctValue thread_dict;
	size_t i;

	/* Make global variables "Thread". */
	if (!noct_make_empty_dict(env, &thread_dict))
		return false;
	if (!noct_set_global(env, "Thread", &thread_dict))
		return false;

	/* Register functions. */
	for (i = 0; i < sizeof(ffi_items) / sizeof(struct ffi_item); i++) {
		NoctValue funcval;

		/* Register a cfunc. */
		if (!noct_register_cfunc(
			    env,
			    ffi_items[i].global_name,
			    ffi_items[i].param_count,
			    ffi_items[i].param,
			    ffi_items[i].cfunc,
			    NULL))
			return false;

		/* Get a function value. */
		if (!noct_get_global(env, ffi_items[i].global_name, &funcval))
			return false;

		/* Make a dictionary element. */
		if (!noct_set_dict_elem_cstr(env, &thread_dict, ffi_items[i].field_name, &funcval))
			return false;
	}

	return true;
}

/*
 * Windows version
 */
#if defined(NOCT_TARGET_WINDOWS)

static bool
cfunc_Thread_createThread(
	NoctEnv *env)
{
	UNUSED_PARAMETER(env);
	return false;
}

static bool
cfunc_Thread_joinThread(
	NoctEnv *env)
{
	UNUSED_PARAMETER(env);
	return false;
}

#endif /* defined(NOCT_TARGET_WINDOWS) */

/*
 * POSIX thread version
 */
#if defined(NOCT_TARGET_WINDOWS)

static bool
cfunc_Thread_createThread(
	NoctEnv *env)
{
	UNUSED_PARAMETER(env);
	return false;
}

static bool
cfunc_Thread_joinThread(
	NoctEnv *env)
{
	UNUSED_PARAMETER(env);
	return false;
}

#endif /* defined(NOCT_TARGET_WINDOWS) */

/*
 * Common
 */

static bool
cfunc_Thread_createShared(
	NoctEnv *env)
{
	UNUSED_PARAMETER(env);
	return false;
}

static bool
cfunc_Thread_updateShared(
	NoctEnv *env)
{
	UNUSED_PARAMETER(env);
	return false;
}

static bool
cfunc_Thread_snapshotShared(
	NoctEnv *env)
{
	UNUSED_PARAMETER(env);
	return false;
}

static bool
cfunc_Thread_createCounter(
	NoctEnv *env)
{
	UNUSED_PARAMETER(env);
	return false;
}

static bool
cfunc_Thread_incrementCounter(
	NoctEnv *env)
{
	UNUSED_PARAMETER(env);
	return false;
}

static bool
cfunc_Thread_getCounter(
	NoctEnv *env)
{
	UNUSED_PARAMETER(env);
	return false;
}
