#define _POSIX_C_SOURCE 200112L
#include <pthread.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "libbius.h"
#include "utils.h"

#define RAMDISK_SIZE (32lu * 1024 * 1024 * 1024)

char in_memory_data[RAMDISK_SIZE];

static blk_status_t ramdisk_read(void *data, off64_t offset, size_t length) {
    memcpy(data, in_memory_data + offset, length);
    return BLK_STS_OK;
}

static blk_status_t ramdisk_write(const void *data, off64_t offset, size_t length) {
    memcpy(in_memory_data + offset, data, length);
    return BLK_STS_OK;
}

static blk_status_t ramdisk_discard(off64_t offset, size_t length) {
    memset(in_memory_data + offset, 0, length);
    return BLK_STS_OK;
}

static blk_status_t ramdisk_flush() {
    return BLK_STS_OK;
}

int main(int argc, char *argv[]) {
    struct bius_operations operations = {
        .read = ramdisk_read,
        .write = ramdisk_write,
        .discard = ramdisk_discard,
        .flush = ramdisk_flush,
    };
    struct bius_block_device_options options = {
        .model = BLK_ZONED_NONE,
        .num_threads = 4,
        .disk_size = RAMDISK_SIZE,
    };
    strncpy(options.disk_name, "ramdisk", MAX_DISK_NAME_LEN);

    memset(in_memory_data, 0, RAMDISK_SIZE);

    printf("Ready.\n");
    fflush(stdout);

    return bius_main(&operations, &options);
}
