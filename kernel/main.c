#include <linux/module.h>
#include "block_dev.h"
#include "char_dev.h"

static int __init bius_init(void)
{
    return bius_dev_init();
}

static void __exit bius_exit(void)
{
    bius_dev_exit();
}

module_init(bius_init);
module_exit(bius_exit);
MODULE_DESCRIPTION("Block device in userspace");
MODULE_LICENSE("GPL");
