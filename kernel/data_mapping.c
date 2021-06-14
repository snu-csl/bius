#include <linux/rwsem.h>
#include <linux/mm.h>
#include "tlbflush.h"
#include "connection.h"
#include "config.h"
#include "char_dev.h"
#include "data_mapping.h"
#include "utils.h"

void buse_vm_open(struct vm_area_struct *vma) {
    struct buse_connection *connection = vma->vm_private_data;
    unsigned long addr = vma->vm_start;
    pte_t *pte;
    spinlock_t *ptl;
    int err, i;

    for (; addr < vma->vm_end; addr += PAGE_SIZE) {
        err = partial_map_pfn(vma, addr, zero_page_pfn, PAGE_SIZE, PAGE_READONLY);
        if (err) {
            printk("buse: remap_pfn_range failed\n");
            goto error_unmap;
        }
    }

    for (i = 0, addr = vma->vm_start; i < BUSE_PTES_PER_COMMAND; i++, addr += PAGE_SIZE) {
        err = follow_pte(vma->vm_mm, addr, &pte, &ptl);
        if (err) {
            printk("buse: follow_pte failed: %d\n", err);
            goto error_unmap;
        }
        spin_unlock(ptl);
        connection->ptes[i] = pte;
    }

    return;

error_unmap:
    zap_vma_ptes(vma, vma->vm_start, vma->vm_end - vma->vm_start);
    return;
}

static void buse_vm_close(struct vm_area_struct *vma) {
    struct buse_connection *connection = vma->vm_private_data;
    connection->vma = NULL;
}

static vm_fault_t buse_vm_fault(struct vm_fault *vmf) {
    return VM_FAULT_SIGSEGV;
}

const struct vm_operations_struct buse_vm_operations = {
    .open = buse_vm_open,
    .close = buse_vm_close,
    .fault = buse_vm_fault,
};

int buse_map_data(struct buse_request *request, struct buse_connection *connection) {
    struct vm_area_struct *vma = connection->vma;
    struct req_iterator iter;
    struct bio_vec bvec;
    unsigned long *mapping_list = (unsigned long *)connection->reserved_pages;
    unsigned long list_entry_index = 0;
    int next_reserved_page_num = 1;
    unsigned long user_addr = vma->vm_start;
    int user_page_num = 0;
    bool segment_end_aligned = false;

    if (request->map_type != BUSE_DATAMAP_UNMAPPED) {
        printk("buse: request is already mapped, id = %llu, map_type = %d\n", request->id, request->map_type);
        return 0;
    }

    flush_cache_range(vma, vma->vm_start, vma->vm_end);
    rq_for_each_segment(bvec, blk_mq_rq_from_pdu(request), iter) {
        char *data_address = page_address(bvec.bv_page);
        unsigned long data_pfn = page_to_pfn(bvec.bv_page);
        unsigned long remain_size = bvec.bv_len;

        if (!segment_end_aligned || bvec.bv_offset != 0) {
            mapping_list[list_entry_index * 3] = user_addr;
            mapping_list[list_entry_index * 3 + 1] = bvec.bv_offset;
            mapping_list[list_entry_index * 3 + 2] = 0;
            list_entry_index++;
        }
        mapping_list[list_entry_index * 3 - 1] += remain_size;

        if (bvec.bv_offset != 0) {  // If start of chunk is not page aligned
            char *reserved = connection->reserved_pages + PAGE_SIZE * next_reserved_page_num;

            memset(reserved, 0, PAGE_SIZE);
            memcpy(reserved + bvec.bv_offset, data_address + bvec.bv_offset, PAGE_SIZE - bvec.bv_offset);

            set_pte_at(vma->vm_mm, user_addr, connection->ptes[user_page_num], pte_mkspecial(pfn_pte(connection->reserved_pages_pfn + next_reserved_page_num, vma->vm_page_prot)));

            next_reserved_page_num++;
            user_addr += PAGE_SIZE;
            user_page_num++;
            data_pfn++;
            data_address += PAGE_SIZE;
            remain_size -= (PAGE_SIZE - bvec.bv_offset);
        }

        while (remain_size >= PAGE_SIZE) {
            set_pte_at(vma->vm_mm, user_addr, connection->ptes[user_page_num], pte_mkspecial(pfn_pte(data_pfn, vma->vm_page_prot)));
            user_addr += PAGE_SIZE;
            user_page_num++;
            data_pfn++;
            data_address += PAGE_SIZE;
            remain_size -= PAGE_SIZE;
        }

        if (remain_size > 0) {
            char *reserved = connection->reserved_pages + PAGE_SIZE * next_reserved_page_num;

            memset(reserved, 0, PAGE_SIZE);
            memcpy(reserved, data_address, remain_size);

            set_pte_at(vma->vm_mm, user_addr, connection->ptes[user_page_num], pte_mkspecial(pfn_pte(connection->reserved_pages_pfn + next_reserved_page_num, vma->vm_page_prot)));

            next_reserved_page_num++;
            user_addr += PAGE_SIZE;
            user_page_num++;
            data_pfn++;
            data_address += PAGE_SIZE;
            remain_size = 0;

            segment_end_aligned = false;
        } else {
            segment_end_aligned = true;
        }
    }

    if (list_entry_index > 1) {
        set_pte_at(vma->vm_mm, user_addr, connection->ptes[user_page_num], pte_mkspecial(pfn_pte(connection->reserved_pages_pfn, vma->vm_page_prot)));
        mapping_list[list_entry_index * 3] = mapping_list[list_entry_index * 3 + 1] = mapping_list[list_entry_index * 3 + 2] = 0;
        request->map_type = BUSE_DATAMAP_LIST;
        request->map_data = user_addr;
        user_addr += PAGE_SIZE;
    } else if (list_entry_index == 1) {
        request->map_type = BUSE_DATAMAP_SIMPLE;
        request->map_data = mapping_list[1];
    } else {  // list_entry_index == 0
        request->map_type = BUSE_DATAMAP_UNMAPPED;
    }

    request->mapped_size = user_addr - vma->vm_start;
    flush_tlb_mm_range(vma->vm_mm, vma->vm_start, request->mapped_size, PAGE_SHIFT, false);

    return 0;
}

void buse_unmap_data(struct buse_request *request, struct buse_connection *connection) {
    struct vm_area_struct *vma = connection->vma;
    unsigned long addr = vma->vm_start;
    int mapped_pages = request->mapped_size / PAGE_SIZE;

    if (request->map_type == BUSE_DATAMAP_UNMAPPED)
        return;

    for (int i = 0; i < mapped_pages; i++, addr += PAGE_SIZE) {
        set_pte_at(vma->vm_mm, addr, connection->ptes[i], pte_mkspecial(pfn_pte(zero_page_pfn, PAGE_READONLY)));
    }
    flush_tlb_mm_range(vma->vm_mm, vma->vm_start, request->mapped_size, PAGE_SHIFT, false);

    request->map_type = BUSE_DATAMAP_UNMAPPED;
}
