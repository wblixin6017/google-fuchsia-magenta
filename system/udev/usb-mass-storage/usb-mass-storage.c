// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/common/usb.h>
#include <magenta/assert.h>
#include <magenta/hw/usb.h>
#include <magenta/listnode.h>
#include <sync/completion.h>
#include <ddk/protocol/block.h>

#include <endian.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include "ums-hw.h"

// comment the next line if you don't want debug messages
#define DEBUG 0
#ifdef DEBUG
# define DEBUG_PRINT(x) printf x
#else
# define DEBUG_PRINT(x) do {} while (0)
#endif

// used to implement IOCTL_DEVICE_SYNC
typedef struct {
    // iotxn we are waiting to complete
    iotxn_t* iotxn;
    // completion for IOCTL_DEVICE_SYNC to wait on
    completion_t completion;
    // node for ums_t.sync_nodes list
    list_node_t node;
} ums_sync_node_t;

typedef struct {
    mx_device_t device;         // root device we publish
    mx_device_t* usb_device;    // USB device we are bound to
    mx_driver_t* driver;

    struct ums_block_dev* block_devs;

    uint32_t tag_send;          // next tag to send in CBW
    uint32_t tag_receive;       // next tag we expect to receive in CSW

    uint8_t max_lun;            // index of last logical unit
    size_t max_transfer;        // maximum transfer size reported by usb_get_max_transfer_size()

    uint8_t bulk_in_addr;
    uint8_t bulk_out_addr;
    size_t bulk_in_max_packet;
    size_t bulk_out_max_packet;

    // FIXME (voydanoff) We only need small buffers for CBW, CSW and SCSI commands
    // Once we have the new iotxn API we can allocate these iotxns inline
    // and use a single VMO for their buffers
    iotxn_t* cbw_iotxn;
    iotxn_t* data_iotxn;
    iotxn_t* csw_iotxn;

    thrd_t worker_thread;
    bool dead;

    // list of queued io transactions
    list_node_t queued_iotxns;
    // used to signal ums_worker_thread when new iotxns are ready for processing
    // and when device is dead
    completion_t iotxn_completion;
    // protects queued_iotxns, iotxn_completion and dead
    mtx_t iotxn_lock;

    // list of active ums_sync_node_t
    list_node_t sync_nodes;
    // current iotxn being processed (needed for IOCTL_DEVICE_SYNC)
    iotxn_t* curr_txn;
} ums_t;
#define get_ums(dev) containerof(dev, ums_t, device)

// block device for a logical unit
typedef struct ums_block_dev {
    // block device we publish
    mx_device_t device;
    ums_t* ums;
    block_callbacks_t* cb;

    uint64_t total_blocks;
    uint32_t block_size;

    uint8_t lun;
    bool use_read_write_16; // use READ16 and WRITE16 if total_blocks > 0xFFFFFFFF
    bool device_added;
} ums_block_dev_t;
#define get_block_dev(dev) containerof(dev, ums_block_dev_t, device)

// extra data for clone txns
typedef struct {
    ums_t*      ums;
    mx_off_t    offset;
    size_t      total_length;
    size_t      max_packet;
} ums_txn_extra_t;
static_assert(sizeof(ums_txn_extra_t) <= sizeof (iotxn_extra_data_t), "");

static csw_status_t ums_verify_csw(ums_t* ums, iotxn_t* csw_request, uint32_t* out_residue);

static mx_status_t constrain_args(ums_block_dev_t* dev, mx_off_t offset, mx_off_t length) {
    if (offset % dev->block_size) {
        return ERR_INVALID_ARGS;
    }
    if (length % dev->block_size) {
        return ERR_INVALID_ARGS;
    }
    return NO_ERROR;
}

static mx_status_t ums_reset(ums_t* ums) {
    // for all these control requests, data is null, length is 0 because nothing is passed back
    // value and index not used for first command, though index is supposed to be set to interface number
    // TODO: check interface number, see if index needs to be set
    DEBUG_PRINT(("UMS: performing reset recovery\n"));
    mx_status_t status = usb_control(ums->usb_device, USB_DIR_OUT | USB_TYPE_CLASS
                                            | USB_RECIP_INTERFACE, USB_REQ_RESET, 0x00, 0x00, NULL, 0);
    status = usb_control(ums->usb_device, USB_DIR_OUT | USB_TYPE_CLASS
                                           | USB_RECIP_INTERFACE, USB_REQ_CLEAR_FEATURE, FS_ENDPOINT_HALT,
                                           ums->bulk_in_addr, NULL, 0);
    status = usb_control(ums->usb_device, USB_DIR_OUT | USB_TYPE_CLASS
                                           | USB_RECIP_INTERFACE, USB_REQ_CLEAR_FEATURE, FS_ENDPOINT_HALT,
                                           ums->bulk_out_addr, NULL, 0);
    return status;
}

