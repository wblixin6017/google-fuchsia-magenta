#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <magenta/types.h>
#include <magenta/syscalls.h>

#define KB(x) (x * 1024UL)
#define MB(x) (KB(x) * 1024UL)

#define BENCH_SIZE MB(64)
const char blk_dev[] = "/dev/class/block/003";

int main(int argc, char* argv[]) {
    // Write a pattern into the 64MB buffer.
    // for (size_t i = 0; i < BENCH_SIZE; i++) {
    //     buf[i] = (uint8_t)(i % 256);
    // }
    printf("Attempting to open block device at %s\n", blk_dev);

    FILE* fp;
    fp = fopen(blk_dev, "r");
    if (fp == NULL) {
        printf("Failed to open block device at %s\n", blk_dev);
        return -1;
    }

    printf("Successfully opened block device at %s\n", blk_dev);

    uint8_t* buf = malloc(BENCH_SIZE);

    printf("Reading %lu bytes from block device at %s\n", BENCH_SIZE, blk_dev);

    mx_time_t start = mx_time_get(MX_CLOCK_MONOTONIC);
    int bytes_read = fread(buf, 1, BENCH_SIZE, fp);
    mx_time_t finish = mx_time_get(MX_CLOCK_MONOTONIC);

    if (bytes_read == BENCH_SIZE) {
        printf("Read returned %lu as expected\n", BENCH_SIZE);
    } else {
        printf("ERROR: Read returned %u, expected %lu\n", bytes_read, BENCH_SIZE);
    }

    fclose(fp);
    free(buf);

    printf("Entire op took %lu milliseconds\n", (finish - start) / MX_MSEC(1));

    uint32_t seconds = (finish - start) / MX_SEC(1);
    uint32_t megabytes = (BENCH_SIZE / (1024 * 1024));

    uint32_t rate = megabytes / seconds;

    printf("Speed = %uMB/s\n", rate);


    printf("Done!\n");

    return 0;
}