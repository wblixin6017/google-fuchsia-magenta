// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
// Copyright (c) 2012-2012 Shantanu Gupta
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


/**
 * @file
 * @brief  Mutex functions
 *
 * @defgroup mutex Mutex
 * @{
 */

#include <kernel/mutex.h>

#include <debug.h>
#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/thread.h>
#include <kernel/sched.h>
#include <trace.h>

#define LOCAL_TRACE 0

/**
 * @brief  Initialize a mutex_t
 */
void mutex_init(mutex_t *m)
{
    *m = (mutex_t)MUTEX_INITIAL_VALUE(*m);
}

/**
 * @brief  Destroy a mutex_t
 *
 * This function frees any resources that were allocated
 * in mutex_init().  The mutex_t object itself is not freed.
 */
void mutex_destroy(mutex_t *m)
{
    DEBUG_ASSERT(m->magic == MUTEX_MAGIC);
    DEBUG_ASSERT(!arch_in_int_handler());

    THREAD_LOCK(state);
#if LK_DEBUGLEVEL > 0
    if (unlikely(m->val != 0)) {
        thread_t *holder = MUTEX_HOLDER(m);
        panic("mutex_destroy: thread %p (%s) tried to destroy locked mutex %p,"
              " locked by %p (%s)\n",
              get_current_thread(), get_current_thread()->name, m,
              holder, holder->name);
    }
#endif
    m->magic = 0;
    m->val = 0;
    wait_queue_destroy(&m->wait);
    THREAD_UNLOCK(state);
}

status_t mutex_acquire_slow(mutex_t *m) TA_NO_THREAD_SAFETY_ANALYSIS
{
    DEBUG_ASSERT(m->magic == MUTEX_MAGIC);
    DEBUG_ASSERT(!arch_in_int_handler());

    thread_t *ct = get_current_thread();
    uintptr_t oldval;

    LTRACEF("%p slow path\n", ct);

#if LK_DEBUGLEVEL > 0
    if (unlikely(ct == MUTEX_HOLDER(m)))
        panic("mutex_acquire: thread %p (%s) tried to acquire mutex %p it already owns.\n",
              ct, ct->name, m);
#endif

    // we contended with someone else, will probably need to block
    THREAD_LOCK(state);

    // save the current state and check to see if it wasn't released in the interim
    oldval = m->val;
    if (unlikely(oldval == 0)) {
        THREAD_UNLOCK(state);
        return ERR_BAD_STATE;
    }

    // try to exchange again with a flag indicating that we're blocking is set
    if (unlikely(!atomic_cmpxchg_u64(&m->val, &oldval, oldval | MUTEX_FLAG_QUEUED))) {
        // if we fail, just start over from the top
        THREAD_UNLOCK(state);
        return ERR_BAD_STATE;
    }

    status_t ret = wait_queue_block(&m->wait, INFINITE_TIME);
    if (unlikely(ret < NO_ERROR)) {
        /* mutexes are not interruptable and cannot time out, so it
         * is illegal to return with any error state.
         */
        panic("mutex_acquire: wait_queue_block returns with error %d m %p, thr %p, sp %p\n",
               ret, m, ct, __GET_FRAME());
    }

    LTRACEF("%p woken up, m->val is %#" PRIxPTR "\n", ct, m->val);

    // someone must have woken us up, we should own the mutex now
    DEBUG_ASSERT(ct == MUTEX_HOLDER(m));

    THREAD_UNLOCK(state);

    return NO_ERROR;
}

void mutex_release_slow(mutex_t *m, bool resched, bool thread_lock_held) TA_NO_THREAD_SAFETY_ANALYSIS
{
    DEBUG_ASSERT(m->magic == MUTEX_MAGIC);
    DEBUG_ASSERT(!arch_in_int_handler());

    thread_t *ct = get_current_thread();
    uintptr_t oldval;

    // slow path from now on out
    LTRACEF("%p slow path\n", ct);

#if LK_DEBUGLEVEL > 0
    if (unlikely(ct != MUTEX_HOLDER(m))) {
        thread_t *holder = MUTEX_HOLDER(m);
        panic("mutex_release: thread %p (%s) tried to release mutex %p it doesn't own. owned by %p (%s)\n",
              ct, ct->name, m, holder, holder ? holder->name : "none");
    }
#endif

    // must have been some contention, try the slow release
    spin_lock_saved_state_t state;
    if (!thread_lock_held)
        spin_lock_irqsave(&thread_lock, state);

    oldval = (uintptr_t)ct | MUTEX_FLAG_QUEUED;

    thread_t *t = wait_queue_dequeue_one(&m->wait, NO_ERROR);
    DEBUG_ASSERT_MSG(t, "mutex_release: wait queue didn't have anything, but m->val = %#" PRIxPTR "\n", m->val);

    // we woke up a thread, mark the mutex owned by that thread
    uintptr_t newval = (uintptr_t)t | (wait_queue_is_empty(&m->wait) ? 0 : MUTEX_FLAG_QUEUED);

    LTRACEF("%p woke up thread %p, marking it as owner, newval %#" PRIxPTR "\n", ct, t, newval);

    if (!atomic_cmpxchg_u64(&m->val, &oldval, newval)) {
        panic("bad state in mutex release %p, current thread %p\n", m, ct);
    }

    sched_unblock(t, resched);

    if (!thread_lock_held)
        spin_unlock_irqrestore(&thread_lock, state);
}

