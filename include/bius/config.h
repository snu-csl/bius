#ifndef BIUS_CONFIG
#define BIUS_CONFIG

#ifdef __KERNEL__
#include <linux/blkdev.h>
#endif

#define BIUS_MAX_SEGMENTS 256
#define BIUS_MAX_SIZE_PER_COMMAND (128 * 1024 * 1024)
#define BIUS_PTES_PER_COMMAND (BIUS_MAX_SIZE_PER_COMMAND / PAGE_SIZE)
#define BIUS_MAX_ZONE_SECTORS ((1 * 1024 * 1024 * 1024) >> SECTOR_SHIFT)

#define BIUS_MAX_ZONES (128 * 1024)

//#define CONFIG_BIUS_DATAMAP
#define BIUS_MAP_DATA_THRESHOLD (128 * 1024)

#endif
