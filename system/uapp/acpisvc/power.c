// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power.h"

#include <assert.h>
#include <stdio.h>

#include <acpica/acpi.h>
#include <hw/inout.h>
#include <magenta/prctl.h>
#include <magenta/syscalls.h>

#include "suspend.h"

void poweroff(void) {
    ACPI_STATUS status = AcpiEnterSleepStatePrep(5);
    if (status == AE_OK) {
        AcpiEnterSleepState(5);
    }
}

void reboot(void) {
    AcpiReset();
}

static void assert_interrupt_status(bool enabled) {
    uint64_t state;
    __asm__ volatile(
        "pushfq;"
        "popq %0"
        : "=rm" (state)
        :: "memory");

    assert(!!(state & 0x200) == enabled);
}

void debug_putc(char c) {
    static uint16_t uart_io_port = 0x3f8;
    while ((inp(uart_io_port + 5) & (1<<6)) == 0);
    outp(uart_io_port + 0, c);
}

void debug_puts(const char* s) {
    while (*s) {
        char c = *s;
        debug_putc(c);
        if (c == '\n') {
            debug_putc('\r');
        }
        s++;
    }
}

mx_status_t perform_suspend(void) {
    debug_puts("Performing suspend!\n");
    extern mx_handle_t root_resource_handle;

    debug_puts("Saving fs/gs\n");
    uintptr_t fs, gs;
    mx_handle_t self = 0;
    mx_status_t status = mx_thread_arch_prctl(self, ARCH_GET_FS, (uintptr_t*)&fs);
    if (status != NO_ERROR) {
        debug_puts("failed to save fs\n");
        return status;
    }
    status = mx_thread_arch_prctl(self, ARCH_GET_GS, (uintptr_t*)&gs);
    if (status != NO_ERROR) {
        debug_puts("failed to save gs\n");
        return status;
    }

    status = mx_acpi_set_interrupts_enabled(root_resource_handle, false);
    if (status != NO_ERROR) {
        debug_puts("Failed to disable interrupts\n");
        return status;
    }

    assert_interrupt_status(false);
    debug_puts("Assert 1 passed\n");
    uint32_t wake_vector;

    status = mx_acpi_prepare_for_suspend(root_resource_handle, (void*)&x86_suspend_resume, &wake_vector);
    if (status != NO_ERROR) {
        debug_puts("Failed to prep\n");
        return status;
    }

    assert_interrupt_status(false);
    debug_puts("Assert 2 passed\n");

    AcpiSetFirmwareWakingVector(wake_vector, 0);

    // TODO(teisenbe): Don't leak kernel resources if we fail to suspend

    debug_puts("Entering sleep state prep\n");
    ACPI_STATUS acpi_status = AcpiEnterSleepStatePrep(3);
    if (acpi_status != NO_ERROR) {
        debug_puts("Failed sleep state prep\n");
        // TODO: I think we need to do LeaveSleepState{Prep,} on failure
        return ERR_INTERNAL;
    }

    debug_puts("Doing suspend\n");
    acpi_status = x86_do_suspend();
    if (acpi_status != NO_ERROR) {
        debug_puts("Failed to suspend\n");
        // TODO: figure out what to do here
        return ERR_INTERNAL;
    }

    status = mx_thread_arch_prctl(self, ARCH_SET_FS, (uintptr_t*)&fs);
    if (status != NO_ERROR) {
        debug_puts("failed to restore fs\n");
    }
    status = mx_thread_arch_prctl(self, ARCH_SET_GS, (uintptr_t*)&gs);
    if (status != NO_ERROR) {
        debug_puts("failed to restore gs\n");
    }

    debug_puts("Leaving sleep state prep\n");
    acpi_status = AcpiLeaveSleepStatePrep(3);
    if (acpi_status != NO_ERROR) {
        debug_puts("Failed leaving sleep state prep\n");
        // TODO: figure ut what to do here
        //return ERR_INTERNAL;
    }

    debug_puts("Leaving sleep state\n");
    acpi_status = AcpiLeaveSleepState(3);
    if (acpi_status != NO_ERROR) {
        debug_puts("Failed leaving sleep state\n");
        // TODO: figure ut what to do here
        //return ERR_INTERNAL;
    }

    debug_puts("Enabling interrupts\n");
    status = mx_acpi_set_interrupts_enabled(root_resource_handle, true);
    if (status != NO_ERROR) {
        printf("Failed to enable interrupts: %d\n", status);
        return status;
    }

    assert_interrupt_status(true);

    debug_puts("Returning success\n");
    return NO_ERROR;
}
