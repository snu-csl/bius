#include <linux/types.h>
#include "block_dev.h"
#include "config.h"
#include "char_dev.h"
#include "config.h"
#include "request.h"
#include "utils.h"

atomic64_t next_request_id = ATOMIC64_INIT(0);
struct buse_block_device *only_device = NULL;

static inline buse_req_t to_buse_request(unsigned int op) {
    switch (op) {
        case REQ_OP_READ:
            return BUSE_READ;
        case REQ_OP_WRITE:
            return BUSE_WRITE;
        case REQ_OP_FLUSH:
            return BUSE_FLUSH;
        case REQ_OP_DISCARD:
            return BUSE_DISCARD;
        case REQ_OP_ZONE_OPEN:
        case REQ_OP_ZONE_CLOSE:
        case REQ_OP_ZONE_FINISH:
        case REQ_OP_ZONE_APPEND:
        case REQ_OP_ZONE_RESET:
        case REQ_OP_ZONE_RESET_ALL:
            return (buse_req_t)op;
        default:
            return BUSE_INVALID_OP;
    }
}

static void buse_blk_request_end(struct buse_request *request) {
    struct request *rq = blk_mq_rq_from_pdu(request);
    blk_mq_end_request(rq, request->blk_result);
}

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
    buse_request->type = to_buse_request(req_op(rq));
    buse_request->pos = pos;
    buse_request->length = blk_rq_bytes(rq);
    buse_request->bio = rq->bio;
    buse_request->map_type = BUSE_DATAMAP_UNMAPPED;
    buse_request->on_request_end = buse_blk_request_end;

    printd("buse: new_request: type = %d, op = %d, pos = %lld, length = %ld\n", buse_request->type, req_op(bd->rq), pos, buse_request->length);

    if (unlikely(buse_request->type == BUSE_INVALID_OP)) {
        printk("buse: unsupported operation: %d\n", req_op(rq));
        blk_mq_end_request(rq, BLK_STS_NOTSUPP);
        return BLK_STS_NOTSUPP;
    }

    spin_lock(&only_device->pending_lock);
    list_add_tail(&buse_request->list, &only_device->pending_requests);
    spin_unlock(&only_device->pending_lock);

    wake_up(&only_device->wait_queue);

    return BLK_STS_OK;
}

static void buse_complete_rq(struct request *rq) {
    struct buse_request *request = blk_mq_rq_to_pdu(rq);
    blk_mq_end_request(rq, request->int_result);
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

static void buse_report_zones_request_end(struct buse_request *request) {
    up(&request->sem);
}

static int buse_report_zones(struct gendisk *disk, sector_t sector, unsigned int nr_zones, report_zones_cb cb, void *data) {
    struct buse_request request;
    struct blk_zone *blkz;
    int result;

    nr_zones = min_t(unsigned int, nr_zones, BUSE_MAX_ZONES);

    blkz = kvmalloc(sizeof(struct blk_zone) * nr_zones, GFP_KERNEL);
    if (blkz == NULL)
        return -ENOMEM;

    request.id = atomic64_inc_return(&next_request_id);
    request.type = BUSE_REPORT_ZONES;
    request.pos = sector << SECTOR_SHIFT;
    request.length = nr_zones;
    request.data = blkz;
    request.on_request_end = buse_report_zones_request_end;
    sema_init(&request.sem, 0);

    spin_lock(&only_device->pending_lock);
    list_add_tail(&request.list, &only_device->pending_requests);
    spin_unlock(&only_device->pending_lock);

    wake_up(&only_device->wait_queue);

    result = down_killable(&request.sem);
    if (result < 0)
        goto out_free;

    result = request.int_result;
    if (result < 0)
        goto out_free;

    nr_zones = result / sizeof(struct blk_zone);

    printd("buse: recevied nr_zones = %u\n", nr_zones);
    for (int i = 0; i < nr_zones; i++) {
        printd("buse: zone %i, start = %llu, wp = %llu, len = %llu\n", i, blkz[i].start, blkz[i].wp, blkz[i].len);
        result = cb(&blkz[i], i, data);
        if (result) {
            printk("buse: report_zones_cb failed: %d\n", result);
            goto out_free;
        }
    }

    result = nr_zones;

out_free:
    kvfree(blkz);

    return result;
}

struct block_device_operations buse_fops = {
    .owner = THIS_MODULE,
    .report_zones = buse_report_zones,
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
    blk_queue_max_hw_sectors(buse_device->disk->queue, BUSE_MAX_ZONE_SECTORS);
    blk_queue_chunk_sectors(buse_device->disk->queue, BUSE_MAX_ZONE_SECTORS);
    blk_queue_dma_alignment(buse_device->disk->queue, PAGE_SIZE - 1);

    strncpy(buse_device->disk->disk_name, name, DISK_NAME_LEN);
    buse_device->disk->major = buse_device->major;
    buse_device->disk->first_minor = 1;
    buse_device->disk->fops = &buse_fops;
    buse_device->disk->private_data = NULL;

    /* TODO: FIX HERE */
    set_capacity(buse_device->disk, device_size / SECTOR_SIZE);

    blk_queue_set_zoned(buse_device->disk, BLK_ZONED_HM);
    blk_queue_flag_set(QUEUE_FLAG_ZONE_RESETALL, buse_device->q);
    blk_queue_required_elevator_features(buse_device->q, ELEVATOR_F_ZBD_SEQ_WRITE);

    blk_queue_max_zone_append_sectors(buse_device->q, BUSE_MAX_ZONE_SECTORS);
    blk_queue_max_open_zones(buse_device->q, 65536);
    blk_queue_max_active_zones(buse_device->q, 65536);

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

int buse_revalidate(struct buse_block_device *device) {
    int ret;

    device->validated = 1;

    ret = blk_revalidate_disk_zones(device->disk, NULL);
    if (ret) {
        printk("buse: blk_revalidate_disk_zones = %d\n", ret);
        return ret;
    }

    return 0;
}
