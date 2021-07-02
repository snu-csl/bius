#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <pthread.h>
#include <bius/config.h>
#include <bius/command_header.h>
#include <bius/map_type.h>
#include "libbius.h"
#include "utils.h"

#define PAGE_SIZE 4096
#define DATA_MAP_AREA_SIZE (BIUS_MAX_SIZE_PER_COMMAND + PAGE_SIZE)

static inline int read_command(int fd, struct bius_k2u_header *header) {
    ssize_t result = read(fd, header, sizeof(struct bius_k2u_header));
    if (result < 0) {
        fprintf(stderr, "Command reading failed: %s\n", strerror(errno));
    } else if (result == 0) {
        fprintf(stderr, "EOF returned while reading\n");
    } else if (result < sizeof(struct bius_k2u_header)) {
        fprintf(stderr, "Read size is smaller than header: %ld\n", result);
        result = -1;
    }

    return result;
}

static inline int write_command(int fd, const struct bius_u2k_header *header) {
    ssize_t result = write(fd, header, sizeof(struct bius_u2k_header));
    if (result < 0) {
        fprintf(stderr, "Reply writing failed: %s\n", strerror(errno));
    } else if (result == 0) {
        fprintf(stderr, "EOF returned while writing\n");
    } else if (result < sizeof(struct bius_u2k_header)) {
        fprintf(stderr, "Written size is smaller than header: %ld\n", result);
        result = -1;
    }

    return result;
}

static inline void handle_copy_in(int fd, struct bius_k2u_header *header, char *buffer) {
    if (request_may_have_data(header->opcode) && header->length <= BIUS_MAP_DATA_THRESHOLD) {
        header->data_map_type = BIUS_DATAMAP_SIMPLE;
        header->data_address = (unsigned long)buffer;
        header->mapping_data = 0;

        if (request_is_write(header->opcode)) {
            const size_t size = header->length;
            ssize_t total_read = 0;

            while (total_read < size) {
                ssize_t read_size = read(fd, buffer + total_read, size - total_read);

                if (read_size <= 0) {
                    fprintf(stderr, "Read failed: read_size = %ld, %s\n", read_size, strerror(errno));
                    exit(1);
                }

                total_read += read_size;
            }
        }
    }
}

static inline int64_t handle_blk_command_with_datamap_list(const struct bius_k2u_header *k2u, const struct bius_operations *ops, unsigned long *out_user_data) {
    unsigned long *datamap_list = (unsigned long *)k2u->mapping_data;
    off64_t offset = k2u->offset;
    bool first_call = true;

    for (int i = 0; datamap_list[i * 2] != 0; i++) {
        void *data_address = (void *)datamap_list[i * 2];
        size_t segment_size = datamap_list[i * 2 + 1];
        blk_status_t result;

        switch (k2u->opcode) {
            case BIUS_READ:
                if (ops->read)
                    result = ops->read(data_address, offset, segment_size);
                else
                    return BLK_STS_NOTSUPP;
                break;
            case BIUS_WRITE:
                if (ops->write)
                    result = ops->write(data_address, offset, segment_size);
                else
                    return BLK_STS_NOTSUPP;
                break;
             case BIUS_ZONE_APPEND:
                if (ops->append_zone)
                    result = ops->append_zone(data_address, offset, segment_size, first_call? (off64_t *)out_user_data : NULL);
                else
                    return BLK_STS_NOTSUPP;
                break;
             default:
                fprintf(stderr, "Unknown opcode at handle_blk_command_with_datamap_list: %d\n", k2u->opcode);
                return BLK_STS_NOTSUPP;
        }

        if (result != BLK_STS_OK)
            return result;

        offset += segment_size;
        first_call = false;
    }

    return BLK_STS_OK;
}

