#ifndef ZONED_COMMON_H
#define ZONED_COMMON_H

#include <pthread.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include "libbius.h"
#include "utils.h"

extern size_t zoned_disk_size;
extern size_t zone_size;
extern unsigned int num_conventional_zones;
#define num_zones (zoned_disk_size / zone_size)
extern unsigned int max_open_zones;
extern unsigned int max_active_zones;
extern blk_status_t (*raw_read)(void *data, off64_t offset, size_t length);
extern blk_status_t (*raw_write)(const void *data, off64_t offset, size_t length);
extern blk_status_t (*raw_discard)(off64_t offset, size_t length);

struct zone_stat {
    unsigned long written;
    unsigned long read;
    unsigned long reset_count;
    unsigned long discard_count;
};

pthread_spinlock_t global_lock;
unsigned int num_open_zones;
unsigned int num_imp_open_zones;
unsigned int num_active_zones;

pthread_spinlock_t *zone_locks;
struct blk_zone *zone_info;
struct zone_stat *stats;

static void initialize_zone_info() {
    memset(zone_info, 0, sizeof(struct blk_zone) * num_zones);

    for (int i = 0; i < num_conventional_zones; i++ ) {
        zone_info[i].start = ZONE_SIZE / SECTOR_SIZE * i;
        zone_info[i].len = ZONE_SIZE / SECTOR_SIZE;
        zone_info[i].wp = ZONE_SIZE / SECTOR_SIZE * i;
        zone_info[i].type = BLK_ZONE_TYPE_CONVENTIONAL;
        zone_info[i].cond = BLK_ZONE_COND_NOT_WP;
        zone_info[i].capacity = ZONE_SIZE / SECTOR_SIZE;
    }

    for (int i = num_conventional_zones; i < num_zones; i++) {
        zone_info[i].start = ZONE_SIZE / SECTOR_SIZE * i;
        zone_info[i].len = ZONE_SIZE / SECTOR_SIZE;
        zone_info[i].wp = ZONE_SIZE / SECTOR_SIZE * i;
        zone_info[i].type = BLK_ZONE_TYPE_SEQWRITE_REQ;
        zone_info[i].cond = BLK_ZONE_COND_EMPTY;
        zone_info[i].capacity = ZONE_SIZE / SECTOR_SIZE;
    }

    num_open_zones = 0;
    num_imp_open_zones = 0;
    num_active_zones = 0;
}

static void initialize() {
    int error;

    error = pthread_spin_init(&global_lock, PTHREAD_PROCESS_PRIVATE);
    if (error < 0) {
        fprintf(stderr, "pthread_spin_init failed: %s\n", strerror(error));
        exit(1);
    }

    zone_locks = malloc(sizeof(pthread_spinlock_t) * num_zones);
    zone_info = malloc(sizeof(struct blk_zone) * num_zones);
    stats = malloc(sizeof(struct zone_stat) * num_zones);
    if (zone_locks == NULL || zone_info == NULL || stats == NULL) {
        fprintf(stderr, "malloc failed\n");
        exit(1);
    }

    for (int i = 0; i < num_zones; i++) {
        int error = pthread_spin_init(&zone_locks[i], PTHREAD_PROCESS_PRIVATE);
        if (error < 0) {
            fprintf(stderr, "pthread_spin_init failed: %s\n", strerror(error));
            exit(1);
        }
    }
    initialize_zone_info();
    memset(stats, 0, sizeof(struct zone_stat) * num_zones);
}

static inline unsigned int zone_number(off64_t offset) {
    return offset / ZONE_SIZE;
}

static inline void close_imp_open_zone(unsigned int zone_to_skip) {
    for (unsigned int zone = num_conventional_zones; zone < num_zones; zone++) {
        if (zone == zone_to_skip)
            continue;

        pthread_spin_lock(&zone_locks[zone]);
        if (zone_info[zone].cond == BLK_ZONE_COND_IMP_OPEN) {
            zone_info[zone].cond = BLK_ZONE_COND_CLOSED;

            num_open_zones--;
            num_imp_open_zones--;
            pthread_spin_unlock(&zone_locks[zone]);
            break;
        }
        pthread_spin_unlock(&zone_locks[zone]);
    }
}

