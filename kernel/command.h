#ifndef BUSE_COMMAND_H
#define BUSE_COMMAND_H

#include <linux/bio.h>
#include "config.h"
#include "request.h"
#include "utils.h"

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
};

inline ssize_t buse_send_command(struct buse_connection *connection, struct buse_request *request, struct iov_iter *to) {
    struct buse_k2u_header header = {
        .id = request->id,
        .opcode = request->type,
        .offset = request->pos,
        .length = request->length,
        .data_address = 0,
    };

    if (request->is_data_mapped)
        header.data_address = connection->vma->vm_start;

    switch (request->type) {
        case BUSE_READ:
        case BUSE_WRITE:
            return copy_to_iter(&header, sizeof(header), to);
        default:
            return -EINVAL;
    }
}

#endif
