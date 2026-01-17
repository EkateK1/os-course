#define _GNU_SOURCE

#include "cache.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define CACHE_SIZE 80
#define PAGE_SIZE 4096

struct cache_page {
  int fd;
  int block_number;
  int used;
  int dirty;
  char* data;
  size_t valid;
};

struct my_file {
  int fd;
  off_t position;
  int flags;
  off_t size;
};

static int fifo_queue[CACHE_SIZE];
static int fifo_size = 0;

static struct my_file* my_files = NULL;
static struct cache_page* cache = NULL;

static int init(void) {
  my_files = malloc(CACHE_SIZE * sizeof(struct my_file));
  cache = malloc(CACHE_SIZE * sizeof(struct cache_page));
  if (cache == NULL || my_files == NULL) {
    perror("malloc");
    return -1;
  }

  for (int i = 0; i < CACHE_SIZE; ++i) {
    my_files[i].fd = -1;
    my_files[i].position = 0;
    my_files[i].flags = 0;
    my_files[i].size = 0;
  }

  for (int i = 0; i < CACHE_SIZE; ++i) {
    cache[i].fd = -1;
    cache[i].block_number = -1;
    cache[i].used = 0;
    cache[i].dirty = 0;
    cache[i].data = NULL;
    cache[i].valid = 0;

    if (posix_memalign((void**)&cache[i].data, PAGE_SIZE, PAGE_SIZE) != 0) {
      perror("posix_memalign");
      return -1;
    }
  }

  fifo_size = 0;
  return 0;
}

static int check_cache_init(void) {
  if (my_files == NULL || cache == NULL) {
    return init();
  }
  return 0;
}

static int check_is_in_arr(int fd) {
  for (int i = 0; i < CACHE_SIZE; i++) {
    if (my_files[i].fd == fd) {
      return i;
    }
  }
  return -1;
}

static int check_is_in_cache(int fd, int block_number) {
  for (int i = 0; i < CACHE_SIZE; i++) {
    if (cache[i].fd == fd && cache[i].block_number == block_number) {
      return i;
    }
  }
  return -1;
}

static int flush_page(int idx) {
  if (idx < 0 || idx >= CACHE_SIZE) {
    return -1;
  }
  struct cache_page* p = &cache[idx];
  if (p->fd >= 0 && p->dirty) {
    off_t offset = (off_t)p->block_number * PAGE_SIZE;
    ssize_t n = pwrite(p->fd, p->data, PAGE_SIZE, offset);
    if (n < 0) {
      perror("pwrite");
      return -1;
    }
    if (n != PAGE_SIZE) {
      printf("short pwrite() while flushing page\n");
      return -1;
    }
    p->dirty = 0;
  }
  return 0;
}

static void fifo_remove(int page_idx) {
  for (int i = 0; i < fifo_size; ++i) {
    if (fifo_queue[i] == page_idx) {
      memmove(
          &fifo_queue[i], &fifo_queue[i + 1], (fifo_size - i - 1) * sizeof(int)
      );
      fifo_size--;
      return;
    }
  }
}

static int save_to_cache(int fd, int block_number) {
  if (check_cache_init() < 0) {
    return -1;
  }

  int idx = check_is_in_cache(fd, block_number);
  if (idx >= 0) {
    cache[idx].used = 1;
    return idx;
  }

  int chosen = -1;

  for (int i = 0; i < CACHE_SIZE; ++i) {
    if (cache[i].fd < 0) {
      chosen = i;
      fifo_queue[fifo_size++] = chosen;
      break;
    }
  }

  if (chosen < 0) {
    while (true) {
      int candidate = fifo_queue[0];
      struct cache_page* p = &cache[candidate];

      if (p->used == 0) {
        chosen = candidate;

        memmove(&fifo_queue[0], &fifo_queue[1], (fifo_size - 1) * sizeof(int));
        fifo_queue[fifo_size - 1] = candidate;

        break;
      }
      p->used = 0;
      memmove(&fifo_queue[0], &fifo_queue[1], (fifo_size - 1) * sizeof(int));
      fifo_queue[fifo_size - 1] = candidate;
    }
  }

  struct cache_page* victim = &cache[chosen];

  if (flush_page(chosen) < 0) {
    return -1;
  }

  int fidx = check_is_in_arr(fd);
  if (fidx < 0) {
    errno = EBADF;
    return -1;
  }
  struct my_file* f = &my_files[fidx];

  off_t file_off = (off_t)block_number * PAGE_SIZE;

  if (file_off >= f->size) {
    victim->fd = fd;
    victim->block_number = block_number;
    victim->used = 1;
    victim->dirty = 0;
    victim->valid = 0;
    return chosen;
  }

  ssize_t n = pread(fd, victim->data, PAGE_SIZE, file_off);
  if (n < 0) {
    perror("pread");
    return -1;
  }

  victim->fd = fd;
  victim->block_number = block_number;
  victim->used = 1;
  victim->dirty = 0;
  victim->valid = (size_t)n;

  return chosen;
}

