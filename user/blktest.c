#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

char buffer[128 * 1024];

int main(void) {
    fprintf(stderr, "Writing test\n");

    for (int i = 0; i < sizeof(buffer); i++)
        buffer[i] = (char)i;

    int fd = open("/dev/buse-block", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "open failed: %s\n", strerror(errno));
        return 1;
    }

    ssize_t res = write(fd, buffer, sizeof(buffer));
    fprintf(stderr, "Write result = %ld, %s\n", res, strerror(errno));
    if (res <= 0)
        return 1;

    res = lseek(fd, 0, SEEK_SET);
    if (res < 0) {
        fprintf(stderr, "lseek failed: %s\n", strerror(errno));
        return 1;
    }

    res = read(fd, buffer, sizeof(buffer));
    fprintf(stderr, "Read result = %ld, %s\n", res, strerror(errno));
    if (res <= 0)
        return 1;

    for (int i = 0; i < sizeof(buffer); i++) {
        if (buffer[i] != (char)i) {
            fprintf(stderr, "Verification failed at %d, (buffer[%d] = %d) != %d\n", i, i, buffer[i], (char)i);
            return 1;
        }
    }

    fprintf(stderr, "Verification ok\n");

    return 0;
}
