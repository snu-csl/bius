#include <linux/types.h>
#include "block_dev.h"
#include "char_dev.h"
#include "request.h"
#include "utils.h"

atomic64_t next_request_id = ATOMIC64_INIT(0);

static blk_status_t buse_queue_rq(struct blk_mq_hw_ctx *hctx, const struct blk_mq_queue_data *bd) {
    struct request *rq = bd->rq;
    struct buse_request *buse_request = blk_mq_rq_to_pdu(rq);
    loff_t pos = blk_rq_pos(rq) << SECTOR_SHIFT;

    blk_mq_start_request(rq);

    if (only_connection == NULL) {
        blk_mq_end_request(rq, BLK_STS_IOERR);
        return BLK_STS_IOERR;
    }

    buse_request->id = atomic64_inc_return(&next_request_id);
    buse_request->type = rq_data_dir(rq) == WRITE? BUSE_WRITE : BUSE_READ;
    buse_request->pos = pos;
    buse_request->length = blk_rq_bytes(rq);
    buse_request->bio = rq->bio;
    buse_request->bv_remain = rq->bio->bi_io_vec->bv_len;

    printd("buse: new_request: type = %d, pos = %lld, length = %ld\n", buse_request->type, pos, buse_request->length);

    spin_lock(&only_connection->pending_lock);
    list_add_tail(&buse_request->list, &only_connection->pending_requests);
    spin_unlock(&only_connection->pending_lock);

    wake_up(&only_connection->wait_queue);

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

unsigned int major;
struct gendisk *disk;
struct request_queue *q;

struct blk_mq_tag_set tag_set = {
    .ops = &buse_mq_ops,
    .nr_hw_queues = 1,
    .queue_depth = 128,
    .numa_node = NUMA_NO_NODE,
    .cmd_size = sizeof(struct buse_request),
    .flags = BLK_MQ_F_SHOULD_MERGE | BLK_MQ_F_BLOCKING,
    .driver_data = NULL,
};

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
    int ret = 0;

    major = register_blkdev(0, name);
    if (major < 0)
        return major;

    disk = alloc_disk(1);
    if (disk == NULL) {
        printk("buse: alloc_disk failed\n");
        goto out_unregister;
    }

    ret = blk_mq_alloc_tag_set(&tag_set);
    if (ret < 0) {
        printk("buse: blk_mq_alloc_tag_set failed: %d\n", ret);
        goto out_put_disk;
    }

    q = blk_mq_init_queue(&tag_set);
    if (IS_ERR(q)) {
        ret = PTR_ERR(q);
        goto out_free_tag_set;
    }

    disk->queue = q;

    blk_queue_flag_set(QUEUE_FLAG_NONROT, disk->queue);
    blk_queue_flag_clear(QUEUE_FLAG_ADD_RANDOM, disk->queue);
    disk->queue->limits.discard_granularity = 0;
    disk->queue->limits.discard_alignment = 0;
    blk_queue_max_discard_sectors(disk->queue, 0);
    blk_queue_max_segment_size(disk->queue, 0);
    blk_queue_max_segments(disk->queue, 0);
    blk_queue_max_hw_sectors(disk->queue, 65536);
    disk->queue->limits.max_sectors = 256;

    strncpy(disk->disk_name, name, DISK_NAME_LEN);
    disk->major = major;
    disk->first_minor = 1;
    disk->fops = &buse_fops;
    disk->private_data = NULL;

    /* TODO: FIX HERE */
    set_capacity(disk, 2 * 1024 * 1024 /* 1GB */);

    add_disk(disk);

    return 0;

out_free_tag_set:
    blk_mq_free_tag_set(&tag_set);

out_put_disk:
    put_disk(disk);

out_unregister:
    unregister_blkdev(major, name);

    return ret;
}

void remove_block_device(const char *name) {
    del_gendisk(disk);
    blk_mq_free_tag_set(&tag_set);
    put_disk(disk);
    unregister_blkdev(major, name);
}
