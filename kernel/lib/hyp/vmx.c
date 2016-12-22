// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#if ARCH_X86

#include <inttypes.h>
#include <stdio.h>
#include <err.h>
#include <string.h>
#include <trace.h>
#include <bits.h>
#include <kernel/vm.h>
#include <arch/x86/feature.h>
#include <kernel/thread.h>
#include <lk/init.h>

#include "vmx.h"

#define LOCAL_TRACE 1

// MSRs concerning VMX
#define X86_MSR_IA32_FEATURE_CONTROL        0x3au
#define X86_MSR_IA32_VMX_BASIC              0x480u
#define X86_MSR_IA32_VMX_PINBASED_CTLS      0x481u
#define X86_MSR_IA32_VMX_TRUE_PINBASED_CTLS 0x48du
#define X86_MSR_IA32_VMX_PROCBASED_CTLS     0x482u
#define X86_MSR_IA32_VMX_TRUE_PROCBASED_CTLS  0x48eu
#define X86_MSR_IA32_VMX_PROCBASED_CTLS2    0x48bu
#define X86_MSR_IA32_VMX_EXIT_CTLS          0x483u
#define X86_MSR_IA32_VMX_TRUE_EXIT_CTLS     0x48fu
#define X86_MSR_IA32_VMX_ENTRY_CTLS         0x484u
#define X86_MSR_IA32_VMX_MISC_MSR           0x485u
#define X86_MSR_IA32_VMX_CR0_FIXED0         0x486u
#define X86_MSR_IA32_VMX_CR0_FIXED1         0x487u
#define X86_MSR_IA32_VMX_CR4_FIXED0         0x488u
#define X86_MSR_IA32_VMX_CR4_FIXED1         0x489u
#define X86_MSR_IA32_VMX_VMCS_ENUM          0x48au
#define X86_MSR_IA32_VMX_VPID_CAP           0x48cu
#define X86_MSR_IA32_VMX_VMFUNC             0x491u

#define VMCS_FIELD_32_PIN_BASED_CTLS       (0x4000)
#define VMCS_FIELD_32_PROC_BASED_CTLS      (0x4002)
#define VMCS_FIELD_32_EXC_BITMAP_CTL       (0x4004)
#define VMCS_FIELD_32_VM_EXIT_CTLS         (0x400c)
#define VMCS_FIELD_32_VM_ENTRY_CTLS        (0x4012)
#define VMCS_FIELD_32_VM_INSTRUCTION_ERROR (0x4400)

static struct vmx_state {
    bool initialized;

    uint32_t revision_id;

    vm_page_t *vmxon_page;

} vmx;

__NO_INLINE static uint64_t vmread(uint32_t field) {
    uint8_t err;

    uint64_t val;
    __asm__ volatile(
        "vmread %[val], %[field];"
        "setna %[err];"
        : [err]"=qm"(err), [val]"=r"(val)
        : [field]"r"((uint64_t)field)
        : "cc", "memory");

    if (err) {
        printf("vmread failed on field 0x%x with reason %" PRIu64 "\n", field, vmread(VMCS_FIELD_32_VM_INSTRUCTION_ERROR));
    }
    DEBUG_ASSERT(!err);

    return val;
}

__NO_INLINE static void vmwrite(uint32_t field, uint64_t val) {
    uint8_t err;

    __asm__ volatile(
        "vmwrite %[val], %[field];"
        "setna %[err];"
        : [err]"=qm"(err)
        : [val]"r"(val), [field]"r"((uint64_t)field)
        : "cc", "memory");

    if (err) {
        printf("vmwrite failed on field 0x%x with reason %" PRIu64 "\n", field, vmread(VMCS_FIELD_32_VM_INSTRUCTION_ERROR));
    }
    DEBUG_ASSERT(!err);
}

