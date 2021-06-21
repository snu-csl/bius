#include <linux/poll.h>
#include <linux/file.h>
#include <linux/splice.h>
#include <linux/uio.h>

#include "char_dev.h"
#include "connection.h"
#include "command.h"
#include "data_mapping.h"
#include "utils.h"

void *zero_page = NULL;
unsigned long zero_page_pfn = 0;

static int bius_dev_open(struct inode *inode, struct file *file) {
    struct bius_connection *connection;
    struct bius_block_device *device;
    int error = 0;

    if (only_device == NULL)
        return -EIO;
    device = only_device;

    connection = kmalloc(sizeof(struct bius_connection), GFP_KERNEL);
    if (connection == NULL)
        return -ENOMEM;

    init_bius_connection(connection);
    connection->block_dev = device;
    file->private_data = connection;

    connection->reserved_pages = kmalloc(PAGE_SIZE * BIUS_NUM_RESERVED_PAGES, GFP_KERNEL);
    if (connection->reserved_pages == NULL) {
        error = -ENOMEM;
        goto out_free;
    }
    connection->reserved_pages_pfn = PHYS_PFN(virt_to_phys(connection->reserved_pages));

    spin_lock(&device->connection_lock);
    device->num_connection++;
    if (!device->validated && device->num_connection >= 2)
        bius_revalidate(device);
    spin_unlock(&device->connection_lock);

    return 0;

out_free:
    kfree(connection);
    return error;
}

static ssize_t bius_dev_read(struct kiocb *iocb, struct iov_iter *to) {
    ssize_t total_read = 0;
    ssize_t ret;
    struct bius_connection *connection = get_bius_connection(iocb->ki_filp);
    struct bius_block_device *block_dev = connection->block_dev;
    struct bius_request *request;
    size_t user_buffer_size = iov_iter_count(to);

    printd("bius: dev_read: size = %ld\n", user_buffer_size);

    if (connection->sending) {
        request = connection->sending;

        total_read = bius_send_data(request, user_buffer_size, to);

        if (request_io_done(request))
            connection->sending = NULL;

        return total_read;
    }

    if (user_buffer_size < sizeof(struct bius_k2u_header))
        return -EINVAL;

    while (1) {
        spin_lock(&block_dev->pending_lock);
        if (!list_empty(&block_dev->pending_requests))
            break;

        spin_unlock(&block_dev->pending_lock);
        ret = wait_event_interruptible_exclusive(block_dev->wait_queue, !list_empty(&block_dev->pending_requests));

        if (ret)
            return ret;
    }

    request = list_entry(block_dev->pending_requests.next, struct bius_request, list);
    list_del(&request->list);
    spin_unlock(&block_dev->pending_lock);

    printd("bius: sending request: id = %llu, type = %d, pos = %lld, length = %lu\n", request->id, request->type, request->pos, request->length);

    if (request_may_have_data(request->type) && request->length > 0) {
        if (request->length > BIUS_MAP_DATA_THRESHOLD) {
            ret = bius_map_data(request, connection);
            if (ret < 0) {
                printk("bius: bius_map_data failed: %ld\n", ret);
                end_blk_request(request, BLK_STS_IOERR);
                return ret;
            }
        } else if (request_is_write(request->type)) {
            request->map_data = request->length;
            connection->sending = request;
        }
    }

    ret = bius_send_command(connection, request, to);
    if (ret <= 0) {
        end_blk_request(request, BLK_STS_IOERR);
        connection->sending = NULL;
        return ret;
    }

    total_read += ret;

    spin_lock(&connection->waiting_lock);
    list_add_tail(&request->list, &connection->waiting_requests);
    spin_unlock(&connection->waiting_lock);

    return total_read;
}

