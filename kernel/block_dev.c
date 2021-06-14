#include <linux/types.h>
#include "block_dev.h"
#include "config.h"
#include "char_dev.h"
#include "request.h"
#include "utils.h"

atomic64_t next_request_id = ATOMIC64_INIT(0);
struct buse_block_device *only_device = NULL;

static blk_status_t buse_queue_rq(struct blk_mq_hw_ctx *hctx, const struct blk_mq_queue_data *bd) {
    struct request *rq = bd->rq;
    struct buse_request *buse_request = blk_mq_rq_to_pdu(rq);
    loff_t pos = blk_rq_pos(rq) << SECTOR_SHIFT;

    blk_mq_start_request(rq);

    if (only_device == NULL) {
        blk_mq_end_request(rq, BLK_STS_IOERR);
        return BLK_STS_IOERR;
    }

    buse_request->id = atomic64_inc_return(&next_request_id);
    buse_request->type = rq_data_dir(rq) == WRITE? BUSE_WRITE : BUSE_READ;
    buse_request->pos = pos;
    buse_request->remain_length = buse_request->length = blk_rq_bytes(rq);
    buse_request->bio = rq->bio;

    printd("buse: new_request: type = %d, pos = %lld, length = %ld\n", buse_request->type, pos, buse_request->length);

    spin_lock(&only_device->pending_lock);
    list_add_tail(&buse_request->list, &only_device->pending_requests);
    spin_unlock(&only_device->pending_lock);

    wake_up(&only_device->wait_queue);

    return BLK_STS_OK;
}

static void buse_complete_rq(struct request *rq) {
    struct buse_request *request = blk_mq_rq_to_pdu(rq);
    blk_mq_end_request(rq, request->result);
}

static const struct blk_mq_ops buse_mq_ops = {
    .queue_rq = buse_queue_rq,
    .complete = buse_complete_rq,
};

static void initialize_tag_set(struct blk_mq_tag_set *tag_set) {
    memset(tag_set, 0, sizeof(struct blk_mq_tag_set));
    tag_set->ops = &buse_mq_ops;
    tag_set->nr_hw_queues = 4;
    tag_set->queue_depth = 128;
    tag_set->numa_node = NUMA_NO_NODE;
    tag_set->cmd_size = sizeof(struct buse_request);
    tag_set->flags = BLK_MQ_F_SHOULD_MERGE | BLK_MQ_F_BLOCKING;
    tag_set->driver_data = NULL;
}

static int buse_open(struct block_device *bdev, fmode_t mode) {
    return 0;
}

static void buse_release(struct gendisk *disk, fmode_t mode) {
}

static int buse_ioctl(struct block_device *bdev, fmode_t mode, unsigned int cmd, unsigned long arg) {
    return 0;
}

struct block_device_operations buse_fops = {
    .owner = THIS_MODULE,
    .open = buse_open,
    .release = buse_release,
    .ioctl = buse_ioctl,
};

int create_block_device(const char *name) {
    const unsigned long device_size = 1lu * 1024 * 1024 * 1024;
    struct buse_block_device *buse_device;
    int ret = 0;

    buse_device = kmalloc(sizeof(struct buse_block_device), GFP_KERNEL);
    if (buse_device == NULL)
        return -ENOMEM;
    init_buse_block_device(buse_device);

    ret = register_blkdev(0, name);
    if (ret < 0) {
        printk("buse: register_blkdev failed: %d\n", ret);
        goto out_free_device;
    }
    buse_device->major = ret;

    buse_device->disk = alloc_disk(1);
    if (buse_device->disk == NULL) {
        printk("buse: alloc_disk failed\n");
        goto out_unregister;
    }

    initialize_tag_set(&buse_device->tag_set);
    ret = blk_mq_alloc_tag_set(&buse_device->tag_set);
    if (ret < 0) {
        printk("buse: blk_mq_alloc_tag_set failed: %d\n", ret);
        goto out_put_disk;
    }

    buse_device->q = blk_mq_init_queue(&buse_device->tag_set);
    if (IS_ERR(buse_device->q)) {
        ret = PTR_ERR(buse_device->q);
        goto out_free_tag_set;
    }

    buse_device->disk->queue = buse_device->q;

    blk_queue_flag_set(QUEUE_FLAG_NONROT, buse_device->disk->queue);
    blk_queue_flag_clear(QUEUE_FLAG_ADD_RANDOM, buse_device->disk->queue);
    buse_device->disk->queue->limits.discard_granularity = 0;
    buse_device->disk->queue->limits.discard_alignment = 0;
    blk_queue_max_discard_sectors(buse_device->disk->queue, 0);
    blk_queue_max_segment_size(buse_device->disk->queue, BUSE_MAX_SEGMENT_SIZE);
    blk_queue_max_segments(buse_device->disk->queue, BUSE_MAX_SEGMENTS);
    blk_queue_max_hw_sectors(buse_device->disk->queue, 65536);

    strncpy(buse_device->disk->disk_name, name, DISK_NAME_LEN);
    buse_device->disk->major = buse_device->major;
    buse_device->disk->first_minor = 1;
    buse_device->disk->fops = &buse_fops;
    buse_device->disk->private_data = NULL;

    /* TODO: FIX HERE */
    set_capacity(buse_device->disk, device_size / SECTOR_SIZE);

    add_disk(buse_device->disk);

    if (only_device == NULL)
        only_device = buse_device;

    return 0;

out_free_tag_set:
    blk_mq_free_tag_set(&buse_device->tag_set);

out_put_disk:
    put_disk(buse_device->disk);

out_unregister:
    unregister_blkdev(buse_device->major, name);

out_free_device:
    kfree(buse_device);

    return ret;
}

void remove_block_device(const char *name) {
    struct buse_block_device *buse_device = only_device;
    struct buse_request *request;

    if (buse_device == NULL)
        return;

    only_device = NULL;

    list_for_each_entry(request, &buse_device->pending_requests, list) {
        struct request *rq = blk_mq_rq_from_pdu(request);
        blk_mq_end_request(rq, BLK_STS_IOERR);
    }

    del_gendisk(buse_device->disk);
    blk_mq_free_tag_set(&buse_device->tag_set);
    put_disk(buse_device->disk);
    unregister_blkdev(buse_device->major, name);
}
