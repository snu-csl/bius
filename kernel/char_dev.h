#ifndef BUSE_CHAR_DEV_H
#define BUSE_CHAR_DEV_H

#include <linux/miscdevice.h>
#include <linux/module.h>
#include "connection.h"

#define BUSE_MINOR MISC_DYNAMIC_MINOR

extern struct buse_connection *only_connection;

int __init buse_dev_init(void);
void buse_dev_exit(void);

#endif
