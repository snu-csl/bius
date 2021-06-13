#ifndef LIBBUSE_H
#define LIBBUSE_H

#include <linux/blkzoned.h>
#include <sys/types.h>
#include "blk_status.h"

#define BUSE_DEFAULT_NUM_THREADS 4
#define SECTOR_SIZE 512

struct buse_operations {
    blk_status_t (*read)(void *data, off64_t offset, size_t length);
    blk_status_t (*write)(const void *data, off64_t offset, size_t length);
    blk_status_t (*discard)(off64_t offset, size_t length);
    blk_status_t (*flush)();
    int (*report_zones)(off64_t offset, int nr_zones, struct blk_zone *zones);
    blk_status_t (*open_zone)(off64_t offset);
    blk_status_t (*close_zone)(off64_t offset);
    blk_status_t (*finish_zone)(off64_t offset);
    blk_status_t (*append_zone)(const void *data, off64_t offset, size_t length, off64_t *out_written_position);
    blk_status_t (*reset_zone)(off64_t offset);
    blk_status_t (*reset_all_zone)();
};

struct buse_options {
    const struct buse_operations *operations;
    size_t num_threads;
};

int buse_main(const struct buse_options *options);

#endif
