#ifndef BUSE_COMMAND_H
#define BUSE_COMMAND_H

#include <stdint.h>
#include <sys/types.h>

typedef enum buse_req {
    BUSE_CONNECT = 0,
    BUSE_DISCONNECT = 1,
    BUSE_READ = 2,
    BUSE_WRITE = 3,
    BUSE_DISCARD = 4,
    BUSE_IOCTL = 5,
} buse_req_t;

struct buse_k2u_header {
    uint64_t id;
    buse_req_t opcode;
    uint64_t offset;
    uint64_t length;
};

struct buse_u2k_header {
    uint64_t id;
    ssize_t reply;
};

#endif
