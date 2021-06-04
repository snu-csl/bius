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
    int err;

    err = partial_map_pfn(vma, addr, PHYS_PFN(virt_to_phys(zero_pages)), vma->vm_end - vma->vm_start, vma->vm_page_prot);
    if (err) {
        printk("buse: remap_pfn_range failed\n");
        return;
    }

    for (int i = 0; i < BUSE_PTES_PER_COMMAND; i++, addr += PAGE_SIZE) {
        err = follow_pte(vma->vm_mm, addr, &pte, &ptl);
        if (err)
            printk("buse: follow_pte failed: %d\n", err);
        spin_unlock(ptl);
        connection->ptes[i] = pte;
    }
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
    unsigned long addr;
    int page_num = 0;

    if (request->is_data_mapped) {
        printk("buse: request is already mapped, id = %llu\n", request->id);
        return 0;
    }

    addr = vma->vm_start;
    flush_cache_range(vma, vma->vm_start, vma->vm_end);
    rq_for_each_segment(bvec, blk_mq_rq_from_pdu(request), iter) {
        unsigned long pfn = page_to_pfn(bvec.bv_page);

        if (unlikely(bvec.bv_offset != 0)) {
            printk("buse: bv offset is not zero\n");
            return -EINVAL;
        } else if (unlikely(bvec.bv_len % PAGE_SIZE != 0)) {
            printk("buse: bv size is not aligned to page size\n");
            return -EINVAL;
        }

        for (int i = 0; i < bvec.bv_len; i += PAGE_SIZE) {
            set_pte_at(vma->vm_mm, addr, connection->ptes[page_num], pte_mkspecial(pfn_pte(pfn, vma->vm_page_prot)));
            addr += PAGE_SIZE;
            page_num++;
            pfn++;
        }
    }
    flush_tlb_mm_range(vma->vm_mm, vma->vm_start, vma->vm_end, PAGE_SHIFT, false);

    request->is_data_mapped = 1;
    return 0;
}

void buse_unmap_data(struct buse_request *request, struct buse_connection *connection) {
    struct vm_area_struct *vma = connection->vma;
    unsigned long addr = vma->vm_start;

    if (!request->is_data_mapped)
        return;

    for (int i = 0; i < BUSE_PTES_PER_COMMAND; i++, addr += PAGE_SIZE) {
        set_pte_at(vma->vm_mm, addr, connection->ptes[i], pte_mkspecial(pfn_pte(zero_pages_pfn + i, PAGE_READONLY)));
    }
    flush_tlb_mm_range(vma->vm_mm, vma->vm_start, vma->vm_end, PAGE_SHIFT, false);

    request->is_data_mapped = 0;
}
