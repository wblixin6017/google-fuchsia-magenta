// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/device/audio.h>
#include <mxio/io.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>

#include "wav.h"

#define DEV_AUDIO   "/dev/class/audio"

#define BUFFER_COUNT 2
#define BUFFER_SIZE 16384

static int do_play(int src_fd, int dest_fd, uint32_t sample_rate)
{
    mx_status_t status = ioctl_audio_set_sample_rate(dest_fd, &sample_rate);
    if (status != NO_ERROR) {
        printf("sample rate %d not supported\n", sample_rate);
        return status;
    }

    mx_handle_t buffer_vmo = MX_HANDLE_INVALID;
    mx_handle_t txring_vmo = MX_HANDLE_INVALID;
    mx_handle_t fifo = MX_HANDLE_INVALID;
    
    status = mx_vmo_create(BUFFER_SIZE * BUFFER_COUNT, 0, &buffer_vmo);
    if (status < 0) {
        printf("failed to create buffer_vmo: %d\n", status);
        goto out;
    }
    status = mx_vmo_create(BUFFER_COUNT * sizeof(mx_audio_txring_entry_t), 0, &txring_vmo);
    if (status < 0) {
        printf("failed to create buffer_vmo: %d\n", status);
        goto out;
    }
    uint8_t* buffer = NULL;
    mx_audio_txring_entry_t* ring = NULL;
    status = mx_process_map_vm(mx_process_self(), buffer_vmo, 0, BUFFER_SIZE * BUFFER_COUNT,
                               (uintptr_t *)&buffer, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE);
    if (status < 0) {
        printf("failed to map buffer VMO: %d\n", status);
        goto out;
    }
    status = mx_process_map_vm(mx_process_self(), txring_vmo, 0, sizeof(*ring) * BUFFER_COUNT,
                               (uintptr_t *)&ring, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE);
    if (status < 0) {
        printf("failed to txring VMO: %d\n", status);
        goto out;
    }

    status = ioctl_audio_set_buffer(dest_fd, &buffer_vmo);
    if (status < 0) {
        printf("ioctl_audio_set_buffer failed: %d\n", status);
        goto out;
    }
    mx_audio_set_txring_args_t args;
    args.txring = txring_vmo;
    args.count = BUFFER_COUNT;
    status = ioctl_audio_set_txring(dest_fd, &args);
    if (status < 0) {
        printf("ioctl_audio_set_txring failed: %d\n", status);
        goto out;
    }
    status = ioctl_audio_get_fifo(dest_fd, &fifo);
    if (status < 0) {
        printf("ioctl_audio_get_fifo failed: %d\n", status);
        goto out;
    }

    int index = 0;

    mx_fifo_state_t fifo_state;
    status = mx_fifo_op(fifo, MX_FIFO_OP_READ_STATE, 0, &fifo_state);
    if (status < 0) {
        printf("mx_fifo_op failed to read state: %d\n", status);
        goto out;
    }

    ioctl_audio_start(dest_fd);

    while (1) {
        if (fifo_state.head - fifo_state.tail < BUFFER_COUNT) {
            mx_audio_txring_entry_t* entry = &ring[index];

            // check status from previous transaction
            // if this entry has never been used, status should be zero
            status = entry->status;
            if (status < 0) {
                printf("driver returned status %d\n", status);
                break;
            }

            // fill and queue a buffer
            uint32_t offset = BUFFER_SIZE * index;
            int count = read(src_fd, &buffer[offset], BUFFER_SIZE);
            if (count <= 0) {
                break;
            }

            entry->data_size = count;
            entry->data_offset = offset;

            status = mx_fifo_op(fifo, MX_FIFO_OP_ADVANCE_HEAD, 1, &fifo_state);
            if (status < 0) {
                printf("mx_fifo_op failed to advance head: %d\n", status);
                goto out;
            }

            index++;
            if (index == BUFFER_COUNT) index = 0;
        } else {
            // buffers are all full - block for one to complete
            mx_handle_wait_one(fifo, MX_FIFO_NOT_FULL, MX_TIME_INFINITE, NULL);
            status = mx_fifo_op(fifo, MX_FIFO_OP_READ_STATE, 0, &fifo_state);
            if (status < 0) {
                printf("mx_fifo_op failed to read state: %d\n", status);
                goto out;
            }
        }
    }

    ioctl_audio_stop(dest_fd);

    // wait for all pending transactions to complete
    mx_handle_wait_one(txring_vmo, MX_FIFO_NOT_FULL, MX_TIME_INFINITE, NULL);

out:
    if (buffer) {
        mx_process_unmap_vm(buffer_vmo, (uintptr_t)buffer, BUFFER_SIZE * BUFFER_COUNT);
    }
    if (ring) {
        mx_process_unmap_vm(txring_vmo, (uintptr_t)ring, sizeof(*ring) * BUFFER_COUNT);
    }
    mx_handle_close(buffer_vmo);
    mx_handle_close(txring_vmo);
    mx_handle_close(fifo);
    return status;
}

