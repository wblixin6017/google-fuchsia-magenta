// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2006-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// N.B. The default operator new,new[] are intentionally *not* defined here.
// We cannot allow their use as the standard says they will never return
// nullptr. This allows the compiler to optimize out checking the result for
// nullptr, and since we don't have exceptions (for good reason) there is no
// way to intelligently recover from failure. Thus they are forbidden.
// There's no way to intelligently poison them in a way that provides
// immediate feedback during compilation that they are not allowed, so we
// have to rely on a linker failure to catch errant uses.
// See MG-210.

#include <string.h>
#include <new.h>
#include <debug.h>
#include <lib/heap.h>

#define ASSERT(x) do { if (!(x)) { while (1) { __asm__ volatile ("" : :: "memory"); } } } while (0)

enum : unsigned {
    alloc_armed   = 1,
    alloc_ok      = 2,
};

void panic_if_armed(unsigned state) {
#if LK_DEBUGLEVEL > 1
    if (state & alloc_armed)
        panic("AllocChecker::check() needs to be called\n");
#endif
}

AllocChecker::AllocChecker() : state_(0U) {
}

AllocChecker::~AllocChecker() {
    panic_if_armed(state_);
}

void AllocChecker::arm(size_t sz, bool result) {
    panic_if_armed(state_);
    state_ =  alloc_armed |
        ((sz == 0u) ? alloc_ok : (result ? alloc_ok : 0u));
}

bool AllocChecker::check() {
    state_ &= ~alloc_armed;
    return (state_ & alloc_ok) == alloc_ok;
}

void *operator new(size_t s, AllocChecker* ac) noexcept {
    if (unlikely(s == 0))
        s = 1;

    s += 16;

    auto mem = malloc(s);
    if (mem != nullptr) {
        memcpy(mem, "HEAPHEA", 8);
        mem = (void *)((uintptr_t)mem + 8);
    }
    ac->arm(s - 16, mem != nullptr);
    return mem;
}

void *operator new[](size_t s, AllocChecker* ac) noexcept {
    if (unlikely(s == 0))
        s = 1;

    s += 16;

    auto mem = malloc(s);
    if (mem != nullptr) {
        memcpy(mem, "HEAPHEA", 8);
        mem = (void *)((uintptr_t)mem + 8);
    }
    ac->arm(s - 16, mem != nullptr);
    return mem;
}

void *operator new(size_t , void *p) {
    return p;
}

void operator delete(void *p) {
    if (p == nullptr)
        return;
    void *buf = (void*)((uintptr_t)p - 8);
    ASSERT(memcmp(buf, "HEAPHEA", 8) == 0);
    return free(buf);
}

void operator delete[](void *p) {
    if (p == nullptr)
        return;
    void *buf = (void*)((uintptr_t)p - 8);
    ASSERT(memcmp(buf, "HEAPHEA", 8) == 0);
    return free(buf);
}

void operator delete(void *p, size_t) {
    if (p == nullptr)
        return;
    void *buf = (void*)((uintptr_t)p - 8);
    ASSERT(memcmp(buf, "HEAPHEA", 8) == 0);
    return free(buf);
}

void operator delete[](void *p, size_t) {
    if (p == nullptr)
        return;
    void *buf = (void*)((uintptr_t)p - 8);
    ASSERT(memcmp(buf, "HEAPHEA", 8) == 0);
    return free(buf);
}
