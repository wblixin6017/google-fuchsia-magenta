// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <intel-hda-driver-utils/stream-base.h>
#include <mxtl/ref_ptr.h>

class QemuStream : public IntelHDAStreamBase {
public:
    QemuStream(uint32_t stream_id, bool is_input, uint16_t converter_nid)
        : IntelHDAStreamBase(stream_id, is_input),
          converter_nid_(converter_nid) { }

protected:
    friend class mxtl::RefPtr<QemuStream>;

    virtual ~QemuStream() { }

    // IntelHDAStreamBase implementation
    mx_status_t OnActivateLocked()    TA_REQ(obj_lock()) final;
    void        OnDeactivateLocked()  TA_REQ(obj_lock()) final;
    mx_status_t BeginChangeStreamFormatLocked(const Audio2StreamSetFmtReq& fmt)
        TA_REQ(obj_lock()) final;
    mx_status_t FinishChangeStreamFormatLocked(uint16_t encoded_fmt)
        TA_REQ(obj_lock()) final;

private:
    mx_status_t RunCmdListLocked(const CodecVerb* list, size_t count, bool force_all = false)
        TA_REQ(obj_lock());
    mx_status_t DisableConverterLocked(bool force_all = false) TA_REQ(obj_lock());
    const uint16_t converter_nid_;
};
