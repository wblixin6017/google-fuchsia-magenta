#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/devicetree.h>

// This driver launches a devhost to host all devices published from the device tree.
// Ideally the devmgr will receive a handle to a VMO containing the device tree blob
// which it will pass to the devhost.

void devhost_launch_devhost(mx_device_t* parent, const char* name, uint32_t protocol_id,
                            const char* procname, int argc, char** argv);

static mx_status_t devicetree_root_init(mx_driver_t* driver) {

    char name[32];
    snprintf(name, sizeof(name), "devicetree");

    char procname[64];
    snprintf(procname, sizeof(procname), "devhost:soc:devicetree");

    char arg1[20];
    snprintf(arg1, sizeof(arg1), "soc");

    char arg2[20];
    snprintf(arg2, sizeof(arg2), "%d", SOC_VID_DEVICETREE);

    char arg3[20];
    snprintf(arg3, sizeof(arg3), "%d", SOC_PID_DEVICETREE);

    const char* args[4] = { "/boot/bin/devhost", arg1, arg2, arg3};
    devhost_launch_devhost(driver_get_root_device(), name, MX_PROTOCOL_SOC, procname, 4, (char**)args);

    return NO_ERROR;
}

mx_driver_t _driver_devicetree_root = {
    .ops = {
        .init = devicetree_root_init,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_devicetree_root, "devicetree-root", "magenta", "0.1", 0)
MAGENTA_DRIVER_END(_driver_devicetree_root)
