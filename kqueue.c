#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/slab.h>

#include <asm/uaccess.h>

#define KQUEUE       "kqueue"
#define DEVPUSH      "kqueue-push"
#define DEVPOP       "kqueue-pop"

#define MAX_MSG       0x10000
#define MAX_QUEUE     0x00400

static        dev_t   rdev;
static struct class  *class;
static struct cdev   *cdev_push;
static struct cdev   *cdev_pop;
static struct device *dev_push;
static struct device *dev_pop;

static        char   buffer[MAX_MSG];
static        size_t buffer_sz;

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

static ssize_t
kqueue_fo_read(struct file *file, char *data, size_t size, loff_t *pos)
{
    if( *pos >= buffer_sz)
       return 0;

    if( *pos + size > buffer_sz)
       size = buffer_sz - *pos;

    if (copy_to_user(data, buffer + *pos, size) != 0 )
       return -EFAULT;

    *pos += size;
    return size;
}

static ssize_t
kqueue_fo_write(struct file *file, const char *data, size_t size, loff_t *pos)
{
    if( *pos >= MAX_MSG)
       return 0;

    size_t wrsize = MAX_MSG - *pos;
    if (wrsize > size)
        wrsize = size;

    wrsize = wrsize - copy_from_user(buffer + *pos, data, wrsize);

    buffer_sz = ( *pos += wrsize );
    return wrsize;
}

static struct
file_operations fops_push = {
    .owner   = THIS_MODULE,
    .write   = kqueue_fo_write
};

static struct
file_operations fops_pop = {
    .owner   = THIS_MODULE,
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
    buffer_sz = snprintf(buffer, MAX_MSG, "%s\n", "buffer initial value");

    printk(KERN_INFO "kqueue: initialized properly\n");

    return 0;
}

static void
kqueue_exit(void)
{
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
