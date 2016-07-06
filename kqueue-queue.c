#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/slab.h>

#include <asm/uaccess.h>

#include "kqueue-file.h"
#include "kqueue-queue.h"

enum task_action {TA_POP, TA_PUSH};

static struct cache {
    int                 async;
    struct task_struct *task;
    struct mutex        mutex;
    struct file        *file;

    enum   task_action  task_action;
    loff_t              off_tail;
    loff_t              off_head;
} cache;

#define MSG_POCKET0 (MAX_QLEN + 0)
#define MSG_POCKET1 (MAX_QLEN + 1)

static struct queue {
    char   *items[MAX_QLEN + 2];
    size_t  sizes[MAX_QLEN + 2];  /* current data sizes     */
    size_t  alszs[MAX_QLEN + 2];  /* items allocation sizes */

    u_int   ihead;
    u_int   itail;
    u_int   length;
} queue;

static void
kqueue_cache_store(char *mdata, size_t msize)
{
    kqueue_file_write(cache.file, cache.off_tail, (char *)&msize, sizeof(msize));
    cache.off_tail += sizeof(msize);
    kqueue_file_write(cache.file, cache.off_tail, mdata, msize);
    cache.off_tail += msize;
}

static int
kqueue_cache_load(char **mdata, size_t *msize, size_t *asize)
{
    if (cache.off_head == cache.off_tail)
        return 0;

    kqueue_file_read(cache.file, cache.off_head, (char *)msize, sizeof(*msize));
    cache.off_head += sizeof(*msize);

    if (*msize > *asize) {
        *mdata = krealloc(*mdata, *msize, GFP_KERNEL);
        *asize = *msize;
    }
    kqueue_file_read(cache.file, cache.off_head, *mdata, *msize);
    cache.off_head += *msize;

    if (cache.off_head > cache.off_tail) {
        cache.off_head = cache.off_tail = 0;
        return -1;
    }
    if (cache.off_head == cache.off_tail)
        cache.off_head = cache.off_tail = 0;

    return 1;
}

static int
kqueue_thread(void *data)
{
    for (;;) {
        set_current_state(TASK_INTERRUPTIBLE);
        schedule();

        mutex_lock(&cache.mutex);

        if (cache.task_action == TA_PUSH) {
            kqueue_cache_store(queue.items[queue.itail], queue.sizes[queue.itail]);
            if (queue.sizes[MSG_POCKET0] == 0) {
                kqueue_cache_load(&queue.items[MSG_POCKET0],
                                  &queue.sizes[MSG_POCKET0],
                                  &queue.alszs[MSG_POCKET0]);

            }
        }

        if (cache.task_action == TA_POP) {
            int rcode = kqueue_cache_load(&queue.items[MSG_POCKET0],
                                          &queue.sizes[MSG_POCKET0],
                                          &queue.alszs[MSG_POCKET0]);
            if (rcode <= 0)
                queue.sizes[MSG_POCKET0] = 0;
        }


        mutex_unlock(&cache.mutex);
    }

    return 0;
}

int
kqueue_init (char *cache_storage, int cache_async)
{
    cache.async = cache_async;
    cache.file = kqueue_file_open(cache_storage, O_CREAT | O_RDWR, 0);
    if (cache.file == NULL) {
        printk (KERN_ALERT "can't open %s as cache storage", cache_storage);
        return -1;
    }
    if (cache.async) {
        mutex_init(&cache.mutex);
        cache.task = kthread_run(&kqueue_thread, NULL, "kqueue-cache");
    }

    return 0;
}

void
kqueue_free (void)
{
    if (cache.async) {
        //kthread_stop(cache.task);
        //mutex_destroy(&cache.mutex);
    }
    kqueue_file_close(cache.file);

    for (int item = 0; item < MAX_QLEN; item++)
        if (queue.items[item])
            kfree(queue.items[item]);
}

u_int
kqueue_get_size  (void)
{
    return queue.length;
}