static void ums_queue_request(ums_t* ums, iotxn_t* txn) {
    iotxn_queue(ums->usb_device, txn);
}

static void ums_txn_complete(iotxn_t* txn, void* cookie) {
    if (cookie) {
        completion_signal((completion_t *)cookie);
    }
}

static void ums_send_cbw(ums_t* ums, uint8_t lun, uint32_t transfer_length, uint8_t flags,
                         uint8_t command_len, void* command) {
    iotxn_t* txn = ums->cbw_iotxn;

    ums_cbw_t* cbw;
    iotxn_mmap(txn, (void **)&cbw);

    memset(cbw, 0, sizeof(*cbw));
    cbw->dCBWSignature = htole32(CBW_SIGNATURE);
    cbw->dCBWTag = htole32(ums->tag_send++);
    cbw->dCBWDataTransferLength = htole32(transfer_length);
    cbw->bmCBWFlags = flags;
    cbw->bCBWLUN = lun;
    cbw->bCBWCBLength = command_len;

    // copy command_len bytes from the command passed in into the command_len
    memcpy(cbw->CBWCB, command, command_len);
    txn->cookie = NULL;
    ums_queue_request(ums, txn);
}

static mx_status_t ums_read_csw(ums_t* ums, uint32_t* out_residue) {
    completion_t completion = COMPLETION_INIT;
    iotxn_t* csw_request = ums->csw_iotxn;
    csw_request->cookie = &completion;
    ums_queue_request(ums, csw_request);
    completion_wait(&completion, MX_TIME_INFINITE);

    csw_status_t csw_error = ums_verify_csw(ums, csw_request, out_residue);

    if (csw_error == CSW_SUCCESS) {
        return NO_ERROR;
    } else if (csw_error == CSW_FAILED) {
        return ERR_BAD_STATE;
    } else {
        // FIXME - best way to handle this?
        // print error and then reset device due to it
        DEBUG_PRINT(("UMS: CSW verify returned error. Check ums-hw.h csw_status_t for enum = %d\n", csw_error));
        ums_reset(ums);
        return ERR_INTERNAL;
    }
}

static csw_status_t ums_verify_csw(ums_t* ums, iotxn_t* csw_request, uint32_t* out_residue) {
    ums_csw_t csw;
    iotxn_copyfrom(csw_request, &csw, sizeof(csw), 0);

    // check signature is "USBS"
    if (letoh32(csw.dCSWSignature) != CSW_SIGNATURE) {
        DEBUG_PRINT(("UMS:invalid csw sig: %08x \n", letoh32(csw.dCSWSignature)));
        return CSW_INVALID;
    }
    // check if tag matches the tag of last CBW
    if (letoh32(csw.dCSWTag) != ums->tag_receive++) {
        DEBUG_PRINT(("UMS:csw tag mismatch, expected:%08x got in csw:%08x \n", ums->tag_receive - 1,
                    letoh32(csw.dCSWTag)));
        return CSW_TAG_MISMATCH;
    }
    // check if success is true or not?
    if (csw.bmCSWStatus == CSW_FAILED) {
        return CSW_FAILED;
    } else if (csw.bmCSWStatus == CSW_PHASE_ERROR) {
        return CSW_PHASE_ERROR;
    }

    if (out_residue) {
        *out_residue = letoh32(csw.dCSWDataResidue);
    }
    return CSW_SUCCESS;
}

static void ums_queue_read(ums_t* ums, uint16_t transfer_length) {
    // read request sense response
    iotxn_t* read_request = ums->data_iotxn;
    read_request->length = transfer_length;
    read_request->cookie = NULL;
    ums_queue_request(ums, read_request);
}

static mx_status_t ums_inquiry(ums_t* ums, uint8_t* out_data) {
    // CBW Configuration
    scsi_command6_t command;
    memset(&command, 0, sizeof(command));
    command.opcode = UMS_INQUIRY;
    command.length = UMS_INQUIRY_TRANSFER_LENGTH;
    ums_send_cbw(ums, 0, UMS_INQUIRY_TRANSFER_LENGTH, USB_DIR_IN, sizeof(command), &command);

    // read inquiry response
    ums_queue_read(ums, UMS_INQUIRY_TRANSFER_LENGTH);

    // wait for CSW
    mx_status_t status = ums_read_csw(ums, NULL);
    if (status == NO_ERROR) {
        iotxn_copyfrom(ums->data_iotxn, out_data, UMS_INQUIRY_TRANSFER_LENGTH, 0);
    }
    return status;
}

