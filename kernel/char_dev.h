#ifndef BIUS_CHAR_DEV_H
#define BIUS_CHAR_DEV_H

#include <linux/miscdevice.h>
#include <linux/module.h>
#include <bius/config.h>
#include "connection.h"

#define BIUS_MINOR MISC_DYNAMIC_MINOR

#ifdef CONFIG_BIUS_DATAMAP
extern void *zero_page;
extern unsigned long zero_page_pfn;
#endif

int __init bius_dev_init(void);
void bius_dev_exit(void);

#endif
