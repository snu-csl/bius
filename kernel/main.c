#include <linux/module.h>
#include "block_dev.h"
#include "char_dev.h"

static int __init bius_init(void)
{
    int ret = 0;

    ret = bius_dev_init();
    if (ret < 0)
        goto out;

    ret = create_block_device("bius-block");
    if (ret < 0)
        goto out_char;

    return 0;

out_char:
    bius_dev_exit();

out:
    return ret;
}

static void __exit bius_exit(void)
{
    remove_block_device("bius-block");
    bius_dev_exit();
}

module_init(bius_init);
module_exit(bius_exit);
MODULE_DESCRIPTION("Block device in userspace");
MODULE_LICENSE("GPL");
