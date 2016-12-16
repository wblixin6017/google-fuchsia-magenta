// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/device/audio.h>

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

#define DEV_AUDIO   "/dev/class/audio"

#define BUFFER_COUNT 32
#define BUFFER_SIZE 500

static int open_source(void) {
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
        if (device_type != AUDIO_TYPE_SOURCE) {
            goto next;
        }

        uint32_t sample_rate;
        ret = ioctl_audio_get_sample_rate(fd, &sample_rate);
        if (ret != sizeof(sample_rate)) {
            printf("%s unable to get sample rate\n", devname);
            goto next;
        }
        printf("%s sample rate %d\n", devname, sample_rate);

        closedir(dir);
        return fd;

next:
        close(fd);
    }

    closedir(dir);
    return -1;
}

static mx_status_t do_record(int src_fd, int dest_fd, int read_count) {
    mx_handle_t buffer_vmo = MX_HANDLE_INVALID;
    mx_handle_t txring_vmo = MX_HANDLE_INVALID;
    mx_handle_t fifo = MX_HANDLE_INVALID;
    
    mx_status_t status = mx_vmo_create(BUFFER_SIZE * BUFFER_COUNT, 0, &buffer_vmo);
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

    status = ioctl_audio_set_buffer(src_fd, &buffer_vmo);
    if (status < 0) {
        printf("ioctl_audio_set_buffer failed: %d\n", status);
        goto out;
    }
    mx_audio_set_txring_args_t args;
    args.txring = txring_vmo;
    args.count = BUFFER_COUNT;
    status = ioctl_audio_set_txring(src_fd, &args);
    if (status < 0) {
        printf("ioctl_audio_set_txring failed: %d\n", status);
        goto out;
    }
    status = ioctl_audio_get_fifo(src_fd, &fifo);
    if (status < 0) {
        printf("ioctl_audio_get_fifo failed: %d\n", status);
        goto out;
    }

    mx_fifo_state_t fifo_state;
    status = mx_fifo_op(fifo, MX_FIFO_OP_READ_STATE, 0, &fifo_state);
    if (status < 0) {
        printf("mx_fifo_op failed to read state: %d\n", status);
        goto out;
    }

    ioctl_audio_start(src_fd);

    int index = 0;
    int completed_count = 0;
    while (1) {
        // queue transactions for empty entries
        int empty_count = BUFFER_COUNT - (fifo_state.head - fifo_state.tail);
        for (int i = 0; i < empty_count; i++) {
            mx_audio_txring_entry_t* entry = &ring[index];
            // check status from previous transaction
            // if this entry has never been used, status should be zero
            int result = entry->status;
            if (result < 0) {
                status = result;
                printf("driver returned status %d\n", status);
                break;
            } else if (result > 0) {
                completed_count++;
                if (read_count && completed_count > read_count) break;
                
                if (dest_fd >= 0) {
                    result = write(dest_fd, buffer + entry->data_offset, result);
                    if (result < 0) {
                        printf("write failed: %d\n", result);
                        status = result;
                        break;
                    }
                } else {
                    printf("read %d\n", result);
                }
            }

            entry->data_offset = index * BUFFER_SIZE;
            entry->data_size = BUFFER_SIZE;
            entry->status = 0;

            index++;
            if (index == BUFFER_COUNT) index = 0;
        }
    
        status = mx_fifo_op(fifo, MX_FIFO_OP_ADVANCE_HEAD, empty_count, &fifo_state);
        if (status < 0) {
            printf("mx_fifo_op failed to advance head: %d\n", status);
            goto out;
        }
        mx_handle_wait_one(fifo, MX_FIFO_NOT_FULL, MX_TIME_INFINITE, NULL);
        status = mx_fifo_op(fifo, MX_FIFO_OP_READ_STATE, 0, &fifo_state);
        if (status < 0) {
            printf("mx_fifo_op failed to read state: %d\n", status);
            goto out;
        }
    }
/*
    for (int i = 0; i < read_count; i++) {
        uint16_t buffer[500];
        int length = read(src_fd, buffer, sizeof(buffer));
        if (length < 0) {
            status = length;
            break;
        }
        if (dest_fd >= 0) {
            length = write(dest_fd, buffer, length);
            if (length < 0) {
                status = length;
                break;
            }
        } else {
            printf("read %d\n", length);
        }
    }
*/

out:
    // wait for all pending transactions to complete
    mx_handle_wait_one(fifo, MX_FIFO_NOT_FULL, MX_TIME_INFINITE, NULL);

    ioctl_audio_stop(src_fd);

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

static void usage(char* me) {
    fprintf(stderr, "usage: %s [-f <file to write PCM data to>] "
                    "[-s <number of times to start/stop>] "
                    "[-r <number of buffers to read per start/stop>]\n", me);
}

int main(int argc, char **argv) {
    char* file_path = NULL;
    int dest_fd = -1;

    // number of times to start & stop audio
    int start_stop_count = 1;
    // number of times to read per start/stop
    int read_count = INT_MAX;

    for (int i = 1; i < argc; i++) {
        char* arg = argv[i];
        if (strcmp(arg, "-f") == 0) {
            if (++i < argc) {
                file_path = argv[i];
                continue;
            }
            usage(argv[0]);
            return -1;
        } else if (strcmp(arg, "-s") == 0) {
            if (++i < argc) {
                int count = atoi(argv[i]);
                if (count > 0) {
                    start_stop_count = count;
                    continue;
                }
            }
            usage(argv[0]);
            return -1;
        } else if (strcmp(arg, "-r") == 0) {
            if (++i < argc) {
                int count = atoi(argv[i]);
                if (count > 0) {
                    read_count = count;
                    continue;
                }
            }
            usage(argv[0]);
            return -1;
        } else {
            usage(argv[0]);
            return -1;
        }
    }

    if (file_path) {
        dest_fd =  open(file_path, O_RDWR | O_CREAT | O_TRUNC);
        if (dest_fd < 0) {
            printf("couldn't open %s for writing\n", file_path);
            return -1;
        }
    }

    int fd = open_source();
    if (fd < 0) {
        printf("couldn't find a usable audio source\n");
        close(dest_fd);
        return -1;
    }

    for (int i = 0; i < start_stop_count; i++) {
        do_record(fd, dest_fd, read_count);
    }

    close(fd);
    close(dest_fd);
    return 0;
}
