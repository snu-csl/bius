#ifndef BIUS_REQUEST_TYPE_H
#define BIUS_REQUEST_TYPE_H

#ifndef __KERNEL__
#include <stdbool.h>
#endif

typedef enum bius_req {
    BIUS_CONNECT = 0,
    BIUS_DISCONNECT = 1,
    BIUS_READ = 2,
    BIUS_WRITE = 3,
    BIUS_DISCARD = 4,
    BIUS_IOCTL = 5,
    BIUS_FLUSH = 6,
    BIUS_REPORT_ZONES = 9,
    BIUS_ZONE_OPEN = 10,
    BIUS_ZONE_CLOSE = 11,
    BIUS_ZONE_FINISH = 12,
    BIUS_ZONE_APPEND = 13,
    BIUS_ZONE_RESET = 15,
    BIUS_ZONE_RESET_ALL = 17,
} bius_req_t;

#define BIUS_INVALID_OP ((bius_req_t)-1)

static inline bool is_blk_request(bius_req_t type) {
    return type != BIUS_REPORT_ZONES;
}

static inline bool request_may_have_data(bius_req_t type) {
    return type == BIUS_READ || type == BIUS_WRITE || type == BIUS_ZONE_APPEND;
}

static inline bool request_is_write(bius_req_t type) {
    return type == BIUS_WRITE || type == BIUS_ZONE_APPEND;
}

#endif
