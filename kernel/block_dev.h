#include <linux/blkdev.h>
#include <linux/blk-mq.h>

struct buse_cmd {
};

int create_block_device(const char *name);
void remove_block_device(const char *name);
