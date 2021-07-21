#ifndef BIUS_DATA_MAPPING_H
#define BIUS_DATA_MAPPING_H

#include <bius/config.h>

#ifdef CONFIG_BIUS_DATAMAP
#include <linux/mm.h>

extern const struct vm_operations_struct bius_vm_operations;

void bius_vm_open(struct vm_area_struct *vma);
int bius_map_data(struct bius_request *request, struct bius_connection *connection);
void bius_copy_in_misaligned_pages(struct bius_request *request, struct bius_connection *connection);
void bius_unmap_data(struct bius_request *request, struct bius_connection *connection);
#endif

#endif
