#ifndef BIUS_UTILS_H
#define BIUS_UTILS_H

#ifdef DEBUG
#define printd printk
#else
static inline int printd(const char *format, ...) {
    return 0;
}
#endif

#define mp_bio_iter_iovec(bio, iter) \
        mp_bvec_iter_bvec((bio)->bi_io_vec, (iter))

#endif
