#ifndef BUSE_REQUEST_H
#define BUSE_REQUEST_H

#include <linux/list.h>
#include <linux/blk_types.h>

typedef enum buse_req {
    BUSE_CONNECT = 0,
    BUSE_DISCONNECT = 1,
    BUSE_READ = 2,
    BUSE_WRITE = 3,
    BUSE_DISCARD = 4,
    BUSE_IOCTL = 5,
} buse_req_t;

struct buse_request {
    uint64_t id;
    buse_req_t type;
    loff_t pos;
    size_t length;
    struct list_head list;
    struct bio *bio;
    size_t bv_remain;
    int done;
};

static inline void end_request(struct buse_request *request) {
    request->done = 1;
}

static inline struct buse_request *get_request_by_id(struct list_head *list, uint64_t id) {
    struct buse_request *request;

    list_for_each_entry(request, list, list) {
        if (request->id == id)
            return request;
    }

    return NULL;
}

static inline int request_io_done(struct buse_request *request) {
    return request->bio == NULL;
}

#endif
