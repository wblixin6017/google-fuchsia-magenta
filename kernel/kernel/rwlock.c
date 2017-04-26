// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#include <kernel/rwlock.h>
#include <debug.h>
#include <assert.h>
#include <err.h>
#include <trace.h>
#include <kernel/thread.h>

/**
 * @brief  Initialize a rwlock_t
 */
void rwlock_init(rwlock_t *rw)
{
    *rw = (rwlock_t)RWLOCK_INITIAL_VALUE(*rw);
}

/**
 * @brief  Destroy a rwlock_t
 *
 * This function frees any resources that were allocated
 * in rwlock_init().  The rwlock_t object itself is not freed.
 */
void rwlock_destroy(rwlock_t *rw)
{
    DEBUG_ASSERT(rw->magic == RWLOCK_MAGIC);
    DEBUG_ASSERT(!arch_in_int_handler());

#if 0
    THREAD_LOCK(state);
#if LK_DEBUGLEVEL > 0
    if (unlikely(m->count > 0)) {
        panic("mutex_destroy: thread %p (%s) tried to destroy locked mutex %p,"
              " locked by %p (%s)\n",
              get_current_thread(), get_current_thread()->name, m,
              m->holder, m->holder->name);
    }
#endif
    m->magic = 0;
    m->count = 0;
    wait_queue_destroy(&m->wait);
    THREAD_UNLOCK(state);
#endif
}

void rwlock_acquire_read(rwlock_t *rw)
{
    DEBUG_ASSERT(rw->magic == RWLOCK_MAGIC);
    DEBUG_ASSERT(!arch_in_int_handler());

    THREAD_LOCK(state);

    // increment the reader count
    rw->read_count++;

    // if a writer has it in any way, keep blocking
    while (rw->write_holder != 0) {
        status_t ret = wait_queue_block(&rw->read_wait, INFINITE_TIME);
        ASSERT(ret == NO_ERROR);
    }

    THREAD_UNLOCK(state);
}

void rwlock_release_read(rwlock_t *rw)
{
    DEBUG_ASSERT(rw->magic == RWLOCK_MAGIC);
    DEBUG_ASSERT(!arch_in_int_handler());

    THREAD_LOCK(state);

    DEBUG_ASSERT(rw->write_holder == 0);

    // decrement the reader count
    DEBUG_ASSERT(rw->read_count > 0);
    rw->read_count--;

    // if we're the last reader out, and a writer is queued up, wake one of them up
    if (unlikely(rw->read_count == 0 && rw->write_count > 0))
        wait_queue_wake_one(&rw->write_wait, true, NO_ERROR);

    THREAD_UNLOCK(state);
}

void rwlock_acquire_write(rwlock_t *rw)
{
    DEBUG_ASSERT(rw->magic == RWLOCK_MAGIC);
    DEBUG_ASSERT(!arch_in_int_handler());

    thread_t *current_thread = get_current_thread();

#if LK_DEBUGLEVEL > 0
    if (unlikely(get_current_thread() == rw->write_holder))
        panic("rwlock_acquire_write: thread %p (%s) tried to acquire rwlock %p it already owns.\n",
              current_thread, current_thread->name, rw);
#endif

    THREAD_LOCK(state);

    // increment the writer count
    rw->write_count++;

    // see if a reader or another writer has it
    while (unlikely(rw->read_count > 0 || rw->write_holder != 0)) {
        status_t ret = wait_queue_block(&rw->write_wait, INFINITE_TIME);
        ASSERT(ret == NO_ERROR);
    }

    // mark ourself as owner
    DEBUG_ASSERT(rw->write_holder == 0);
    rw->write_holder = current_thread;

    THREAD_UNLOCK(state);
}

