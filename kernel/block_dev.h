#ifndef BUSE_BLOCK_DEV_H
#define BUSE_BLOCK_DEV_H

#include <linux/blkdev.h>
#include <linux/blk-mq.h>

struct buse_cmd {
};

int create_block_device(const char *name);
void remove_block_device(const char *name);

int __init buse_block_init(void);
void buse_block_exit(void);

#endif
