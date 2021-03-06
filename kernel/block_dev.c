#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <bius/config.h>
#include "block_dev.h"
#include "char_dev.h"
#include "request.h"
#include "utils.h"

atomic64_t next_request_id = ATOMIC64_INIT(0);
static struct list_head all_disk_list = LIST_HEAD_INIT(all_disk_list);
DEFINE_SPINLOCK(disk_list_lock);

static inline bius_req_t to_bius_request(unsigned int op) {
    switch (op) {
        case REQ_OP_READ:
            return BIUS_READ;
        case REQ_OP_WRITE:
            return BIUS_WRITE;
        case REQ_OP_FLUSH:
            return BIUS_FLUSH;
        case REQ_OP_DISCARD:
            return BIUS_DISCARD;
        case REQ_OP_ZONE_OPEN:
        case REQ_OP_ZONE_CLOSE:
        case REQ_OP_ZONE_FINISH:
        case REQ_OP_ZONE_APPEND:
        case REQ_OP_ZONE_RESET:
        case REQ_OP_ZONE_RESET_ALL:
#ifdef CONFIG_ZONE_DESC_EXT
        case REQ_OP_ZONE_SET_DESC:
#endif
            return (bius_req_t)op;
        default:
            return BIUS_INVALID_OP;
    }
}

static void bius_blk_request_end(struct bius_request *request) {
    struct request *rq = blk_mq_rq_from_pdu(request);

    if (request->type == BIUS_ZONE_APPEND)
        rq->bio->bi_iter.bi_sector = request->pos / SECTOR_SIZE;

    blk_mq_end_request(rq, request->blk_result);
}

static blk_status_t bius_queue_rq(struct blk_mq_hw_ctx *hctx, const struct blk_mq_queue_data *bd) {
    struct bius_block_device *device = hctx->driver_data;
    struct request *rq = bd->rq;
    struct bius_request *bius_request = blk_mq_rq_to_pdu(rq);
    loff_t pos = blk_rq_pos(rq) << SECTOR_SHIFT;

    blk_mq_start_request(rq);

    bius_request->id = atomic64_inc_return(&next_request_id);
    bius_request->type = to_bius_request(req_op(rq));
    bius_request->pos = pos;
    bius_request->length = blk_rq_bytes(rq);
    bius_request->bio = rq->bio;
    bius_request->map_type = BIUS_DATAMAP_UNMAPPED;
    bius_request->on_request_end = bius_blk_request_end;

    printd("bius: new_request: type = %d, op = %d, pos = %lld, length = %ld\n", bius_request->type, req_op(bd->rq), pos, bius_request->length);

    if (unlikely(bius_request->type == BIUS_INVALID_OP)) {
        printk("bius: unsupported operation: %d\n", req_op(rq));
        blk_mq_end_request(rq, BLK_STS_NOTSUPP);
        return BLK_STS_NOTSUPP;
    }

    spin_lock(&device->pending_lock);
    list_add_tail(&bius_request->list, &device->pending_requests);
    spin_unlock(&device->pending_lock);

    wake_up(&device->wait_queue);

    return BLK_STS_OK;
}

static void bius_complete_rq(struct request *rq) {
    struct bius_request *request = blk_mq_rq_to_pdu(rq);
    blk_mq_end_request(rq, request->int_result);
}

static int bius_init_hctx(struct blk_mq_hw_ctx *hctx, void *driver_data, unsigned int hctx_idx) {
    hctx->driver_data = driver_data;  // bius_block_device set in blk_mq_tag_set.driver_data
    return 0;
}

static const struct blk_mq_ops bius_mq_ops = {
    .queue_rq = bius_queue_rq,
    .complete = bius_complete_rq,
    .init_hctx = bius_init_hctx,
};