static inline int64_t handle_blk_command(const struct bius_k2u_header *k2u, const struct bius_operations *ops, unsigned long *out_user_data) {
    if (k2u->data_map_type == BIUS_DATAMAP_LIST)
        return handle_blk_command_with_datamap_list(k2u, ops, out_user_data);

    switch (k2u->opcode) {
        case BIUS_READ:
            if (k2u->length <= BIUS_MAP_DATA_THRESHOLD)
                *out_user_data = (unsigned long)(k2u->data_address + k2u->mapping_data);
            if (ops->read)
                return ops->read((void *)k2u->data_address + k2u->mapping_data, k2u->offset, k2u->length);
            else
                return BLK_STS_NOTSUPP;
        case BIUS_WRITE:
            if (ops->write)
                return ops->write((void *)k2u->data_address + k2u->mapping_data, k2u->offset, k2u->length);
            else
                return BLK_STS_NOTSUPP;
        case BIUS_DISCARD:
            if (ops->discard)
                return ops->discard(k2u->offset, k2u->length);
            else
                return BLK_STS_NOTSUPP;
        case BIUS_FLUSH:
            if (ops->flush)
                return ops->flush();
            else
                return BLK_STS_NOTSUPP;
        case BIUS_ZONE_OPEN:
            if (ops->open_zone)
                return ops->open_zone(k2u->offset);
            else
                return BLK_STS_NOTSUPP;
        case BIUS_ZONE_CLOSE:
            if (ops->close_zone)
                return ops->close_zone(k2u->offset);
            else
                return BLK_STS_NOTSUPP;
        case BIUS_ZONE_FINISH:
            if (ops->finish_zone)
                return ops->finish_zone(k2u->offset);
            else
                return BLK_STS_NOTSUPP;
        case BIUS_ZONE_APPEND:
            if (ops->append_zone)
                return ops->append_zone((void *)k2u->data_address + k2u->mapping_data, k2u->offset, k2u->length, (off64_t *)out_user_data);
            else
                return BLK_STS_NOTSUPP;
        case BIUS_ZONE_RESET:
            if (ops->reset_zone)
                return ops->reset_zone(k2u->offset);
            else
                return BLK_STS_NOTSUPP;
        case BIUS_ZONE_RESET_ALL:
            if (ops->reset_all_zone)
                return ops->reset_all_zone();
            else
                return BLK_STS_NOTSUPP;
        default:
            fprintf(stderr, "Unknown opcode at handle_blk_command: %d\n", k2u->opcode);
            return BLK_STS_NOTSUPP;
    }
}

static void *thread_main(void *arg) {
    const struct bius_options *options = arg;
    const struct bius_operations *ops = options->operations;
    struct bius_k2u_header k2u;
    struct bius_u2k_header u2k;
    struct blk_zone *zone_info = NULL;
    int bius_char_dev = open("/dev/bius", O_RDWR);
    if (bius_char_dev < 0) {
        fprintf(stderr, "char dev open failed: %s\n", strerror(errno));
        exit(1);
    }
    void *data_area = mmap(NULL, DATA_MAP_AREA_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, bius_char_dev, 0);
    printd("mmap result = %p\n", data_area);
    if (data_area == MAP_FAILED) {
        fprintf(stderr, "mmap failed: %s\n", strerror(errno));
        exit(1);
    }
    char *data_copy_buffer = aligned_alloc(PAGE_SIZE, BIUS_MAP_DATA_THRESHOLD);
    if (data_copy_buffer == NULL) {
        fprintf(stderr, "data copy buffer allocation failed: %s\n", strerror(errno));
        exit(1);
    }


    while (1) {
        int result = read_command(bius_char_dev, &k2u);
        printd("command read. id = %lu, opcode = %d, offset = %lu, length = %lu, data_address = %lx\n", k2u.id, k2u.opcode, k2u.offset, k2u.length, k2u.data_address);
        if (result < 0)
            exit(1);

        handle_copy_in(bius_char_dev, &k2u, data_copy_buffer);

        u2k.id = k2u.id;
        if (is_blk_request(k2u.opcode)) {
            unsigned long user_data = 0;

            u2k.reply = handle_blk_command(&k2u, ops, &user_data);
            u2k.user_data = user_data;
        } else if (k2u.opcode == BIUS_REPORT_ZONES && ops->report_zones) {
            zone_info = malloc(sizeof(struct blk_zone) * k2u.length);
            u2k.reply = ops->report_zones(k2u.offset, (int)k2u.length, zone_info) * sizeof(struct blk_zone);
            u2k.user_data = (uint64_t)zone_info;
        }

        result = write_command(bius_char_dev, &u2k);
        if (result < 0)
            exit(1);

        if (zone_info) {
            free(zone_info);
            zone_info = NULL;
        }
    }

    return NULL;
}

static inline int bius_main_real(const struct bius_options *options) {
    size_t num_threads = BIUS_DEFAULT_NUM_THREADS;
    int result = 0;
    pthread_t *threads;

    if (options->operations == NULL)
        return -EINVAL;
    if (options->num_threads != 0)
        num_threads = options->num_threads;

    threads = malloc(sizeof(pthread_t) * num_threads);
    if (threads == NULL)
        return -ENOMEM;

    for (int i = 0; i < num_threads; i++) {
        result = pthread_create(&threads[i], NULL, thread_main, (struct bius_options *)options);
        if (result < 0) {
            fprintf(stderr, "pthread_create failed: %s\n", strerror(result));
            goto out_free;
        }
    }

    for (int i = 0; i < num_threads; i++) {
        void *thread_result;

        result = pthread_join(threads[i], &thread_result);
        if (result < 0) {
            fprintf(stderr, "pthread_join failed: %s\n", strerror(result));
            goto out_free;
        }
    }

out_free:
    free(threads);

    return result;
}

int bius_main(const struct bius_options *options) {
    int result = bius_main_real(options);

    if (result < 0) {
        errno = result;
        return -1;
    } else {
        errno = 0;
        return result;
    }
}
