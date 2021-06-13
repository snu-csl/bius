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
#include <stdbool.h>
#include "libbuse.h"
#include "utils.h"

static int target_fd;

#define ZONE_SIZE (512 * 1024 * 1024)

uint64_t disk_size;
uint64_t num_zones;

struct blk_zone *zone_info;
pthread_spinlock_t *zone_locks;

static void initialize_zone_info(bool hold_lock) {
    for (int i = 0; i < num_zones; i++) {
        if (hold_lock)
            pthread_spin_lock(&zone_locks[i]);

        zone_info[i].start = ZONE_SIZE / SECTOR_SIZE * i;
        zone_info[i].len = ZONE_SIZE / SECTOR_SIZE;
        zone_info[i].wp = ZONE_SIZE / SECTOR_SIZE * i;
        zone_info[i].type = BLK_ZONE_TYPE_SEQWRITE_REQ;
        zone_info[i].cond = BLK_ZONE_COND_EMPTY;
        zone_info[i].capacity = ZONE_SIZE / SECTOR_SIZE;

        if (i == num_zones - 1) {
            zone_info[i].capacity = disk_size / SECTOR_SIZE - zone_info[i].start;
        }

        if (hold_lock)
            pthread_spin_unlock(&zone_locks[i]);
    }
}

static void initialize() {
    zone_info = malloc(sizeof(struct blk_zone) * num_zones);
    zone_locks = malloc(sizeof(pthread_spinlock_t) * num_zones);
    if (zone_info == NULL || zone_locks == NULL) {
        fprintf(stderr, "zone info malloc failed, num_zones = %lu\n", num_zones);
    }

    memset(zone_info, 0, sizeof(struct blk_zone) * num_zones);

    initialize_zone_info(false);

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

static blk_status_t passthrough_write_common(const void *data, off64_t offset, size_t length, off64_t *out_written_position, bool append) {
    blk_status_t result = BLK_STS_OK;
    int zone = zone_number(offset);
    ssize_t written_size = 0;

    pthread_spin_lock(&zone_locks[zone]);

    if (append) {
        offset = zone_info[zone].wp * SECTOR_SIZE;
        if (out_written_position)
            *out_written_position = offset;
    } else if (zone_info[zone].wp * SECTOR_SIZE != offset) {
        result = BLK_STS_IOERR;
        goto out_unlock;
    }
    
    if ((zone_info[zone].start + zone_info[zone].capacity) * SECTOR_SIZE < offset + length) {
        result = BLK_STS_IOERR;
        goto out_unlock;
    }

    switch (zone_info[zone].cond) {
        case BLK_ZONE_COND_EMPTY:
        case BLK_ZONE_COND_CLOSED:
            zone_info[zone].cond = BLK_ZONE_COND_IMP_OPEN;
            break;
        case BLK_ZONE_COND_IMP_OPEN:
        case BLK_ZONE_COND_EXP_OPEN:
            break;
        default:
            result = BLK_STS_IOERR;
            goto out_unlock;
    }

    zone_info[zone].wp += length / SECTOR_SIZE;

    if (zone_info[zone].wp == zone_info[zone].start + zone_info[zone].capacity)
        zone_info[zone].cond = BLK_ZONE_COND_FULL;

    pthread_spin_unlock(&zone_locks[zone]);

    do {
        ssize_t result = pwrite(target_fd, data + written_size, length - written_size, offset + written_size);

        if (result <= 0) {
            fprintf(stderr, "pwrite failed: %s\n", strerror(errno));
            return BLK_STS_IOERR;
        }

        written_size += result;
    } while (written_size < length);

    return BLK_STS_OK;

out_unlock:
    pthread_spin_unlock(&zone_locks[zone]);
    return result;
}

static blk_status_t passthrough_write(const void *data, off64_t offset, size_t length) {
    return passthrough_write_common(data, offset, length, NULL, false);
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

static blk_status_t passthrough_open_zone(off64_t offset) {
    blk_status_t result = BLK_STS_OK;
    int zone = zone_number(offset);

    pthread_spin_lock(&zone_locks[zone]);

    switch (zone_info[zone].cond) {
        case BLK_ZONE_COND_EMPTY:
        case BLK_ZONE_COND_IMP_OPEN:
        case BLK_ZONE_COND_EXP_OPEN:
        case BLK_ZONE_COND_CLOSED:
            zone_info[zone].cond = BLK_ZONE_COND_EXP_OPEN;
            break;
        default:
            result = BLK_STS_IOERR;
            break;
    }

    pthread_spin_unlock(&zone_locks[zone]);

    return result;
}

static blk_status_t passthrough_close_zone(off64_t offset) {
    blk_status_t result = BLK_STS_OK;
    int zone = zone_number(offset);

    pthread_spin_lock(&zone_locks[zone]);

    switch (zone_info[zone].cond) {
        case BLK_ZONE_COND_IMP_OPEN:
        case BLK_ZONE_COND_EXP_OPEN:
        case BLK_ZONE_COND_CLOSED:
            if (zone_info[zone].wp == zone_info[zone].start)
                zone_info[zone].cond = BLK_ZONE_COND_EMPTY;
            else
                zone_info[zone].cond = BLK_ZONE_COND_CLOSED;
            break;
        default:
            result = BLK_STS_IOERR;
            break;
    }

    pthread_spin_unlock(&zone_locks[zone]);

    return result;
}

static blk_status_t passthrough_finish_zone(off64_t offset) {
    blk_status_t result = BLK_STS_OK;
    int zone = zone_number(offset);

    pthread_spin_lock(&zone_locks[zone]);

    switch (zone_info[zone].cond) {
        case BLK_ZONE_COND_EMPTY:
        case BLK_ZONE_COND_IMP_OPEN:
        case BLK_ZONE_COND_EXP_OPEN:
        case BLK_ZONE_COND_CLOSED:
        case BLK_ZONE_COND_FULL:
            zone_info[zone].cond = BLK_ZONE_COND_FULL;
            zone_info[zone].wp = zone_info[zone].start + zone_info[zone].len;
            break;
        default:
            result = BLK_STS_IOERR;
            break;
    }

    pthread_spin_unlock(&zone_locks[zone]);

    return result;
}

static blk_status_t passthrough_append_zone(const void *data, off64_t offset, size_t length, off64_t *out_written_position) {
    return passthrough_write_common(data, offset, length, out_written_position, true);
}

static blk_status_t passthrough_reset_zone(off64_t offset) {
    blk_status_t result = BLK_STS_OK;
    int zone = zone_number(offset);
    off64_t discard_start;
    size_t discard_length;

    pthread_spin_lock(&zone_locks[zone]);

    switch (zone_info[zone].cond) {
        case BLK_ZONE_COND_EMPTY:
            goto out_unlock;
        case BLK_ZONE_COND_IMP_OPEN:
        case BLK_ZONE_COND_EXP_OPEN:
        case BLK_ZONE_COND_CLOSED:
        case BLK_ZONE_COND_FULL:
            zone_info[zone].cond = BLK_ZONE_COND_EMPTY;
            zone_info[zone].wp = zone_info[zone].start;
            break;
        default:
            result = BLK_STS_IOERR;
            goto out_unlock;
    }

    discard_start = (off64_t)zone_info[zone].start * SECTOR_SIZE;
    discard_length = (size_t)zone_info[zone].len * SECTOR_SIZE;

    pthread_spin_unlock(&zone_locks[zone]);

    return passthrough_discard(discard_start, discard_length);

out_unlock:
    pthread_spin_unlock(&zone_locks[zone]);
    return result;
}

static blk_status_t passthrough_reset_all_zone() {
    initialize_zone_info(true);
    return passthrough_discard(0, disk_size);
}

int main(int argc, char *argv[]) {
    struct buse_operations operations = {
        .read = passthrough_read,
        .write = passthrough_write,
        .discard = passthrough_discard,
        .flush = passthrough_flush,
        .report_zones = passthrough_report_zones,
        .open_zone = passthrough_open_zone,
        .close_zone = passthrough_close_zone,
        .finish_zone = passthrough_finish_zone,
        .append_zone = passthrough_append_zone,
        .reset_zone = passthrough_reset_zone,
        .reset_all_zone = passthrough_reset_all_zone,
    };
    struct buse_options options = {
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

    initialize();

    return buse_main(&options);
}
