// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef __KERNEL_RWLOCK_H
#define __KERNEL_RWLOCK_H

#include <magenta/compiler.h>
#include <magenta/thread_annotations.h>
#include <debug.h>
#include <stdint.h>
#include <kernel/thread.h>

__BEGIN_CDECLS

#define RWLOCK_MAGIC (0x72776c6b)  // 'rwlk'

typedef struct rwlock {
    uint32_t magic;

    // read half
    int read_count;
    wait_queue_t read_wait;

    // write half
    int write_count;
    thread_t *write_holder;
    wait_queue_t write_wait;
} rwlock_t;

#define RWLOCK_INITIAL_VALUE(rw) \
{ \
    .magic = RWLOCK_MAGIC, \
\
    .read_count = 0, \
    .read_wait = WAIT_QUEUE_INITIAL_VALUE((rw).read_wait), \
\
    .write_count = 0, \
    .write_holder = NULL, \
    .write_wait = WAIT_QUEUE_INITIAL_VALUE((rw).write_wait), \
}

/* Rules for RW locks:
 * - RW locks are only safe to use from thread context.
*/

void rwlock_init(rwlock_t *rw);
void rwlock_destroy(rwlock_t *rw);
void rwlock_acquire_read(rwlock_t *rw);
void rwlock_release_read(rwlock_t *rw);
void rwlock_acquire_write(rwlock_t *rw);
void rwlock_release_write(rwlock_t *rw);

#if 0
/* does the current thread hold the rwlock? */
static bool is_rwlock_held(const rwlock_t *m)
{
    return m->holder == get_current_thread();
}
#endif

__END_CDECLS

#if 0
// Include the handy C++ Mutex/AutoLock wrappers from mxtl.  Note, this include
// must come after the kernel definition of mutex_t and the prototypes for the
// mutex routines.
#include <mxtl/auto_lock.h>
#include <mxtl/mutex.h>
#endif

#endif

