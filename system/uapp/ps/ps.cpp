// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#include <magenta/types.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <magenta/device/sysinfo.h>

#include <mx/handle.h>
#include <mx/job.h>
#include <mx/process.h>
#include <mx/thread.h>

static void dump_handle_info(const mx_record_handle_basic_t &info) {
    printf("handle info: koid %" PRIu64 " rights %#x type %u prop %u\n",
            info.koid, info.rights, info.type, info.props);
}

static void enumerate_threads(const mx::process &proc) {
    printf("enumerate_threads: proc %u\n", proc.get());

    union {
        uint8_t buffer[4096];
        mx_info_process_threads_t thread_list;
    };

    // read the thread list from this process
    mx_size_t return_size;
    auto status = proc.get_info(MX_INFO_PROCESS_THREADS, sizeof(thread_list.rec[0]),
            buffer, sizeof(buffer), &return_size);
    printf("status %d, return size %zu\n", status, return_size);

    if (status < 0 || return_size < sizeof(mx_info_process_threads_t))
        return;

    // iterate each thread
    printf("count %u\n", thread_list.hdr.count);
    for (size_t i = 0; i < thread_list.hdr.count; i++) {
        printf("%zu: %" PRIu64 "\n", i, thread_list.rec[i].koid);

        // convert the koid to a thread handle
        mx::thread thread;
        status = proc.get_child(thread_list.rec[i].koid, MX_RIGHT_READ, &thread);
        printf("get_child status %d, handle %d\n", status, thread.get());
        if (status < 0)
            continue;

        // read the basic handle info about the thread
        mx_info_handle_basic_t info;
        status = thread.get_info(MX_INFO_HANDLE_BASIC, sizeof(info.rec),
                &info, sizeof(info), &return_size);
        printf("get_info on thread returns %d, return size %zu\n", status, return_size);
        if (status < 0 || return_size < sizeof(info))
            continue;

        dump_handle_info(info.rec);
    }
}

int main(int argc, char** argv) {
    // open the sysinfo node
    int fd = open("/dev/sysinfo", O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "failed to open /dev/sysinfo\n");
        return 1;
    }

    mx::job root_job;

    mx_handle_t _root_job;
    auto ret = ioctl_sysinfo_get_root_job(fd, &_root_job);
    close(fd);
    if (ret < (ssize_t)sizeof(_root_job)) {
        fprintf(stderr, "failed to get root job handle\n");
        return 1;
    }
    root_job.reset(_root_job);

    printf("root job handle %u\n", root_job.get());

    // XXX for now just enumerate the threads on myself to get things going
    enumerate_threads(mx::process::self());

    return 0;
}
