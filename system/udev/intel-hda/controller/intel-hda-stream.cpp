// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hw/arch_ops.h>
#include <intel-hda-driver-utils/debug-logging.h>
#include <intel-hda-driver-utils/utils.h>
#include <magenta/syscalls.h>
#include <mxtl/limits.h>
#include <string.h>

#include "intel-hda-stream.h"
#include "utils.h"

constexpr size_t IntelHDAStream::MAX_BDL_LENGTH;
constexpr size_t IntelHDAStream::MAX_STREAMS_PER_CONTROLLER;

namespace {
// Note: these timeouts are arbitrary; the spec provides no guidance here.
// That said, it is hard to imagine it taking more than a single audio
// frame's worth of time, so 10mSec should be more then generous enough.
static constexpr mx_time_t IHDA_SD_MAX_RESET_TIME_NSEC  = 10000000u;  // 10mSec
static constexpr mx_time_t IHDA_SD_RESET_POLL_TIME_NSEC = 100000u;    // 100uSec
constexpr uint32_t DMA_ALIGN = 128;
constexpr uint32_t DMA_ALIGN_MASK = DMA_ALIGN - 1;
}  // namespace

void IntelHDAStream::PrintDebugPrefix() const {
    printf("[IHDA_SD #%u] ", id_);
}

IntelHDAStream::IntelHDAStream(Type                    type,
                               uint16_t                id,
                               hda_stream_desc_regs_t* regs,
                               mx_paddr_t              bdl_phys,
                               uintptr_t               bdl_virt)
    : type_(type),
      id_(id),
      regs_(regs),
      bdl_(reinterpret_cast<IntelHDABDLEntry*>(bdl_virt)),
      bdl_phys_(bdl_phys) {
    // Check the alignment restrictions
    DEBUG_ASSERT(!(bdl_phys & static_cast<mx_paddr_t>(DMA_ALIGN_MASK)));
    DEBUG_ASSERT(!(bdl_virt & static_cast<uintptr_t>(DMA_ALIGN_MASK)));
}

IntelHDAStream::~IntelHDAStream() {
    DEBUG_ASSERT(!running_);
}

void IntelHDAStream::EnterReset() {
    // Enter the reset state  To do this, we...
    // 1) Clear the RUN bit.
    // 2) Set the SRST bit to 1.
    // 3) Poll until the hardware acks by setting the SRST bit to 1.
    REG_WR(&regs_->ctl_sts.w, 0u);
    hw_wmb();   // Do not let the CPU reorder this write.
    REG_WR(&regs_->ctl_sts.w, HDA_SD_REG_CTRL_SRST); // Set the reset bit.
    hw_rmb();   // Do not let the CPU snoop the write pipe; HW must set this bit before we proceed

    // Wait until the hardware acks the reset.
    mx_status_t res;
    res = WaitCondition(
            IHDA_SD_MAX_RESET_TIME_NSEC,
            IHDA_SD_RESET_POLL_TIME_NSEC,
            [](void* r) -> bool {
                auto regs = reinterpret_cast<hda_stream_desc_regs_t*>(r);
                auto val  = REG_RD(&regs->ctl_sts.w);
                return (val & HDA_SD_REG_CTRL_SRST) != 0;
            },
            regs_);

    if (res != NO_ERROR)
        LOG("Failed to place stream descriptor HW into reset! (res %d)\n", res);
}

void IntelHDAStream::ExitReset() {
    // Leave the reset state.  To do this, we...
    // 1) Set the SRST bit to 0.
    // 2) Poll until the hardware acks by setting the SRST bit back to 0.

    REG_WR(&regs_->ctl_sts.w, 0u);
    hw_rmb();   // Do not let the CPU snoop the write pipe; HW must clear this bit before we proceed

    // Wait until the hardware acks the release from reset.
    mx_status_t res;
    res = WaitCondition(
           IHDA_SD_MAX_RESET_TIME_NSEC,
           IHDA_SD_RESET_POLL_TIME_NSEC,
           [](void* r) -> bool {
               auto regs = reinterpret_cast<hda_stream_desc_regs_t*>(r);
               auto val  = REG_RD(&regs->ctl_sts.w);
               return (val & HDA_SD_REG_CTRL_SRST) == 0;
           },
           regs_);

    if (res != NO_ERROR)
        LOG("Failed to release stream descriptor HW from reset! (res %d)\n", res);
}

