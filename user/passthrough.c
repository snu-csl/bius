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
    return pread(target_fd, data, length, offset);
}

static ssize_t passthrough_write(const void *data, off64_t offset, size_t length) {
    return pwrite(target_fd, data, length, offset);
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
