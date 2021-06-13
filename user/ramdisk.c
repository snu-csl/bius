#define _POSIX_C_SOURCE 200112L
#include <pthread.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "libbuse.h"
#include "utils.h"

#define RAMDISK_SIZE (1 * 1024 * 1024 * 1024)
#define ZONE_SIZE (32 * 1024 * 1024)
#define NUM_ZONES (RAMDISK_SIZE / ZONE_SIZE)

char in_memory_data[RAMDISK_SIZE];
struct blk_zone zone_info[NUM_ZONES];
pthread_spinlock_t zone_locks[NUM_ZONES];

static void initialize_zone_info() {
    for (int i = 0; i < NUM_ZONES; i++) {
        zone_info[i].start = ZONE_SIZE / SECTOR_SIZE * i;
        zone_info[i].len = ZONE_SIZE / SECTOR_SIZE;
        zone_info[i].wp = ZONE_SIZE / SECTOR_SIZE * i;
        zone_info[i].type = BLK_ZONE_TYPE_CONVENTIONAL;
        zone_info[i].cond = BLK_ZONE_COND_NOT_WP;
        zone_info[i].capacity = ZONE_SIZE / SECTOR_SIZE;
    }

   for (int i = 0; i < NUM_ZONES; i++) {
       int error = pthread_spin_init(&zone_locks[i], PTHREAD_PROCESS_PRIVATE);
       if (error < 0) {
           fprintf(stderr, "pthread_spin_init failed: %s\n", strerror(error));
           exit(1);
       }
   }
}

static inline int zone_number(off64_t offset) {
    return offset / ZONE_SIZE;
}

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

static int ramdisk_report_zones(off64_t offset, int nr_zones, struct blk_zone *zones) {
    int start_zone = zone_number(offset);

    nr_zones = min(nr_zones, NUM_ZONES - start_zone);

    for (int i = 0; i < nr_zones; i++) {
        pthread_spin_lock(&zone_locks[start_zone + i]);
        memcpy(&zones[i], &zone_info[start_zone + i], sizeof(struct blk_zone));
        pthread_spin_unlock(&zone_locks[start_zone + i]);
    }

    return nr_zones;
}

static blk_status_t ramdisk_open_zone(off64_t offset) {
    return BLK_STS_IOERR;
}

static blk_status_t ramdisk_close_zone(off64_t offset) {
    return BLK_STS_IOERR;
}

static blk_status_t ramdisk_finish_zone(off64_t offset) {
    return BLK_STS_IOERR;
}

static blk_status_t ramdisk_append_zone(const void *data, off64_t offset, size_t length, off64_t *out_written_position) {
    return BLK_STS_IOERR;
}

static blk_status_t ramdisk_reset_zone(off64_t offset) {
    return BLK_STS_IOERR;
}

static blk_status_t ramdisk_reset_all_zone() {
    return BLK_STS_IOERR;
}

int main(int argc, char *argv[]) {
    struct buse_operations operations = {
        .read = ramdisk_read,
        .write = ramdisk_write,
        .discard = ramdisk_discard,
        .flush = ramdisk_flush,
        .report_zones = ramdisk_report_zones,
        .open_zone = ramdisk_open_zone,
        .close_zone = ramdisk_close_zone,
        .finish_zone = ramdisk_finish_zone,
        .append_zone = ramdisk_append_zone,
        .reset_zone = ramdisk_reset_zone,
        .reset_all_zone = ramdisk_reset_all_zone,
    };
    struct buse_options options = {
        .operations = &operations,
        .num_threads = 4,
    };

    initialize_zone_info();

    return buse_main(&options);
}
