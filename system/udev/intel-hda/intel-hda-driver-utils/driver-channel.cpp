// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <intel-hda-driver-utils/client-thread.h>
#include <intel-hda-driver-utils/driver-channel.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>

// Instantiate storage for the static allocator.
DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(DriverChannelAllocTraits, 0x100, true);

// Static storage
mxtl::atomic_uint64_t DriverChannel::driver_channel_id_gen_(1u);
mxtl::Mutex DriverChannel::active_channels_lock_;
mxtl::WAVLTree<uint64_t, mxtl::RefPtr<DriverChannel>> DriverChannel::active_channels_;

DriverChannel::DriverChannel(uintptr_t owner_ctx)
    : client_thread_active_(ClientThread::AddClient() == NO_ERROR),
      bind_id_(driver_channel_id_gen_.fetch_add(1u)),
      owner_ctx_(owner_ctx) {
}

DriverChannel::~DriverChannel() {
    channel_.reset();

    if (client_thread_active_)
        ClientThread::RemoveClient();

    MX_DEBUG_ASSERT(owner_ == nullptr);
    MX_DEBUG_ASSERT(!InOwnersList());
    MX_DEBUG_ASSERT(!InActiveChannelSet());
}

mx_status_t DriverChannel::Activate(mxtl::RefPtr<Owner>&& owner, mx::channel* client_channel_out) {
    // Arg and constant state checks first
    if ((client_channel_out == nullptr) || client_channel_out->is_valid())
        return ERR_INVALID_ARGS;

    if (owner == nullptr)
        return ERR_INVALID_ARGS;

    // Create the channel endpoints.
    mx::channel channel;
    mx_status_t res;

    res = mx::channel::create(0u, &channel, client_channel_out);
    if (res != NO_ERROR)
        return res;

    // Lock and attempt to activate.
    {
        mxtl::AutoLock obj_lock(&obj_lock_);
        res = ActivateLocked(mxtl::move(owner), mxtl::move(channel));
    }
    MX_DEBUG_ASSERT(channel == MX_HANDLE_INVALID);

    // If something went wrong, make sure we close the channel endpoint we were
    // going to give back to the caller.
   if (res != NO_ERROR)
       client_channel_out->reset();

   return res;
}

mx_status_t DriverChannel::ActivateLocked(mxtl::RefPtr<Owner>&& owner, mx::channel&& channel) {
    if (!channel.is_valid())
        return ERR_INVALID_ARGS;

    if ((client_thread_active_ == false) ||
        (channel_ != MX_HANDLE_INVALID)  ||
        (owner_   != nullptr))
        return ERR_BAD_STATE;

    // Bind our half of the channel to port which was provided.
    mx_status_t res = ClientThread::port().bind(bind_id(),
                                                channel.get(),
                                                MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED);

    if (res != NO_ERROR)
        return res;

    // Add ourselves to the set of active channels so that users can fetch
    // references to us.
    {
        mxtl::AutoLock channels_lock(&active_channels_lock_);
        active_channels_.insert(mxtl::WrapRefPtr(this));
    }

    // Finally, add ourselves to our Owner's list of channels.
    res = owner->AddChannel(mxtl::WrapRefPtr(this));
    if (res != NO_ERROR) {
        mxtl::AutoLock channels_lock(&active_channels_lock_);
        active_channels_.erase(*this);
        return res;
    }

    // Success, take ownership of our channel and owner reference.
    channel_ = mxtl::move(channel);
    owner_   = mxtl::move(owner);
    return res;
}

void DriverChannel::Deactivate(bool do_notify) {
    mxtl::RefPtr<Owner> old_owner;

    {
        mxtl::AutoLock obj_lock(&obj_lock_);

        {
            mxtl::AutoLock channels_lock(&active_channels_lock_);
            if (InActiveChannelSet())
                active_channels_.erase(*this);
        }

        if (owner_ != nullptr) {
            owner_->RemoveChannel(this);
            old_owner = mxtl::move(owner_);
        }

        channel_.reset();
    }

    if (do_notify && (old_owner != nullptr))
        old_owner->NotifyChannelDeactivated(*this);
}

