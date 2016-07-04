#ifndef _KQUEUE_H
#define _KQUEUE_H

#include <linux/types.h>

#define MAX_MSIZE     0x10000
#define MAX_QLEN      0x00004

int    kqueue_init (char *cache_storage, int cache_async);
void   kqueue_free (void);

u_int  kqueue_get_size  (void);

void   kqueue_push_back (char **mdata, size_t  msize);
void   kqueue_pop_front (char **mdata, size_t *msize);

#endif
