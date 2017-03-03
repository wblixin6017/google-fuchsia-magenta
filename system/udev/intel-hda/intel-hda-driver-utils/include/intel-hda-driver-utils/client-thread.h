// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <mx/port.h>
#include <mxtl/auto_lock.h>
#include <mxtl/intrusive_single_list.h>
#include <mxtl/mutex.h>
#include <mxtl/unique_ptr.h>
#include <threads.h>

class ClientThread : public mxtl::SinglyLinkedListable<mxtl::unique_ptr<ClientThread>> {
public:
    static mx_status_t AddClient() {
        mxtl::AutoLock pool_lock(&pool_lock_);
        return AddClientLocked();
    }

    static void RemoveClient() {
        mxtl::AutoLock pool_lock(&pool_lock_);
        MX_DEBUG_ASSERT(active_client_count_ > 0);
        active_client_count_--;
    }

    static void ShutdownThreadPool() {
        mxtl::AutoLock pool_lock(&pool_lock_);
        ShutdownPoolLocked();
    }

    static mx::port& port() { return port_; }

    void PrintDebugPrefix() const;

private:
    friend class mxtl::unique_ptr<ClientThread>;
    explicit ClientThread(uint32_t id);
    ~ClientThread() { }

    int Main();

    static mx_status_t AddClientLocked() TA_REQ(pool_lock_);
    static void ShutdownPoolLocked()     TA_REQ(pool_lock_);

    // TODO(johngro) : migrate away from C11 threads, use native magenta
    // primatives instead.
    //
    // TODO(johngro) : What is the proper "invalid" value to initialize with
    // here?
    thrd_t thread_;
    char name_buffer_[MX_MAX_NAME_LEN];

    static mxtl::Mutex pool_lock_;
    static mx::port port_;
    static uint32_t active_client_count_ TA_GUARDED(pool_lock_);
    static uint32_t active_thread_count_ TA_GUARDED(pool_lock_);
    static mxtl::SinglyLinkedList<mxtl::unique_ptr<ClientThread>> thread_pool_
        TA_GUARDED(pool_lock_);
};

