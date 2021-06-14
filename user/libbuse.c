#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include "command.h"
#include "libbuse.h"
#include "utils.h"

static inline int read_command(int fd, struct buse_k2u_header *header) {
    ssize_t result = read(fd, header, sizeof(struct buse_k2u_header));
    if (result < 0) {
        fprintf(stderr, "Command reading failed: %s\n", strerror(errno));
    } else if (result == 0) {
        fprintf(stderr, "EOF returned while reading\n");
    } else if (result < sizeof(struct buse_k2u_header)) {
        fprintf(stderr, "Read size is smaller than header: %ld\n", result);
        result = -1;
    }

    return result;
}

static inline int read_data(int fd, char *buffer, size_t size) {
    ssize_t total_read = 0;

    while (total_read < size) {
        ssize_t read_size = read(fd, buffer, size - total_read);
        printd("data read: request = %lu, got = %ld\n", size - total_read, read_size);

        if (read_size <= 0) {
            fprintf(stderr, "Read failed: %ld\n", read_size);
            return read_size;
        }

        total_read += read_size;
        buffer += read_size;
    }

    return total_read;
}

static inline int write_command(int fd, const struct buse_u2k_header *header) {
    ssize_t result = write(fd, header, sizeof(struct buse_u2k_header));
    if (result < 0) {
        fprintf(stderr, "Reply writing failed: %s\n", strerror(errno));
    } else if (result == 0) {
        fprintf(stderr, "EOF returned while writing\n");
    } else if (result < sizeof(struct buse_u2k_header)) {
        fprintf(stderr, "Written size is smaller than header: %ld\n", result);
        result = -1;
    }

    return result;
}

static inline int write_data(int fd, const char *buffer, size_t size) {
    ssize_t total_written = 0;

    while (total_written < size) {
        ssize_t written = write(fd, buffer, size - total_written);
        printd("data write: request = %lu, sent = %ld\n", size - total_written, written);
        if (written <= 0) {
            fprintf(stderr, "Writing failed: %s\n", strerror(errno));
            return -1;
        }

        total_written += written;
        buffer += written;
    }

    return total_written;
}

static void *thread_main(void *arg) {
    const struct buse_options *options = arg;
    const struct buse_operations *ops = options->operations;
    char *buffer = aligned_alloc(4096, 128 * 1024);
    struct buse_k2u_header k2u;
    struct buse_u2k_header u2k;
    int buse_char_dev = open("/dev/buse", O_RDWR);
    if (buse_char_dev < 0) {
        fprintf(stderr, "char dev open failed: %s\n", strerror(errno));
        exit(1);
    }

    while (1) {
        int result = read_command(buse_char_dev, &k2u);
        printd("command read. id = %lu, opcode = %d, offset = %lu, length = %lu\n", k2u.id, k2u.opcode, k2u.offset, k2u.length);
        if (result < 0)
            exit(1);

        switch (k2u.opcode) {
            case BUSE_READ:
                u2k.id = k2u.id;
                u2k.reply = ops->read(buffer, k2u.offset, k2u.length);

                result = write_command(buse_char_dev, &u2k);
                if (result < 0)
                    exit(1);
                result = write_data(buse_char_dev, buffer, u2k.reply);
                if (result < 0)
                    exit(1);
                break;
            case BUSE_WRITE:
                result = read_data(buse_char_dev, buffer, k2u.length);
                if (result < 0)
                    exit(1);
                ops->write(buffer, k2u.offset, k2u.length);
                break;
            default:
                fprintf(stderr, "Unknown opcode: %d\n", k2u.opcode);
                exit(1);
        }
    }

    free(buffer);

    return NULL;
}

static inline int buse_main_real(const struct buse_options *options) {
    size_t num_threads = BUSE_DEFAULT_NUM_THREADS;
    int result = 0;
    pthread_t *threads;

    if (options->operations == NULL)
        return -EINVAL;
    if (options->num_threads != 0)
        num_threads = options->num_threads;

    threads = malloc(sizeof(pthread_t) * num_threads);
    if (threads == NULL)
        return -ENOMEM;

    for (int i = 0; i < num_threads; i++) {
        result = pthread_create(&threads[i], NULL, thread_main, (struct buse_options *)options);
        if (result < 0) {
            fprintf(stderr, "pthread_create failed: %s\n", strerror(result));
            goto out_free;
        }
    }

    for (int i = 0; i < num_threads; i++) {
        void *thread_result;

        result = pthread_join(threads[i], &thread_result);
        if (result < 0) {
            fprintf(stderr, "pthread_join failed: %s\n", strerror(result));
            goto out_free;
        }
    }

out_free:
    free(threads);

    return result;
}

int buse_main(const struct buse_options *options) {
    int result = buse_main_real(options);

    if (result < 0) {
        errno = result;
        return -1;
    } else {
        errno = 0;
        return result;
    }
}
