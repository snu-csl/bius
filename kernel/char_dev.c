#include <linux/poll.h>
#include <linux/file.h>
#include <linux/splice.h>
#include <linux/uio.h>

#include "char_dev.h"
#include "connection.h"
#include "command.h"
#include "utils.h"

struct buse_connection *only_connection;

static int buse_dev_open(struct inode *inode, struct file *file) {
    struct buse_connection *connection = kmalloc(sizeof(struct buse_connection), GFP_KERNEL);
    init_buse_connection(connection);
    file->private_data = connection;

    if (only_connection == NULL)
        only_connection = connection;

    return 0;
}

static ssize_t buse_dev_read(struct kiocb *iocb, struct iov_iter *to) {
    ssize_t ret = 0;
    struct buse_connection *connection = get_buse_connection(iocb->ki_filp);
    struct buse_request *request;

    if (connection->sending) {
        request = connection->sending;

        ret = buse_send_data(request, iov_iter_count(to), to);

        if (request_io_done(request)) {
            connection->sending = NULL;
            end_request(request, BLK_STS_OK);
        }

        return ret;
    } else {
        size_t user_buffer_size = iov_iter_count(to);

        if (user_buffer_size < sizeof(struct buse_k2u_header))
            return -EINVAL;

        while (1) {
            spin_lock(&connection->pending_lock);
            if (!list_empty(&connection->pending_requests))
                break;

            spin_unlock(&connection->pending_lock);
            ret = wait_event_interruptible_exclusive(connection->wait_queue, !list_empty(&connection->pending_requests));

            if (ret)
                return ret;
        }

        request = list_entry(connection->pending_requests.next, struct buse_request, list);
        list_del(&request->list);
        spin_unlock(&connection->pending_lock);

        ret = buse_send_command(request, to);
        if (ret < 0)
            return ret;

        user_buffer_size -= ret;

        if (request->type == BUSE_READ) {
            spin_lock(&connection->waiting_lock);
            list_add(&request->list, &connection->waiting_requests);
            spin_unlock(&connection->waiting_lock);
        } else if (request->type == BUSE_WRITE) {
            if (user_buffer_size > 0)
                ret += buse_send_data(request, user_buffer_size, to);

            if (request_io_done(request))
                end_request(request, BLK_STS_OK);
            else
                connection->sending = request;
        }

        return ret;
    }
}

static ssize_t buse_dev_write(struct kiocb *iocb, struct iov_iter *from) {
    ssize_t ret = 0;
    struct buse_connection *connection = get_buse_connection(iocb->ki_filp);
    size_t user_buffer_size = iov_iter_count(from);
    struct buse_request *request;

    if (connection->receiving) {
        request = connection->receiving;

        ret = buse_receive_data(request, user_buffer_size, from);
        if (request_io_done(request)) {
            connection->receiving = NULL;
            end_request(request, BLK_STS_OK);
        }

        return ret;
    } else {
        struct buse_u2k_header header;

        if (user_buffer_size < sizeof(struct buse_u2k_header))
            return -EINVAL;

        ret = copy_from_iter(&header, sizeof(header), from);

        spin_lock(&connection->waiting_lock);
        request = get_request_by_id(&connection->waiting_requests, header.id);
        if (request)
            list_del(&request->list);
        spin_unlock(&connection->waiting_lock);

        if (!request)
            return -EINVAL;

        user_buffer_size -= ret;

        if (request->type == BUSE_READ) {
            if (user_buffer_size > 0) {
                ret += buse_receive_data(request, user_buffer_size, from);

                if (request_io_done(request)) {
                    end_request(request, BLK_STS_OK);
                    return ret;
                }
            }

            connection->receiving = request;
            spin_lock(&connection->waiting_lock);
            list_add(&request->list, &connection->waiting_requests);
            spin_unlock(&connection->waiting_lock);
        } else {
            end_request(request, BLK_STS_OK);
        }

        return ret;
    }
}

static int buse_dev_release(struct inode *inode, struct file *file) {
    struct buse_connection *connection = get_buse_connection(file);
    struct buse_request *request;

    if (connection == only_connection)
        only_connection = NULL;

    list_for_each_entry(request, &connection->pending_requests, list) {
        end_request(request, BLK_STS_IOERR);
    }

    list_for_each_entry(request, &connection->waiting_requests, list) {
        end_request(request, BLK_STS_IOERR);
    }

    if (connection->sending)
        end_request(connection->sending, BLK_STS_IOERR);
    if (connection->receiving)
        end_request(connection->receiving, BLK_STS_IOERR);

    kfree(connection);
    return 0;
}

const struct file_operations buse_dev_operations = {
    .owner = THIS_MODULE,
    .open = buse_dev_open,
    .llseek = no_llseek,
    .read_iter = buse_dev_read,
//    .splice_read = buse_dev_splice_read,
    .write_iter = buse_dev_write,
//    .splice_write = buse_dev_splice_write,
//    .poll = buse_dev_poll,
    .release = buse_dev_release,
//    .fasync = buse_dev_fasync,
//    .unlocked_ioctl = buse_dev_ioctl,
//    .compat_ioctl = compat_ptr_ioctl,
};

static struct miscdevice buse_device = {
    .minor = BUSE_MINOR,
    .name = "buse",
    .fops = &buse_dev_operations,
};

int __init buse_dev_init(void) {
    return misc_register(&buse_device);
}

void buse_dev_exit(void) {
    misc_deregister(&buse_device);
}
