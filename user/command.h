#ifndef BUSE_COMMAND_H
#define BUSE_COMMAND_H

#include <stdint.h>
#include <sys/types.h>
#include "request_type.h"

struct buse_k2u_header {
    uint64_t id;
    buse_req_t opcode;
    uint64_t offset;
    uint64_t length;
    uint64_t data_address;
};

struct buse_u2k_header {
    uint64_t id;
    ssize_t reply;
    uint64_t user_data_address;
};

#endif