static mx_status_t ums_test_unit_ready(ums_t* ums, uint8_t lun) {
    // CBW Configuration
    scsi_command6_t command;
    memset(&command, 0, sizeof(command));
    command.opcode = UMS_TEST_UNIT_READY;
    ums_send_cbw(ums, lun, 0, USB_DIR_IN, sizeof(command), &command);

    // wait for CSW
    return ums_read_csw(ums, NULL);
}

static mx_status_t ums_request_sense(ums_t* ums, uint8_t lun, uint8_t* out_data) {
    // CBW Configuration
    scsi_command6_t command;
    memset(&command, 0, sizeof(command));
    command.opcode = UMS_REQUEST_SENSE;
    command.length = UMS_REQUEST_SENSE_TRANSFER_LENGTH;
    ums_send_cbw(ums, lun, UMS_REQUEST_SENSE_TRANSFER_LENGTH, USB_DIR_IN, sizeof(command), &command);

    // read request sense response
    ums_queue_read(ums, UMS_REQUEST_SENSE_TRANSFER_LENGTH);

    // wait for CSW
    mx_status_t status = ums_read_csw(ums, NULL);
    if (status == NO_ERROR) {
        iotxn_copyfrom(ums->data_iotxn, out_data, UMS_REQUEST_SENSE_TRANSFER_LENGTH, 0);
    }
    return status;
}

static mx_status_t ums_read_capacity10(ums_t* ums, uint8_t lun, scsi_read_capacity_10_t* out_data) {
    // CBW Configuration
    scsi_command10_t command;
    memset(&command, 0, sizeof(command));
    command.opcode = UMS_READ_CAPACITY10;
    ums_send_cbw(ums, lun, sizeof(*out_data), USB_DIR_IN, sizeof(command), &command);

    // read capacity10 response
    ums_queue_read(ums, sizeof(*out_data));

    mx_status_t status = ums_read_csw(ums, NULL);
    if (status == NO_ERROR) {
        iotxn_copyfrom(ums->data_iotxn, out_data, sizeof(*out_data), 0);
    }
    return status;
}

static mx_status_t ums_read_capacity16(ums_t* ums, uint8_t lun, scsi_read_capacity_16_t* out_data) {
    // CBW Configuration
    scsi_command16_t command;
    memset(&command, 0, sizeof(command));
    command.opcode = UMS_READ_CAPACITY16;
    // service action = 10, not sure what that means
    command.misc = 0x10;
    command.length = sizeof(*out_data);
    ums_send_cbw(ums, lun, sizeof(*out_data), USB_DIR_IN, sizeof(command), &command);

    // read capacity16 response
    ums_queue_read(ums, sizeof(*out_data));

    mx_status_t status = ums_read_csw(ums, NULL);
    if (status == NO_ERROR) {
        iotxn_copyfrom(ums->data_iotxn, out_data, sizeof(*out_data), 0);
    }
    return status;
}

static void clone_complete(iotxn_t* clone, void* cookie) {
    ums_txn_extra_t* extra = (ums_txn_extra_t *)&clone->extra;
    ums_t* ums = extra->ums;

    if (clone->status == NO_ERROR) {
        extra->offset += clone->actual;
        // Queue another read if we haven't read full length and did not receive a short packet
        if (extra->offset < extra->total_length && clone->actual != 0 &&
                (clone->actual % extra->max_packet) == 0) {
            size_t length = extra->total_length - extra->offset;
            if (length > ums->max_transfer) {
                length = ums->max_transfer;
            }
            clone->length = length;
            clone->vmo_offset += clone->actual;
            ums_queue_request(ums, clone);
            return;
        }
    }

    // transfer is done if we get here
    completion_signal((completion_t *)cookie);
}

