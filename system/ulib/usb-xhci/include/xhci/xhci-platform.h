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

typedef mutex_t mtx_t;
typedef event_t completion_t;

typedef thread_start_routine thrd_start_t;

static inline void mtx_lock(mtx_t* mutex) {
    mutex_acquire(mutex);
}

static inline void mtx_unlock(mtx_t* mutex) {
    mutex_release(mutex);
}

static inline void completion_init(completion_t* completion) {
    event_init(completion, false, 0);
}

static inline mx_status_t completion_wait(completion_t* completion, mx_time_t timeout) {
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

static inline void completion_init(completion_t* completion) {
    completion_reset(completion);
}

static inline void xhci_sleep_ms(uint32_t ms) {
    usleep(ms * 1000);
}
#endif


