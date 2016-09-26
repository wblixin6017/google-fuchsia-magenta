// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/x86/suspend.h>

#include <arch/mmu.h>
#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <arch/x86/bootstrap16.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/tsc.h>
#include <kernel/mp.h>
#include <kernel/timer.h>

uint8_t x86_resume_stack[4096] __ALIGNED(16);

void x86_suspend_wakeup(void* usermode_aspace, uint64_t usermode_ip, void* bootstrap_aspace)
{
    // TODO: remove
    extern void platform_init_debug_early(void);
    platform_init_debug_early();

    x86_init_percpu(0);
    x86_mmu_percpu_init();

    /* Free the bootstrap resources we used. */
    vmm_free_aspace(bootstrap_aspace);

    /* Reset usermode fs/gs.  acpisvc will use syscalls to reinitialize them */
    write_msr(X86_MSR_IA32_FS_BASE, 0);
    write_msr(X86_MSR_IA32_KERNEL_GS_BASE, 0);

    apic_local_init();

    timer_thaw_percpu();

    /* Switch into our aspace */
    arch_mmu_context_switch(NULL, usermode_aspace);

    /* IOPL 0, interrupts disabled */
    uint64_t flags = (0 << X86_FLAGS_IOPL_SHIFT);

    /* Return to usermode */
    x86_uspace_entry(0, 0, 0, usermode_ip, flags);
}
