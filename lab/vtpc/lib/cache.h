#ifndef CACHE_H
#define CACHE_H

#include <sys/types.h>

int my_open(const char*, int, int);
int my_close(int);
off_t my_lseek(int, off_t, int);
ssize_t my_read(int, void *, size_t);
ssize_t my_write(int, const void *, size_t);
int my_fsync(int);

#endif