static void ums_queue_data_transfer(ums_t* ums, iotxn_t* txn, uint8_t ep_address, size_t max_packet) {
    iotxn_t* clone = NULL;
    mx_status_t status = iotxn_clone(txn, &clone);
    if (status != NO_ERROR) {
        iotxn_complete(txn, status, 0);
        return;
    }

    clone->complete_cb = clone_complete;

    ums_txn_extra_t* extra = (ums_txn_extra_t *)&clone->extra;
    extra->ums = ums;
    extra->offset = 0;
    extra->total_length = txn->length;
    extra->max_packet = max_packet;

    if (clone->length > ums->max_transfer) {
        clone->length = ums->max_transfer;
    }

    usb_protocol_data_t* pdata = iotxn_pdata(clone, usb_protocol_data_t);
    memset(pdata, 0, sizeof(*pdata));
    pdata->ep_address = ep_address;

    completion_t completion = COMPLETION_INIT;
    clone->cookie = &completion;
    ums_queue_request(ums, clone);
    completion_wait(&completion, MX_TIME_INFINITE);

    txn->status = clone->status;
    txn->actual = (txn->status == NO_ERROR ? extra->offset : 0);

    iotxn_release(clone);
}

static mx_status_t ums_read(ums_block_dev_t* dev, iotxn_t* txn) {
    if (txn->length > UINT32_MAX) return ERR_INVALID_ARGS;

    uint64_t lba = txn->offset / dev->block_size;
    if (lba > dev->total_blocks) {
        return ERR_OUT_OF_RANGE;
    }
    uint32_t num_blocks = txn->length / dev->block_size;
    if (lba + num_blocks >= dev->total_blocks) {
        num_blocks = dev->total_blocks - lba;
        if (num_blocks == 0) {
            return 0;
        }
    }
    uint32_t transfer_length = num_blocks * dev->block_size;
    ums_t* ums = dev->ums;

    // CBW Configuration

    if (dev->use_read_write_16) {
        scsi_command16_t command;
        memset(&command, 0, sizeof(command));
        command.opcode = UMS_READ16;
        command.lba = htobe64(lba);
        command.length = htobe32(num_blocks);

        ums_send_cbw(ums, dev->lun, transfer_length, USB_DIR_IN, sizeof(command), &command);
    } else if (num_blocks <= UINT16_MAX) {
        scsi_command10_t command;
        memset(&command, 0, sizeof(command));
        command.opcode = UMS_READ10;
        command.lba = htobe32(lba);
        command.length_hi = num_blocks >> 8;
        command.length_lo = num_blocks & 0xFF;
        ums_send_cbw(ums, dev->lun, transfer_length, USB_DIR_IN, sizeof(command), &command);
    } else {
        scsi_command12_t command;
        memset(&command, 0, sizeof(command));
        command.opcode = UMS_READ12;
        command.lba = htobe32(lba);
        command.length = htobe32(num_blocks);
        ums_send_cbw(ums, dev->lun, transfer_length, USB_DIR_IN, sizeof(command), &command);
    }

    ums_queue_data_transfer(ums, txn, ums->bulk_in_addr, ums->bulk_in_max_packet);

    uint32_t residue;
    mx_status_t status = ums_read_csw(ums, &residue);
    if (status == NO_ERROR) {
        status = txn->actual - residue;
    }

    return status;
}

static mx_status_t ums_write(ums_block_dev_t* dev, iotxn_t* txn) {
    if (txn->length > UINT32_MAX) return ERR_INVALID_ARGS;

    uint64_t lba = txn->offset / dev->block_size;
    if (lba > dev->total_blocks) {
        return ERR_OUT_OF_RANGE;
    }
    uint32_t num_blocks = txn->length / dev->block_size;
    if (lba + num_blocks >= dev->total_blocks) {
        num_blocks = dev->total_blocks - lba;
        if (num_blocks == 0) {
            return 0;
        }
    }
    uint32_t transfer_length = num_blocks * dev->block_size;
    ums_t* ums = dev->ums;

    if (dev->use_read_write_16) {
        scsi_command16_t command;
        memset(&command, 0, sizeof(command));
        command.opcode = UMS_WRITE16;
        command.lba = htobe64(lba);
        command.length = htobe32(num_blocks);
        ums_send_cbw(ums, dev->lun, transfer_length, USB_DIR_OUT, sizeof(command), &command);
    } else if (num_blocks <= UINT16_MAX) {
        scsi_command10_t command;
        memset(&command, 0, sizeof(command));
        command.opcode = UMS_WRITE10;
        command.lba = htobe32(lba);
        command.length_hi = num_blocks >> 8;
        command.length_lo = num_blocks & 0xFF;
        ums_send_cbw(ums, dev->lun, transfer_length, USB_DIR_OUT, sizeof(command), &command);
    } else {
        scsi_command12_t command;
        memset(&command, 0, sizeof(command));
        command.opcode = UMS_WRITE12;
        command.lba = htobe32(lba);
        command.length = htobe32(num_blocks);
        ums_send_cbw(ums, dev->lun, transfer_length, USB_DIR_OUT, sizeof(command), &command);
    }

    ums_queue_data_transfer(ums, txn, ums->bulk_out_addr, ums->bulk_out_max_packet);

    // receive CSW
    uint32_t residue;
    mx_status_t status = ums_read_csw(ums, &residue);
    if (status == NO_ERROR) {
        status = transfer_length - residue;
    }

    return status;
}