mx_status_t DriverChannel::Process(const mx_io_packet_t& io_packet) {
    mxtl::RefPtr<Owner> owner;

    {
        // If our owner still exists, take a reference to them and call their
        // ProcessChannel handler.
        //
        // If the owner has gone away, then we should already be in the process
        // of shutting down.  Don't bother to report an error, we are already
        // being cleaned up.
        mxtl::AutoLock obj_lock(&obj_lock_);
        if (owner_ == nullptr)
            return NO_ERROR;

        owner = owner_;
    }

    return owner->ProcessChannel(*this, io_packet);
}

mx_status_t DriverChannel::Read(void*       buf,
                                uint32_t    buf_len,
                                uint32_t*   bytes_read_out,
                                mx::handle* rxed_handle) const {
    if (!buf || !buf_len || !bytes_read_out ||
       ((rxed_handle != nullptr) && rxed_handle->is_valid()))
        return ERR_INVALID_ARGS;

    mxtl::AutoLock obj_lock(&obj_lock_);

    if (!channel_.is_valid())
        return ERR_BAD_STATE;

    uint32_t rxed_handle_count = 0;
    return channel_.read(0,
                         buf, buf_len, bytes_read_out,
                         rxed_handle ? rxed_handle->get_address() : nullptr,
                         rxed_handle ? 1 : 0,
                         &rxed_handle_count);
}

mx_status_t DriverChannel::Write(const void*  buf,
                                 uint32_t     buf_len,
                                 mx::handle&& tx_handle) const {
    mx_status_t res;
    if (!buf || !buf_len)
        return ERR_INVALID_ARGS;

    mxtl::AutoLock obj_lock(&obj_lock_);
    if (!channel_.is_valid())
        return ERR_BAD_STATE;

    if (!tx_handle.is_valid())
        return channel_.write(0, buf, buf_len, nullptr, 0);

    mx_handle_t h = tx_handle.release();
    res = channel_.write(0, buf, buf_len, &h, 1);
    if (res != NO_ERROR)
        tx_handle.reset(h);

    return res;
}

void DriverChannel::Owner::ShutdownDriverChannels() {
    // Flag ourselves as deactivated.  This will prevent any new channels from
    // being added to the channels_ list.  We can then swap the contents of the
    // channels_ list with a temp list, leave the lock and deactivate all of the
    // channels at our leisure.
    mxtl::DoublyLinkedList<mxtl::RefPtr<DriverChannel>> to_deactivate;

    {
        mxtl::AutoLock activation_lock(&channels_lock_);
        if (deactivated_) {
            MX_DEBUG_ASSERT(channels_.is_empty());
            return;
        }

        deactivated_ = true;
        to_deactivate.swap(channels_);
    }

    // Now deactivate all of our channels and release all of our references.
    for (auto& channel : to_deactivate)
        channel.Deactivate(true);

    to_deactivate.clear();
}

mx_status_t DriverChannel::Owner::AddChannel(mxtl::RefPtr<DriverChannel>&& channel) {
    if (channel == nullptr)
        return ERR_INVALID_ARGS;

    // This check is a bit sketchy...  This channel should *never* be in any
    // Owner's channel list at this point in time, however if it is, we don't
    // really know what lock we need to obtain to make this observation
    // atomically.  That said, the check will not mutate any state, so it should
    // be safe.  It just might not catch a bad situation which should never
    // happen.
    MX_DEBUG_ASSERT(!channel->InOwnersList());

    // If this Owner has become deactivated, then it is not accepting any new
    // channels.  Fail the request to add this channel.
    mxtl::AutoLock channels_lock(&channels_lock_);
    if (deactivated_)
        return ERR_BAD_STATE;

    // We are still active.  Transfer the reference to this channel to our set
    // of channels.
    channels_.push_front(mxtl::move(channel));
    return NO_ERROR;
}

void DriverChannel::Owner::RemoveChannel(DriverChannel* channel) {
    mxtl::AutoLock channels_lock(&channels_lock_);

    // Has this DriverChannel::Owner become deactivated?  If so, then this
    // channel may still be on a list (the local 'to_deactivate' list in
    // ShutdownDriverChannels), but it is not in the Owner's channels_ list, so
    // there is nothing to do here.
    if (deactivated_) {
        MX_DEBUG_ASSERT(channels_.is_empty());
        return;
    }

    // If the channel has not already been removed from the owners list, do so now.
    if (channel->InOwnersList())
        channels_.erase(*channel);
}