static inline blk_status_t open_zone_locked(unsigned int zone, bool explicit) {
    blk_status_t result = BLK_STS_OK;

    pthread_spin_lock(&global_lock);

    switch (zone_info[zone].cond) {
        case BLK_ZONE_COND_IMP_OPEN:
            if (explicit) {
                num_imp_open_zones--;
                zone_info[zone].cond = BLK_ZONE_COND_EXP_OPEN;
            }
            break;
        case BLK_ZONE_COND_EXP_OPEN:
            break;
        case BLK_ZONE_COND_EMPTY:
            if (num_active_zones >= max_active_zones) {
                result = BLK_STS_ZONE_ACTIVE_RESOURCE;
                break;
            }
            num_active_zones++;
        case BLK_ZONE_COND_CLOSED:
            if (num_open_zones >= max_open_zones) {
                if (num_imp_open_zones > 0) {
                    close_imp_open_zone(zone);
                } else {
                    result = BLK_STS_ZONE_OPEN_RESOURCE;
                }
                break;
            }
            num_open_zones++;

            if (explicit) {
                zone_info[zone].cond = BLK_ZONE_COND_EXP_OPEN;
            } else {
                num_imp_open_zones++;
                zone_info[zone].cond = BLK_ZONE_COND_IMP_OPEN;
            }
            break;
        default:
            result = BLK_STS_IOERR;
    }

    pthread_spin_unlock(&global_lock);

    return result;
}

static inline blk_status_t mark_zone_full_locked(unsigned int zone) {
    blk_status_t result = BLK_STS_OK;

    pthread_spin_lock(&global_lock);

    switch (zone_info[zone].cond) {
        case BLK_ZONE_COND_IMP_OPEN:
            num_imp_open_zones--;
        case BLK_ZONE_COND_EXP_OPEN:
            num_open_zones--;
        case BLK_ZONE_COND_CLOSED:
            num_active_zones--;
        case BLK_ZONE_COND_EMPTY:
        case BLK_ZONE_COND_FULL:
            zone_info[zone].cond = BLK_ZONE_COND_FULL;
            zone_info[zone].wp = zone_info[zone].start + zone_info[zone].len;
            break;
        default:
            result = BLK_STS_IOERR;
            break;
    }

    pthread_spin_unlock(&global_lock);

    return result;
}

static blk_status_t zoned_read(void *data, off64_t offset, size_t length) {
    int zone = zone_number(offset);

    pthread_spin_lock(&zone_locks[zone]);
    stats[zone].read += length;
    pthread_spin_unlock(&zone_locks[zone]);

    return raw_read(data, offset, length);
}

static blk_status_t zoned_write_common(const void *data, off64_t offset, size_t length, off64_t *out_written_position, bool append) {
    blk_status_t result = BLK_STS_OK;
    unsigned int zone = zone_number(offset);

    pthread_spin_lock(&zone_locks[zone]);

    if (zone_info[zone].type == BLK_ZONE_TYPE_CONVENTIONAL) {
        if (append) {
            result = BLK_STS_IOERR;
            goto out_unlock;
        }
    } else {
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
                result = open_zone_locked(zone, false);
                if (result != BLK_STS_OK)
                    goto out_unlock;
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
            mark_zone_full_locked(zone);
    }

    stats[zone].written += length;

    pthread_spin_unlock(&zone_locks[zone]);

    return raw_write(data, offset, length);

out_unlock:
    pthread_spin_unlock(&zone_locks[zone]);
    return result;
}

static blk_status_t zoned_write(const void *data, off64_t offset, size_t length) {
    return zoned_write_common(data, offset, length, NULL, false);
}

static int zoned_report_zones(off64_t offset, int nr_zones, struct blk_zone *zones) {
    int start_zone = zone_number(offset);

    nr_zones = min(nr_zones, num_zones - start_zone);

    for (int i = 0; i < nr_zones; i++) {
        pthread_spin_lock(&zone_locks[start_zone + i]);
        memcpy(&zones[i], &zone_info[start_zone + i], sizeof(struct blk_zone));
        pthread_spin_unlock(&zone_locks[start_zone + i]);
    }

    return nr_zones;
}

