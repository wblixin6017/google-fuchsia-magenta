// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/syscalls/port.h>
#include <magenta/types.h>
#include <magenta/thread_annotations.h>
#include <mx/channel.h>
#include <mx/handle.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/intrusive_wavl_tree.h>
#include <mxtl/mutex.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/slab_allocator.h>
#include <unistd.h>

class DriverChannel;
using DriverChannelAllocTraits = mxtl::StaticSlabAllocatorTraits<mxtl::RefPtr<DriverChannel>>;
using DriverChannelAllocator = mxtl::SlabAllocator<DriverChannelAllocTraits>;

class DriverChannel : public mxtl::RefCounted<DriverChannel>,
                      public mxtl::DoublyLinkedListable<mxtl::RefPtr<DriverChannel>>,
                      public mxtl::WAVLTreeContainable<mxtl::RefPtr<DriverChannel>>,
                      public mxtl::SlabAllocated<DriverChannelAllocTraits> {
public:
    class Owner : public mxtl::RefCounted<Owner> {
    protected:
        friend class mxtl::RefPtr<Owner>;
        friend class DriverChannel;

        virtual ~Owner() {
            // Assert that the Owner implementation properly deactivated itself
            // before destructing.
            DEBUG_ASSERT(deactivated_);
            DEBUG_ASSERT(channels_.is_empty());
        }

        void ShutdownDriverChannels() TA_EXCL(channels_lock_);

        // ProcessChannel
        //
        // Called by the thread pool infrastructure to notify an owner that
        // there is a message pending on the channel.  Returning any error at
        // this point in time will cause the channel to be deactivated and
        // be released.
        virtual mx_status_t ProcessChannel(
                DriverChannel& channel,
                const mx_io_packet_t& io_packet) TA_EXCL(channels_lock_) = 0;

        // NotifyChannelDeactivated.
        //
        // Called by the thread pool infrastructure to notify an owner that a
        // channel is has become deactivated.  No new ProcessChannel callbacks
        // will arrive from 'channel', but it is possible that there are still
        // some callbacks currently in flight.  DriverChannel::Owner
        // implementers should take whatever synchronization steps are
        // appropriate.
        virtual void NotifyChannelDeactivated(const DriverChannel& channel)
            TA_EXCL(channels_lock_) { }

    private:
        mx_status_t AddChannel(mxtl::RefPtr<DriverChannel>&& channel)
            TA_EXCL(channels_lock_);
        void RemoveChannel(DriverChannel* channel)
            TA_EXCL(channels_lock_);

        mxtl::Mutex channels_lock_;
        bool deactivated_ TA_GUARDED(channels_lock_) = false;
        mxtl::DoublyLinkedList<mxtl::RefPtr<DriverChannel>> channels_ TA_GUARDED(channels_lock_);
    };

    static mxtl::RefPtr<DriverChannel> GetActiveChannel(uint64_t id)
        TA_EXCL(active_channels_lock_) {
        mxtl::AutoLock channels_lock(&active_channels_lock_);
        return GetActiveChannelLocked(id);
    }

    uint64_t  bind_id()   const { return bind_id_; }
    uint64_t  GetKey()    const { return bind_id(); }
    uintptr_t owner_ctx() const { return owner_ctx_; }

    bool InOwnersList() const {
        return mxtl::DoublyLinkedListable<mxtl::RefPtr<DriverChannel>>::InContainer();
    }

    bool InActiveChannelSet() const {
        return mxtl::WAVLTreeContainable<mxtl::RefPtr<DriverChannel>>::InContainer();
    }

    mx_status_t Activate(mxtl::RefPtr<Owner>&& owner, mx::channel* client_channel_out)
        TA_EXCL(obj_lock_, active_channels_lock_);

    mx_status_t Activate(mxtl::RefPtr<Owner>&& owner, mx::channel&& client_channel)
        TA_EXCL(obj_lock_, active_channels_lock_) {
        mxtl::AutoLock obj_lock(&obj_lock_);
        return ActivateLocked(mxtl::move(owner), mxtl::move(client_channel));
    }

    void Deactivate(bool do_notify) TA_EXCL(obj_lock_, active_channels_lock_);
    mx_status_t Process(const mx_io_packet_t& io_packet) TA_EXCL(obj_lock_, active_channels_lock_);
    mx_status_t Read(void* buf,
                     uint32_t buf_len,
                     uint32_t* bytes_read_out,
                     mx::handle* rxed_handle = nullptr) const
        TA_EXCL(obj_lock_, active_channels_lock_);
    mx_status_t Write(const void* buf,
                      uint32_t buf_len,
                      mx::handle&& tx_handle = mx::handle()) const
        TA_EXCL(obj_lock_, active_channels_lock_);

private:
    friend DriverChannelAllocator;
    friend class mxtl::RefPtr<DriverChannel>;

    static mxtl::RefPtr<DriverChannel> GetActiveChannelLocked(uint64_t id)
        TA_REQ(active_channels_lock_) {
        auto iter = active_channels_.find(id);
        return iter.IsValid() ? iter.CopyPointer() : nullptr;
    }

    DriverChannel(uintptr_t owner_ctx = 0);
    ~DriverChannel();

    mx_status_t ActivateLocked(mxtl::RefPtr<Owner>&& owner, mx::channel&& channel)
        TA_REQ(obj_lock_);

    mxtl::RefPtr<Owner>  owner_    TA_GUARDED(obj_lock_) = nullptr;
    mx::channel          channel_  TA_GUARDED(obj_lock_);
    mutable mxtl::Mutex  obj_lock_ TA_ACQ_BEFORE(active_channels_lock_, owner_->channels_lock_);
    const bool           client_thread_active_;
    const uint64_t       bind_id_;
    const uintptr_t      owner_ctx_;

    static mxtl::atomic_uint64_t driver_channel_id_gen_;
    static mxtl::Mutex active_channels_lock_;
    static mxtl::WAVLTree<uint64_t, mxtl::RefPtr<DriverChannel>>
        active_channels_ TA_GUARDED(active_channels_lock_);
};

FWD_DECL_STATIC_SLAB_ALLOCATOR(DriverChannelAllocTraits);
