#ifndef BLK_STATUS_H
#define BLK_STATUS_H

#include <stdint.h>

typedef uint32_t blk_status_t;

#define	BLK_STS_OK 0
#define BLK_STS_NOTSUPP		((blk_status_t)1)
#define BLK_STS_TIMEOUT		((blk_status_t)2)
#define BLK_STS_NOSPC		((blk_status_t)3)
#define BLK_STS_TRANSPORT	((blk_status_t)4)
#define BLK_STS_TARGET		((blk_status_t)5)
#define BLK_STS_NEXUS		((blk_status_t)6)
#define BLK_STS_MEDIUM		((blk_status_t)7)
#define BLK_STS_PROTECTION	((blk_status_t)8)
#define BLK_STS_RESOURCE	((blk_status_t)9)
#define BLK_STS_IOERR		((blk_status_t)10)
#define BLK_STS_AGAIN		((blk_status_t)12)

/*
 * BLK_STS_DEV_RESOURCE is returned from the driver to the block layer if
 * device related resources are unavailable, but the driver can guarantee
 * that the queue will be rerun in the future once resources become
 * available again. This is typically the case for device specific
 * resources that are consumed for IO. If the driver fails allocating these
 * resources, we know that inflight (or pending) IO will free these
 * resource upon completion.
 *
 * This is different from BLK_STS_RESOURCE in that it explicitly references
 * a device specific resource. For resources of wider scope, allocation
 * failure can happen without having pending IO. This means that we can't
 * rely on request completions freeing these resources, as IO may not be in
 * flight. Examples of that are kernel memory allocations, DMA mappings, or
 * any other system wide resources.
 */
#define BLK_STS_DEV_RESOURCE	((blk_status_t)13)

/*
 * BLK_STS_ZONE_RESOURCE is returned from the driver to the block layer if zone
 * related resources are unavailable, but the driver can guarantee the queue
 * will be rerun in the future once the resources become available again.
 *
 * This is different from BLK_STS_DEV_RESOURCE in that it explicitly references
 * a zone specific resource and IO to a different zone on the same device could
 * still be served. Examples of that are zones that are write-locked, but a read
 * to the same zone could be served.
 */
#define BLK_STS_ZONE_RESOURCE	((blk_status_t)14)

/*
 * BLK_STS_ZONE_OPEN_RESOURCE is returned from the driver in the completion
 * path if the device returns a status indicating that too many zone resources
 * are currently open. The same command should be successful if resubmitted
 * after the number of open zones decreases below the device's limits, which is
 * reported in the request_queue's max_open_zones.
 */
#define BLK_STS_ZONE_OPEN_RESOURCE	((blk_status_t)15)

/*
 * BLK_STS_ZONE_ACTIVE_RESOURCE is returned from the driver in the completion
 * path if the device returns a status indicating that too many zone resources
 * are currently active. The same command should be successful if resubmitted
 * after the number of active zones decreases below the device's limits, which
 * is reported in the request_queue's max_active_zones.
 */
#define BLK_STS_ZONE_ACTIVE_RESOURCE	((blk_status_t)16)

#endif