void IntelHDAStream::Configure(Type type, uint8_t tag) {
    if (type == Type::INVALID) {
        DEBUG_ASSERT(tag == 0);
        // Make certain that the stream DMA engine has been stopped and being
        // held in reset.
        EnterReset();
    } else {
        // Release the stream from reset.
        DEBUG_ASSERT(type != Type::BIDIR);
        DEBUG_ASSERT((tag != 0) && (tag < 16));
        ExitReset();
    }

    configured_type_ = type;
    tag_ = tag;
}

mx_status_t IntelHDAStream::SetStreamFormat(uint16_t encoded_fmt,
                                            const mxtl::RefPtr<DriverChannel>& channel) {
    if (channel == nullptr)
        return ERR_INVALID_ARGS;

    // We are being given a new format.  Reset any client connection we may have
    // and stop the hardware.
    Deactivate();

    // Set the stream tag and direction bit (in the case of a bi-directional channel)
    DEBUG_ASSERT((configured_type_ == Type::INPUT) || (configured_type_ == Type::OUTPUT));
    uint32_t val = HDA_SD_REG_CTRL_STRM_TAG(tag_)
                 | HDA_SD_REG_CTRL_STRIPE1
                 | (configured_type_ == Type::INPUT ? HDA_SD_REG_CTRL_DIR_IN
                                                    : HDA_SD_REG_CTRL_DIR_OUT);
    REG_WR(&regs_->ctl_sts.w, val);

    // Set up the pointers to our buffer descriptor list.
    REG_WR(&regs_->bdpl, static_cast<uint32_t>(bdl_phys_ & 0xFFFFFFFFu));
    REG_WR(&regs_->bdpu, static_cast<uint32_t>((bdl_phys_ >> 32) & 0xFFFFFFFFu));

    // Program the stream format.
    REG_WR(&regs_->fmt, encoded_fmt);

    // Make sure the stream format has been written before we read the fifo depth.
    hw_rmb();
    fifo_depth_ = REG_RD(&regs_->fifod);

    DEBUG_LOG("Stream format set 0x%04hx; fifo is %hu bytes deep\n", encoded_fmt, fifo_depth_);

    // Record our new client channel
    mxtl::AutoLock channel_lock(&channel_lock_);
    channel_ = channel;
    bytes_per_frame_ = StreamFormat(encoded_fmt).bytes_per_frame();

    return NO_ERROR;
}

void IntelHDAStream::Deactivate() {
    mxtl::AutoLock channel_lock(&channel_lock_);
    DeactivateLocked();
}

void IntelHDAStream::OnChannelClosed(const DriverChannel& channel) {
    // Is the channel being closed our currently active channel?  If so, go
    // ahead and deactivate this DMA stream.  Otherwise, just ignore this
    // request.
    mxtl::AutoLock channel_lock(&channel_lock_);

    if (&channel == channel_.get()) {
        DEBUG_LOG("Client closed channel to stream\n");
        DeactivateLocked();
    }
}

#define CHECK_CMD(_ioctl, _payload)                     \
    do {                                                \
        if (req_size != sizeof(req._payload)) {         \
            DEBUG_LOG("Bad " #_payload                  \
                      " request length (%u != %zu)\n",  \
                      req_size, sizeof(req._payload));  \
            return ERR_INVALID_ARGS;                    \
        }                                               \
    } while (false)                                     \

#define PROCESS_CMD(_ioctl, _handler, _payload) \
case _ioctl:                                    \
    CHECK_CMD(_ioctl, _payload);                \
    return _handler(req._payload)