static status_t vmxon(paddr_t pa) {
    uint8_t err;

    __asm__ volatile(
        "vmxon %1;"
        "setna %0;"
        : "=qm"(err) : "m"(pa) : "cc", "memory");

    return err ? ERR_INTERNAL : NO_ERROR;
}

static status_t vmclear(paddr_t pa) {
    uint8_t err;

    __asm__ volatile(
        "vmclear %1;"
        "setna %0;"
        : "=qm"(err) : "m"(pa) : "cc", "memory");

    return err ? ERR_INTERNAL : NO_ERROR;
}

static status_t vmptrld(paddr_t pa) {
    uint8_t err;

    __asm__ volatile(
        "vmptrld %1;"
        "setna %0;"
        : "=qm"(err) : "m"(pa) : "cc", "memory");

    return err ? ERR_INTERNAL : NO_ERROR;
}

static status_t vmlaunch(uint32_t *fail_reason) {
    uint8_t err;

    __asm__ volatile(
        "vmlaunch;"
        "setna %0;"
        : "=qm"(err) :: "cc", "memory");

    if (err) {
        *fail_reason = vmread(VMCS_FIELD_32_VM_INSTRUCTION_ERROR);
    }

    return err ? ERR_INTERNAL : NO_ERROR;
}

static void dump_vmx_msrs(void) {

    printf("VMX MSRs:\n");
    printf("0x%x X86_MSR_IA32_VMX_BASIC = %#" PRIx64 "\n", X86_MSR_IA32_VMX_BASIC, read_msr(X86_MSR_IA32_VMX_BASIC));
    printf("0x%x X86_MSR_IA32_VMX_PINBASED_CTLS = %#" PRIx64 "\n", X86_MSR_IA32_VMX_PINBASED_CTLS, read_msr(X86_MSR_IA32_VMX_PINBASED_CTLS));
    printf("0x%x X86_MSR_IA32_VMX_TRUE_PINBASED_CTLS = %#" PRIx64 "\n", X86_MSR_IA32_VMX_TRUE_PINBASED_CTLS, read_msr(X86_MSR_IA32_VMX_TRUE_PINBASED_CTLS));
    printf("0x%x X86_MSR_IA32_VMX_PROCBASED_CTLS = %#" PRIx64 "\n", X86_MSR_IA32_VMX_PROCBASED_CTLS, read_msr(X86_MSR_IA32_VMX_PROCBASED_CTLS));
    printf("0x%x X86_MSR_IA32_VMX_TRUE_PROCBASED_CTLS = %#" PRIx64 "\n", X86_MSR_IA32_VMX_TRUE_PROCBASED_CTLS, read_msr(X86_MSR_IA32_VMX_TRUE_PROCBASED_CTLS));
    printf("0x%x X86_MSR_IA32_VMX_PROCBASED_CTLS2 = %#" PRIx64 "\n", X86_MSR_IA32_VMX_PROCBASED_CTLS2, read_msr(X86_MSR_IA32_VMX_PROCBASED_CTLS2));
    printf("0x%x X86_MSR_IA32_VMX_EXIT_CTLS = %#" PRIx64 "\n", X86_MSR_IA32_VMX_EXIT_CTLS, read_msr(X86_MSR_IA32_VMX_EXIT_CTLS));
    printf("0x%x X86_MSR_IA32_VMX_TRUE_EXIT_CTLS = %#" PRIx64 "\n", X86_MSR_IA32_VMX_TRUE_EXIT_CTLS, read_msr(X86_MSR_IA32_VMX_TRUE_EXIT_CTLS));
    printf("0x%x X86_MSR_IA32_VMX_ENTRY_CTLS = %#" PRIx64 "\n", X86_MSR_IA32_VMX_ENTRY_CTLS, read_msr(X86_MSR_IA32_VMX_ENTRY_CTLS));
    printf("0x%x X86_MSR_IA32_VMX_MISC_MSR = %#" PRIx64 "\n", X86_MSR_IA32_VMX_MISC_MSR, read_msr(X86_MSR_IA32_VMX_MISC_MSR));
    printf("0x%x X86_MSR_IA32_VMX_CR0_FIXED0 = %#" PRIx64 "\n", X86_MSR_IA32_VMX_CR0_FIXED0, read_msr(X86_MSR_IA32_VMX_CR0_FIXED0));
    printf("0x%x X86_MSR_IA32_VMX_CR0_FIXED1 = %#" PRIx64 "\n", X86_MSR_IA32_VMX_CR0_FIXED1, read_msr(X86_MSR_IA32_VMX_CR0_FIXED1));
    printf("0x%x X86_MSR_IA32_VMX_CR4_FIXED0 = %#" PRIx64 "\n", X86_MSR_IA32_VMX_CR4_FIXED0, read_msr(X86_MSR_IA32_VMX_CR4_FIXED0));
    printf("0x%x X86_MSR_IA32_VMX_CR4_FIXED1 = %#" PRIx64 "\n", X86_MSR_IA32_VMX_CR4_FIXED1, read_msr(X86_MSR_IA32_VMX_CR4_FIXED1));
    printf("0x%x X86_MSR_IA32_VMX_VMCS_ENUM = %#" PRIx64 "\n", X86_MSR_IA32_VMX_VMCS_ENUM, read_msr(X86_MSR_IA32_VMX_VMCS_ENUM));
    printf("0x%x X86_MSR_IA32_VMX_VPID_CAP = %#" PRIx64 "\n", X86_MSR_IA32_VMX_VPID_CAP, read_msr(X86_MSR_IA32_VMX_VPID_CAP));
    //printf("0x%x X86_MSR_IA32_VMX_VMFUNC = %#" PRIx64 "\n", X86_MSR_IA32_VMX_VMFUNC, read_msr(X86_MSR_IA32_VMX_VMFUNC));
}

