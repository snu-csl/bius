#ifndef BUSE_BLOCK_DEV_H
#define BUSE_BLOCK_DEV_H

#include <linux/blkdev.h>
#include <linux/blk-mq.h>

struct buse_block_device {
    int major;
    struct gendisk *disk;
    struct blk_mq_tag_set tag_set;
    struct request_queue *q;

    spinlock_t connection_lock;
    unsigned int num_connection;
    int validated;

    struct list_head pending_requests;
    spinlock_t pending_lock;
    wait_queue_head_t wait_queue;
};

int create_block_device(const char *name);
void remove_block_device(const char *name);
int buse_revalidate(struct buse_block_device *device);

extern struct buse_block_device *only_device;

static inline void init_buse_block_device(struct buse_block_device *device) {
    spin_lock_init(&device->connection_lock);
    device->num_connection = 0;
    device->validated = 0;
    INIT_LIST_HEAD(&device->pending_requests);
    spin_lock_init(&device->pending_lock);
    init_waitqueue_head(&device->wait_queue);
}

#endif
