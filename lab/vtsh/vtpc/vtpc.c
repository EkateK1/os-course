#include "vtpc.h"

#include <fcntl.h>
#include <stddef.h>
#include <sys/types.h>
#include <unistd.h>

#include "cache.h"

int vtpc_open(const char* path, int mode, int access) {
  return my_open(path, mode, access);
}

int vtpc_close(int fd) {
  return my_close(fd);
}

ssize_t vtpc_read(int fd, void* buf, size_t count) {
  return my_read(fd, buf, count);
}

ssize_t vtpc_write(int fd, const void* buf, size_t count) {
  return my_write(fd, buf, count);
}

off_t vtpc_lseek(int fd, off_t offset, int whence) {
  return my_lseek(fd, offset, whence);
}

int vtpc_fsync(int fd) {
  return my_fsync(fd);
}
