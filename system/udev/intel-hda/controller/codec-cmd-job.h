// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <intel-hda-driver-utils/codec-commands.h>
#include <intel-hda-driver-utils/driver-channel.h>
#include <intel-hda-driver-utils/intel-hda-proto.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/slab_allocator.h>
#include <mxtl/unique_ptr.h>


class IntelHDACodec;

class CodecCmdJob;
using CodecCmdJobAllocTraits = mxtl::StaticSlabAllocatorTraits<mxtl::unique_ptr<CodecCmdJob>>;
using CodecCmdJobAllocator = mxtl::SlabAllocator<CodecCmdJobAllocTraits>;

class CodecCmdJob : public mxtl::DoublyLinkedListable<mxtl::unique_ptr<CodecCmdJob>>,
                    public mxtl::SlabAllocated<CodecCmdJobAllocTraits> {
public:
    CodecCommand command()   const { return cmd_; }
    uint8_t      codec_id()  const { return cmd_.codec_id(); }
    uint16_t     nid()       const { return cmd_.nid(); }
    CodecVerb    verb()      const { return cmd_.verb(); }

    const mxtl::RefPtr<DriverChannel>& response_channel() const { return response_channel_; }
    uint32_t transaction_id() const { return transaction_id_; }

private:
    // Only our slab allocators is allowed to construct us, and only the
    // unique_ptrs it hands out are allowed to destroy us.
    friend CodecCmdJobAllocator;
    friend mxtl::unique_ptr<CodecCmdJob>;

    CodecCmdJob(CodecCommand cmd) : cmd_(cmd) { }
    CodecCmdJob(mxtl::RefPtr<DriverChannel>&& response_channel,
                uint32_t transaction_id,
                CodecCommand cmd)
        : cmd_(cmd),
          transaction_id_(transaction_id),
          response_channel_(mxtl::move(response_channel)) { }

    ~CodecCmdJob() = default;

    const CodecCommand cmd_;
    const uint32_t     transaction_id_ = IHDA_INVALID_TRANSACTION_ID;
    mxtl::RefPtr<DriverChannel> response_channel_ = nullptr;
};

// Let users of the slab allocator know that the storage for the allocator is
// instantiated in a separate translation unit.
FWD_DECL_STATIC_SLAB_ALLOCATOR(CodecCmdJobAllocTraits);
