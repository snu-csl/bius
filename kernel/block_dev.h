#ifndef BUSE_BLOCK_DEV_H
#define BUSE_BLOCK_DEV_H

#include <linux/blkdev.h>
#include <linux/blk-mq.h>

int create_block_device(const char *name);
void remove_block_device(const char *name);

#endif
