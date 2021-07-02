#ifndef BIUS_COMMAND_HEADER_H
#define BIUS_COMMAND_HEADER_H

#include <bius/request_type.h>
#ifndef __KERNEL__
#include <stdint.h>
#endif  // __KERNEL__

struct bius_k2u_header {
    uint64_t id;
    bius_req_t opcode;
    uint64_t offset;
    uint64_t length;
    uint64_t data_address;
    uint64_t mapping_data;
    int32_t data_map_type;
};

struct bius_u2k_header {
    uint64_t id;
    ssize_t reply;
    uint64_t user_data;
};

#endif