mx_status_t IntelHDAStream::ProcessClientRequest(DriverChannel& channel,
                                                 const RequestBufferType& req,
                                                 uint32_t req_size,
                                                 mx::handle&& rxed_handle) {
    // Is this request from our currently active channel?  If not, make sure the
    // channel has been de-activated and ignore the request.
    mxtl::AutoLock channel_lock(&channel_lock_);

    if (channel_.get() != &channel) {
        channel.Deactivate(false);
        return NO_ERROR;
    }

    // Sanity check the request, then dispatch it to the appropriate handler.
    if (req_size < sizeof(req.hdr)) {
        DEBUG_LOG("Client request too small to contain header (%u < %zu)\n",
                req_size, sizeof(req.hdr));
        return ERR_INVALID_ARGS;
    }

    VERBOSE_LOG("Client Request (cmd 0x%04x tid %u) len %u\n",
                req.hdr.cmd,
                req.hdr.transaction_id,
                req_size);

    if (req.hdr.transaction_id == AUDIO2_INVALID_TRANSACTION_ID)
        return ERR_INVALID_ARGS;

    switch (req.hdr.cmd) {
    case AUDIO2_RB_CMD_SET_BUFFER: {
        CHECK_CMD(AUDIO2_RB_CMD_SET_BUFFER, set_buffer);

        mx::vmo ring_buffer_vmo;
        mx_status_t res = ConvertHandle(&rxed_handle, &ring_buffer_vmo);
        if (res != NO_ERROR) {
            DEBUG_LOG("Invalid or non-VMO handle for AUDIO2_RB_CMD_SET_BUFFER (res %d)\n", res);
            return res;
        }

        return ProcessSetBufferLocked(req.set_buffer, mxtl::move(ring_buffer_vmo));
    } break;

    PROCESS_CMD(AUDIO2_RB_CMD_START, ProcessStartLocked, start);
    PROCESS_CMD(AUDIO2_RB_CMD_STOP,  ProcessStopLocked,  stop);

    default:
        DEBUG_LOG("Unrecognized command ID 0x%04x\n", req.hdr.cmd);
        return ERR_INVALID_ARGS;
    }
}
#undef PROCESS_CMD
#undef CHECK_CMD

void IntelHDAStream::ProcessStreamIRQ() {
    // Regarless of whether we are currently active or not, make sure we ack any
    // pending IRQs so we don't accidentally spin out of control.
    uint8_t sts = REG_RD(&regs_->ctl_sts.b.sts);
    REG_WR(&regs_->ctl_sts.b.sts, sts);

    // Enter the lock and check to see if we should still be sending update
    // notifications.  If our channel has been nulled out, then this stream was
    // were stopped after the IRQ fired but before it was handled.  Don't send
    // any notifications in this case.
    mxtl::AutoLock notif_lock(&notif_lock_);
    if (irq_channel_ == nullptr)
        return;

    // TODO(johngro):  Deal with FIFO errors or descriptor errors.  There is no
    // good way to recover from such a thing.  If it happens, we need to shut
    // the stream down and send the client an error notification informing them
    // that their stream was ruined and that they need to restart it.
    if (sts & (HDA_SD_REG_STS8_FIFOE | HDA_SD_REG_STS8_DESE))
        DEBUG_LOG("Unexpected stream IRQ status 0x%02x!\n", sts);

    if (sts & HDA_SD_REG_STS8_BCIS) {
        Audio2RBPositionNotify msg;
        msg.hdr.cmd = AUDIO2_RB_POSITION_NOTIFY;
        msg.hdr.transaction_id = AUDIO2_INVALID_TRANSACTION_ID;
        msg.ring_buffer_pos = REG_RD(&regs_->lpib);
        irq_channel_->Write(&msg, sizeof(msg));
    }
}

