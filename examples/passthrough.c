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

int main(int argc, char *argv[]) {
    struct bius_operations operations = {
        .read = passthrough_read,
        .write = passthrough_write,
        .discard = passthrough_discard,
        .flush = passthrough_flush,
    };
    struct bius_block_device_options options = {
        .model = BLK_ZONED_NONE,
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

    if (ioctl(target_fd, BLKGETSIZE64, &options.disk_size) < 0) {
        fprintf(stderr, "ioctl BLKGETSIZE64 failed: %s\n", strerror(errno));
        return 1;
    }
    printd("disk_size = %lu\n", options.disk_size);

    strncpy(options.disk_name, "passthrough", MAX_DISK_NAME_LEN);

    return bius_main(&operations, &options);
}
