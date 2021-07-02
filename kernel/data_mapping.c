#include <linux/rwsem.h>
#include <linux/mm.h>
#include <asm/tlbflush.h>
#include "connection.h"
#include <bius/config.h>
#include "char_dev.h"
#include "data_mapping.h"
#include "utils.h"

void bius_vm_open(struct vm_area_struct *vma) {
    struct bius_connection *connection = vma->vm_private_data;
    unsigned long addr = vma->vm_start;
    pte_t *pte;
    spinlock_t *ptl;
    int err, i;

    for (; addr < vma->vm_end; addr += PAGE_SIZE) {
        err = partial_map_pfn(vma, addr, zero_page_pfn, PAGE_SIZE, PAGE_READONLY);
        if (err) {
            printk("bius: remap_pfn_range failed\n");
            goto error_unmap;
        }
    }

    for (i = 0, addr = vma->vm_start; i < BIUS_PTES_PER_COMMAND; i++, addr += PAGE_SIZE) {
        err = follow_pte(vma->vm_mm, addr, &pte, &ptl);
        if (err) {
            printk("bius: follow_pte failed: %d\n", err);
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

static void bius_vm_close(struct vm_area_struct *vma) {
    struct bius_connection *connection = vma->vm_private_data;
    connection->vma = NULL;
}

static vm_fault_t bius_vm_fault(struct vm_fault *vmf) {
    return VM_FAULT_SIGSEGV;
}

const struct vm_operations_struct bius_vm_operations = {
    .open = bius_vm_open,
    .close = bius_vm_close,
    .fault = bius_vm_fault,
};

int bius_map_data(struct bius_request *request, struct bius_connection *connection) {
    struct vm_area_struct *vma = connection->vma;
    struct req_iterator iter;
    struct bio_vec bvec;
    const bool is_write = request_is_write(request->type);
    unsigned long *mapping_list = (unsigned long *)connection->reserved_pages;
    unsigned long *reserve_mapping_list = (unsigned long *)(connection->reserved_pages + PAGE_SIZE);
    unsigned long list_entry_index = 0;
    int next_reserved_page_num = 2;
    unsigned long user_addr = vma->vm_start;
    int user_page_num = 0;
    bool segment_end_aligned = false;

    if (request->map_type != BIUS_DATAMAP_UNMAPPED) {
        printk("bius: request is already mapped, id = %llu, map_type = %d\n", request->id, request->map_type);
        return 0;
    }

    flush_cache_range(vma, vma->vm_start, vma->vm_end);
    rq_for_each_segment(bvec, blk_mq_rq_from_pdu(request), iter) {
        char *data_address = page_address(bvec.bv_page);
        unsigned long data_pfn = page_to_pfn(bvec.bv_page);
        unsigned long remain_size = bvec.bv_len;

        if (!segment_end_aligned || bvec.bv_offset != 0) {
            mapping_list[list_entry_index * 2] = user_addr + bvec.bv_offset;
            mapping_list[list_entry_index * 2 + 1] = 0;
            list_entry_index++;
        }
        mapping_list[list_entry_index * 2 - 1] += remain_size;

        if (bvec.bv_offset != 0) {  // If start of chunk is not page aligned
            size_t data_size_in_page = min_t(size_t, PAGE_SIZE - bvec.bv_offset, bvec.bv_len);
            char *reserved = connection->reserved_pages + PAGE_SIZE * next_reserved_page_num;

            memset(reserved, 0, PAGE_SIZE);
            if (is_write)
                memcpy(reserved + bvec.bv_offset, data_address + bvec.bv_offset, data_size_in_page);

            set_pte_at(vma->vm_mm, user_addr, connection->ptes[user_page_num], pte_mkspecial(pfn_pte(connection->reserved_pages_pfn + next_reserved_page_num, vma->vm_page_prot)));
            reserve_mapping_list[next_reserved_page_num] = (unsigned long)data_address;

            next_reserved_page_num++;
            user_addr += PAGE_SIZE;
            user_page_num++;
            data_pfn++;
            data_address += PAGE_SIZE;
            remain_size -= data_size_in_page;

            if (data_size_in_page != PAGE_SIZE - bvec.bv_offset) {
                segment_end_aligned = false;
                continue;
            }
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
            if (is_write)
                memcpy(reserved, data_address, remain_size);

            set_pte_at(vma->vm_mm, user_addr, connection->ptes[user_page_num], pte_mkspecial(pfn_pte(connection->reserved_pages_pfn + next_reserved_page_num, vma->vm_page_prot)));
            reserve_mapping_list[next_reserved_page_num] = (unsigned long)data_address;

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
        mapping_list[list_entry_index * 2] = mapping_list[list_entry_index * 2 + 1] = 0;
        request->map_type = BIUS_DATAMAP_LIST;
        request->map_data = user_addr;
        user_addr += PAGE_SIZE;
    } else if (list_entry_index == 1) {
        request->map_type = BIUS_DATAMAP_SIMPLE;
        request->map_data = mapping_list[0] % PAGE_SIZE;
    } else {  // list_entry_index == 0
        request->map_type = BIUS_DATAMAP_UNMAPPED;
    }

    request->mapped_size = user_addr - vma->vm_start;
    flush_tlb_mm_range(vma->vm_mm, vma->vm_start, request->mapped_size, PAGE_SHIFT, false);

    return 0;
}

void bius_copy_in_misaligned_pages(struct bius_request *request, struct bius_connection *connection) {
    unsigned long simple_mapping_list[4] = {0, 0, 0, 0};
    unsigned long *mapping_list;
    unsigned long *reserve_mapping_list = (unsigned long *)(connection->reserved_pages + PAGE_SIZE);
    int reserved_page_num = 2;

    switch (request->map_type) {
        case BIUS_DATAMAP_UNMAPPED:
            return;
        case BIUS_DATAMAP_SIMPLE:
            simple_mapping_list[0] = connection->vma->vm_start + request->map_data;
            simple_mapping_list[1] = request->length;
            mapping_list = simple_mapping_list;
            break;
        case BIUS_DATAMAP_LIST:
            mapping_list = (unsigned long *)connection->reserved_pages;
            break;
        default:
            printk("bius: bius_copy_misaligned_pages: unknown mapping type: %d\n", request->map_type);
            return;
    }

    for (int i = 0; mapping_list[i * 2] != 0; i++) {
        bool segment_front_aligned = ((mapping_list[i * 2] % PAGE_SIZE) == 0);
        bool segment_end_aligned = (((mapping_list[i * 2] + mapping_list[i * 2 + 1]) % PAGE_SIZE) == 0);

        if (!segment_front_aligned) {
            size_t length;
            unsigned offset = (mapping_list[i] % PAGE_SIZE);
            char *src = connection->reserved_pages + PAGE_SIZE * reserved_page_num;
            char *dest = (char *)reserve_mapping_list[reserved_page_num];

            if (offset + mapping_list[i * 2 + 1] < PAGE_SIZE) {
                length = mapping_list[i * 2 + 1];
                segment_end_aligned = true;
            } else {
                length = PAGE_SIZE - offset;
            }

            memcpy(dest + offset, src + offset, length);
            reserved_page_num++;
        }

        if (!segment_end_aligned) {
            size_t length = ((mapping_list[i] + mapping_list[i * 2 + 1]) % PAGE_SIZE);
            char *src = connection->reserved_pages + PAGE_SIZE * reserved_page_num;
            char *dest = (char *)reserve_mapping_list[reserved_page_num];

            memcpy(dest, src, length);
            reserved_page_num++;
        }
    }
}

void bius_unmap_data(struct bius_request *request, struct bius_connection *connection) {
    struct vm_area_struct *vma = connection->vma;
    unsigned long addr = vma->vm_start;
    int mapped_pages = request->mapped_size / PAGE_SIZE;

    if (request->map_type == BIUS_DATAMAP_UNMAPPED)
        return;

    for (int i = 0; i < mapped_pages; i++, addr += PAGE_SIZE) {
        set_pte_at(vma->vm_mm, addr, connection->ptes[i], pte_mkspecial(pfn_pte(zero_page_pfn, PAGE_READONLY)));
    }
    flush_tlb_mm_range(vma->vm_mm, vma->vm_start, request->mapped_size, PAGE_SHIFT, false);

    request->map_type = BIUS_DATAMAP_UNMAPPED;
}