void IntelHDAStream::DeactivateLocked() {
    // Prevent the IRQ thread from sending channel notifications by making sure
    // the irq_channel_ reference has been cleared.
    {
        mxtl::AutoLock notif_lock(&notif_lock_);
        irq_channel_ = nullptr;
    }

    // Stop the stream, but do not place it into reset.  Ack any lingering IRQ
    // status bits in the process.
    REG_WR(&regs_->ctl_sts.w, HDA_SD_REG_STS32_ACK);
    hw_wmb();  // No reordering!  We must be sure that the stream is stopped before moving on.

    // Clear out the format and buffer descriptor list pointers
    REG_WR(&regs_->bdpl, 0u);
    REG_WR(&regs_->bdpu, 0u);
    REG_WR(&regs_->fmt, 0u);
    hw_wmb();

    // We are now stopped and unconfigured.
    running_         = false;
    fifo_depth_      = 0;
    bytes_per_frame_ = 0;

    // Release any assigned ring buffer.
    ReleaseRingBufferLocked();

    // If we have a connection to a client, close it.
    if (channel_ != nullptr) {
        channel_->Deactivate(false);
        channel_ = nullptr;
    }

    DEBUG_LOG("Stream deactivated\n");
}

mx_status_t IntelHDAStream::ProcessSetBufferLocked(const Audio2RBSetBufferReq& req,
                                                   mx::vmo&& ring_buffer_vmo) {
    DEBUG_ASSERT(ring_buffer_vmo.is_valid());
    DEBUG_ASSERT(channel_ != nullptr);

    Audio2RBSetBufferResp resp;
    resp.hdr = req.hdr;

    // We cannot change buffers while we are running, and we cannot assign a
    // buffer if our format has not been set yet.
    if (running_ || (bytes_per_frame_ == 0)) {
        DEBUG_LOG("Bad state while setting buffer %s%s.",
                  running_ ? "(running)" : "",
                  bytes_per_frame_ == 0 ? "(not configured)" : "");
        resp.result = ERR_BAD_STATE;
        goto finished;
    }

    // If we have an existing buffer, let go of it now.
    ReleaseRingBufferLocked();

    // The request arguments are invalid if any of the following are true...
    //
    // 1) The user's ring buffer size is 0.
    // 2) The user's ring buffer size is not a multiple of the frame size.
    // 3) The user wants more notifications per ring than we have BDL entries.
    if ((req.ring_buffer_bytes == 0)                ||
        (req.ring_buffer_bytes % bytes_per_frame_)  ||
        (req.notifications_per_ring > MAX_BDL_LENGTH)) {
        DEBUG_LOG("Invalid client args while setting buffer "
                  "(vmo %d, bytes %u, frame_sz %u, notif/ring %u)\n",
                  ring_buffer_vmo.get(),
                  req.ring_buffer_bytes,
                  bytes_per_frame_,
                  req.notifications_per_ring);
        resp.result = ERR_INVALID_ARGS;
        goto finished;
    }

    // Fetch the scatter-gather list for the provided VMO.  Make sure that it is
    // as large as the user says and that everything is aligned properly.
    VMORegion  regions[MAX_BDL_LENGTH];
    uint64_t   total_vmo_size;
    uint32_t   num_regions;

    num_regions = countof(regions);
    resp.result = GetVMORegionInfo(ring_buffer_vmo, &total_vmo_size, regions, &num_regions);
    if (resp.result != NO_ERROR) {
        DEBUG_LOG("Failed to fetch VMO scatter/gather map (res %d)\n", resp.result);
        goto finished;
    }

    if (total_vmo_size < req.ring_buffer_bytes) {
        DEBUG_LOG("VMO too small to hold ring buffer! (%" PRIu64 " < %u",
                  total_vmo_size, req.ring_buffer_bytes);
        goto finished;
    }

    // Program the buffer descriptor list.  Mark BDL entries as needed to
    // generate interrupts with the frequency requested by the user.
    uint32_t nominal_irq_spacing;
    nominal_irq_spacing = req.notifications_per_ring
                        ? (req.ring_buffer_bytes + req.notifications_per_ring - 1) /
                           req.notifications_per_ring
                        : 0;

    uint32_t next_irq_pos;
    uint32_t amt_done;
    uint32_t region_num, region_offset;
    uint32_t entry;

    next_irq_pos = nominal_irq_spacing;
    amt_done = 0;
    region_num = 0;
    region_offset = 0;

    for (entry = 0; (entry < MAX_BDL_LENGTH) && (amt_done < req.ring_buffer_bytes); ++entry) {
        DEBUG_ASSERT(region_num < num_regions);
        const auto& r = regions[region_num];

        if (r.size > mxtl::numeric_limits<uint32_t>::max()) {
            DEBUG_LOG("VMO region too large! (%" PRIu64 " bytes)", r.size);
            resp.result = ERR_INTERNAL;
            goto finished;
        }

        DEBUG_ASSERT(region_offset < r.size);
        uint32_t amt_left    = req.ring_buffer_bytes - amt_done;
        uint32_t region_left = static_cast<uint32_t>(r.size) - region_offset;
        uint32_t todo        = mxtl::min(amt_left, region_left);

        DEBUG_ASSERT(region_left >= DMA_ALIGN);
        bdl_[entry].flags = 0;

        if (nominal_irq_spacing) {
            uint32_t ipos = (next_irq_pos + DMA_ALIGN - 1) & ~DMA_ALIGN_MASK;

            if ((amt_done + todo) >= ipos) {
                bdl_[entry].flags = IntelHDABDLEntry::IOC_FLAG;
                next_irq_pos += nominal_irq_spacing;

                if (ipos <= amt_done)
                    todo = mxtl::min(todo, DMA_ALIGN);
                else
                    todo = mxtl::min(todo, ipos - amt_done);
            }
        }

        DEBUG_ASSERT(!(todo & DMA_ALIGN_MASK) || (todo == amt_left));

        bdl_[entry].address = r.phys_addr + region_offset;
        bdl_[entry].length  = todo;

        DEBUG_ASSERT(!(bdl_[entry].address & DMA_ALIGN_MASK));

        amt_done += todo;
        region_offset += todo;

        if (region_offset >= r.size) {
            DEBUG_ASSERT(region_offset == r.size);
            region_offset = 0;
            region_num++;
        }
    }

#if DEBUG_LOGGING
    DEBUG_LOG("DMA Scatter/Gather used %u entries for %u/%u bytes of ring buffer\n",
                entry, amt_done, req.ring_buffer_bytes);
    for (uint32_t i = 0; i < entry; ++i) {
        DEBUG_LOG("[%2u] : %016" PRIx64 " - 0x%04x %sIRQ\n",
                    i,
                    bdl_[i].address,
                    bdl_[i].length,
                    bdl_[i].flags ? "" : "NO ");
    }
#endif

    if (amt_done < req.ring_buffer_bytes) {
        DEBUG_ASSERT(entry == MAX_BDL_LENGTH);
        DEBUG_LOG("Ran out of BDL entires after %u/%u bytes of ring buffer\n",
                  amt_done, req.ring_buffer_bytes);
        resp.result = ERR_INTERNAL;
        goto finished;
    }

    // Program the cyclic buffer length and the BDL last valid index.
    DEBUG_ASSERT(entry > 0);
    REG_WR(&regs_->cbl, req.ring_buffer_bytes);
    REG_WR(&regs_->lvi, static_cast<uint16_t>(entry - 1));

    // TODO(johngro) : Force writeback of the cache to make sure that the BDL
    // has hit physical memory?
    hw_wmb();

    // Success.  DMA is set up and ready to go.  Claim the vmo handle passed to
    // us by the user.
    ring_buffer_vmo_ = mxtl::move(ring_buffer_vmo);
    resp.result      = NO_ERROR;

finished:
    return channel_->Write(&resp, sizeof(resp));
}

