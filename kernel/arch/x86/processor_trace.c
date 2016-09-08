// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// We currently only support Table of Physical Addresses mode currently, so that
// we can have stop-on-full behavior rather than wrap-around.

#include <arch/x86.h>
#include <arch/x86/feature.h>
#include <arch/x86/processor_trace.h>
#include <err.h>
#include <kernel/thread.h>
#include <kernel/vm.h>

// Control MSRs
#define IA32_RTIT_OUTPUT_BASE 0x560
#define IA32_RTIT_OUTPUT_MASK_PTRS 0x561
#define IA32_RTIT_CTL 0x570
#define IA32_RTIT_STATUS 0x571
#define IA32_RTIT_CR3_MATCH 0x572
#define IA32_RTIT_ADDR0_A 0x580
#define IA32_RTIT_ADDR0_B 0x581
#define IA32_RTIT_ADDR1_A 0x582
#define IA32_RTIT_ADDR1_B 0x583
#define IA32_RTIT_ADDR2_A 0x584
#define IA32_RTIT_ADDR2_B 0x585
#define IA32_RTIT_ADDR3_A 0x586
#define IA32_RTIT_ADDR3_B 0x587

// Macros for building entries for the Table of Physical Addresses
#define TOPA_ENTRY_PHYS_ADDR(x) ((uint64_t)(x) & ~((1ULL<<12)-1))
#define TOPA_ENTRY_SIZE(size_log2) ((uint64_t)((size_log2) - 12) << 6)
#define TOPA_ENTRY_STOP (1ULL << 4)
#define TOPA_ENTRY_INT (1ULL << 1)
#define TOPA_ENTRY_END (1ULL << 0)

// Macros for extracting info from ToPA entries
#define TOPA_ENTRY_EXTRACT_PHYS_ADDR(e) ((paddr_t)((e) & ~((1ULL<<12)-1)))
#define TOPA_ENTRY_EXTRACT_SIZE(e) ((uint)((((e) >> 6) & 0xf) + 12))

// Macros for building IA32_RTIT_CTL values
#define RTIT_CTL_TRACE_EN (1ULL<<0)
#define RTIT_CTL_CYC_EN (1ULL<<1)
#define RTIT_CTL_OS_ALLOWED (1ULL<<2)
#define RTIT_CTL_USER_ALLOWED (1ULL<<3)
#define RTIT_CTL_POWER_EVENT_EN (1ULL<<4)
#define RTIT_CTL_FUP_ON_PTW (1ULL<<5)
#define RTIT_CTL_FABRIC_EN (1ULL<<6)
#define RTIT_CTL_CR3_FILTER (1ULL<<7)
#define RTIT_CTL_TOPA (1ULL<<8)
#define RTIT_CTL_MTC_EN (1ULL<<9)
#define RTIT_CTL_TSC_EN (1ULL<<10)
#define RTIT_CTL_DIS_RETC (1ULL<<11)
#define RTIT_CTL_PTW_EN (1ULL<<12)
#define RTIT_CTL_BRANCH_EN (1ULL<<13)

// Masks for reading IA32_RTIT_STATUS
#define RTIT_STATUS_FILTER_EN (1ULL<<0)
#define RTIT_STATUS_CONTEXT_EN (1ULL<<1)
#define RTIT_STATUS_TRIGGER_EN (1ULL<<2)
#define RTIT_STATUS_ERROR (1ULL<<4)
#define RTIT_STATUS_STOPPED (1ULL<<5)

static bool supports_cr3_filtering = false;
static bool supports_psb = false;
static bool supports_ip_filtering = false;
static bool supports_mtc = false;
static bool supports_ptwrite = false;
static bool supports_power_events = false;

static bool supports_output_topa = false;
static bool supports_output_topa_multi = false;
static bool supports_output_single = false;
static bool supports_output_transport = false;

void x86_processor_trace_init(void)
{
    if (!x86_feature_test(X86_FEATURE_PT)) {
        return;
    }

    struct cpuid_leaf leaf;
    if (!x86_get_cpuid_subleaf(X86_CPUID_PT, 0, &leaf)) {
        return;
    }

    supports_cr3_filtering = !!(leaf.b & (1<<0));
    supports_psb = !!(leaf.b & (1<<1));
    supports_ip_filtering = !!(leaf.b & (1<<2));
    supports_mtc = !!(leaf.b & (1<<3));
    supports_ptwrite = !!(leaf.b & (1<<4));
    supports_power_events = !!(leaf.b & (1<<5));

    supports_output_topa = !!(leaf.c & (1<<0));
    supports_output_topa_multi = !!(leaf.c & (1<<1));
    supports_output_single = !!(leaf.c & (1<<2));
    supports_output_transport = !!(leaf.c & (1<<3));

    // TODO(teisenbe): For IP filtering, MTC, CYC, and PSB support, we need
    // to enumerate subleaf 1
}