static int open_sink(void) {
    struct dirent* de;
    DIR* dir = opendir(DEV_AUDIO);
    if (!dir) {
        printf("Error opening %s\n", DEV_AUDIO);
        return -1;
    }

    while ((de = readdir(dir)) != NULL) {
       char devname[128];

        snprintf(devname, sizeof(devname), "%s/%s", DEV_AUDIO, de->d_name);
        int fd = open(devname, O_RDWR);
        if (fd < 0) {
            printf("Error opening %s\n", devname);
            continue;
        }

        int device_type;
        int ret = ioctl_audio_get_device_type(fd, &device_type);
        if (ret != sizeof(device_type)) {
            printf("ioctl_audio_get_device_type failed for %s\n", devname);
            goto next;
        }
        if (device_type != AUDIO_TYPE_SINK) {
            goto next;
        }

        closedir(dir);
        return fd;

next:
        close(fd);
    }

    closedir(dir);
    return -1;

}

static int play_file(const char* path, int dest_fd) {
    riff_wave_header riff_wave_header;
    chunk_header chunk_header;
    chunk_fmt chunk_fmt;
    bool more_chunks = true;
    uint32_t sample_rate = 0;

    int src_fd = open(path, O_RDONLY);
    if (src_fd < 0) {
        fprintf(stderr, "Unable to open file '%s'\n", path);
        return src_fd;
    }

    read(src_fd, &riff_wave_header, sizeof(riff_wave_header));
    if ((riff_wave_header.riff_id != ID_RIFF) ||
        (riff_wave_header.wave_id != ID_WAVE)) {
        fprintf(stderr, "Error: '%s' is not a riff/wave file\n", path);
        return -1;
    }

    do {
        read(src_fd, &chunk_header, sizeof(chunk_header));

        switch (chunk_header.id) {
        case ID_FMT:
            read(src_fd, &chunk_fmt, sizeof(chunk_fmt));
            sample_rate = le32toh(chunk_fmt.sample_rate);
            /* If the format header is larger, skip the rest */
            if (chunk_header.sz > sizeof(chunk_fmt))
                lseek(src_fd, chunk_header.sz - sizeof(chunk_fmt), SEEK_CUR);
            break;
        case ID_DATA:
            /* Stop looking for chunks */
            more_chunks = false;
            break;
        default:
            /* Unknown chunk, skip bytes */
            lseek(src_fd, chunk_header.sz, SEEK_CUR);
        }
    } while (more_chunks);

    printf("playing %s\n", path);

    int ret = do_play(src_fd, dest_fd, sample_rate);
    close(src_fd);
    return ret;
}

static int play_files(const char* directory, int dest_fd) {
    int ret = 0;

    struct dirent* de;
    DIR* dir = opendir(directory);
    if (!dir) {
        printf("Error opening %s\n", directory);
        return -1;
    }

    while ((de = readdir(dir)) != NULL) {
        char path[PATH_MAX];
        int namelen = strlen(de->d_name);
        if (namelen < 5 || strcasecmp(de->d_name + namelen - 4, ".wav") != 0) continue;

        snprintf(path, sizeof(path), "%s/%s", directory, de->d_name);
        if ((ret = play_file(path, dest_fd)) < 0) {
            break;
        }
    }

    closedir(dir);
    return ret;
}

int main(int argc, char **argv) {
    int dest_fd = open_sink();
    if (dest_fd < 0) {
        printf("couldn't find a usable audio sink\n");
        return -1;
    }

    int ret = 0;
    if (argc == 1) {
        ret = play_files("/data", dest_fd);
    } else {
        for (int i = 1; i < argc && ret == 0; i++) {
            ret = play_file(argv[i], dest_fd);
        }
    }

    close(dest_fd);

    return ret;
}