static void setup_vmcs(paddr_t pa) {
    uint64_t msr;
    uint32_t val;

    // set up the pin base controls
    msr = read_msr(X86_MSR_IA32_VMX_PINBASED_CTLS);
    val = (BITS(msr, 31, 0)) & (BITS_SHIFT(msr, 63, 32)); // set the 1 based parts
    LTRACEF("writing 0x%x to pin based ctls\n", val);
    vmwrite(VMCS_FIELD_32_PIN_BASED_CTLS, val);

    // set up the processor base controls
    msr = read_msr(X86_MSR_IA32_VMX_PROCBASED_CTLS);
    val = (BITS(msr, 31, 0)) & (BITS_SHIFT(msr, 63, 32)); // set the 1 based parts
    LTRACEF("writing 0x%x to proc based ctls\n", val);
    vmwrite(VMCS_FIELD_32_PROC_BASED_CTLS, val);

    // try to catch all exceptions
    vmwrite(VMCS_FIELD_32_EXC_BITMAP_CTL, 0xffffffff);

    // set up the exit controls
    msr = read_msr(X86_MSR_IA32_VMX_EXIT_CTLS);
    val = (BITS(msr, 31, 0)) & (BITS_SHIFT(msr, 63, 32)); // set the 1 based parts
    LTRACEF("writing 0x%x to exit ctls\n", val);
    vmwrite(VMCS_FIELD_32_VM_EXIT_CTLS, val);

    // set up the entry controls
    msr = read_msr(X86_MSR_IA32_VMX_ENTRY_CTLS);
    val = (BITS(msr, 31, 0)) & (BITS_SHIFT(msr, 63, 32)); // set the 1 based parts
    LTRACEF("writing 0x%x to entry ctls\n", val);
    vmwrite(VMCS_FIELD_32_VM_ENTRY_CTLS, val);
}