mx_status_t IntelHDAStream::ProcessStartLocked(const Audio2RBStartReq& req) {
    Audio2RBStartResp resp;
    resp.hdr = req.hdr;
    resp.result = NO_ERROR;
    resp.start_ticks = 0;

    // We cannot start unless we have configured the ring buffer and are not already started.
    if (!ring_buffer_vmo_.is_valid() || running_) {
        DEBUG_LOG("Bad state during start request %s%s.\n",
                !ring_buffer_vmo_.is_valid() ? "(ring buffer not configured)" : "",
                running_ ? "(already running)" : "");
        resp.result = ERR_BAD_STATE;
        goto finished;
    }

    // Make a copy of our reference to our channel which can be used by the IRQ
    // thread to deliver notifications to the application.
    {
        mxtl::AutoLock notif_lock(&notif_lock_);
        DEBUG_ASSERT(irq_channel_ == nullptr);
        irq_channel_ = channel_;

        // Set the RUN bit in our control register.  Mark the time that we did
        // so.  Do this from within the notification lock so that there is no
        // chance of us fighting with the IRQ thread over the ctl/sts register.
        // After this point in time, we may not write to the ctl/sts register
        // unless we have nerfed IRQ thread callbacks by clearing irq_channel_
        // from within the notif_lock_.
        //
        // TODO(johngro) : Do a better job of estimating when the first frame gets
        // clocked out.  For outputs, using the SSYNC register to hold off the
        // stream until the DMA has filled the FIFO could help.  There may also be a
        // way to use the WALLCLK register to determine exactly when the next HDA
        // frame will begin transmission.  Compensating for the external codec FIFO
        // delay would be a good idea as well.
        //
        // For now, we just assume that transmission starts "very soon" after we
        // whack the bit.
        constexpr uint32_t VAL  = HDA_SD_REG_CTRL_RUN  |
                                  HDA_SD_REG_CTRL_IOCE |
                                  HDA_SD_REG_STS32_ACK;
        constexpr uint32_t MASK = VAL |
                                  HDA_SD_REG_CTRL_FEIE |
                                  HDA_SD_REG_CTRL_DEIE;
        REG_MOD(&regs_->ctl_sts.w, MASK, VAL);
        hw_wmb();
        resp.start_ticks = mx_ticks_get();
    }

    // Success, we are now running.
    running_ = true;

finished:
    return channel_->Write(&resp, sizeof(resp));
}