// This operates in the thread-context.  The currently running thread will
// be traced until either trace_disable() is called or until the capture
// buffer fills.
//
// *capture_size_log2* must be in the range [12, 27].
status_t x86_processor_trace_enable(uint capture_size_log2) {
    if (!supports_output_topa) {
        return ERR_NOT_SUPPORTED;
    }

    if (capture_size_log2 < 12 || capture_size_log2 > 27) {
        return ERR_NOT_SUPPORTED;
    }

    thread_t* thread = get_current_thread();
    if (thread->arch.processor_trace_ctx) {
        return ERR_ALREADY_STARTED;
    }
    if ((read_msr(IA32_RTIT_CTL) & RTIT_CTL_TRACE_EN) ||
        (read_msr(IA32_RTIT_STATUS) & RTIT_STATUS_STOPPED)) {
        return ERR_ALREADY_STARTED;
    }

    // Allocate the capture buffer.  It must be aligned to its size.
    struct list_node list = LIST_INITIAL_VALUE(list);
    paddr_t capture_phys = 0;
    size_t requested_count = (1ULL << capture_size_log2) / PAGE_SIZE;
    size_t allocated = pmm_alloc_contiguous(
            requested_count, 0, capture_size_log2, &capture_phys, &list);
    if (allocated != requested_count) {
        return ERR_NO_MEMORY;
    }

    // Create the Table of Physical Addresses.  We currently only support one
    // structure, which is a table with a single entry that triggers a STOP
    // when it is full, followed by a mandatory END entry.
    // TODO(teisenbe): Support for more complex ToPAs, so we can have
    // larger capture buffers
    uint64_t* topa = memalign(PAGE_SIZE, 2 * sizeof(uint64_t));
    if (!topa) {
        pmm_free(&list);
        return ERR_NO_MEMORY;
    }
    paddr_t topa_phys = vaddr_to_paddr(topa);

    topa[0] = TOPA_ENTRY_PHYS_ADDR(capture_phys);
    topa[0] |= TOPA_ENTRY_SIZE(capture_size_log2) | TOPA_ENTRY_STOP;

    // The address shouldn't actually be needed in this entry since
    // STOP is set on entry 0, but set it to the top the table to leave it
    // as a well-defined safe address.
    topa[1] = TOPA_ENTRY_PHYS_ADDR(topa_phys) | TOPA_ENTRY_END;

    // Load the ToPA configuration
    write_msr(IA32_RTIT_OUTPUT_BASE, topa_phys);
    write_msr(IA32_RTIT_OUTPUT_MASK_PTRS, 0);

    // Enable the trace
    uint64_t ctl = RTIT_CTL_TOPA | RTIT_CTL_TRACE_EN;
    // TODO(teisenbe): Allow caller provided flags for controlling
    // these options.
    ctl |= RTIT_CTL_USER_ALLOWED | RTIT_CTL_OS_ALLOWED;
    ctl |= RTIT_CTL_BRANCH_EN;
    ctl |= RTIT_CTL_TSC_EN;
    write_msr(IA32_RTIT_CTL, ctl);

    thread->arch.processor_trace_ctx = topa;

    return NO_ERROR;
}

status_t x86_processor_trace_disable(
        paddr_t* capture_buf, size_t* buffer_size, size_t* capture_size) {
    // *capture_buf* will be populated with the physical address of the buffer on
    // success.  It is the caller's responsibility to free it.

    // Disable the trace
    write_msr(IA32_RTIT_CTL, 0);

    // Save info we care about for output
    uint64_t trace_cursors = read_msr(IA32_RTIT_OUTPUT_MASK_PTRS);

    // Zero all MSRs so that we are in the XSAVE initial configuration
    write_msr(IA32_RTIT_OUTPUT_BASE, 0);
    write_msr(IA32_RTIT_OUTPUT_MASK_PTRS, 0);
    write_msr(IA32_RTIT_STATUS, 0);
    if (supports_cr3_filtering) {
        write_msr(IA32_RTIT_CR3_MATCH, 0);
    }

    // TODO(teisenbe): Clear ADDR* MSRs depending on leaf 1

    thread_t* thread = get_current_thread();
    if (!thread->arch.processor_trace_ctx) {
        return ERR_BAD_STATE;
    }

    uint64_t* topa = thread->arch.processor_trace_ctx;
    *capture_buf = TOPA_ENTRY_EXTRACT_PHYS_ADDR(topa[0]);
    *buffer_size = 1ULL << TOPA_ENTRY_EXTRACT_SIZE(topa[0]);
    *capture_size = trace_cursors >> 32;

    free(topa);
    thread->arch.processor_trace_ctx = NULL;
    return NO_ERROR;
}

// Clean up the processor trace resources for the given thread
// Must only be called if the thread is dead.
void x86_processor_trace_cleanup(thread_t* thread) {
    DEBUG_ASSERT(thread->state == THREAD_DEATH);
    DEBUG_ASSERT(arch_ints_disabled());

    if (!thread->arch.processor_trace_ctx) {
        return;
    }

    paddr_t capture_phys;
    size_t buffer_size;

    thread_t* self = get_current_thread();
    if (thread == self) {
        size_t capture_size;
        status_t status = x86_processor_trace_disable(
                &capture_phys, &buffer_size, &capture_size);
        if (status != NO_ERROR) {
            return;
        }
    } else {
        uint64_t* topa = thread->arch.processor_trace_ctx;
        capture_phys = TOPA_ENTRY_EXTRACT_PHYS_ADDR(topa[0]);
        buffer_size = 1ULL << TOPA_ENTRY_EXTRACT_SIZE(topa[0]);
        free(topa);
        thread->arch.processor_trace_ctx = NULL;
    }

    for (size_t offset = 0; offset < buffer_size; offset += PAGE_SIZE) {
        vm_page_t* page = paddr_to_vm_page(capture_phys + offset);
        pmm_free_page(page);
    }
}
