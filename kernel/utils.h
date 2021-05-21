#ifndef BUSE_UTILS_H
#define BUSE_UTILS_H

#ifdef DEBUG
#define printd printk
#else
static inline int printd(const char *format, ...) {
    return 0;
}
#endif

#endif
