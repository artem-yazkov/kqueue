#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/slab.h>

#include <asm/uaccess.h>

#define KQUEUE       "kqueue"
#define DEVPUSH      "kqueue-push"
#define DEVPOP       "kqueue-pop"

#define MAX_MSIZE     0x10000
#define MAX_QLEN      0x00400


static struct queue {
    char   *items[MAX_QLEN];
    size_t  sizes[MAX_QLEN];  /* current data sizes     */
    size_t  alszs[MAX_QLEN];  /* items allocation sizes */

    int     ihead;
    int     itail;
    int     length;
} queue;

static        dev_t   rdev;
static struct class  *class;
static struct cdev   *cdev_push;
static struct cdev   *cdev_pop;
static struct device *dev_push;
static struct device *dev_pop;

static DECLARE_WAIT_QUEUE_HEAD(wait_read);

static void
kqueue_push_back(char **mdata, size_t msize)
{
    if (queue.length == MAX_QLEN) {
        // TODO: more actions here
        *mdata = NULL;
        return;
    }

    if (msize > queue.alszs[queue.itail]) {
        queue.items[queue.itail] = krealloc(queue.items[queue.itail], msize, GFP_KERNEL);
        queue.alszs[queue.itail] = msize;
    }

    queue.sizes[queue.itail] = msize;
    *mdata = queue.items[queue.itail];

    if (queue.itail < (MAX_QLEN - 1))
        queue.itail++;
    else
        queue.itail = 0;

    queue.length++;

}

static void
kqueue_pop_front(char **mdata, size_t *msize)
{
    if (queue.length == 0) {
        *mdata = NULL;
        *msize = 0;
        return;
    }
    *mdata = queue.items[queue.ihead];
    *msize = queue.sizes[queue.ihead];

    if (queue.ihead < (MAX_QLEN - 1))
        queue.ihead++;
    else
        queue.ihead = 0;

    queue.length--;
}



static int
kqueue_fo_open(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "kqueue: device opened\n");
    return 0;
}

static int
kqueue_fo_release(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "kqueue: device released\n");
    return 0;
}

static unsigned int
kqueue_fo_poll(struct file *file, struct poll_table_struct *poll_table)
{
    unsigned int mask = 0;

    poll_wait(file, &wait_read, poll_table);

    if (queue.length)
        mask |= POLLIN | POLLRDNORM;

    return mask;
}

static ssize_t
kqueue_fo_read(struct file *file, char *data, size_t size, loff_t *pos)
{
    printk(KERN_INFO "kqueue_fo_read; pos: %lld\n", *pos);
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
kqueue_fo_write(struct file *file, const char *data, size_t size, loff_t *pos)
{
    char  *mdata = NULL;

    if (size > MAX_MSIZE) {
        printk(KERN_ALERT "kqueue: message size too long - %lu bytes - be truncated to %d\n",
               size, MAX_MSIZE);
        size = MAX_MSIZE;
    }

    kqueue_push_back(&mdata, size);
    if (mdata == NULL) {
        /* queue is full */
        // TODO: more actions here
        return -EFAULT;
    }

    if (copy_from_user(mdata, data, size) != 0)
        return -EFAULT;

    wake_up_interruptible(&wait_read);

    *pos += size;
    return size;
}

static struct
file_operations fops_push = {
    .owner   = THIS_MODULE,
    .write   = kqueue_fo_write
};

static struct
file_operations fops_pop = {
    .owner  = THIS_MODULE,
    .poll   = kqueue_fo_poll,
    .read   = kqueue_fo_read
};

static int
kqueue_init(void)
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

    dev_pop   = device_create(class, NULL, MKDEV(MAJOR(rdev), 1), NULL, DEVPOP);
    cdev_pop  = cdev_alloc();
    cdev_pop->owner = THIS_MODULE;
    cdev_pop->ops   = &fops_pop;
    int r_pop = cdev_add(cdev_pop,  MKDEV(MAJOR(rdev), 1), 1);

#if 0
    if (!dev_push || !dev_pop) {
        printk(KERN_ALERT "kqueue: can't create device\n");
        class_destroy(class);
        unregister_chrdev_region(rdev, 1);
        return -1;
    }

    if ((r_push < 0) || (r_pop < 0)) {
        printk(KERN_ALERT "kqueue: device adding to the kernel failed\n");
        device_destroy(class, rdev);
        class_destroy(class);
        unregister_chrdev_region(rdev, 1);
        return rcode;
    }
#endif

    printk(KERN_INFO "kqueue: initialized properly\n");

    return 0;
}

static void
kqueue_exit(void)
{
    for (int item = 0; item < MAX_QLEN; item++)
        if (queue.items[item])
            kfree(queue.items[item]);

    device_destroy(class, MKDEV(MAJOR(rdev), 0));
    cdev_del(cdev_push);

    device_destroy(class, MKDEV(MAJOR(rdev), 1));
    cdev_del(cdev_pop);

    class_destroy(class);
    unregister_chrdev_region(rdev, 1);

    printk(KERN_INFO "kqueue: deinitialized properly\n");
}

module_init(kqueue_init);
module_exit(kqueue_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("artem.yazkov@gmail.com");
