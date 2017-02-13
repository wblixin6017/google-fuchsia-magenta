#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>

#include <rpm-glink.h>
extern void platform_clock_init(void);
extern void glink_init(void);

static int irq_thread(void* arg) {
    printf("irq_thread start\n");

    //msm-gladiator-v2
    mx_handle_t irq_handle = mx_interrupt_create(get_root_resource(), 32 + 22,
                                                MX_FLAG_REMAP_IRQ);
    if (irq_handle == MX_HANDLE_INVALID) {
        printf("mx_interrupt_create failed!\n");
        return -1;
    }

    while (1) {
        mx_status_t wait_res;

        wait_res = mx_interrupt_wait(irq_handle);
        if (wait_res != NO_ERROR) {
            if (wait_res != ERR_HANDLE_CLOSED) {
                printf("unexpected pci_wait_interrupt failure (%d)\n", wait_res);
            }
            mx_interrupt_complete(irq_handle);
            break;
        }
        printf("got IRQ!\n");
        mx_interrupt_complete(irq_handle);
    }

    mx_handle_close(irq_handle);

    printf("irq_thread done\n");
    return 0;
}



static mx_status_t test_init(mx_driver_t* driver) {
    printf("test_init HELLO!\n");

// qcom clock driver init
    platform_clock_init();
    
    
//com glink driver init
    glink_init();
// currently this hangs
    printf("call rpm_glink_init!\n");
    rpm_glink_init();
    printf("did rpm_glink_init!\n");

/*
    thrd_t thread;
    thrd_create_with_name(&thread, irq_thread, NULL, "irq_thread");
    thrd_detach(thread);
*/
    return NO_ERROR;
}

mx_driver_t _driver_test = {
    .ops = {
        .init = test_init,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_test, "soc", "magenta", "0.1", 0)
MAGENTA_DRIVER_END(_driver_test)
