#ifndef _KQUEUE_FILE_H
#define _KQUEUE_FILE_H

#include <linux/fs.h>

struct file*
kqueue_file_open(const char* path, int flags, int rights);

void
kqueue_file_close(struct file* file);

int
kqueue_file_read(struct file* file, loff_t offset, char* data, size_t size);

int
kqueue_file_write(struct file* file, loff_t offset, char* data, size_t size);

#endif
