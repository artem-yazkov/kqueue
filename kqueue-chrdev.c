#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>

#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/slab.h>

#include <asm/uaccess.h>

#include "kqueue-queue.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("artem.yazkov@gmail.com");

#define KQUEUE       "kqueue"
#define DEVPUSH      "kqueue-push"
#define DEVPOP       "kqueue-pop"

static        char   *cache_storage = "/tmp/kqueue-cache";
static        int     cache_async;

module_param(cache_storage, charp, S_IRUSR | S_IRGRP | S_IROTH);
module_param(cache_async,   int,   S_IRUSR | S_IRGRP | S_IROTH);

static        dev_t   rdev;
static struct class  *class;
static struct cdev   *cdev_push;
static struct cdev   *cdev_pop;
static struct device *dev_push;
static struct device *dev_pop;
static struct mutex   mutex_push;
static struct mutex   mutex_pop;

static DECLARE_WAIT_QUEUE_HEAD(wait_read);


static unsigned int
chrdev_fo_poll(struct file *file, struct poll_table_struct *poll_table)
{
    unsigned int mask = 0;

    poll_wait(file, &wait_read, poll_table);

    if (kqueue_get_size() > 0)
        mask |= POLLIN | POLLRDNORM;

    return mask;
}

static ssize_t
chrdev_fo_read(struct file *file, char *data, size_t size, loff_t *pos)
{
    if (*pos > 0) {
        *pos = 0;
        return 0;
    }

    char  *mdata = NULL;
    size_t msize = 0;

    kqueue_pop_front(&mdata, &msize);
    if (mdata == NULL)
        /* queue is empty */
        return 0;

    if (size < msize) {
        printk(KERN_ALERT "kqueue: insufficient user buffer: %lu/%lu bytes provided/necessary\n",
               size, msize);
        msize = size;
    }

    if (copy_to_user(data, mdata, msize) != 0 )
       return -EFAULT;

    *pos += msize;
    return msize;
}

static ssize_t
chrdev_fo_write(struct file *file, const char *data, size_t size, loff_t *pos)
{
    char  *mdata = NULL;

    if (size > MAX_MSIZE) {
        printk(KERN_ALERT "kqueue: message size too long - %lu bytes - be truncated to %d\n",
               size, MAX_MSIZE);
        size = MAX_MSIZE;
    }

    kqueue_push_back(&mdata, size);
    if (mdata == NULL)
        /* queue is full; with cache subsystem we should never care about */
        return -EFAULT;

    if (copy_from_user(mdata, data, size) != 0)
        return -EFAULT;

    wake_up_interruptible(&wait_read);

    *pos += size;
    return size;
}

static int
chrdev_fo_push_open(struct inode *inode, struct file *filp)
{
    if (!mutex_trylock(&mutex_push)) {
        printk(KERN_ALERT "kqueue: %s device has been opened already\n", DEVPUSH);
        return -EBUSY;
    }
    return 0;
}

static int
chrdev_fo_push_close(struct inode *inode, struct file *filp)
{
    mutex_unlock(&mutex_push);
    return 0;
}

static int
chrdev_fo_pop_open(struct inode *inode, struct file *filp)
{
    if (!mutex_trylock(&mutex_pop)) {
        printk(KERN_ALERT "kqueue: %s device has been opened already\n", DEVPOP);
        return -EBUSY;
    }
    return 0;
}

static int
chrdev_fo_pop_close(struct inode *inode, struct file *filp)
{
    mutex_unlock(&mutex_pop);
    return 0;
}

static struct
file_operations fops_push = {
    .owner   = THIS_MODULE,
    .write   = chrdev_fo_write,
    .open    = chrdev_fo_push_open,
    .release = chrdev_fo_push_close
};

static struct
file_operations fops_pop = {
    .owner   = THIS_MODULE,
    .poll    = chrdev_fo_poll,
    .read    = chrdev_fo_read,
    .open    = chrdev_fo_pop_open,
    .release = chrdev_fo_pop_close
};

static int
chrdev_init(void)
{
    int rcode = alloc_chrdev_region(&rdev, 0, 1, KQUEUE);
    if(rcode < 0) {
        printk(KERN_ALERT "kqueue: alloc_chrdev_region() failed\n");
        return rcode;
    }

    class = class_create(THIS_MODULE, KQUEUE);
    if (class == NULL) {
        printk(KERN_ALERT "kqueue: can't create device class\n");
        unregister_chrdev_region(rdev, 1);
        return -1;
    }

    dev_push  = device_create(class, NULL, MKDEV(MAJOR(rdev), 0), NULL, DEVPUSH);
    cdev_push = cdev_alloc();
    cdev_push->owner = THIS_MODULE;
    cdev_push->ops   = &fops_push;
    int r_push = cdev_add(cdev_push, MKDEV(MAJOR(rdev), 0), 1);

    if ((dev_push == NULL) && (r_push < 0)) {
        printk(KERN_ALERT "kqueue: can't create %s device\n", DEVPUSH);
        return -1;
    }

    dev_pop   = device_create(class, NULL, MKDEV(MAJOR(rdev), 1), NULL, DEVPOP);
    cdev_pop  = cdev_alloc();
    cdev_pop->owner = THIS_MODULE;
    cdev_pop->ops   = &fops_pop;
    int r_pop = cdev_add(cdev_pop,  MKDEV(MAJOR(rdev), 1), 1);

    if ((dev_pop == NULL) && (r_pop < 0)) {
        printk(KERN_ALERT "kqueue: can't create %s device\n", DEVPOP);
        return -1;
    }

    mutex_init(&mutex_pop);
    mutex_init(&mutex_push);

    if (kqueue_init(cache_storage, cache_async) < 0)
        return -1;

    printk(KERN_INFO "kqueue: initialized properly (async: %s)\n",
           cache_async ? "on" : "off");

    return 0;
}

static void
chrdev_exit(void)
{
    kqueue_free();

    mutex_destroy(&mutex_pop);
    mutex_destroy(&mutex_push);

    device_destroy(class, MKDEV(MAJOR(rdev), 0));
    cdev_del(cdev_push);

    device_destroy(class, MKDEV(MAJOR(rdev), 1));
    cdev_del(cdev_pop);

    class_destroy(class);
    unregister_chrdev_region(rdev, 1);

    printk(KERN_INFO "kqueue: deinitialized properly\n");
}

module_init(chrdev_init);
module_exit(chrdev_exit);

