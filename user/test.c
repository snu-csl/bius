#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "command.h"
#include "utils.h"

char in_memory_data[1 * 1024 * 1024 * 1024];

int read_command(int fd, struct buse_k2u_header *header) {
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

int read_data(int fd, char *buffer, size_t size) {
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

int write_command(int fd, struct buse_u2k_header *header) {
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

int write_data(int fd, char *buffer, size_t size) {
    ssize_t total_written = 0;

    while (total_written < size) {
        ssize_t written = write(fd, buffer, size - total_written);
        printd("data write: request = %lu, sent = %ld\n", size - total_written, written);
        if (written <= 0) {
            fprintf(stderr, "Writing failed: %ld\n", written);
            return -1;
        }
        total_written += written;
    }

    return total_written;
}

int main(int argc, char **argv) {
    struct buse_k2u_header k2u;
    struct buse_u2k_header u2k;
    int buse_char_dev = open("/dev/buse", O_RDWR);
    if (buse_char_dev < 0) {
        fprintf(stderr, "char dev open failed: %s\n", strerror(errno));
        return 1;
    }

    while (1) {
        int result = read_command(buse_char_dev, &k2u);
        printd("command read. id = %lu, opcode = %d, offset = %lu, length = %lu\n", k2u.id, k2u.opcode, k2u.offset, k2u.length);
        if (result < 0)
            return 1;

        switch (k2u.opcode) {
            case BUSE_READ:
                u2k.id = k2u.id;
                u2k.reply = k2u.length;

                result = write_command(buse_char_dev, &u2k);
                if (result < 0)
                    return 1;
                result = write_data(buse_char_dev, in_memory_data + k2u.offset, k2u.length);
                if (result < 0)
                    return 1;
                break;
            case BUSE_WRITE:
                result = read_data(buse_char_dev, in_memory_data + k2u.offset, k2u.length);
                if (result < 0)
                    return 1;
                break;
            default:
                fprintf(stderr, "Unknown opcode: %d\n", k2u.opcode);
                return 1;
        }
    }

    return 0;
}
