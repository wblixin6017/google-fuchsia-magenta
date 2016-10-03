// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifdef KERNEL
#include <list.h>
#include <platform.h>
#include <kernel/event.h>
#include <kernel/mutex.h>
#include <kernel/thread.h>

#define le16toh(x)  LE16(x)

typedef thread_t* thrd_t;
typedef mutex_t mtx_t;
typedef event_t completion_t;

typedef thread_start_routine thrd_start_t;

static inline int thrd_create_with_name(thrd_t* thread, thrd_start_t entry, void* arg, const char* name) {
    thread_t* result = thread_create(name, entry, arg, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE);
    if (result) {
        thread_detach_and_resume(result);
        *thread = result;
        return NO_ERROR;
    } else {
        return ERR_NO_MEMORY;
    }
}

static inline void mtx_lock(mtx_t* mutex) {
    mutex_acquire(mutex);
}

static inline void mtx_unlock(mtx_t* mutex) {
    mutex_release(mutex);
}

static mx_status_t completion_wait(completion_t* completion, mx_time_t timeout) {
    lk_time_t t = (timeout == MX_TIME_INFINITE ? INFINITE_TIME : timeout / 1000000);
    return event_wait_timeout(completion, t, true);
}

static inline void completion_signal(completion_t* completion) {
    event_signal(completion, true);
}

static inline void completion_reset(completion_t* completion) {
    event_unsignal(completion);
}

static inline void xhci_sleep_ms(uint32_t ms) {
    thread_sleep(ms);
}

static inline mx_time_t mx_current_time(void) {
    return current_time() * 1000000;
}

#else
#include <ddk/completion.h>
#include <magenta/listnode.h>
#include <magenta/syscalls.h>
#include <threads.h>
#include <unistd.h>

static inline void xhci_sleep_ms(uint32_t ms) {
    usleep(ms * 1000);
}
#endif


