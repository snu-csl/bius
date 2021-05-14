#ifndef BUSE_CONNECTION_H
#define BUSE_CONNECTION_H

#include <linux/list.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include "request.h"

struct buse_connection {
    struct list_head pending_requests;
    spinlock_t pending_lock;
    /* List of requests waiting for userspace response */
    struct list_head waiting_requests;
    spinlock_t waiting_lock;
    wait_queue_head_t wait_queue;
    struct buse_request *sending;
    struct buse_request *receiving;
};

static inline struct buse_connection *get_buse_connection(struct file *file) {
    return (struct buse_connection *)file->private_data;
}

static inline void init_buse_connection(struct buse_connection *connection) {
    INIT_LIST_HEAD(&connection->pending_requests);
    spin_lock_init(&connection->pending_lock);
    INIT_LIST_HEAD(&connection->waiting_requests);
    spin_lock_init(&connection->waiting_lock);
    init_waitqueue_head(&connection->wait_queue);
    connection->sending = NULL;
    connection->receiving = NULL;
}

#endif
