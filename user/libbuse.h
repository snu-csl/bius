#ifndef LIBBUSE_H
#define LIBBUSE_H

#include <sys/types.h>

#define BUSE_DEFAULT_NUM_THREADS 4

struct buse_operations {
    ssize_t (*read)(void *data, off64_t offset, size_t length);
    ssize_t (*write)(const void *data, off64_t offset, size_t length);
};

struct buse_options {
    const struct buse_operations *operations;
    size_t num_threads;
};

int buse_main(const struct buse_options *options);

#endif
