#include <linux/module.h>
#include "block_dev.h"
#include "char_dev.h"

static int __init buse_init(void)
{
    int ret = 0;

    ret = buse_block_init();
    if (ret < 0)
        goto out;

    ret = buse_dev_init();
    if (ret < 0)
        goto out_block;

    ret = create_block_device("buse-block");
    if (ret < 0)
        goto out_char;

    return 0;

out_char:
    buse_dev_exit();

out_block:
    buse_block_exit();

out:
    return ret;
}

static void __exit buse_exit(void)
{
    remove_block_device("buse-block");
    buse_dev_exit();
    buse_block_exit();
}

module_init(buse_init);
module_exit(buse_exit);
MODULE_DESCRIPTION("Block device in userspace");
MODULE_LICENSE("GPL");
