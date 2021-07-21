#ifndef BIUS_BLOCK_DEV_H
#define BIUS_BLOCK_DEV_H

#include <linux/blkdev.h>
#include <linux/blk-mq.h>

#include <bius/command_header.h>

struct bius_block_device {
    int major;
    struct gendisk *disk;
    struct blk_mq_tag_set tag_set;
    struct request_queue *q;
    enum blk_zoned_model model;

    spinlock_t connection_lock;
    unsigned int num_connection;

    struct list_head pending_requests;
    spinlock_t pending_lock;
    wait_queue_head_t wait_queue;

    struct list_head disk_list;
};

int create_block_device(struct bius_block_device_options *options, struct bius_block_device **out_device);
void remove_block_device(const char *name);
void bius_revalidate(struct bius_block_device *device);
struct bius_block_device *get_block_device(const char *disk_name);

static inline void init_bius_block_device(struct bius_block_device *device) {
    spin_lock_init(&device->connection_lock);
    device->num_connection = 0;
    INIT_LIST_HEAD(&device->pending_requests);
    spin_lock_init(&device->pending_lock);
    init_waitqueue_head(&device->wait_queue);
    INIT_LIST_HEAD(&device->disk_list);
}

#endif