mx_status_t IntelHDAStream::ProcessStopLocked(const Audio2RBStopReq& req) {
    Audio2RBStopResp resp;
    resp.hdr = req.hdr;

    if (running_) {
        // Start by preventing the IRQ thread from processing status interrupts.
        // After we have done this, it should be safe to manipulate the ctl/sts
        // register.
        {
            mxtl::AutoLock notif_lock(&notif_lock_);
            DEBUG_ASSERT(irq_channel_ != nullptr);
            irq_channel_ = nullptr;
        }

        // Ack all interrupts while we also disable all interrupts and clear the
        // run bit.
        constexpr uint32_t VAL  = HDA_SD_REG_STS32_ACK;
        constexpr uint32_t MASK = VAL |
                                  HDA_SD_REG_CTRL_RUN  |
                                  HDA_SD_REG_CTRL_IOCE |
                                  HDA_SD_REG_CTRL_FEIE |
                                  HDA_SD_REG_CTRL_DEIE;
        REG_MOD(&regs_->ctl_sts.w, MASK, VAL);
        hw_wmb();

        resp.result = NO_ERROR;
    } else {
        resp.result = ERR_BAD_STATE;
    }

finished:
    return channel_->Write(&resp, sizeof(resp));
}

void IntelHDAStream::ReleaseRingBufferLocked() {
    ring_buffer_vmo_.reset();
    DEBUG_ASSERT(bdl_);
    memset(bdl_, 0, sizeof(*bdl_) * MAX_BDL_LENGTH);
}
