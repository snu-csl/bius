#ifndef BUSE_COMMAND_H
#define BUSE_COMMAND_H

#include "request.h"

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

inline ssize_t buse_send_command(struct buse_request *request, struct iov_iter *to) {
    struct buse_k2u_header header = {
        .id = request->id,
        .opcode = request->type,
        .offset = request->pos,
        .length = request->length,
    };

    switch (request->type) {
        case BUSE_READ:
        case BUSE_WRITE:
            return copy_to_iter(&header, sizeof(header), to);
        default:
            return -EINVAL;
    }
}

inline ssize_t buse_send_data(struct buse_request *request, size_t remain_buffer, struct iov_iter *to) {
    ssize_t total_sent = 0;
    struct bio_vec *bvec;
    size_t bv_remain;
    void *data;

    if (request->bio == NULL)
        return 0;

    while (request->bio) {
        bvec = request->bio->bi_io_vec;
        bv_remain = request->bv_remain;
        data = page_address(bvec->bv_page) + bvec->bv_offset + bvec->bv_len - request->bv_remain;

        if (remain_buffer >= bv_remain) {
            if (copy_to_iter(data, bv_remain, to) < bv_remain)
                return -EIO;
            remain_buffer -= bv_remain;
            total_sent += bv_remain;

            request->bio = request->bio->bi_next;
            request->bv_remain = request->bio? request->bio->bi_io_vec->bv_len : 0;
        } else {
            if (copy_to_iter(data, remain_buffer, to) < remain_buffer)
                return -EIO;
            total_sent += bv_remain;

            request->bv_remain -= remain_buffer;
            break;
        }
    }
    
    return total_sent;
}

inline ssize_t buse_receive_data(struct buse_request *request, size_t remain_buffer, struct iov_iter *from) {
    ssize_t total_received = 0;
    struct bio_vec *bvec;
    size_t bv_remain;
    void *data;

    if (request->bio == NULL)
        return 0;

    while (request->bio) {
        bvec = request->bio->bi_io_vec;
        bv_remain = request->bv_remain;
        data = page_address(bvec->bv_page) + bvec->bv_offset + bvec->bv_len - request->bv_remain;

        if (remain_buffer >= bv_remain) {
            if (copy_from_iter(data, bv_remain, from) < bv_remain)
                return -EIO;
            remain_buffer -= bv_remain;
            total_received += bv_remain;

            request->bio = request->bio->bi_next;
            request->bv_remain = request->bio? request->bio->bi_io_vec->bv_len : 0;
        } else {
            if (copy_from_iter(data, remain_buffer, from) < remain_buffer)
                return -EIO;
            total_received += bv_remain;

            request->bv_remain -= remain_buffer;
            break;
        }
    }

    return total_received;
}

#endif
