// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
// Copyright (c) 2012 Shantanu Gupta
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef __KERNEL_MUTEX_H
#define __KERNEL_MUTEX_H

#include <magenta/compiler.h>
#include <magenta/thread_annotations.h>
#include <debug.h>
#include <err.h>
#include <stdint.h>
#include <kernel/thread.h>

__BEGIN_CDECLS;

#define MUTEX_MAGIC (0x6D757478)  // 'mutx'

typedef struct TA_CAP("mutex") mutex {
    uint32_t magic;
    uintptr_t val;
    wait_queue_t wait;
} mutex_t;

#define MUTEX_FLAG_QUEUED ((uintptr_t)1)

#define MUTEX_HOLDER(m) ((thread_t *)(((m)->val) & ~MUTEX_FLAG_QUEUED))

#define MUTEX_INITIAL_VALUE(m) \
{ \
    .magic = MUTEX_MAGIC, \
    .val = 0, \
    .wait = WAIT_QUEUE_INITIAL_VALUE((m).wait), \
}

/* Rules for Mutexes:
 * - Mutexes are only safe to use from thread context.
 * - Mutexes are non-recursive.
*/
void mutex_init(mutex_t *m);
void mutex_destroy(mutex_t *m);
status_t mutex_acquire_slow(mutex_t *m) TA_ACQ(m);
void mutex_release_slow(mutex_t *m, bool resched, bool thread_lock_held) TA_REL(m);

/* fast path for mutexes */
static inline void mutex_acquire(mutex_t *m) TA_ACQ(m)
{
    thread_t *ct = get_current_thread();

    for (;;) {
        uintptr_t zero = 0;
        if (likely(atomic_cmpxchg_u64(&m->val, &zero, (uintptr_t)ct))) {
            // acquired it cleanly
            return;
        }

        if (likely(mutex_acquire_slow(m) == NO_ERROR))
            return;
    }
}

/* fast path for release */
static inline void mutex_release_etc(mutex_t *m, bool resched, bool thread_lock_held) TA_REL(m)
{
    thread_t *ct = get_current_thread();
    uintptr_t oldval;

    // in case there's no contention, try the fast path
    oldval = (uintptr_t)ct;
    if (likely(atomic_cmpxchg_u64(&m->val, &oldval, 0))) {
        // we're done, exit
        return;
    }

    mutex_release_slow(m, resched, thread_lock_held);
}

static inline void mutex_release(mutex_t *m) TA_REL(m)
{
    mutex_release_etc(m, true, false);
}

/* does the current thread hold the mutex? */
static inline bool is_mutex_held(const mutex_t *m)
{
    return (MUTEX_HOLDER(m) == get_current_thread());
}

__END_CDECLS;

// Include the handy C++ Mutex/AutoLock wrappers from mxtl.  Note, this include
// must come after the kernel definition of mutex_t and the prototypes for the
// mutex routines.
#include <mxtl/auto_lock.h>
#include <mxtl/mutex.h>

#endif

