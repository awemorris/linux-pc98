// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal debug-register state for CONFIG_M386 without perf events.
 *
 * Linux normally gets these symbols from the perf-backed x86 hardware
 * breakpoint implementation.  The i386 boot milestone omits that framework,
 * but entry and temporary-mm code still save and restore architectural DR7.
 */

#include <linux/percpu.h>

#include <asm/debugreg.h>

DEFINE_PER_CPU(unsigned long, cpu_dr7) = DR7_FIXED_1;

void hw_breakpoint_restore(void)
{
	set_debugreg(this_cpu_read(cpu_dr7), 7);
}