void rwlock_release_write(rwlock_t *rw)
{
    DEBUG_ASSERT(rw->magic == RWLOCK_MAGIC);
    DEBUG_ASSERT(!arch_in_int_handler());

    thread_t *current_thread = get_current_thread();

#if LK_DEBUGLEVEL > 0
    if (unlikely(current_thread != rw->write_holder)) {
        panic("rwlock_release_write: thread %p (%s) tried to release rwlock %p it doesn't own. owned by %p (%s)\n",
              current_thread, current_thread->name, rw, rw->write_holder,
              rw->write_holder ? rw->write_holder->name : "none");
    }
#endif

    THREAD_LOCK(state);

    // decrement the writer count
    DEBUG_ASSERT(rw->write_count > 0);
    rw->write_count--;

    // mark it unowned
    rw->write_holder = 0;

    // if there are any readers queued up, wake up all of them
    if (unlikely(rw->read_count > 0)) {
        //TRACEF("waking all readers (R %d W %d)\n", rw->read_count, rw->write_count);
        wait_queue_wake_all(&rw->read_wait, true, NO_ERROR);
    } else if (unlikely(rw->write_count > 0)) {
        // else if there are any more writers queued up, wake one
        //TRACEF("waking a writer (R %d W %d)\n", rw->read_count, rw->write_count);
        wait_queue_wake_one(&rw->write_wait, true, NO_ERROR);
    }

    THREAD_UNLOCK(state);
}

#if 0
status_t mutex_acquire_internal(mutex_t *m) TA_NO_THREAD_SAFETY_ANALYSIS
{
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(spin_lock_held(&thread_lock));
    DEBUG_ASSERT(!arch_in_int_handler());

    if (unlikely(++m->count > 1)) {
        status_t ret = wait_queue_block(&m->wait, INFINITE_TIME);
        if (unlikely(ret < NO_ERROR)) {
            /* mutexes are not interruptable and cannot time out, so it
             * is illegal to return with any error state.
             */
            panic("mutex_acquire_internal: wait_queue_block returns with error %d m %p, thr %p, sp %p\n",
                   ret, m, get_current_thread(), __GET_FRAME());
        }
    }

    m->holder = get_current_thread();

    return NO_ERROR;
}

/**
 * @brief  Acquire the mutex
 *
 * @return  NO_ERROR on success, other values on error
 */
status_t mutex_acquire(mutex_t *m)
{
    DEBUG_ASSERT(m->magic == MUTEX_MAGIC);
    DEBUG_ASSERT(!arch_in_int_handler());

#if LK_DEBUGLEVEL > 0
    if (unlikely(get_current_thread() == m->holder))
        panic("mutex_acquire: thread %p (%s) tried to acquire mutex %p it already owns.\n",
              get_current_thread(), get_current_thread()->name, m);
#endif

    THREAD_LOCK(state);
    status_t ret = mutex_acquire_internal(m);
    THREAD_UNLOCK(state);
    return ret;
}

void mutex_release_internal(mutex_t *m, bool reschedule) TA_NO_THREAD_SAFETY_ANALYSIS
{
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(spin_lock_held(&thread_lock));
    DEBUG_ASSERT(!arch_in_int_handler());

    m->holder = 0;

    if (unlikely(--m->count >= 1)) {
        /* release a thread */
        wait_queue_wake_one(&m->wait, reschedule, NO_ERROR);
    }
}

/**
 * @brief  Release mutex
 */
void mutex_release(mutex_t *m)
{
    DEBUG_ASSERT(m->magic == MUTEX_MAGIC);
    DEBUG_ASSERT(!arch_in_int_handler());

#if LK_DEBUGLEVEL > 0
    if (unlikely(get_current_thread() != m->holder)) {
        panic("mutex_release: thread %p (%s) tried to release mutex %p it doesn't own. owned by %p (%s)\n",
              get_current_thread(), get_current_thread()->name, m, m->holder, m->holder ? m->holder->name : "none");
    }
#endif

    THREAD_LOCK(state);
    mutex_release_internal(m, true);
    THREAD_UNLOCK(state);
}

#endif
