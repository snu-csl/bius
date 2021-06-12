#ifndef BUSE_REQUEST_H
#define BUSE_REQUEST_H

#include <linux/list.h>
#include <linux/blk_types.h>
#include <linux/blk-mq.h>
#include <linux/semaphore.h>
#include "request_type.h"

struct buse_request {
    uint64_t id;
    buse_req_t type;
    loff_t pos;
    size_t length;
    struct list_head list;
    union {
        struct {
            struct bio *bio;
            int is_data_mapped;
            blk_status_t blk_result;
        };
        struct {
            void *data;
            struct semaphore sem;
            int int_result;
        };
    };
    void (*on_request_end)(struct buse_request *);
};

static inline void end_blk_request(struct buse_request *request, blk_status_t result) {
    request->blk_result = result;
    request->on_request_end(request);
}

static inline void end_request_int(struct buse_request *request, int result) {
    request->int_result = result;
    request->on_request_end(request);
}

static inline struct buse_request *get_request_by_id(struct list_head *list, uint64_t id) {
    struct buse_request *request;

    list_for_each_entry(request, list, list) {
        if (request->id == id)
            return request;
    }

    return NULL;
}

#endif