static void ums_unbind(mx_device_t* device) {
    ums_t* ums = get_ums(device);
    device_remove(&ums->device);
}

static mx_status_t ums_release(mx_device_t* device) {
    ums_t* ums = get_ums(device);

    // terminate our worker thread
    mtx_lock(&ums->iotxn_lock);
    ums->dead = true;
    mtx_unlock(&ums->iotxn_lock);
    completion_signal(&ums->iotxn_completion);
// FIXME only call if thread is started
// maybe do this in unbind?
    thrd_join(ums->worker_thread, NULL);

    if (ums->cbw_iotxn) {
        iotxn_release(ums->cbw_iotxn);
    }
    if (ums->data_iotxn) {
        iotxn_release(ums->data_iotxn);
    }
    if (ums->csw_iotxn) {
        iotxn_release(ums->csw_iotxn);
    }
    if (ums->block_devs) {
        free(ums->block_devs);
    }

    free(ums);
    return NO_ERROR;
}

static void ums_block_dev_queue(ums_block_dev_t* dev, iotxn_t* txn) {
    ums_t* ums = dev->ums;

    mx_status_t status = constrain_args(dev, txn->offset, txn->length);
    if (status != NO_ERROR) {
        iotxn_complete(txn, status, 0);
        return;
    }

    txn->context = dev;
    mtx_lock(&ums->iotxn_lock);
    list_add_tail(&ums->queued_iotxns, &txn->node);
    mtx_unlock(&ums->iotxn_lock);
    completion_signal(&ums->iotxn_completion);
}

static void ums_iotxn_queue(mx_device_t* device, iotxn_t* txn) {
    ums_block_dev_t* dev = get_block_dev(device);
    ums_block_dev_queue(dev, txn);
}

static ssize_t ums_ioctl(mx_device_t* device, uint32_t op, const void* cmd, size_t cmdlen, void* reply, size_t max) {
    ums_block_dev_t* dev = get_block_dev(device);

    // TODO implement other block ioctls
    switch (op) {
    case IOCTL_BLOCK_GET_SIZE: {
        uint64_t* size = reply;
        if (max < sizeof(*size)) return ERR_BUFFER_TOO_SMALL;
        *size = dev->total_blocks * dev->block_size;
        return sizeof(*size);
    }
    case IOCTL_BLOCK_GET_BLOCKSIZE: {
         uint64_t* blksize = reply;
         if (max < sizeof(*blksize)) return ERR_BUFFER_TOO_SMALL;
         *blksize = dev->block_size;
         return sizeof(*blksize);
    }
    case IOCTL_DEVICE_SYNC: {
        ums_sync_node_t node;

        ums_t* ums = dev->ums;
        mtx_lock(&ums->iotxn_lock);
        iotxn_t* txn = list_peek_tail_type(&ums->queued_iotxns, iotxn_t, node);
        if (!txn) {
            txn = ums->curr_txn;
        }
        if (!txn) {
            mtx_unlock(&ums->iotxn_lock);
            return NO_ERROR;
        }
        // queue a stack allocated sync node on ums_t.sync_nodes
        node.iotxn = txn;
        completion_reset(&node.completion);
        list_add_head(&ums->sync_nodes, &node.node);
        mtx_unlock(&ums->iotxn_lock);

        return completion_wait(&node.completion, MX_TIME_INFINITE);
    }
    default:
        return ERR_NOT_SUPPORTED;
    }
}

static mx_off_t ums_get_size(mx_device_t* device) {
    ums_block_dev_t* dev = get_block_dev(device);
    return dev->block_size * dev->total_blocks;
}

static mx_protocol_device_t ums_block_dev_proto = {
    .ioctl = ums_ioctl,
//    .unbind = ums_unbind,
//    .release = ums_release,
    .iotxn_queue = ums_iotxn_queue,
    .get_size = ums_get_size,
};

static void ums_async_set_callbacks(mx_device_t* device, block_callbacks_t* cb) {
    ums_block_dev_t* dev = get_block_dev(device);
    dev->cb = cb;
}

static void ums_async_complete(iotxn_t* txn, void* cookie) {
    ums_block_dev_t* dev = (ums_block_dev_t*)txn->extra[0];
    dev->cb->complete(cookie, txn->status);
    iotxn_release(txn);
}

