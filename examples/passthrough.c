#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <pthread.h>
#include "libbius.h"
#include "utils.h"

static int target_fd;

#define ZONE_SIZE (512 * 1024 * 1024)

uint64_t disk_size;
uint64_t num_zones;

struct blk_zone *zone_info;
pthread_spinlock_t *zone_locks;

static void initialize_zone_info() {
    zone_info = malloc(sizeof(struct blk_zone) * num_zones);
    zone_locks = malloc(sizeof(pthread_spinlock_t) * num_zones);
    if (zone_info == NULL || zone_locks == NULL) {
        fprintf(stderr, "zone info malloc failed, num_zones = %lu\n", num_zones);
    }

    memset(zone_info, 0, sizeof(struct blk_zone) * num_zones);

    for (int i = 0; i < num_zones; i++) {
        zone_info[i].start = ZONE_SIZE / SECTOR_SIZE * i;
        zone_info[i].len = ZONE_SIZE / SECTOR_SIZE;
        zone_info[i].wp = ZONE_SIZE / SECTOR_SIZE * i;
        zone_info[i].type = BLK_ZONE_TYPE_CONVENTIONAL;
        zone_info[i].cond = BLK_ZONE_COND_NOT_WP;
        zone_info[i].capacity = ZONE_SIZE / SECTOR_SIZE;

        if (i == num_zones - 1) {
            zone_info[i].capacity = disk_size / SECTOR_SIZE - zone_info[i].start;
        }
    }

    for (int i = 0; i < num_zones; i++) {
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

static blk_status_t passthrough_read(void *data, off64_t offset, size_t length) {
    ssize_t read_size = 0;

    do {
        ssize_t result = pread(target_fd, data + read_size, length - read_size, offset + read_size);

        if (result <= 0) {
            fprintf(stderr, "pread failed: %s\n", strerror(errno));
            return BLK_STS_IOERR;
        }

        read_size += result;
    } while (read_size < length);

    return BLK_STS_OK;
}

static blk_status_t passthrough_write(const void *data, off64_t offset, size_t length) {
    ssize_t written_size = 0;

    do {
        ssize_t result = pwrite(target_fd, data + written_size, length - written_size, offset + written_size);

        if (result <= 0) {
            fprintf(stderr, "pwrite failed: %s\n", strerror(errno));
            return BLK_STS_IOERR;
        }

        written_size += result;
    } while (written_size < length);

    return BLK_STS_OK;
}

static blk_status_t passthrough_discard(off64_t offset, size_t length) {
    uint64_t range[2] = {offset, length};

    if (ioctl(target_fd, BLKDISCARD, &range) < 0) {
        fprintf(stderr, "ioctl BLKDISCARD failed: %s\n", strerror(errno));
        return BLK_STS_IOERR;
    }

    return BLK_STS_OK;
}

static blk_status_t passthrough_flush() {
    if (fsync(target_fd) < 0) {
        fprintf(stderr, "fsync failed: %s\n", strerror(errno));
        return BLK_STS_IOERR;
    }

    return BLK_STS_OK;
}

static int passthrough_report_zones(off64_t offset, int nr_zones, struct blk_zone *zones) {
    int start_zone = zone_number(offset);

    nr_zones = min(nr_zones, num_zones - start_zone);

    for (int i = 0; i < nr_zones; i++) {
        pthread_spin_lock(&zone_locks[start_zone + i]);
        memcpy(&zones[i], &zone_info[start_zone + i], sizeof(struct blk_zone));
        pthread_spin_unlock(&zone_locks[start_zone + i]);
    }

    return nr_zones;
}

int main(int argc, char *argv[]) {
    struct bius_operations operations = {
        .read = passthrough_read,
        .write = passthrough_write,
        .discard = passthrough_discard,
        .flush = passthrough_flush,
        .report_zones = passthrough_report_zones,
    };
    struct bius_options options = {
        .operations = &operations,
        .num_threads = 4,
    };

    if (argc <  2) {
        fprintf(stderr, "Target path not given.\n");
        return 1;
    }

    target_fd = open(argv[1], O_RDWR);
    if (target_fd < 0) {
        fprintf(stderr, "Target open failed: %s\n", strerror(errno));
        return 1;
    }

    if (ioctl(target_fd, BLKGETSIZE64, &disk_size) < 0) {
        fprintf(stderr, "ioctl BLKGETSIZE64 failed: %s\n", strerror(errno));
        return 1;
    }
    disk_size = disk_size - (disk_size % ZONE_SIZE);
    num_zones = disk_size / ZONE_SIZE;
    printd("disk_size = %lu, num_zones = %lu\n", disk_size, num_zones);

    initialize_zone_info();

    return bius_main(&options);
}
