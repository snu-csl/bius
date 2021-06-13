#ifndef BUSE_REQUEST_TYPE_H
#define BUSE_REQUEST_TYPE_H

typedef enum buse_req {
    BUSE_CONNECT = 0,
    BUSE_DISCONNECT = 1,
    BUSE_READ = 2,
    BUSE_WRITE = 3,
    BUSE_DISCARD = 4,
    BUSE_IOCTL = 5,
    BUSE_FLUSH = 6,
    BUSE_REPORT_ZONES = 9,
    BUSE_ZONE_OPEN = 10,
    BUSE_ZONE_CLOSE = 11,
    BUSE_ZONE_FINISH = 12,
    BUSE_ZONE_APPEND = 13,
    BUSE_ZONE_RESET = 15,
    BUSE_ZONE_RESET_ALL = 17,
} buse_req_t;

#define BUSE_INVALID_OP ((buse_req_t)-1)

static inline bool is_blk_request(buse_req_t type) {
    return type != BUSE_REPORT_ZONES;
}

static inline bool request_may_have_data(buse_req_t type) {
    return type == BUSE_READ || type == BUSE_WRITE || type == BUSE_ZONE_APPEND;
}

#endif
