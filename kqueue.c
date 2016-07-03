#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <asm/uaccess.h>

#define MAX_MSG      0x10000
#define MAX_QUEUE    0x00400

static        dev_t  ndev;
static struct cdev  *cdev;

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
file_operations fops = {
    .owner   = THIS_MODULE,
    .read    = kqueue_fo_read,
    .write   = kqueue_fo_write
};

static int
kqueue_init(void)
{
    int rcode = alloc_chrdev_region(&ndev, 0, 1, "kqueue");
    if(rcode < 0) {
        printk(KERN_ALERT "kqueue: alloc_chrdev_region() failed\n");
        return rcode;
    }
    printk(KERN_INFO "kqueue: major number of our device is %d\n", MAJOR(ndev));
    printk(KERN_INFO "kqueue: to use mknod /dev/%s c %d 0\n",      "kqueue", MAJOR(ndev));

    cdev = cdev_alloc();
    cdev->owner = THIS_MODULE;
    cdev->ops   = &fops;

    rcode = cdev_add(cdev, ndev, 1);
    if(rcode < 0) {
        printk(KERN_ALERT "kqueue: device adding to the kernel failed\n");
        return rcode;
    }

    buffer_sz = snprintf(buffer, MAX_MSG, "%s\n", "buffer initial value");

    printk(KERN_INFO "kqueue: initialized properly\n");

    return 0;
}

static void
kqueue_exit(void)
{
    cdev_del(cdev);
    unregister_chrdev_region(ndev,1);

    printk(KERN_INFO "kqueue: deinitialized properly\n");
}

module_init(kqueue_init);
module_exit(kqueue_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("artem.yazkov@gmail.com");
