#ifndef BIUS_CHAR_DEV_H
#define BIUS_CHAR_DEV_H

#include <linux/miscdevice.h>
#include <linux/module.h>
#include "connection.h"

#define BIUS_MINOR MISC_DYNAMIC_MINOR

extern void *zero_page;
extern unsigned long zero_page_pfn;

int __init bius_dev_init(void);
void bius_dev_exit(void);

#endif
