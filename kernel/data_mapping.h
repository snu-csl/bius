#ifndef BUSE_DATA_MAPPING_H
#define BUSE_DATA_MAPPING_H

#include <linux/mm.h>

extern const struct vm_operations_struct buse_vm_operations;

int buse_map_data(struct buse_request *request, struct buse_connection *connection);
void buse_unmap_data(struct buse_request *request, struct buse_connection *connection);

#endif
