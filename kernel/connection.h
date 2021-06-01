#ifndef BUSE_CONNECTION_H
#define BUSE_CONNECTION_H

#include <linux/list.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include "block_dev.h"
#include "request.h"

struct buse_connection {
    struct buse_block_device *block_dev;
    /* List of requests waiting for userspace response */
    struct list_head waiting_requests;
    spinlock_t waiting_lock;
    struct buse_request *sending;
    struct buse_request *receiving;
};

static inline struct buse_connection *get_buse_connection(struct file *file) {
    return (struct buse_connection *)file->private_data;
}

static inline void init_buse_connection(struct buse_connection *connection) {
    INIT_LIST_HEAD(&connection->waiting_requests);
    spin_lock_init(&connection->waiting_lock);
    connection->sending = NULL;
    connection->receiving = NULL;
}

#endif
