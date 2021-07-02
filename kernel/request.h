#ifndef BIUS_REQUEST_H
#define BIUS_REQUEST_H

#include <linux/list.h>
#include <linux/blk_types.h>
#include <linux/blk-mq.h>
#include <linux/semaphore.h>
#include "bius/map_type.h"
#include <bius/request_type.h>

struct bius_request {
    uint64_t id;
    bius_req_t type;
    loff_t pos;
    size_t length;
    struct list_head list;
    union {
        struct {
            struct bio *bio;
            /* Map type specific data. Offset of first page for simple, list address for list, remain length for copy */
            unsigned long map_data;
            unsigned long mapped_size;
            data_map_type_t map_type;
            blk_status_t blk_result;
        };
        struct {
            void *data;
            struct semaphore sem;
            int int_result;
        };
    };
    void (*on_request_end)(struct bius_request *);
};

static inline void end_blk_request(struct bius_request *request, blk_status_t result) {
    request->blk_result = result;
    request->on_request_end(request);
}

static inline void end_request_int(struct bius_request *request, int result) {
    request->int_result = result;
    request->on_request_end(request);
}

static inline struct bius_request *get_request_by_id(struct list_head *list, uint64_t id) {
    struct bius_request *request;

    list_for_each_entry(request, list, list) {
        if (request->id == id)
            return request;
    }

    return NULL;
}

static inline bool request_io_done(struct bius_request *request) {
    if (unlikely(request->map_type != BIUS_DATAMAP_UNMAPPED)) {
        printk("bius: request_io_done called on data mapped request\n");
        return true;
    }

    return request->map_data == 0;
}

#endif
