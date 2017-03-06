// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <intel-hda-driver-utils/stream-base.h>
#include <mxtl/ref_ptr.h>

#include "utils.h"

class RealtekStream : public IntelHDAStreamBase {
public:
    RealtekStream(const StreamProperties& props)
        : IntelHDAStreamBase(props.stream_id, props.is_input),
          props_(props) { }

protected:
    friend class mxtl::RefPtr<RealtekStream>;

    virtual ~RealtekStream() { }

    // IntelHDAStreamBase implementation
    mx_status_t OnActivateLocked()    TA_REQ(obj_lock()) final;
    void        OnDeactivateLocked()  TA_REQ(obj_lock()) final;
    mx_status_t BeginChangeStreamFormatLocked(const audio2_proto::StreamSetFmtReq& fmt)
        TA_REQ(obj_lock()) final;
    mx_status_t FinishChangeStreamFormatLocked(uint16_t encoded_fmt)
        TA_REQ(obj_lock()) final;

private:
    mx_status_t RunCmdListLocked(const CommandListEntry* list, size_t count, bool force_all = false)
        TA_REQ(obj_lock());
    mx_status_t DisableConverterLocked(bool force_all = false) TA_REQ(obj_lock());

    const StreamProperties props_;
};