static ssize_t bius_dev_write(struct kiocb *iocb, struct iov_iter *from) {
    ssize_t total_written = 0;
    ssize_t ret;
    struct bius_connection *connection = get_bius_connection(iocb->ki_filp);
    size_t user_buffer_size = iov_iter_count(from);
    struct bius_request *request;
    struct bius_u2k_header header;

    printd("bius: dev_write: size = %ld\n", iov_iter_count(from));

    if (user_buffer_size < sizeof(struct bius_u2k_header))
        return -EINVAL;

    ret = copy_from_iter(&header, sizeof(header), from);
    if (ret <= 0)
        return ret;

    spin_lock(&connection->waiting_lock);
    request = get_request_by_id(&connection->waiting_requests, header.id);
    if (request)
        list_del(&request->list);
    spin_unlock(&connection->waiting_lock);

    if (!request)
        return -EINVAL;
    else if (request == connection->sending)
        connection->sending = NULL;

    total_written += ret;

    printd("bius: received response: id = %llu, reply = %ld\n", header.id, header.reply);

    if (is_blk_request(request->type)) {
        if (header.reply == BLK_STS_OK && request->type == BIUS_READ) {
            if (request->length <= BIUS_MAP_DATA_THRESHOLD) {
                void __user *data = (void __user *)header.user_data;

                ret = bius_receive_data(request, data);
                if (ret < 0) {
                    end_blk_request(request, BLK_STS_IOERR);
                    return ret;
                }
            } else {
                bius_copy_in_misaligned_pages(request, connection);
            }
        }

        bius_unmap_data(request, connection);

        if (request->type == BIUS_ZONE_APPEND)
            request->pos = (loff_t)header.user_data;

        end_blk_request(request, header.reply);
    } else {
        if (header.reply <= 0) {
            end_request_int(request, header.reply);
        } else {
            void __user *data = (void __user *)header.user_data;
            unsigned long result = copy_from_user(request->data, data, header.reply);

            if (unlikely(result > 0)) {
                printk("bius: copy_from_user failed: %lu\n", result);
                end_request_int(request, -EINVAL);
            } else {
                end_request_int(request, header.reply);
            }
        }
    }

    return total_written;
}

static int bius_dev_release(struct inode *inode, struct file *file) {
    struct bius_connection *connection = get_bius_connection(file);
    struct bius_block_device *device = connection->block_dev;
    struct bius_request *request;

    list_for_each_entry(request, &connection->waiting_requests, list) {
        end_blk_request(request, BLK_STS_IOERR);
    }

    if (device) {
        spin_lock(&device->connection_lock);
        device->num_connection--;
        spin_unlock(&device->connection_lock);
    }

    kfree(connection->reserved_pages);
    kfree(connection);
    return 0;
}

static int bius_dev_mmap(struct file *file, struct vm_area_struct *vma) {
    struct bius_connection *connection = get_bius_connection(file);
    size_t vma_size = vma->vm_end - vma->vm_start;

    if (connection->vma)
        return -EBUSY;
    if (vma_size < BIUS_MAX_SIZE_PER_COMMAND) {
        printk("bius: mmap: size is smaller than BIUS_MAX_SIZE_PER_COMMAND\n");
        return -EINVAL;
    }

    connection->vma = vma;
    vma->vm_flags |= VM_DONTCOPY | VM_DONTEXPAND | VM_DONTDUMP | VM_PFNMAP | VM_IO;
    vma->vm_private_data = connection;
    vma->vm_ops = &bius_vm_operations;

    bius_vm_open(vma);

    return 0;
}

const struct file_operations bius_dev_operations = {
    .owner = THIS_MODULE,
    .open = bius_dev_open,
    .llseek = no_llseek,
    .read_iter = bius_dev_read,
//    .splice_read = bius_dev_splice_read,
    .write_iter = bius_dev_write,
//    .splice_write = bius_dev_splice_write,
//    .poll = bius_dev_poll,
    .release = bius_dev_release,
//    .fasync = bius_dev_fasync,
//    .unlocked_ioctl = bius_dev_ioctl,
//    .compat_ioctl = compat_ptr_ioctl,
    .mmap = bius_dev_mmap,
};

static struct miscdevice bius_device = {
    .minor = BIUS_MINOR,
    .name = "bius",
    .fops = &bius_dev_operations,
};

int __init bius_dev_init(void) {
    zero_page = kzalloc(PAGE_SIZE, GFP_KERNEL);
    if (!zero_page)
        return -ENOMEM;
    zero_page_pfn = PHYS_PFN(virt_to_phys(zero_page));

    return misc_register(&bius_device);
}

void bius_dev_exit(void) {
    misc_deregister(&bius_device);
    if (zero_page)
        kfree(zero_page);
}
