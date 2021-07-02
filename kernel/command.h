#ifndef BIUS_COMMAND_H
#define BIUS_COMMAND_H

#include <linux/bio.h>
#include <bius/command_header.h>
#include <bius/config.h>
#include "request.h"
#include "utils.h"

inline ssize_t bius_send_command(struct bius_connection *connection, struct bius_request *request, struct iov_iter *to) {
    struct bius_k2u_header header = {
        .id = request->id,
        .opcode = request->type,
        .offset = request->pos,
        .length = request->length,
        .data_address = 0,
        .data_map_type = BIUS_DATAMAP_UNMAPPED,
    };

    if (is_blk_request(request->type) && request->map_type != BIUS_DATAMAP_UNMAPPED) {
        header.data_address = connection->vma->vm_start;
        header.mapping_data = request->map_data;
        header.data_map_type = request->map_type;
    }

    return copy_to_iter(&header, sizeof(header), to);
}

inline ssize_t bius_send_data(struct bius_request *request, size_t remain_buffer, struct iov_iter *to) {
    ssize_t total_sent = 0;
    struct bio_vec bvec;
    size_t size_to_send;
    void *data;

    if (unlikely(request->bio == NULL)) {
        return 0;
    } else if (unlikely(request->map_type != BIUS_DATAMAP_UNMAPPED)) {
        printk("bius: send_data called on data mapped request\n");
        return -EINVAL;
    }

    while (request->map_data > 0 && remain_buffer > 0) {
        bvec = mp_bio_iter_iovec(request->bio, request->bio->bi_iter);
        size_to_send = min_t(size_t, bvec.bv_len, remain_buffer);
        data = page_address(bvec.bv_page) + bvec.bv_offset;

        if (copy_to_iter(data, size_to_send, to) < size_to_send)
            return -EIO;

        remain_buffer -= size_to_send;
        total_sent += size_to_send;
        request->map_data -= size_to_send;
        bio_advance_iter(request->bio, &request->bio->bi_iter, size_to_send);

        if (request->bio->bi_iter.bi_size == 0) {
            request->bio = request->bio->bi_next;
        }
    }

    return total_sent;
}

inline int bius_receive_data(struct bius_request *request, char __user *user_data) {
    struct req_iterator iter;
    struct bio_vec bvec;

    if (unlikely(request->bio == NULL)) {
        return 0;
    } else if (unlikely(request->map_type != BIUS_DATAMAP_UNMAPPED)) {
        printk("bius: receive_data called on data mapped request\n");
        return -EINVAL;
    }

    rq_for_each_segment(bvec, blk_mq_rq_from_pdu(request), iter) {
        void *dest = page_address(bvec.bv_page) + bvec.bv_offset;
        unsigned long result = copy_from_user(dest, user_data, bvec.bv_len);

        if (unlikely(result > 0)) {
            printk("bius: receive_data: copy_from_user failed: %lu\n", result);
            return -EIO;
        }

        user_data += bvec.bv_len;
    }

    return 0;
}

#endif
