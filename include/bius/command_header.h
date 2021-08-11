#ifndef BIUS_COMMAND_HEADER_H
#define BIUS_COMMAND_HEADER_H

#include <bius/request_type.h>
#include <linux/blkzoned.h>
#ifdef __KERNEL__
#include <linux/blkdev.h>
#else
#include <stdint.h>

enum blk_zoned_model {
	BLK_ZONED_NONE = 0,	/* Regular block device */
	BLK_ZONED_HA,		/* Host-aware zoned block device */
	BLK_ZONED_HM,		/* Host-managed zoned block device */
};

#endif  // __KERNEL__

#define MAX_DISK_NAME_LEN 32

struct bius_k2u_header {
    uint64_t id;
    bius_req_t opcode;
    uint64_t offset;
    uint64_t length;
    uint64_t data_address;
    uint64_t mapping_data;
    int32_t data_map_type;
};

struct bius_u2k_header {
    uint64_t id;
    union {
        struct {
            ssize_t reply;
        };
        struct {
            uint32_t u2k_type;
            uint32_t u2k_length;
        };
    };
    uint64_t user_data;
};

struct bius_block_device_options {
    enum blk_zoned_model model;
    unsigned int num_threads;
    unsigned long disk_size;
    unsigned int max_open_zones;
    unsigned int max_active_zones;
    char disk_name[MAX_DISK_NAME_LEN];
};

#endif
