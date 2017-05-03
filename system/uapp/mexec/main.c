// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <magenta/process.h>
#include <magenta/syscalls.h>

#define ROUNDUP(a, b) (((a) + ((b)-1)) & ~((b)-1))

uint32_t
murmurhash (const char *key, uint32_t len, uint32_t seed) {
  uint32_t c1 = 0xcc9e2d51;
  uint32_t c2 = 0x1b873593;
  uint32_t r1 = 15;
  uint32_t r2 = 13;
  uint32_t m = 5;
  uint32_t n = 0xe6546b64;
  uint32_t h = 0;
  uint32_t k = 0;
  uint8_t *d = (uint8_t *) key; // 32 bit extract from `key'
  const uint32_t *chunks = NULL;
  const uint8_t *tail = NULL; // tail - last 8 bytes
  int i = 0;
  int l = len / 4; // chunk length

  h = seed;

  chunks = (const uint32_t *) (d + l * 4); // body
  tail = (const uint8_t *) (d + l * 4); // last 8 byte chunk of `key'

  // for each 4 byte chunk of `key'
  for (i = -l; i != 0; ++i) {
    // next 4 byte chunk of `key'
    k = chunks[i];

    // encode next 4 byte chunk of `key'
    k *= c1;
    k = (k << r1) | (k >> (32 - r1));
    k *= c2;

    // append to hash
    h ^= k;
    h = (h << r2) | (h >> (32 - r2));
    h = h * m + n;
  }

  k = 0;

  // remainder
  switch (len & 3) { // `len % 4'
    case 3: k ^= (tail[2] << 16);
    case 2: k ^= (tail[1] << 8);

    case 1:
      k ^= tail[0];
      k *= c1;
      k = (k << r1) | (k >> (32 - r1));
      k *= c2;
      h ^= k;
  }

  h ^= len;

  h ^= (h >> 16);
  h *= 0x85ebca6b;
  h ^= (h >> 13);
  h *= 0xc2b2ae35;
  h ^= (h >> 16);

  return h;
}

mx_handle_t vmo_from_file(const char* filename) {
    if (!filename) return ERR_INVALID_ARGS;

    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        printf("failed to open file\n");
        return ERR_IO;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        printf("failed to stat file\n");
        return ERR_IO;
    }

    size_t size = st.st_size;
    uint64_t offset = 0;

    mx_handle_t vmo;
    mx_status_t status = mx_vmo_create(ROUNDUP(size, PAGE_SIZE), 0, &vmo);

    while (size > 0) {
        char buffer[PAGE_SIZE];
        memset(buffer, 0, PAGE_SIZE);
        size_t xfer = size < sizeof(buffer) ? size : sizeof(buffer);
        ssize_t nread = pread(fd, buffer, xfer, offset);
        if (nread < 0) {
            mx_handle_close(vmo);
            close(fd);
            printf("failed to pread\n");
            return ERR_IO;
        } else if (nread == 0) {
            close(fd);
            printf("read 0 bytes\n");
            mx_handle_close(vmo);
            return ERR_IO;
        }

        size_t n;
        status = mx_vmo_write(vmo, buffer, offset, nread, &n);
        if (status < 0) {
            mx_handle_close(vmo);
            close(fd);
            printf("failed to write to vmo\n");
            return status;
        }
        if (n != (size_t)nread) {
            mx_handle_close(vmo);
            printf("failed to close vmo handle\n");
            close(fd);
            return ERR_IO;
        }

        offset += nread;
        size -= nread;
    }

    close(fd);
    return vmo;
}

int main(int argc, char** argv) {

    printf("Reading kernel vmo from file\n");
    mx_handle_t kernel_vmo = vmo_from_file("/data/magenta.bin");

    printf("Reading bootdata from file\n");
    mx_handle_t bootdata_vmo = vmo_from_file("/data/bootdata.bin");

    if (kernel_vmo < 0) {
        printf("Failed to create kernel vmo, retcode = %d\n", kernel_vmo);
        return -1;
    }

    if (bootdata_vmo < 0) {
        printf("Failed to create bootdata vmo, retcode = %d\n", bootdata_vmo);
        return -1;
    }

    printf("calling mx_system_mexec\n");
    mx_system_mexec(kernel_vmo, bootdata_vmo);

    return 0;
}
