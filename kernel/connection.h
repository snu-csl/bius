#ifndef BIUS_CONNECTION_H
#define BIUS_CONNECTION_H

#include <linux/list.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include "block_dev.h"
#include <bius/config.h>
#include "request.h"

#define BIUS_NUM_RESERVED_PAGES (BIUS_MAX_SEGMENTS + 2)

struct bius_connection {
    struct bius_block_device *block_dev;
    /* List of requests waiting for userspace response */
    struct list_head waiting_requests;
    spinlock_t waiting_lock;
#ifdef CONFIG_BIUS_DATAMAP
    struct vm_area_struct *vma;
    pte_t *ptes[BIUS_PTES_PER_COMMAND];
    char *reserved_pages;
    unsigned long reserved_pages_pfn;
#endif
    struct bius_request *sending;
};

static inline struct bius_connection *get_bius_connection(struct file *file) {
    return (struct bius_connection *)file->private_data;
}

static inline void init_bius_connection(struct bius_connection *connection) {
    INIT_LIST_HEAD(&connection->waiting_requests);
    spin_lock_init(&connection->waiting_lock);
#ifdef CONFIG_BIUS_DATAMAP
    connection->vma = NULL;
#endif
    connection->sending = NULL;
}

#endif
