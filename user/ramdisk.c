#include <string.h>
#include "libbuse.h"

char in_memory_data[1 * 1024 * 1024 * 1024];

static ssize_t ramdisk_read(void *data, off64_t offset, size_t length) {
    memcpy(data, in_memory_data + offset, length);
    return length;
}

static ssize_t ramdisk_write(const void *data, off64_t offset, size_t length) {
    memcpy(in_memory_data + offset, data, length);
    return length;
}

int main(int argc, char *argv[]) {
    struct buse_operations operations = {
        .read = ramdisk_read,
        .write = ramdisk_write,
    };
    struct buse_options options = {
        .operations = &operations,
        .num_threads = 4,
    };

    return buse_main(&options);
}
