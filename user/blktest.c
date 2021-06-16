#define _GNU_SOURCE
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

enum test_mode {
    TEST_MODE_SIMPLE,
    TEST_MODE_HARD,
};

char *buffer;
const size_t buffer_size = 128 * 1024;
const int simple_test_seed = 541615336;
const size_t hard_test_size = 1lu * 1024 * 1024 * 1024;
const size_t hard_test_iteration = hard_test_size / buffer_size;

static inline char pattern(int seed, int index) {
    return (char)(index + seed * index);
}

int do_test_simple(int fd) {
    fprintf(stderr, "Writing test: simple\n");

    for (int i = 0; i < buffer_size; i++)
        buffer[i] = pattern(simple_test_seed, i);

    ssize_t result = write(fd, buffer, buffer_size);
    fprintf(stderr, "Write result = %ld, %s\n", result, strerror(errno));
    if (result <= 0)
        return 1;

    result = lseek(fd, 0, SEEK_SET);
    if (result < 0) {
        fprintf(stderr, "lseek failed: %s\n", strerror(errno));
        return 1;
    }

    result = read(fd, buffer, buffer_size);
    fprintf(stderr, "Read result = %ld, %s\n", result, strerror(errno));
    if (result <= 0)
        return 1;

    for (int i = 0; i < buffer_size; i++) {
        if (buffer[i] != pattern(simple_test_seed, i)) {
            fprintf(stderr, "Verification failed at %d, (buffer[%d] = %d) != %d\n", i, i, buffer[i], (char)i);
            return 1;
        }
    }

    fprintf(stderr, "Verification ok\n");

    return 0;
}

int do_test_hard(int fd) {
    ssize_t result;
    fprintf(stderr, "Writing test: hard\n");

    for (size_t i = 0; i < hard_test_iteration; i++) {
        for (int j = 0; j < buffer_size; j++)
            buffer[j] = pattern(i, j);

        result = write(fd, buffer, buffer_size);
        if (result <= 0) {
            fprintf(stderr, "Write failed: %s\n", strerror(errno));
            return 1;
        } else if (result < buffer_size) {
            fprintf(stderr, "Incomplete write: %ld\n", result);
            return 1;
        }
    }

    result = lseek(fd, 0, SEEK_SET);
    if (result < 0) {
        fprintf(stderr, "lseek failed: %s\n", strerror(errno));
        return 1;
    }

    for (size_t i = 0; i < hard_test_iteration; i++) {
        result = read(fd, buffer, buffer_size);
        if (result <= 0) {
            fprintf(stderr, "Read failed: %s\n", strerror(errno));
            return 1;
        } else if (result < buffer_size) {
            fprintf(stderr, "Incomplete read: %ld\n", result);
            return 1;
        }

        for (int j = 0; j < buffer_size; j++) {
            if (buffer[j] != pattern(i, j)) {
                fprintf(stderr, "Verification failed at (%lu, %d), (buffer = %d) != %d\n", i, j, buffer[j], pattern(i, j));
                return 1;
            }
        }
    }

    fprintf(stderr, "Verification ok\n");

    return 0;
}

int main(int argc, char **argv) {
    int fd = open("/dev/buse-block", O_RDWR | O_DIRECT);
    enum test_mode mode = TEST_MODE_SIMPLE;
    if (fd < 0) {
        fprintf(stderr, "open failed: %s\n", strerror(errno));
        return 1;
    }

    if (argc >= 2) {
        if (strcmp(argv[1], "--hard") == 0)
            mode = TEST_MODE_HARD;
    }

    buffer = aligned_alloc(4096, buffer_size);
    if (buffer == NULL) {
        fprintf(stderr, "buffer allocation failed: %s\n", strerror(errno));
        return 1;
    }

    switch (mode) {
        case TEST_MODE_SIMPLE:
            return do_test_simple(fd);
        case TEST_MODE_HARD:
            return do_test_hard(fd);
        default:
            fprintf(stderr, "Mode error: %d\n", mode);
            return 1;
    }
}