static int flush_all_pages_for_fd(int fd) {
  for (int i = 0; i < CACHE_SIZE; ++i) {
    if (cache[i].fd == fd) {
      if (flush_page(i) < 0) {
        return -1;
      }
    }
  }
  return 0;
}

static void invalidate_pages_for_fd(int fd) {
  for (int i = 0; i < CACHE_SIZE; ++i) {
    if (cache[i].fd == fd) {
      fifo_remove(i);

      cache[i].fd = -1;
      cache[i].block_number = -1;
      cache[i].used = 0;
      cache[i].dirty = 0;
      cache[i].valid = 0;
    }
  }
}

int my_open(const char* path, int flags, int mode) {
  if (check_cache_init() < 0) {
    return -1;
  }

  if (!(flags & O_DIRECT)) {
    flags |= O_DIRECT;
  }

  int fd = open(path, flags, mode);
  if (fd < 0) {
    perror("open");
    return -1;
  }

  off_t end = lseek(fd, 0, SEEK_END);
  if (end < 0) {
    perror("lseek");
    end = 0;
  }
  (void)lseek(fd, 0, SEEK_SET);

  bool placed = false;
  for (int i = 0; i < CACHE_SIZE; i++) {
    if (my_files[i].fd < 0) {
      my_files[i].fd = fd;
      my_files[i].position = 0;
      my_files[i].flags = flags;
      my_files[i].size = end;
      placed = true;
      break;
    }
  }

  if (!placed) {
    printf("Too many files are open\n");
    close(fd);
    return -1;
  }

  return fd;
}

int my_close(int fd) {
  if (check_cache_init() < 0) {
    return -1;
  }

  int idx = check_is_in_arr(fd);
  if (idx < 0) {
    errno = EBADF;
    printf("my_close: descriptor %d is not managed by cache\n", fd);
    return -1;
  }

  if (flush_all_pages_for_fd(fd) < 0) {
    return -1;
  }
  invalidate_pages_for_fd(fd);

  my_files[idx].fd = -1;
  my_files[idx].position = 0;
  my_files[idx].flags = 0;
  my_files[idx].size = 0;

  int res = close(fd);
  if (res < 0) {
    perror("close");
    return -1;
  }
  return 0;
}

off_t my_lseek(int fd, off_t offset, int whence) {
  if (check_cache_init() < 0) {
    return -1;
  }

  int idx = check_is_in_arr(fd);
  if (idx < 0) {
    errno = EBADF;
    printf("my_lseek: descriptor %d is not managed by cache\n", fd);
    return (off_t)-1;
  }

  off_t new_pos = 0;
  switch (whence) {
    case SEEK_SET:
      new_pos = offset;
      break;
    case SEEK_CUR:
      new_pos = my_files[idx].position + offset;
      break;
    case SEEK_END: {
      off_t end = my_files[idx].size;
      new_pos = end + offset;
      break;
    }
    default:
      errno = EINVAL;
      return (off_t)-1;
  }

  if (new_pos < 0) {
    errno = EINVAL;
    return (off_t)-1;
  }

  my_files[idx].position = new_pos;
  return new_pos;
}

