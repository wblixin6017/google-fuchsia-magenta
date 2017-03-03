// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/intel-hda-codec.h>
#include <intel-hda-driver-utils/audio2-proto.h>
#include <intel-hda-driver-utils/codec-driver-base.h>
#include <intel-hda-driver-utils/driver-channel.h>
#include <intel-hda-driver-utils/intel-hda-proto.h>
#include <mx/channel.h>
#include <mxtl/intrusive_wavl_tree.h>
#include <mxtl/mutex.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>

class IntelHDAStreamBase : public DriverChannel::Owner,
                           public mxtl::WAVLTreeContainable<mxtl::RefPtr<IntelHDAStreamBase>> {
public:
    mx_status_t Activate(const mxtl::RefPtr<DriverChannel>& codec_channel) TA_EXCL(obj_lock_);
    void        Deactivate() TA_EXCL(obj_lock_);
    mx_status_t PublishDevice(mx_driver_t* codec_driver,
                              mx_device_t* codec_device) TA_EXCL(obj_lock_);

    mx_status_t ProcessSendCORBCmd  (const ihda_proto::SendCORBCmdResp& resp)   TA_EXCL(obj_lock_);
    mx_status_t ProcessRequestStream(const ihda_proto::RequestStreamResp& resp) TA_EXCL(obj_lock_);
    mx_status_t ProcessSetStreamFmt (const ihda_proto::SetStreamFmtResp& resp,
                                     mx::channel&& ring_buffer_channel) TA_EXCL(obj_lock_);

    uint32_t id()       const { return id_; }
    bool     is_input() const { return is_input_; }
    uint32_t GetKey()   const { return id(); }

protected:
    friend class mxtl::RefPtr<IntelHDAStreamBase>;

    IntelHDAStreamBase(uint32_t id, bool is_input);
    virtual ~IntelHDAStreamBase() { }

    // Overloads to control stream behavior.
    virtual mx_status_t OnActivateLocked()    TA_REQ(obj_lock_) { return NO_ERROR; }
    virtual void        OnDeactivateLocked()  TA_REQ(obj_lock_) { }
    virtual mx_status_t OnDMAAssignedLocked() TA_REQ(obj_lock_) { return NO_ERROR; }
    virtual mx_status_t BeginChangeStreamFormatLocked(const audio2_proto::StreamSetFmtReq& fmt)
        TA_REQ(obj_lock_) { return ERR_NOT_SUPPORTED; }
    virtual mx_status_t FinishChangeStreamFormatLocked(uint16_t encoded_fmt) TA_REQ(obj_lock_) {
        return ERR_INTERNAL;
    }

    uint8_t dma_stream_tag() const TA_REQ(obj_lock_) { return dma_stream_tag_; }

    // Debug logging
    virtual void PrintDebugPrefix() const;

    mx_status_t SendCodecCommandLocked(uint16_t nid, CodecVerb verb, bool no_ack) TA_REQ(obj_lock_);
    mx_status_t SendCodecCommand(uint16_t nid, CodecVerb verb, bool no_ack) TA_EXCL(obj_lock_) {
        mxtl::AutoLock obj_lock(&obj_lock_);
        return SendCodecCommandLocked(nid, verb, no_ack);
    }

    // Exposed to derived class for thread annotations.
    const mxtl::Mutex& obj_lock() const TA_RET_CAP(obj_lock_) { return obj_lock_; }

    // DriverChannel::Owner implementation
    mx_status_t ProcessChannel(DriverChannel& channel, const mx_io_packet_t& io_packet) final;
    void NotifyChannelDeactivated(const DriverChannel& channel) final;

private:
    mx_status_t SetDMAStreamLocked(uint16_t id, uint8_t tag) TA_REQ(obj_lock_);
    mx_status_t DoSetStreamFormatLocked(const audio2_proto::StreamSetFmtReq& fmt) TA_REQ(obj_lock_);

    ssize_t DeviceIoctl(uint32_t op,
                        const void* in_buf,
                        size_t in_len,
                        void* out_buf,
                        size_t out_len) TA_EXCL(obj_lock_);

    const uint32_t id_;
    const bool     is_input_;
    char           dev_name_[MX_DEVICE_NAME_MAX] = { 0 };

    mxtl::Mutex obj_lock_;

    bool shutting_down_ TA_GUARDED(obj_lock_) = false;
    mxtl::RefPtr<DriverChannel> codec_channel_ TA_GUARDED(obj_lock_);
    uint16_t dma_stream_id_  TA_GUARDED(obj_lock_) = IHDA_INVALID_STREAM_ID;
    uint8_t  dma_stream_tag_ TA_GUARDED(obj_lock_) = IHDA_INVALID_STREAM_TAG;

    mx_device_t* parent_device_ TA_GUARDED(obj_lock_) = nullptr;
    mx_device_t  stream_device_ TA_GUARDED(obj_lock_);

    mxtl::RefPtr<DriverChannel> stream_channel_ TA_GUARDED(obj_lock_);

    uint32_t set_format_tid_ TA_GUARDED(obj_lock_) = AUDIO2_INVALID_TRANSACTION_ID;
    uint16_t encoded_fmt_    TA_GUARDED(obj_lock_);

    static mx_status_t EncodeStreamFormat(const audio2_proto::StreamSetFmtReq& fmt,
                                          uint16_t* encoded_fmt_out);

    static mx_protocol_device_t STREAM_DEVICE_THUNKS;
};
