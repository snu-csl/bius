#include <linux/rwsem.h>
#include <linux/mm.h>
#include "connection.h"
#include "config.h"
#include "data_mapping.h"
#include "utils.h"

static void buse_vm_open(struct vm_area_struct *vma) {
}

static void buse_vm_close(struct vm_area_struct *vma) {
    struct buse_connection *connection = vma->vm_private_data;

    kvfree(connection->page_bitmap);
    connection->page_bitmap = NULL;
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

static int get_area(struct buse_connection *connection) {
    int result;

    for (size_t i = 0; i < connection->bitmap_count; i++) {
        if (connection->page_bitmap[i] != ~0ul) {
            result = ffz(connection->page_bitmap[i]);
            connection->page_bitmap[i] |= (1 << result);

            printd("buse: get_area = %d\n", (int)(i * sizeof(unsigned long) * 8 + result));
            return i * sizeof(unsigned long) * 8 + result;
        }
    }

    return -ENOMEM;
}

static void free_area(struct buse_connection *connection, int area) {
    int index = area / (sizeof(unsigned long) * 8);
    int bit = area % (sizeof(unsigned long) * 8);

    printd("buse: free_area %d\n", area);
    connection->page_bitmap[index] &= ~(1 << bit);
}

int buse_map_data(struct buse_request *request, struct buse_connection *connection) {
    struct vm_area_struct *vma = connection->vma;
    struct req_iterator iter;
    struct bio_vec bvec;
    unsigned long addr;
    int area;
    int result;

    if (request->is_data_mapped) {
        printk("buse: request is already mapped\n");
        return 0;
    }

    down_write(&vma->vm_mm->mmap_lock);
    result = area = get_area(connection);
    if (area < 0)
        goto out;

    addr = vma->vm_start + area * BUSE_PER_BITMAP_SIZE;
    rq_for_each_segment(bvec, blk_mq_rq_from_pdu(request), iter) {
        if (unlikely(bvec.bv_offset != 0)) {
            printk("buse: bv offset is not zero\n");
            result = -EINVAL;
            goto out_free;
        } else if (unlikely(bvec.bv_len % PAGE_SIZE != 0)) {
            printk("buse: bv size is not aligned to page size\n");
            result = -EINVAL;
            goto out_free;
        }

//      result = remap_pfn_range(vma, addr, page_to_pfn(bvec.bv_page), bvec.bv_len, vma->vm_page_prot);
        result = partial_map_pfn(vma, addr, page_to_pfn(bvec.bv_page), bvec.bv_len, vma->vm_page_prot);
//      result = vm_insert_page(vma, addr, bvec.bv_page);
        if (result < 0)
            goto out_unmap;

        addr += PAGE_SIZE;
    }

    up_write(&vma->vm_mm->mmap_lock);

    request->is_data_mapped = 1;
    request->mapped_area = area;
    return 0;

out_unmap:
    addr = vma->vm_start + area * BUSE_PER_BITMAP_SIZE;
    zap_vma_ptes(vma, addr, BUSE_PER_BITMAP_SIZE);

out_free:
    free_area(connection, area);

out:
    up_write(&vma->vm_mm->mmap_lock);
    return result;
}

void buse_unmap_data(struct buse_request *request, struct buse_connection *connection) {
    struct vm_area_struct *vma = connection->vma;
    unsigned long addr = vma->vm_start + request->mapped_area * (BUSE_PER_BITMAP_SIZE / PAGE_SIZE);

    down_write(&vma->vm_mm->mmap_lock);
    zap_vma_ptes(vma, addr, BUSE_PER_BITMAP_SIZE);
    free_area(connection, request->mapped_area);
    up_write(&vma->vm_mm->mmap_lock);

    request->is_data_mapped = 0;
    request->mapped_area = 0;
}
