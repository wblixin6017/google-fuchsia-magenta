// Copyright 2017 The Fuchsia Authors
// Copyright (c) 2017, Google Inc. All rights reserved.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <string.h>
#include <dev/interrupt/arm_gicv3.h>
#include <dev/interrupt/arm_gicv3_regs.h>
#include <kernel/thread.h>
#include <lk/init.h>
#include <dev/interrupt.h>
#include <trace.h>
#include <lib/ktrace.h>

#define LOCAL_TRACE 1

#include <arch/arm64.h>
#define iframe arm64_iframe_short
#define IFRAME_PC(frame) ((frame)->elr)

static spin_lock_t gicd_lock;
#define GICD_LOCK_FLAGS SPIN_LOCK_FLAG_INTERRUPTS
#define GIC_MAX_PER_CPU_INT 32

struct int_handler_struct {
    int_handler handler;
    void* arg;
};

static bool arm_gic_interrupt_change_allowed(int irq)
{
    return true;
}

static struct int_handler_struct int_handler_table_per_cpu[GIC_MAX_PER_CPU_INT][SMP_MAX_CPUS];
static struct int_handler_struct int_handler_table_shared[MAX_INT-GIC_MAX_PER_CPU_INT];

static struct int_handler_struct* get_int_handler(unsigned int vector, uint cpu)
{
    if (vector < GIC_MAX_PER_CPU_INT) {
        return &int_handler_table_per_cpu[vector][cpu];
    } else {
        return &int_handler_table_shared[vector - GIC_MAX_PER_CPU_INT];
    }
}

void register_int_handler(unsigned int vector, int_handler handler, void* arg)
{
    struct int_handler_struct *h;
    uint cpu = arch_curr_cpu_num();

    spin_lock_saved_state_t state;

    if (vector >= MAX_INT) {
        panic("register_int_handler: vector out of range %u\n", vector);
    }

    spin_lock_save(&gicd_lock, &state, GICD_LOCK_FLAGS);

    if (arm_gic_interrupt_change_allowed(vector)) {
        h = get_int_handler(vector, cpu);
        h->handler = handler;
        h->arg = arg;
    }

    spin_unlock_restore(&gicd_lock, state, GICD_LOCK_FLAGS);
}

bool is_valid_interrupt(unsigned int vector, uint32_t flags)
{
    return (vector < MAX_INT);
}

static void gic_set_enable(uint vector, bool enable)
{
    int reg = vector / 32;
    uint32_t mask = 1ULL << (vector % 32);

    LTRACEF("gic_set_enable: vector=%u enable=%d\n", vector, enable);

    if (enable)
        GICREG(0, GICR_ISENABLER(reg)) = mask;
    else
        GICREG(0, GICR_ICENABLER(reg)) = mask;
}

static void arm_gic_init_percpu(uint level)
{
    printf("SRE=0x%x\n", gic_read_sre_el1());
    uint32_t sre = gic_read_sre_el1();
    if (!(sre & 0x1)) {
        gic_write_sre_el1(sre | 0x1);
        sre = gic_read_sre_el1();
        if (!(sre & 0x1)) {
            panic("gic: unable to set SRE\n");
        }
    }

    // set priority threshold to max
    gic_write_pmr_el1(0xff);

    // enable group 1 interrupts
    gic_write_igrpen1_el1(1);
}

LK_INIT_HOOK_FLAGS(arm_gic_init_percpu, arm_gic_init_percpu, LK_INIT_LEVEL_PLATFORM_EARLY, LK_INIT_FLAG_SECONDARY_CPUS);

void arm_gicv3_init(void)
{
    printf("GICD_PIDR2=0x%x\n", GICREG(0, GICD_PIDR2));
    printf("GICD_TYPER=0x%x\n", GICREG(0, GICD_TYPER));
    printf("GICD_CTLR=0x%x\n", GICREG(0, GICD_CTLR));

    // enable distributor with ARE, group 1 enable
    GICREG(0, GICD_CTLR) = (1 << 4) | (1 << 1) | (1 << 0);
    printf("GICD_CTLR=0x%x\n", GICREG(0, GICD_CTLR));

    int i;
    // route all global irqs to boot cpu only
    for (i = 0; i < 512; i++) {
        //printf("GICD_IROUTER%d=0x%" PRIx64 "\n", i, GICREG64(0, GICD_IROUTER(i)));
        // TODO
    }

    // configure SGI/PPI as non secure group 1
    for (i = 0; i < MAX_INT; i += 32) {
        GICREG(0, GICR_IGROUPR(i / 32)) = ~0;
        printf("GICR_IGROUPR%d=0x%x\n", i / 32, GICREG(0, GICR_IGROUPR(i / 32)));
    }

    arm_gic_init_percpu(0);
}