static void initialize_tag_set(struct blk_mq_tag_set *tag_set, struct bius_block_device *device) {
    memset(tag_set, 0, sizeof(struct blk_mq_tag_set));
    tag_set->ops = &bius_mq_ops;
    tag_set->nr_hw_queues = 4;
    tag_set->queue_depth = 128;
    tag_set->numa_node = NUMA_NO_NODE;
    tag_set->cmd_size = sizeof(struct bius_request);
    tag_set->flags = BLK_MQ_F_SHOULD_MERGE | BLK_MQ_F_BLOCKING;
    tag_set->driver_data = device;
}

static void bius_report_zones_request_end(struct bius_request *request) {
    up(&request->sem);
}

static int bius_report_zones(struct gendisk *disk, sector_t sector, unsigned int nr_zones, report_zones_cb cb, void *data) {
    struct bius_block_device *device = disk->private_data;
    struct bius_request request;
    struct blk_zone *blkz;
    int result;

    nr_zones = min_t(unsigned int, nr_zones, BIUS_MAX_ZONES);

    blkz = kvmalloc(sizeof(struct blk_zone) * nr_zones, GFP_KERNEL);
    if (blkz == NULL)
        return -ENOMEM;

    request.id = atomic64_inc_return(&next_request_id);
    request.type = BIUS_REPORT_ZONES;
    request.pos = sector << SECTOR_SHIFT;
    request.length = nr_zones;
    request.data = blkz;
    request.on_request_end = bius_report_zones_request_end;
    sema_init(&request.sem, 0);

    spin_lock(&device->pending_lock);
    list_add_tail(&request.list, &device->pending_requests);
    spin_unlock(&device->pending_lock);

    wake_up(&device->wait_queue);

    result = down_killable(&request.sem);
    if (result < 0)
        goto out_free;

    result = request.int_result;
    if (result < 0)
        goto out_free;

    nr_zones = result / sizeof(struct blk_zone);

    printd("bius: recevied nr_zones = %u\n", nr_zones);
    for (int i = 0; i < nr_zones; i++) {
        printd("bius: zone %i, start = %llu, wp = %llu, len = %llu\n", i, blkz[i].start, blkz[i].wp, blkz[i].len);
        result = cb(&blkz[i], i, data);
        if (result) {
            printk("bius: report_zones_cb failed: %d\n", result);
            goto out_free;
        }
    }

    result = nr_zones;

out_free:
    kvfree(blkz);

    return result;
}

struct block_device_operations bius_fops = {
    .owner = THIS_MODULE,
    .report_zones = bius_report_zones,
};

