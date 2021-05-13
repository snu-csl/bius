#include <linux/module.h>
#include "block_dev.h"

static int __init buse_init(void)
{
    int ret = 0;

    ret = create_block_device("buse");

    return ret;
}

static void __exit buse_exit(void)
{
    remove_block_device("buse");
}

module_init(buse_init);
module_exit(buse_exit);
MODULE_DESCRIPTION("Block device in userspace");
MODULE_LICENSE("GPL");