ssize_t my_read(int fd, void* buf, size_t count) {
  if (check_cache_init() < 0) {
    return -1;
  }

  int idx = check_is_in_arr(fd);
  if (idx < 0) {
    errno = EBADF;
    printf("my_read: descriptor %d is not managed by cache\n", fd);
    return -1;
  }

  struct my_file* f = &my_files[idx];

  int accmode = f->flags & O_ACCMODE;
  if (accmode == O_WRONLY) {
    errno = EBADF;
    printf("Not enough rights for reading file %d\n", fd);
    return -1;
  }

  if (count == 0) {
    return 0;
  }

  if (f->position >= f->size) {
    return 0;
  }

  off_t pos = f->position;
  size_t remaining = count;
  size_t copied = 0;
  while (remaining > 0) {
    if (pos >= f->size) {
      break;
    }

    int block_number = (int)(pos / PAGE_SIZE);
    int page_off = (int)(pos % PAGE_SIZE);

    int page_idx = check_is_in_cache(fd, block_number);
    if (page_idx < 0) {
      page_idx = save_to_cache(fd, block_number);
      if (page_idx < 0) {
        if (copied > 0) {
          break;
        }
        return -1;
      }
    } else {
      cache[page_idx].used = 1;
    }

    struct cache_page* p = &cache[page_idx];

    if (p->valid == 0 || (size_t)page_off >= p->valid) {
      break;
    }

    size_t can_copy = p->valid - (size_t)page_off;
    if (can_copy > remaining) {
      can_copy = remaining;
    }

    if (can_copy == 0) {
      break;
    }

    memcpy((char*)buf + copied, p->data + page_off, can_copy);

    copied += can_copy;
    remaining -= can_copy;
    pos += (off_t)can_copy;
  }

  f->position += (off_t)copied;

  return (ssize_t)copied;
}

ssize_t my_write(int fd, const void* buf, size_t count) {
  if (check_cache_init() < 0) {
    return -1;
  }

  int idx = check_is_in_arr(fd);
  if (idx < 0) {
    errno = EBADF;
    printf("my_write: descriptor %d is not managed by cache\n", fd);
    return -1;
  }

  struct my_file* f = &my_files[idx];

  int accmode = f->flags & O_ACCMODE;
  if (accmode == O_RDONLY) {
    errno = EBADF;
    printf("Not enough rights for writing file %d\n", fd);
    return -1;
  }

  if (count == 0) {
    return 0;
  }

  off_t pos = f->position;
  size_t remaining = count;
  size_t written = 0;

  while (remaining > 0) {
    int block_number = (int)(pos / PAGE_SIZE);
    int page_off = (int)(pos % PAGE_SIZE);

    int page_idx = check_is_in_cache(fd, block_number);
    if (page_idx < 0) {
      page_idx = save_to_cache(fd, block_number);
      if (page_idx < 0) {
        if (written > 0) {
          break;
        }
        return -1;
      }
    } else {
      cache[page_idx].used = 1;
    }

    struct cache_page* p = &cache[page_idx];

    size_t can_copy = PAGE_SIZE - (size_t)page_off;
    if (can_copy > remaining) {
      can_copy = remaining;
    }

    memcpy(p->data + page_off, (const char*)buf + written, can_copy);
    p->dirty = 1;

    size_t end_off = (size_t)page_off + can_copy;
    if (end_off > p->valid) {
      p->valid = end_off;
    }

    written += can_copy;
    remaining -= can_copy;
    pos += (off_t)can_copy;
  }

  f->position += (off_t)written;
  if (f->position > f->size) {
    f->size = f->position;
  }
  return (ssize_t)written;
}

int my_fsync(int fd) {
  if (check_cache_init() < 0) {
    return -1;
  }

  int idx = check_is_in_arr(fd);
  if (idx < 0) {
    errno = EBADF;
    printf("my_fsync: descriptor %d is not managed by cache\n", fd);
    return -1;
  }

  if (flush_all_pages_for_fd(fd) < 0) {
    return -1;
  }

  if (fsync(fd) < 0) {
    perror("fsync");
    return -1;
  }

  return 0;
}