static void ums_async_read(mx_device_t* device, mx_handle_t vmo, uint64_t length,
                           uint64_t vmo_offset, uint64_t dev_offset, void* cookie) {
    ums_block_dev_t* dev = get_block_dev(device);
    iotxn_t* txn;
    mx_status_t status = iotxn_alloc_vmo(&txn, IOTXN_ALLOC_POOL, vmo, vmo_offset, length);
    if (status != NO_ERROR) {
        dev->cb->complete(cookie, status);
        return;
    }
    txn->opcode = IOTXN_OP_READ;
    txn->offset = dev_offset;
    txn->complete_cb = ums_async_complete;
    txn->cookie = cookie;
    txn->extra[0] = (uintptr_t)dev;
    ums_block_dev_queue(dev, txn);
}

static void ums_async_write(mx_device_t* device, mx_handle_t vmo, uint64_t length,
                            uint64_t vmo_offset, uint64_t dev_offset, void* cookie) {
    ums_block_dev_t* dev = get_block_dev(device);
    iotxn_t* txn;
    mx_status_t status = iotxn_alloc_vmo(&txn, IOTXN_ALLOC_POOL, vmo, vmo_offset, length);
    if (status != NO_ERROR) {
        dev->cb->complete(cookie, status);
        return;
    }
    txn->opcode = IOTXN_OP_WRITE;
    txn->offset = dev_offset;
    txn->complete_cb = ums_async_complete;
    txn->cookie = cookie;
    txn->extra[0] = (uintptr_t)dev;
    ums_block_dev_queue(dev, txn);
}

static block_ops_t ums_block_ops = {
    .set_callbacks = ums_async_set_callbacks,
    .read = ums_async_read,
    .write = ums_async_write,
};

static mx_status_t ums_add_block_device(ums_block_dev_t* dev) {
    MX_DEBUG_ASSERT(!dev->device_added);
    ums_t* ums = dev->ums;
    uint8_t lun = dev->lun;

    scsi_read_capacity_10_t data;
    mx_status_t status = ums_read_capacity10(ums, lun, &data);
    if (status < 0) {
        printf("read_capacity10 failed: %d\n", status);
        return status;
    }

    dev->total_blocks = betoh32(data.lba);
    dev->block_size = betoh32(data.block_length);

    if (dev->total_blocks == 0xFFFFFFFF) {
        scsi_read_capacity_16_t data;
        status = ums_read_capacity16(ums, lun, &data);
        if (status < 0) {
            printf("read_capacity16 failed: %d\n", status);
            return status;
        }

        dev->total_blocks = betoh64(data.lba);
        dev->block_size = betoh32(data.block_length);
    }
    if (dev->block_size == 0) {
        printf("UMS zero block size\n");
        return ERR_INVALID_ARGS;
    }

    // +1 because this returns the address of the final block, and blocks are zero indexed
    dev->total_blocks++;

    // Need to use READ16/WRITE16 if block addresses are greater than 32 bit
    dev->use_read_write_16 = dev->total_blocks > UINT32_MAX;

    DEBUG_PRINT(("UMS:block size is: 0x%08x\n", dev->block_size));
    DEBUG_PRINT(("UMS:total blocks is: %" PRId64 "\n", dev->total_blocks));
    DEBUG_PRINT(("UMS:total size is: %" PRId64 "\n", dev->total_blocks * dev->block_size));

    char name[16];
    snprintf(name, sizeof(name), "ums-lun-%02d", lun);

    device_init(&dev->device, ums->driver, name, &ums_block_dev_proto);
    dev->device.protocol_id = MX_PROTOCOL_BLOCK_CORE;
    dev->device.protocol_ops = &ums_block_ops;
    dev->cb = NULL;

    status = device_add(&dev->device, &ums->device);
    if (status == NO_ERROR) {
        dev->device_added = true;
    } else {
        printf("UMS: device_add for block device failed %d\n", status);
    }
    return status;
}

static mx_status_t ums_check_ready(ums_t* ums) {
    mx_status_t status = NO_ERROR;

    for (uint8_t lun = 0; lun <= ums->max_lun && status == NO_ERROR; lun++) {
        ums_block_dev_t* dev = &ums->block_devs[lun];
        bool ready;
        
        status = ums_test_unit_ready(ums, lun);
        if (status == NO_ERROR) {
            ready = true;
        } if (status == ERR_BAD_STATE) {
            ready = false;
            // command returned CSW_FAILED. device is there but media is not ready.
            uint8_t request_sense_data[UMS_REQUEST_SENSE_TRANSFER_LENGTH];
            status = ums_request_sense(ums, lun, request_sense_data);
        }
        if (status != NO_ERROR) {
            break;
        }

        if (ready && !dev->device_added) {
            ums_add_block_device(dev);
        } else if (!ready && dev->device_added) {
//            ums_remove_block_device(dev);
        }
    }

    return status;
}


