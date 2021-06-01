#ifndef BUSE_COMMAND_H
#define BUSE_COMMAND_H

#include <linux/bio.h>
#include "request.h"
#include "utils.h"

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
    struct bio_vec bvec;
    size_t size_to_send;
    void *data;

    if (request->bio == NULL)
        return 0;

    while (request->remain_length > 0 && remain_buffer > 0) {
        bvec = mp_bio_iter_iovec(request->bio, request->bio->bi_iter);
        size_to_send = min_t(size_t, bvec.bv_len, remain_buffer);
        data = page_address(bvec.bv_page) + bvec.bv_offset;

        if (copy_to_iter(data, size_to_send, to) < size_to_send)
            return -EIO;

        remain_buffer -= size_to_send;
        total_sent += size_to_send;
        request->remain_length -= size_to_send;
        bio_advance_iter(request->bio, &request->bio->bi_iter, size_to_send);

        if (request->bio->bi_iter.bi_size == 0) {
            request->bio = request->bio->bi_next;
        }
    }

    return total_sent;
}

inline ssize_t buse_receive_data(struct buse_request *request, size_t remain_buffer, struct iov_iter *from) {
    ssize_t total_received = 0;
    struct bio_vec bvec;
    size_t size_to_receive;
    void *data;

    if (request->bio == NULL)
        return 0;

    while (request->remain_length > 0 && remain_buffer > 0) {
        bvec = mp_bio_iter_iovec(request->bio, request->bio->bi_iter);
        size_to_receive = min_t(size_t, bvec.bv_len, remain_buffer);
        data = page_address(bvec.bv_page) + bvec.bv_offset;

        if (copy_from_iter(data, size_to_receive, from) < size_to_receive)
            return -EIO;

        remain_buffer -= size_to_receive;
        total_received += size_to_receive;
        request->remain_length -= size_to_receive;
        bio_advance_iter(request->bio, &request->bio->bi_iter, size_to_receive);

        if (request->bio->bi_iter.bi_size == 0) {
            request->bio = request->bio->bi_next;
        }
    }

    return total_received;
}

#endif
