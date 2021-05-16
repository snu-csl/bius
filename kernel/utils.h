#ifndef BUSE_UTILS_H
#define BUSE_UTILS_H

#ifdef DEBUG
#define printd printk
#else
#define printd(x, ...) (void)(x)
#endif

#endif