static mx_protocol_device_t ums_device_proto = {
    .unbind = ums_unbind,
    .release = ums_release,
};

static int ums_worker_thread(void* arg) {
    ums_t* ums = (ums_t*)arg;

    // we need to send the Inquiry command first,
    // but currently we do not do anything with the response
    uint8_t inquiry_data[UMS_INQUIRY_TRANSFER_LENGTH];
    mx_status_t status = ums_inquiry(ums, inquiry_data);
    if (status < 0) {
        printf("ums_inquiry failed: %d\n", status);
        goto fail;
    }

    // Add root device, which will contain block devices for logical units
    device_init(&ums->device, ums->driver, "ums", &ums_device_proto);
    device_set_bindable(&ums->device, false);
    if (device_add(&ums->device, ums->usb_device) != NO_ERROR) {
        printf("ums device_add failed: %d\n", status);
        goto fail;
    }

    bool wait = true;
    while (1) {
        if (wait) {
            status = completion_wait(&ums->iotxn_completion, MX_SEC(1));
            if (status == ERR_TIMED_OUT) {
                if (ums_check_ready(ums) != NO_ERROR) {
                    goto fail;
                }
                continue;
            } 
            completion_reset(&ums->iotxn_completion);
        }

        mtx_lock(&ums->iotxn_lock);
        if (ums->dead) {
            mtx_unlock(&ums->iotxn_lock);
            return 0;
        }
        iotxn_t* txn = list_remove_head_type(&ums->queued_iotxns, iotxn_t, node);
        if (txn == NULL) {
            mtx_unlock(&ums->iotxn_lock);
            wait = true;
            continue;
        }
        ums->curr_txn = txn;
        mtx_unlock(&ums->iotxn_lock);

        ums_block_dev_t* dev = txn->context;

        mx_status_t status;
        if (txn->opcode == IOTXN_OP_READ) {
            status = ums_read(dev, txn);
        }else if (txn->opcode == IOTXN_OP_WRITE) {
            status = ums_write(dev, txn);
        } else {
            status = ERR_INVALID_ARGS;
        }

        mtx_lock(&ums->iotxn_lock);
        // unblock calls to IOCTL_DEVICE_SYNC that are waiting for curr_txn to complete
        ums_sync_node_t* sync_node;
        ums_sync_node_t* temp;
        list_for_every_entry_safe(&ums->sync_nodes, sync_node, temp, ums_sync_node_t, node) {
            if (sync_node->iotxn == txn) {
                list_delete(&sync_node->node);
                completion_signal(&sync_node->completion);
            }
        }
        ums->curr_txn = NULL;
        // make sure we have processed all queued transactions before waiting again
        wait = list_is_empty(&ums->queued_iotxns);
        mtx_unlock(&ums->iotxn_lock);

        if (status >= 0) {
            iotxn_complete(txn, NO_ERROR, status);
        } else {
            iotxn_complete(txn, status, 0);
        }
    }
    return NO_ERROR;

fail:
    printf("ums_worker_thread failed\n");
/*
                        // complete any pending txns
                        list_node_t txns = LIST_INITIAL_VALUE(txns);
                        mtx_lock(&ums->iotxn_lock);
                        list_move(&ums->queued_iotxns, &txns);
                        mtx_unlock(&ums->iotxn_lock);



    if (block_device_added) {
        device_remove(&ums->device);
    }
*/
    ums_release(&ums->device);
    return status;
}