static int vmx_thread(void *arg) {
    status_t r;

    LTRACE_ENTRY;

    // check the status of the enable and lock bit for vmx
    uint64_t fc = read_msr(X86_MSR_IA32_FEATURE_CONTROL);
    LTRACEF("IA32_FEATURE_CONTROL %#" PRIx64 "\n", read_msr(X86_MSR_IA32_FEATURE_CONTROL));
    if (BIT(fc, 2) == 0) {
        // vmx outside smx is not enabled
        if (BIT(fc, 0)) {
            // locked
            TRACEF("VMX locked out, probably by the BIOS\n");
            return ERR_NOT_SUPPORTED;
        }

        // it's not set, and it's not locked, try to set it ourself
        // XXX not really safe unless we're pinned to a cpu
        write_msr(X86_MSR_IA32_FEATURE_CONTROL, fc | (1<<2));
        fc = read_msr(X86_MSR_IA32_FEATURE_CONTROL);
        if (BIT(fc, 2) == 0) {
            // we're hosed
            TRACEF("VMX locked out, tried to unlock but no avail\n");
            return ERR_NOT_SUPPORTED;
        }
    }

    // set the vmx enable bit in CR4
    x86_set_cr4(x86_get_cr4() | X86_CR4_VMXE);

    uint64_t basic_info = read_msr(X86_MSR_IA32_VMX_BASIC);
    TRACEF("basic vmx info %#" PRIx64 "\n", basic_info);
    vmx.revision_id = BITS(basic_info, 30, 0);

    dump_vmx_msrs();

    // allocate a physical page to hold the vmxon region
    paddr_t pa;
    vmx.vmxon_page = pmm_alloc_page(0, &pa);
    DEBUG_ASSERT(vmx.vmxon_page);

    // write the revision id to the first word of the VMXON region
    volatile uint32_t *p = paddr_to_kvaddr(pa);
    DEBUG_ASSERT(p);
    *p = vmx.revision_id;

    // try to enable vmx
    LTRACEF("enabling vmx via vmxon, pa %#" PRIxPTR "\n", pa);
    r = vmxon(pa);
    LTRACEF("done enabling vmx, err %d\n", r);

    // we made it

    // create a vmcs region
    vm_page_t *vmcs = pmm_alloc_page(0, &pa);
    DEBUG_ASSERT(vmcs);

    // zero it out
    p = paddr_to_kvaddr(pa);
    DEBUG_ASSERT(p);
    memset((void *)p, 0, PAGE_SIZE);

    // set the revision id
    *p = vmx.revision_id;

    // run vmclear on it
    LTRACEF("calling vmclear on vmcs at pa %#" PRIxPTR "\n", pa);
    r = vmclear(pa);
    LTRACEF("vmclear returns %d\n", r);

    // load it
    r = vmptrld(pa);
    LTRACEF("vmptrld returns %d\n", r);

    // set up the vmcs
    setup_vmcs(pa);

    //return NO_ERROR;

    // launch it
    uint32_t fail_reason;
    LTRACEF("launching\n");
    r = vmlaunch(&fail_reason);
    LTRACEF("vmlaunch returns %d\n", r);
    if (r < 0)
        printf("vmlaunch failed for reason 0x%x\n", fail_reason);

    for (;;)
        thread_sleep(1000);

    return NO_ERROR;
}

status_t vmx_init(void) {
    LTRACE_ENTRY;

    // test for feature
    if (!x86_feature_test(X86_FEATURE_VMX)) {
        // no vmx root capability, dont bother
        TRACEF("no VMX root support\n");
        return ERR_NOT_SUPPORTED;
    }

    // create a thread that we can pin on a cpu to continue
    thread_t *t = thread_create("vmx", &vmx_thread, NULL, HIGH_PRIORITY, DEFAULT_STACK_SIZE);
    if (!t)
        return ERR_NO_MEMORY;

    // pin on the first cpu
    thread_set_pinned_cpu(t, 0);
    thread_detach_and_resume(t);

    return NO_ERROR;
}

#endif // ARCH_X86
