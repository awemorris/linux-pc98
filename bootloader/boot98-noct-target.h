/*
 * Boots Noct target adapters
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: Zlib
 */

#ifndef BOOT98_NOCT_TARGET_H
#define BOOT98_NOCT_TARGET_H

typedef struct rt_env NoctEnv;

struct boot98_noct_services;

int boot98_noct_target_register(NoctEnv *env,
				const struct boot98_noct_services *services);
void boot98_noct_target_cleanup(void);

#endif