static mx_status_t ums_bind(mx_driver_t* driver, mx_device_t* device, void** cookie) {
    // find our endpoints
    usb_desc_iter_t iter;
    mx_status_t result = usb_desc_iter_init(device, &iter);
    if (result < 0) return result;

    usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, true);
    if (!intf) {
        usb_desc_iter_release(&iter);
        return ERR_NOT_SUPPORTED;
    }
    if (intf->bNumEndpoints < 2) {
        DEBUG_PRINT(("UMS:ums_bind wrong number of endpoints: %d\n", intf->bNumEndpoints));
        usb_desc_iter_release(&iter);
        return ERR_NOT_SUPPORTED;
    }

    uint8_t bulk_in_addr = 0;
    uint8_t bulk_out_addr = 0;
    size_t bulk_in_max_packet = 0;
    size_t bulk_out_max_packet = 0;

   usb_endpoint_descriptor_t* endp = usb_desc_iter_next_endpoint(&iter);
    while (endp) {
        if (usb_ep_direction(endp) == USB_ENDPOINT_OUT) {
            if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
                bulk_out_addr = endp->bEndpointAddress;
                bulk_out_max_packet = usb_ep_max_packet(endp);
            }
        } else {
            if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
                bulk_in_addr = endp->bEndpointAddress;
                bulk_in_max_packet = usb_ep_max_packet(endp);
            }
        }
        endp = usb_desc_iter_next_endpoint(&iter);
    }
    usb_desc_iter_release(&iter);

    if (!bulk_in_addr || !bulk_out_addr) {
        DEBUG_PRINT(("UMS:ums_bind could not find endpoints\n"));
        return ERR_NOT_SUPPORTED;
    }

    ums_t* ums = calloc(1, sizeof(ums_t));
    if (!ums) {
        DEBUG_PRINT(("UMS:Not enough memory for ums_t\n"));
        return ERR_NO_MEMORY;
    }

    mx_status_t status = usb_control(device, USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                                     USB_REQ_GET_MAX_LUN, 0x00, 0x00, &ums->max_lun, 1);
    if (status != 1) {
        goto fail;
    }

    DEBUG_PRINT(("UMS:Max lun is: %u\n", ums->max_lun));

    ums->block_devs = calloc(ums->max_lun + 1, sizeof(ums_block_dev_t));
    if (!ums->block_devs) {
        DEBUG_PRINT(("UMS:Not enough memory for block_devs\n"));
        status = ERR_NO_MEMORY;
        goto fail;
    }
    for (uint8_t lun = 0; lun <= ums->max_lun; lun++) {
        ums_block_dev_t* dev = &ums->block_devs[lun];
        dev->ums = ums;
        dev->lun = lun;
    }

    list_initialize(&ums->queued_iotxns);
    list_initialize(&ums->sync_nodes);
    completion_reset(&ums->iotxn_completion);
    mtx_init(&ums->iotxn_lock, mtx_plain);

    ums->usb_device = device;
    ums->driver = driver;
    ums->bulk_in_addr = bulk_in_addr;
    ums->bulk_out_addr = bulk_out_addr;
    ums->bulk_in_max_packet = bulk_in_max_packet;
    ums->bulk_out_max_packet = bulk_out_max_packet;

    size_t max_in = usb_get_max_transfer_size(device, bulk_in_addr);
    size_t max_out = usb_get_max_transfer_size(device, bulk_out_addr);
    ums->max_transfer = (max_in < max_out ? max_in : max_out);

    ums->cbw_iotxn = usb_alloc_iotxn(bulk_out_addr, sizeof(ums_cbw_t));
    if (!ums->cbw_iotxn) {
        status = ERR_NO_MEMORY;
        goto fail;
    }
    ums->data_iotxn = usb_alloc_iotxn(bulk_in_addr, PAGE_SIZE);
    if (!ums->data_iotxn) {
        status = ERR_NO_MEMORY;
        goto fail;
    }
    ums->csw_iotxn = usb_alloc_iotxn(bulk_in_addr, sizeof(ums_csw_t));
    if (!ums->csw_iotxn) {
        status = ERR_NO_MEMORY;
        goto fail;
    }

    ums->cbw_iotxn->length = sizeof(ums_cbw_t);
    ums->csw_iotxn->length = sizeof(ums_csw_t);
    ums->cbw_iotxn->complete_cb = ums_txn_complete;
    ums->data_iotxn->complete_cb = ums_txn_complete;
    ums->csw_iotxn->complete_cb = ums_txn_complete;

    ums->tag_send = ums->tag_receive = 8;

    thrd_create_with_name(&ums->worker_thread, ums_worker_thread, ums, "ums_worker_thread");

    return status;

fail:
    printf("ums_bind failed: %d\n", status);
    ums_release(&ums->device);
    return status;
}

mx_driver_t _driver_usb_mass_storage = {
    .ops = {
        .bind = ums_bind,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_usb_mass_storage, "usb-mass-storage", "magenta", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_USB),
    BI_ABORT_IF(NE, BIND_USB_CLASS, USB_CLASS_MSC),
    BI_ABORT_IF(NE, BIND_USB_SUBCLASS, 6),      // SCSI transparent command set
    BI_MATCH_IF(EQ, BIND_USB_PROTOCOL, 0x50),   // bulk-only protocol
MAGENTA_DRIVER_END(_driver_usb_mass_storage)
