/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PC9800_H
#define _ASM_X86_PC9800_H

#ifdef CONFIG_X86_PC9800
void pc9800_init_platform(void);
#else
static inline void pc9800_init_platform(void) { }
#endif

#endif /* _ASM_X86_PC9800_H */