static blk_status_t zoned_open_zone(off64_t offset) {
    blk_status_t result = BLK_STS_OK;
    unsigned int zone = zone_number(offset);

    pthread_spin_lock(&zone_locks[zone]);

    switch (zone_info[zone].cond) {
        case BLK_ZONE_COND_EMPTY:
        case BLK_ZONE_COND_IMP_OPEN:
        case BLK_ZONE_COND_CLOSED:
            result = open_zone_locked(zone, true);
            break;
        case BLK_ZONE_COND_EXP_OPEN:
            break;
        default:
            result = BLK_STS_IOERR;
            break;
    }

    pthread_spin_unlock(&zone_locks[zone]);

    return result;
}

static blk_status_t zoned_close_zone(off64_t offset) {
    blk_status_t result = BLK_STS_OK;
    unsigned int zone = zone_number(offset);

    pthread_spin_lock(&zone_locks[zone]);
    pthread_spin_lock(&global_lock);

    switch (zone_info[zone].cond) {
        case BLK_ZONE_COND_IMP_OPEN:
            num_imp_open_zones--;
        case BLK_ZONE_COND_EXP_OPEN:
            num_open_zones--;
        case BLK_ZONE_COND_CLOSED:
            if (zone_info[zone].wp == zone_info[zone].start) {
                zone_info[zone].cond = BLK_ZONE_COND_EMPTY;
                num_active_zones--;
            } else {
                zone_info[zone].cond = BLK_ZONE_COND_CLOSED;
            }
            break;
        default:
            result = BLK_STS_IOERR;
            break;
    }

    pthread_spin_unlock(&global_lock);
    pthread_spin_unlock(&zone_locks[zone]);

    return result;
}

static blk_status_t zoned_finish_zone(off64_t offset) {
    blk_status_t result = BLK_STS_OK;
    unsigned int zone = zone_number(offset);

    pthread_spin_lock(&zone_locks[zone]);
    mark_zone_full_locked(zone);
    pthread_spin_unlock(&zone_locks[zone]);

    return result;
}

static blk_status_t zoned_append_zone(const void *data, off64_t offset, size_t length, off64_t *out_written_position) {
    return zoned_write_common(data, offset, length, out_written_position, true);
}

static blk_status_t zoned_reset_zone(off64_t offset) {
    blk_status_t result = BLK_STS_OK;
    unsigned int zone = zone_number(offset);

    pthread_spin_lock(&zone_locks[zone]);

    stats[zone].reset_count++;
    stats[zone].discard_count += (zone_info[zone].wp - zone_info[zone].start) * SECTOR_SIZE;

    pthread_spin_lock(&global_lock);

    switch (zone_info[zone].cond) {
        case BLK_ZONE_COND_EMPTY:
            goto out_unlock;
        case BLK_ZONE_COND_IMP_OPEN:
            num_imp_open_zones--;
        case BLK_ZONE_COND_EXP_OPEN:
            num_open_zones--;
        case BLK_ZONE_COND_CLOSED:
            num_active_zones--;
        case BLK_ZONE_COND_FULL:
            zone_info[zone].cond = BLK_ZONE_COND_EMPTY;
            zone_info[zone].wp = zone_info[zone].start;
            break;
        default:
            result = BLK_STS_IOERR;
            goto out_unlock;
    }

    pthread_spin_unlock(&global_lock);
    pthread_spin_unlock(&zone_locks[zone]);

    return raw_discard(zone_info[zone].start * SECTOR_SIZE, zone_info[zone].len * SECTOR_SIZE);

out_unlock:
    pthread_spin_unlock(&global_lock);
    pthread_spin_unlock(&zone_locks[zone]);
    return result;
}

static blk_status_t zoned_reset_all_zone() {
    pthread_spin_lock(&global_lock);
    initialize_zone_info();
    pthread_spin_unlock(&global_lock);
    return raw_discard(0, zoned_disk_size);
}

static inline void get_zoned_operations(struct bius_operations *out_operations) {
    struct bius_operations operations = {
        .read = zoned_read,
        .write = zoned_write,
        .discard = 0,
        .flush = 0,
        .report_zones = zoned_report_zones,
        .open_zone = zoned_open_zone,
        .close_zone = zoned_close_zone,
        .finish_zone = zoned_finish_zone,
        .append_zone = zoned_append_zone,
        .reset_zone = zoned_reset_zone,
        .reset_all_zone = zoned_reset_all_zone,
    };

    memcpy(out_operations, &operations, sizeof(struct bius_operations));
}

#endif // ZONED_COMMON_H
