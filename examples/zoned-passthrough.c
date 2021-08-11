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
#include "libbius.h"
#include "utils.h"


#define ZONE_SIZE (512 * 1024 * 1024)

static int target_fd;

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

size_t zoned_disk_size;
size_t zone_size = ZONE_SIZE;
unsigned int num_conventional_zones = 0;
unsigned int max_open_zones = 32;
unsigned int max_active_zones = 64;
blk_status_t (*raw_read)(void *data, off64_t offset, size_t length) = passthrough_read;
blk_status_t (*raw_write)(const void *data, off64_t offset, size_t length) = passthrough_write;
blk_status_t (*raw_discard)(off64_t offset, size_t length) = passthrough_discard;

#include "zoned-common.h"

int main(int argc, char *argv[]) {
    struct bius_operations operations;
    get_zoned_operations(&operations);
    operations.flush = passthrough_flush;

    struct bius_block_device_options options = {
        .model = BLK_ZONED_HM,
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

    if (ioctl(target_fd, BLKGETSIZE64, &zoned_disk_size) < 0) {
        fprintf(stderr, "ioctl BLKGETSIZE64 failed: %s\n", strerror(errno));
        return 1;
    }
    zoned_disk_size = zoned_disk_size - (zoned_disk_size % ZONE_SIZE);
    printd("disk_size = %lu, num_zones = %lu\n", zoned_disk_size, num_zones);

    options.disk_size = zoned_disk_size;
    strncpy(options.disk_name, "zoned-passthrough", MAX_DISK_NAME_LEN);

    initialize();

    return bius_main(&operations, &options);
}