int create_block_device(struct bius_block_device_options *options, struct bius_block_device **out_device) {
    struct bius_block_device *bius_device;
    int ret = 0;

    bius_device = kmalloc(sizeof(struct bius_block_device), GFP_KERNEL);
    if (bius_device == NULL)
        return -ENOMEM;
    init_bius_block_device(bius_device);
    bius_device->model = options->model;

    ret = register_blkdev(0, options->disk_name);
    if (ret < 0) {
        printk("bius: register_blkdev failed: %d\n", ret);
        goto out_free_device;
    }
    bius_device->major = ret;

    bius_device->disk = alloc_disk(1);
    if (bius_device->disk == NULL) {
        printk("bius: alloc_disk failed\n");
        goto out_unregister;
    }

    initialize_tag_set(&bius_device->tag_set, bius_device);
    ret = blk_mq_alloc_tag_set(&bius_device->tag_set);
    if (ret < 0) {
        printk("bius: blk_mq_alloc_tag_set failed: %d\n", ret);
        goto out_put_disk;
    }

    bius_device->q = blk_mq_init_queue(&bius_device->tag_set);
    if (IS_ERR(bius_device->q)) {
        ret = PTR_ERR(bius_device->q);
        goto out_free_tag_set;
    }

    bius_device->disk->queue = bius_device->q;

    blk_queue_flag_set(QUEUE_FLAG_NONROT, bius_device->disk->queue);
    blk_queue_flag_clear(QUEUE_FLAG_ADD_RANDOM, bius_device->disk->queue);
    bius_device->disk->queue->limits.discard_granularity = 0;
    bius_device->disk->queue->limits.discard_alignment = 0;
    blk_queue_max_discard_sectors(bius_device->disk->queue, 0);
    blk_queue_max_segments(bius_device->disk->queue, BIUS_MAX_SEGMENTS);
    bius_device->disk->queue->limits.max_dev_sectors = BIUS_MAX_SIZE_PER_COMMAND / SECTOR_SIZE;
    blk_queue_max_hw_sectors(bius_device->disk->queue, BIUS_MAX_SIZE_PER_COMMAND / SECTOR_SIZE);
    blk_queue_chunk_sectors(bius_device->disk->queue, BIUS_MAX_SIZE_PER_COMMAND / SECTOR_SIZE);
    blk_queue_io_min(bius_device->disk->queue, 512 * 1024);

    strncpy(bius_device->disk->disk_name, options->disk_name, DISK_NAME_LEN);
    bius_device->disk->major = bius_device->major;
    bius_device->disk->first_minor = 1;
    bius_device->disk->fops = &bius_fops;
    bius_device->disk->private_data = bius_device;

    set_capacity(bius_device->disk, options->disk_size / SECTOR_SIZE);

    if (options->model != BLK_ZONED_NONE) {
        blk_queue_set_zoned(bius_device->disk, BLK_ZONED_HM);
        blk_queue_flag_set(QUEUE_FLAG_ZONE_RESETALL, bius_device->q);
        blk_queue_required_elevator_features(bius_device->q, ELEVATOR_F_ZBD_SEQ_WRITE);

        blk_queue_max_zone_append_sectors(bius_device->q, BIUS_MAX_ZONE_SECTORS);
        blk_queue_max_open_zones(bius_device->q, options->max_open_zones);
        blk_queue_max_active_zones(bius_device->q, options->max_active_zones);
    }

    add_disk(bius_device->disk);

    spin_lock(&disk_list_lock);
    list_add_tail(&bius_device->disk_list, &all_disk_list);
    spin_unlock(&disk_list_lock);

    *out_device = bius_device;
    return 0;

out_free_tag_set:
    blk_mq_free_tag_set(&bius_device->tag_set);

out_put_disk:
    put_disk(bius_device->disk);

out_unregister:
    unregister_blkdev(bius_device->major, options->disk_name);

out_free_device:
    kfree(bius_device);

    return ret;
}

void remove_block_device(const char *name) {
    struct bius_block_device *bius_device = get_block_device(name);
    struct bius_request *request;

    if (bius_device == NULL)
        return;

    spin_lock(&disk_list_lock);
    list_del(&bius_device->disk_list);
    spin_unlock(&disk_list_lock);

    list_for_each_entry(request, &bius_device->pending_requests, list) {
        struct request *rq = blk_mq_rq_from_pdu(request);
        blk_mq_end_request(rq, BLK_STS_IOERR);
    }

    del_gendisk(bius_device->disk);
    blk_mq_free_tag_set(&bius_device->tag_set);
    put_disk(bius_device->disk);
    unregister_blkdev(bius_device->major, name);
}

static int bius_do_revalidate(void *arg) {
    struct bius_block_device *device = arg;
    int ret;

    ret = blk_revalidate_disk_zones(device->disk, NULL);
    if (ret)
        printk("bius: blk_revalidate_disk_zones = %d\n", ret);

    do_exit(ret);
    return ret;
}

void bius_revalidate(struct bius_block_device *device) {
    kthread_run(bius_do_revalidate, device, "revalidate %s", device->disk->disk_name);
}

struct bius_block_device *get_block_device(const char *disk_name) {
    struct bius_block_device *device;
    bool found = false;

    spin_lock(&disk_list_lock);
    list_for_each_entry_reverse(device, &all_disk_list, disk_list) {
        if (strncmp(device->disk->disk_name, disk_name, DISK_NAME_LEN) == 0) {
            found = true;
            break;
        }
    }
    spin_unlock(&disk_list_lock);

    if (found)
        return device;
    else
        return NULL;
}
