#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/slab.h>

#include <asm/uaccess.h>

#include "kqueue-queue.h"

static struct cache {
    int a;
} cache;

static struct task_struct *cache_task;
static struct mutex        cache_mut;

static struct queue {
    char   *items[MAX_QLEN];
    size_t  sizes[MAX_QLEN];  /* current data sizes     */
    size_t  alszs[MAX_QLEN];  /* items allocation sizes */

    u_int   ihead;
    u_int   itail;
    u_int   length;
} queue;


static int
kqueue_thread(void *data)
{
    for (;;)
        mdelay(1000);
    return 0;
}

static void
kqueue_cache_store(char *mdata, size_t msize)
{
    cache.a = 0;
}

static void
kqueue_cache_load(char *mdata, size_t msize)
{
    cache.a = 0;
}

int
kqueue_init (char *cache_storage, int cache_async)
{
    mutex_init(&cache_mut);
    cache_task = kthread_run(&kqueue_thread, NULL, "kqueue-cache");

    return 0;
}

void
kqueue_free (void)
{
    kthread_stop(cache_task);
    mutex_destroy(&cache_mut);

    for (int item = 0; item < MAX_QLEN; item++)
        if (queue.items[item])
            kfree(queue.items[item]);
}

u_int
kqueue_get_size  (void)
{
    return queue.length;
}

void
kqueue_push_back(char **mdata, size_t msize)
{
    if (queue.length == MAX_QLEN) {
        // TODO: more action here
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

void
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