static void
kqueue_push_sync(char **mdata, size_t msize)
{
    if (queue.length == MAX_QLEN) {
        kqueue_cache_store(queue.items[queue.itail], queue.sizes[queue.itail]);
    }

    if (msize > queue.alszs[queue.itail]) {
        queue.items[queue.itail] = krealloc(queue.items[queue.itail], msize, GFP_KERNEL);
        queue.alszs[queue.itail] = msize;
    }
    queue.sizes[queue.itail] = msize;
    *mdata = queue.items[queue.itail];

    queue.itail = (queue.itail + 1) % MAX_QLEN;

    if (queue.length < MAX_QLEN)
        queue.length++;
}

static void
kqueue_pop_sync(char **mdata, size_t *msize)
{
    if ((queue.length == MAX_QLEN) &&
        (kqueue_cache_load(&queue.items[MAX_QLEN],
                            &queue.sizes[MAX_QLEN],
                            &queue.alszs[MAX_QLEN]) == 1)) {

        *mdata = queue.items[MAX_QLEN];
        *msize = queue.sizes[MAX_QLEN];
        return;
    }

    if (queue.length == MAX_QLEN)
        queue.ihead = queue.itail;

    if (queue.length == 0) {
        *mdata = NULL;
        *msize = 0;
        return;
    }

    *mdata = queue.items[queue.ihead];
    *msize = queue.sizes[queue.ihead];

    queue.ihead = (queue.ihead + 1) % MAX_QLEN;
    queue.length--;
}

static void
kqueue_push_async(char **mdata, size_t msize)
{
    if (msize > queue.alszs[queue.itail]) {
        queue.items[queue.itail] = krealloc(queue.items[queue.itail], msize, GFP_KERNEL);
        queue.alszs[queue.itail] = msize;
    }
    queue.sizes[queue.itail] = msize;
    *mdata = queue.items[queue.itail];

    queue.itail = (queue.itail + 1) % MAX_QLEN;

    if (queue.length < MAX_QLEN)
        queue.length++;
}

static void
kqueue_pop_async(char **mdata, size_t *msize)
{
    if (queue.sizes[MSG_POCKET0] > 0) {
        static char   *item, *item_sw;
        static size_t  size,  size_sw;
        static size_t  alsz,  alsz_sw;

        item_sw = item; item = queue.items[MSG_POCKET0]; queue.items[MSG_POCKET0] = item_sw;
        size_sw = size; size = queue.sizes[MSG_POCKET0]; queue.sizes[MSG_POCKET0] = size_sw;
        alsz_sw = alsz; alsz = queue.alszs[MSG_POCKET0]; queue.alszs[MSG_POCKET0] = alsz_sw;

        *mdata = item;
        *msize = size;

        queue.sizes[MSG_POCKET0] = 0;
        return;
    }

    if (queue.length == MAX_QLEN) {
        queue.ihead = (queue.itail + 1) % MAX_QLEN;
        queue.length--;
    }

    if (queue.length == 0) {
        *mdata = NULL;
        *msize = 0;
        return;
    }

    *mdata = queue.items[queue.ihead];
    *msize = queue.sizes[queue.ihead];

    queue.ihead = (queue.ihead + 1) % MAX_QLEN;
    queue.length--;
}

void
kqueue_push_back(char **mdata, size_t msize)
{
    if (cache.async) {
        mutex_lock(&cache.mutex);
        kqueue_push_async(mdata, msize);
        cache.task_action = TA_PUSH;
        mutex_unlock(&cache.mutex);

        if (queue.length == MAX_QLEN)
            wake_up_process(cache.task);
    } else
        kqueue_push_sync(mdata, msize);
}

void
kqueue_pop_front(char **mdata, size_t *msize)
{
    if (cache.async) {
        mutex_lock(&cache.mutex);
        kqueue_pop_async(mdata, msize);
        cache.task_action = TA_POP;
        mutex_unlock(&cache.mutex);

        if (queue.length == MAX_QLEN)
            wake_up_process(cache.task);
    } else
        kqueue_pop_sync(mdata, msize);
}
