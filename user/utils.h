#ifndef UTILS_H
#define UTILS_H

#ifdef DEBUG
#define printd(format, ...) fprintf(stderr, format, ##__VA_ARGS__)
#else
static inline int printd(const char *format, ...) {
    return 0;
}
#endif

#endif
