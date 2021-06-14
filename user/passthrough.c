#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "libbuse.h"

static int target_fd;

static ssize_t passthrough_read(void *data, off64_t offset, size_t length) {
    size_t total = 0;

    do {
        ssize_t result = pread(target_fd, data + total, length - total, offset + total);
        if (result < 0) {
            fprintf(stderr, "pread failed: %s\n", strerror(errno));
            return result;
        }

        total += result;
    } while (total < length);

    return total;
}

static ssize_t passthrough_write(const void *data, off64_t offset, size_t length) {
    size_t total = 0;

    do {
        ssize_t result = pwrite(target_fd, data + total, length - total, offset + total);
        if (result < 0) {
            fprintf(stderr, "pwrite failed: %s\n", strerror(errno));
            return result;
        }

        total += result;
    } while (total < length);

    return total;
}

int main(int argc, char *argv[]) {
    struct buse_operations operations = {
        .read = passthrough_read,
        .write = passthrough_write,
    };
    struct buse_options options = {
        .operations = &operations,
        .num_threads = 4,
    };

    if (argc < 2) {
        fprintf(stderr, "Target path not given.\n");
        return 1;
    }

    target_fd = open(argv[1], O_RDWR);
    if (target_fd < 0) {
        fprintf(stderr, "Target open failed: %s\n", strerror(errno));
        return 1;
    }

    return buse_main(&options);
}
