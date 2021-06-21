#ifndef BIUS_COMMAND_H
#define BIUS_COMMAND_H

#include <stdint.h>
#include <sys/types.h>
#include <stdbool.h>
#include "../kernel/request_type.h"

typedef enum data_map_type {
    BIUS_DATAMAP_UNMAPPED = 0,
    BIUS_DATAMAP_SIMPLE = 1,
    BIUS_DATAMAP_LIST = 2,
} data_map_type_t;

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
