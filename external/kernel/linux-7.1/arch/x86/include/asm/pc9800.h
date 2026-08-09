/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PC9800_H
#define _ASM_X86_PC9800_H

#include <linux/types.h>

struct pc98_boot_disk_setup;

#ifdef CONFIG_X86_PC9800
void pc9800_init_platform(void);
void pc9800_set_boot_disk_info(const struct pc98_boot_disk_setup *info);
bool pc9800_get_boot_disk_geometry(unsigned int *heads,
				   unsigned int *sectors);
bool pc9800_get_boot_disk_geometry_for(unsigned int bios_base,
				       unsigned int unit,
				       unsigned int *heads,
				       unsigned int *sectors);
#else
static inline void pc9800_init_platform(void) { }
static inline void pc9800_set_boot_disk_info(
	const struct pc98_boot_disk_setup *info) { }
static inline bool pc9800_get_boot_disk_geometry(unsigned int *heads,
					  unsigned int *sectors)
{
	return false;
}
static inline bool pc9800_get_boot_disk_geometry_for(unsigned int bios_base,
					      unsigned int unit,
					      unsigned int *heads,
					      unsigned int *sectors)
{
	return false;
}
#endif

#endif /* _ASM_X86_PC9800_H */
