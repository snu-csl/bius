#ifndef BIUS_TLBFLUSH_H
#define BIUS_TLBFLUSH_H

extern void flush_tlb_mm_range(struct mm_struct *mm, unsigned long start, unsigned long end, unsigned int stride_shift, bool freed_tables);

#endif