#if 0
status_t arm_gic_sgi(u_int irq, u_int flags, u_int cpu_mask)
{
    u_int val =
        ((flags & ARM_GIC_SGI_FLAG_TARGET_FILTER_MASK) << 24) |
        ((cpu_mask & 0xff) << 16) |
        ((flags & ARM_GIC_SGI_FLAG_NS) ? (1U << 15) : 0) |
        (irq & 0xf);

    if (irq >= 16)
        return ERR_INVALID_ARGS;

    LTRACEF("GICD_SGIR: %x\n", val);

    GICREG(0, GICD_SGIR) = val;

    return NO_ERROR;
}
#endif

status_t mask_interrupt(unsigned int vector)
{
    if (vector >= MAX_INT)
        return ERR_INVALID_ARGS;

    if (arm_gic_interrupt_change_allowed(vector))
        gic_set_enable(vector, false);

    return NO_ERROR;
}

status_t unmask_interrupt(unsigned int vector)
{
    if (vector >= MAX_INT)
        return ERR_INVALID_ARGS;

    if (arm_gic_interrupt_change_allowed(vector))
        gic_set_enable(vector, true);

    return NO_ERROR;
}

status_t configure_interrupt(unsigned int vector,
                             enum interrupt_trigger_mode tm,
                             enum interrupt_polarity pol)
{
    if (vector >= MAX_INT)
        return ERR_INVALID_ARGS;

    if (tm != IRQ_TRIGGER_MODE_EDGE) {
        // We don't currently support non-edge triggered interupts via the GIC,
        // and we pre-initialize everything to edge triggered.
        // TODO: re-evaluate this.
        return ERR_NOT_SUPPORTED;
    }

    if (pol != IRQ_POLARITY_ACTIVE_HIGH) {
        // TODO: polarity should actually be configure through a GPIO controller
        return ERR_NOT_SUPPORTED;
    }

    return NO_ERROR;
}

status_t get_interrupt_config(unsigned int vector,
                              enum interrupt_trigger_mode* tm,
                              enum interrupt_polarity* pol)
{
    if (vector >= MAX_INT)
        return ERR_INVALID_ARGS;

    if (tm)  *tm  = IRQ_TRIGGER_MODE_EDGE;
    if (pol) *pol = IRQ_POLARITY_ACTIVE_HIGH;

    return NO_ERROR;
}

unsigned int remap_interrupt(unsigned int vector) {
    return vector;
}

// called from assembly
enum handler_return platform_irq(struct iframe* frame) {
    // get the current vector
    uint32_t iar = gic_read_iar1_el1();
    unsigned vector = iar & 0x3ff;

    //LTRACEF("platform_irq vector=%u\n", vector);

    if (vector >= 0x3fe) {
        // spurious
        // TODO check this
        return INT_NO_RESCHEDULE;
    }

    THREAD_STATS_INC(interrupts);

    uint cpu = arch_curr_cpu_num();

    ktrace_tiny(TAG_IRQ_ENTER, (vector << 8) | cpu);

    LTRACEF_LEVEL(2, "iar 0x%x cpu %u currthread %p vector %u pc %#" PRIxPTR "\n", iar, cpu, get_current_thread(), vector, (uintptr_t)IFRAME_PC(frame));

    // deliver the interrupt
    enum handler_return ret = INT_NO_RESCHEDULE;
    struct int_handler_struct* handler = get_int_handler(vector, cpu);
    if (handler->handler) {
        ret = handler->handler(handler->arg);
    }

    gic_write_eoir1_el1(vector);

    LTRACEF_LEVEL(2, "cpu %u exit %u\n", cpu, ret);

    ktrace_tiny(TAG_IRQ_EXIT, (vector << 8) | cpu);

    return ret;
}

enum handler_return platform_fiq(struct iframe* frame) {
    PANIC_UNIMPLEMENTED;
}
