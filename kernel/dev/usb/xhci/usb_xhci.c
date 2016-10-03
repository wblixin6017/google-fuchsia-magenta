// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <dev/pcie.h>
#include <kernel/vm.h>
#include <sys/types.h>
#include <trace.h>

#include <xhci/xhci.h>
#include <xhci/xhci-device-manager.h>

#define XHCI_PCI_CLASS (0x0C)
#define XHCI_PCI_SUBCLASS (0x03)
#define XHCI_PCI_INTERFACE (0x30)

#define LOCAL_TRACE 1

void* xhci_malloc(xhci_t* xhci, size_t size) {

    void* result = NULL;
    vmm_alloc_contiguous(vmm_get_kernel_aspace(), "usb xhci", size, &result,
                              0, VMM_FLAG_COMMIT,
                              ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE |
                              ARCH_MMU_FLAG_UNCACHED_DEVICE);

    return result;
}

void* xhci_memalign(xhci_t* xhci, size_t alignment, size_t size) {
    uint8_t align_log2 = 0;
    while (alignment) {
        alignment >>= 1;
        align_log2++;
    }

    void* result = NULL;
    vmm_alloc_contiguous(vmm_get_kernel_aspace(), "usb xhci", size, &result,
                              align_log2, VMM_FLAG_COMMIT,
                              ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE |
                              ARCH_MMU_FLAG_UNCACHED_DEVICE);

    return result;
}

void xhci_free(xhci_t* xhci, void* addr) {
    vmm_free_region(vmm_get_kernel_aspace(), (vaddr_t)addr);
}

mx_paddr_t xhci_virt_to_phys(xhci_t* xhci, mx_vaddr_t addr) {
    return vaddr_to_paddr((void *)addr);
}

mx_vaddr_t xhci_phys_to_virt(xhci_t* xhci, mx_paddr_t addr) {
    return (mx_vaddr_t)paddr_to_kvaddr(addr);
}

mx_status_t xhci_add_device(xhci_t* xhci, int slot_id, int hub_address, int speed) {
    return NO_ERROR;
}

void xhci_remove_device(xhci_t* xhci, int slot_id) {
}

void xhci_process_deferred_txns(xhci_t* xhci, xhci_transfer_ring_t* ring, bool closed) {
}

void xhci_rh_port_changed(xhci_t* xhci, xhci_root_hub_t* rh, int port_index) {
}

void xhci_start_device_thread(xhci_t* xhci) {
    thread_detach_and_resume(thread_create("xhci_device_thread", xhci_device_thread, xhci, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
}

typedef struct {
    xhci_t xhci;
} usb_xhci_device_t;


static void* usb_xhci_pci_probe(struct pcie_device_state* pci_device) {
    DEBUG_ASSERT(pci_device);

    if ((pci_device->class_id != XHCI_PCI_CLASS) ||
        (pci_device->subclass != XHCI_PCI_SUBCLASS) ||
        (pci_device->prog_if != XHCI_PCI_INTERFACE)) {
        return NULL;
    }

    /* Allocate our device state */
    usb_xhci_device_t* dev = (usb_xhci_device_t*)calloc(1, sizeof(usb_xhci_device_t));
    if (!dev) {
        LTRACEF("Failed to allocate %zu bytes for Intel HDA device\n", sizeof(usb_xhci_device_t));
        return NULL;
    }

    return dev;
}

static status_t usb_xhci_pci_startup(struct pcie_device_state* pci_device) {
    LTRACE_ENTRY;

    usb_xhci_device_t* dev = (usb_xhci_device_t*)pci_device->driver_ctx;

    LTRACEF("Starting %s @ %02x:%02x.%01x\n",
            pcie_driver_name(pci_device->driver),
            pci_device->bus_id,
            pci_device->dev_id,
            pci_device->func_id);

    /* Fetch the information about where our registers have been mapped for us,
     * then sanity check. */
    const pcie_bar_info_t* info = pcie_get_bar_info(pci_device, 0);
    if (!info || !info->is_allocated || !info->is_mmio) {
        TRACEF("Failed to fetch base address register info!\n");
        return ERR_BAD_STATE;
    }

    /* Map in the device registers */
    vmm_aspace_t *aspace = vmm_get_kernel_aspace();
    void* mmio;
    DEBUG_ASSERT(aspace);
    mx_status_t status = vmm_alloc_physical(aspace, "usb-xhci", info->size, &mmio, PAGE_SIZE_SHIFT,
                                            info->bus_addr, 0,
                                            ARCH_MMU_FLAG_UNCACHED_DEVICE | ARCH_MMU_FLAG_PERM_READ
                                            | ARCH_MMU_FLAG_PERM_WRITE);
    if (status != NO_ERROR) {
        return status;
    }

    pcie_enable_mmio(pci_device, true);
    pcie_enable_bus_master(pci_device, true);

    status = xhci_init(&dev->xhci, mmio);
    if (status != NO_ERROR) {
    TRACEF("xhci_init FAIL\n");
        return status;
    }
    TRACEF("xhci_init SUCCESS\n");

    xhci_start(&dev->xhci);
    return NO_ERROR;
}

static void usb_xhci_pci_shutdown(struct pcie_device_state* pci_device) {

}

void usb_xhci_pci_release(void* ctx) {
    usb_xhci_device_t* dev = (usb_xhci_device_t*)ctx;

    free(dev);
}

static const pcie_driver_fn_table_t USB_XHCI_FN_TABLE = {
    .pcie_probe_fn       = usb_xhci_pci_probe,
    .pcie_startup_fn     = usb_xhci_pci_startup,
    .pcie_shutdown_fn    = usb_xhci_pci_shutdown,
    .pcie_release_fn     = usb_xhci_pci_release,
};

STATIC_PCIE_DRIVER(usb_xhci, "USB XHCI", USB_XHCI_FN_TABLE)
