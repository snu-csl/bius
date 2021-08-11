#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE
#include <pthread.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include "libbius.h"
#include "utils.h"

#define RAMDISK_SIZE (32lu * 1024 * 1024 * 1024)
#define ZONE_SIZE (32 * 1024 * 1024)

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

size_t zoned_disk_size = RAMDISK_SIZE;
size_t zone_size = ZONE_SIZE;
unsigned int num_conventional_zones = 0;
unsigned int max_open_zones = 32;
unsigned int max_active_zones = 64;
blk_status_t (*raw_read)(void *data, off64_t offset, size_t length) = ramdisk_read;
blk_status_t (*raw_write)(const void *data, off64_t offset, size_t length) = ramdisk_write;
blk_status_t (*raw_discard)(off64_t offset, size_t length) = ramdisk_discard;

#include "zoned-common.h"

void sigint_handler(int signum) {
    unsigned long read_total = 0;
    unsigned long write_total = 0;
    unsigned long discard_total = 0;

    for (int i = 0; i < num_zones; i++) {
        pthread_spin_lock(&zone_locks[i]);
        unsigned long using_size = (zone_info[i].wp - zone_info[i].start) * SECTOR_SIZE;
        printf("zone %03d: read = %lu / write = %lu / reset = %lu / discard = %lu / using = %lu\n", i, stats[i].read, stats[i].written, stats[i].reset_count, stats[i].discard_count, using_size);
        read_total += stats[i].read;
        write_total += stats[i].written;
        discard_total += stats[i].discard_count;
        pthread_spin_unlock(&zone_locks[i]);
    }

    printf("total: read = %lu / write = %lu / discard = %lu\n\n", read_total, write_total, discard_total);
    fflush(stdout);
}

static void set_interrupt_handler() {
    struct sigaction sa = {
        .sa_handler = sigint_handler,
        .sa_flags = SA_RESTART,
    };

    if (sigemptyset(&sa.sa_mask) < 0) {
        fprintf(stderr, "sigemptyset failed: %s\n", strerror(errno));
        exit(1);
    }
    if (sigaction(SIGINT, &sa, NULL) < 0) {
        fprintf(stderr, "sigaction failed: %s\n", strerror(errno));
        exit(1);
    }
}

int main(int argc, char *argv[]) {
    struct bius_operations operations;
    get_zoned_operations(&operations);
    operations.flush = ramdisk_flush;

    struct bius_block_device_options options = {
        .model = BLK_ZONED_HM,
        .num_threads = 4,
        .disk_size = RAMDISK_SIZE
    };
    strncpy(options.disk_name, "zoned-ramdisk", MAX_DISK_NAME_LEN);

    set_interrupt_handler();
    initialize();
    memset(in_memory_data, 0, RAMDISK_SIZE);

    printf("Ready.\n");
    fflush(stdout);

    return bius_main(&operations, &options);
}
