#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BLOCK_SIZE 4096

struct params {
  char rw;
  long block_size;
  long block_count;
  char* file;
  long range_min;
  long range_max;
  bool direct;
  char type;
};

struct params fill_params(char** args) {
  char* del = "=";
  char* end;
  struct params params = {0};
  for (size_t i = 1; i < 8; i++) {
    char* param = strtok(args[i], del);
    char* value = strtok(NULL, del);

    if (strcmp(param, "rw") == 0) {
      if (strcmp(value, "read") == 0) {
        params.rw = 'r';
      } else if (strcmp(value, "write") == 0) {
        params.rw = 'w';
      }
    }

    if (strcmp(param, "block_size") == 0) {
      long val = strtol(value, &end, 10);
      if (value != end && errno != ERANGE) {
        params.block_size = val;
      }
    }

    if (strcmp(param, "block_count") == 0) {
      long val = strtol(value, &end, 10);
      if (value != end && errno != ERANGE) {
        params.block_count = val;
      }
    }

    if (strcmp(param, "file") == 0) {
      params.file = value;
    }

    if (strcmp(param, "range") == 0) {
      char* range_del = "-";
      char* range_min = strtok(value, range_del);
      char* range_max = strtok(NULL, range_del);

      long min = strtol(range_min, &end, 10);
      if (range_min != end && errno != ERANGE) {
        params.range_min = min;
      }

      long max = strtol(range_max, &end, 10);
      if (range_max != end && errno != ERANGE) {
        params.range_max = max;
      }
    }

    if (strcmp(param, "direct") == 0) {
      if (strcmp(value, "on") == 0) {
        params.direct = true;
      } else if (strcmp(value, "off") == 0) {
        params.direct = false;
      }
    }

    if (strcmp(param, "type") == 0) {
      if (strcmp(value, "sequence") == 0) {
        params.type = 's';
      } else if (strcmp(value, "random") == 0) {
        params.type = 'r';
      }
    }
  }

  return params;
}

void print_params(const struct params* p) {
  printf("rw: %c\n", p->rw);
  printf("block_size: %ld\n", p->block_size);
  printf("block_count: %ld\n", p->block_count);
  printf("file: %s\n", p->file ? p->file : "(null)");
  printf("range_min: %ld\n", p->range_min);
  printf("range_max: %ld\n", p->range_max);
  printf("direct: %s\n", p->direct ? "true" : "false");
  printf("type: %c\n", p->type);
}

bool is_valid(const struct params* p) {
  if (p->rw == 0) {
    return false;
  }
  if (p->block_size < 1) {
    return false;
  }
  if (p->block_count < 1) {
    return false;
  }
  if (p->file == NULL) {
    return false;
  }
  if (p->range_min < 0) {
    return false;
  }
  if (p->range_max < 0) {
    return false;
  }
  if ((p->range_min != 0) && (p->range_min >= p->range_max)) {
    return false;
  }
  if (p->type == 0) {
    return false;
  }
  return true;
}

void random_bytes(void* buf, size_t size) {
  unsigned char* p = buf;
  for (size_t i = 0; i < size; i++)
    p[i] = (rand() % (126 - 32 + 1)) + 32;
}

bool check_range(struct params* params) {
  if (params->range_max - params->range_min + 1 <
      params->block_size * params->block_count) {
    printf("Range is smaller than you need\n");
    return false;
  }
  return true;
}

bool random_lseek(
    int file_descr,
    long block_size,
    long block_count,
    long range_min,
    long range_max
) {
  int position;

  if (range_min == 0 && range_max == 0) {
    long max = block_size * block_count - block_size;
    position = rand() % max;
  } else {
    position = range_min + rand() % range_max;
  }

  off_t offset_res = lseek(file_descr, position, SEEK_SET);

  if (offset_res == -1)
    return false;

  return true;
}

bool sequence_read(int file_descr, long block_size, char** buf) {
  int res = read(file_descr, *buf, block_size);
  if (res != -1) {
    (*buf)[block_size] = '\0';
    printf("%s\n", *buf);
  } else {
    printf("%s\n", strerror(errno));
  }
  return (res != -1);
}

bool sequence_write(int file_descr, long block_size, char** buf) {
  random_bytes(*buf, block_size);
  int res = write(file_descr, *buf, block_size);
  if (res != -1) {
    printf("Succesfull writing\n");
  } else {
    printf("%s\n", strerror(errno));
  }
  return (res != -1);
}

bool simple_sequence_call(
    int file_descr,
    long block_size,
    long block_count,
    char** buf,
    bool (*seq)(int, long, char**)
) {
  for (long i = 0; i < block_count; i++) {
    bool res = seq(file_descr, block_size, buf);
    if (!res)
      return false;
  }
  return true;
}

bool simple_random_call(
    int file_descr,
    long block_size,
    long block_count,
    long range_min,
    long range_max,
    char** buf,
    bool (*seq)(int, long, char**)
) {
  for (long i = 0; i < block_count; i++) {
    if (!random_lseek(
            file_descr, block_size, block_count, range_min, range_max
        )) {
      return false;
    }

    bool res = seq(file_descr, block_size, buf);
    if (!res)
      return false;
  }
  return true;
}

bool rw_simple(int file_descr, struct params* params) {
  char* buf = malloc(sizeof(char) * (params->block_size + 1));
  bool res;

  if (params->range_min != 0 || params->range_max != 0) {
    if (!check_range(params)) {
      free(buf);
      return false;
    }
  }

  if ((params->type == 's') && (params->range_min != 0)) {
    if (params->range_min != 0) {
      off_t offset_res = lseek(file_descr, params->range_min, SEEK_SET);
      if (offset_res == -1) {
        free(buf);
        return false;
      }
    }
  }

  if (params->rw == 'r') {
    if (params->type == 's') {
      res = simple_sequence_call(
          file_descr,
          params->block_size,
          params->block_count,
          &buf,
          sequence_read
      );

    } else if (params->type == 'r') {
      res = simple_random_call(
          file_descr,
          params->block_size,
          params->block_count,
          params->range_min,
          params->range_max,
          &buf,
          sequence_read
      );
    }
    free(buf);
    return res;
  }

  if (params->rw == 'w') {
    if (params->type == 's') {
      res = simple_sequence_call(
          file_descr,
          params->block_size,
          params->block_count,
          &buf,
          sequence_write
      );

    } else if (params->type == 'r') {
      res = simple_random_call(
          file_descr,
          params->block_size,
          params->block_count,
          params->range_min,
          params->range_max,
          &buf,
          sequence_write
      );
    }
    free(buf);
    return res;
  }
  free(buf);
  return true;
}

off_t direct_random_offset(
    int file_descr, long block_size, long range_min, long range_max
) {
  struct stat st;
  if (fstat(file_descr, &st) != 0) {
    perror("fstat");
    return -1;
  }
  off_t fsize = st.st_size;

  long max = 0;
  if (fsize - block_size > 0) {
    max = fsize - block_size;
  }
  off_t offset;

  if (range_max == 0 && range_min == 0) {
    if (max == 0) {
      offset = 0;
    } else {
      offset = (rand() % max);
    }
  } else {
    offset = range_min + rand() % range_max;
  }
  return offset;
}

bool direct_sequence_read(
    int file_descr, char** buf, long block_size, off_t offset, size_t buf_size
) {
  ssize_t n = -1;
  if (offset == 0 || offset % BLOCK_SIZE == 0) {
    n = pread(file_descr, *buf, buf_size, offset);
  } else {
    off_t read_offset = (offset / BLOCK_SIZE) * BLOCK_SIZE;
    n = pread(file_descr, *buf, buf_size, read_offset);
  }

  if (n < 0) {
    fprintf(stderr, "pread failed: %s\n", strerror(errno));
    return false;
  } else {
    (*buf)[offset + block_size] = '\0';
    printf("%s\n", *buf + offset);
    return true;
  }
}

bool direct_sequence_write(
    int file_descr, char** buf, long block_size, off_t offset, size_t buf_size
) {
  ssize_t n = -1;
  off_t read_offset = offset;
  if (offset == 0 || offset % BLOCK_SIZE == 0) {
    n = pread(file_descr, *buf, buf_size, read_offset);
  } else {
    read_offset = (offset / BLOCK_SIZE) * BLOCK_SIZE;
    n = pread(file_descr, *buf, buf_size, read_offset);
  }
  if (n < 0) {
    fprintf(stderr, "pread failed: %s\n", strerror(errno));
    return false;
  }

  random_bytes(*buf + offset, block_size);
  n = pwrite(file_descr, *buf, buf_size, read_offset);

  if (n < 0) {
    fprintf(stderr, "pwrite failed: %s\n", strerror(errno));
    return false;
  } else {
    printf("Write %zd bytes\n", block_size);
    return true;
  }
}

bool direct_sequence_call(
    int file_descr,
    long block_size,
    long block_count,
    char** buf,
    off_t min,
    size_t buf_size,
    bool (*seq)(int, char**, long, off_t, size_t)
) {
  off_t offset;
  for (long i = 0; i < block_count; i++) {
    offset = min + i * block_size;
    bool res = seq(file_descr, buf, block_size, offset, buf_size);
    if (!res)
      return false;
  }
  return true;
}

bool direct_random_call(
    int file_descr,
    char** buf,
    long block_size,
    long block_count,
    int min,
    int max,
    size_t buf_size,
    bool (*seq)(int, char**, long, off_t, size_t)
) {
  off_t offset;
  for (long i = 0; i < block_count; i++) {
    offset = direct_random_offset(file_descr, block_size, min, max);
    if (offset < 0) {
      return false;
    }
    bool res = seq(file_descr, buf, block_size, offset, buf_size);
    if (!res)
      return false;
  }
  return true;
}

bool rw_direct(int file_descr, struct params* params) {
  size_t buf_size;
  if (params->block_size % BLOCK_SIZE != 0) {
    int n = params->block_size / BLOCK_SIZE;
    buf_size = BLOCK_SIZE * (n + 1);
  } else {
    buf_size = params->block_size;
  }

  void* buf_raw;
  int res = posix_memalign(&buf_raw, BLOCK_SIZE, buf_size + 1);
  if (res != 0) {
    printf("posix_memalign failed: %s\n", strerror(res));
    return false;
  }
  char* buf = (char*)buf_raw;
  off_t offset;
  off_t min_off = 0;
  int min = 0;
  int max = 0;

  if (params->range_min != 0 || params->range_max != 0) {
    if (!check_range(params)) {
      free(buf_raw);
      return false;
    }
    min_off = params->range_min;
  }

  if (params->rw == 'r') {
    if (params->type == 's') {
      res = direct_sequence_call(
          file_descr,
          params->block_size,
          params->block_count,
          &buf,
          min_off,
          buf_size,
          direct_sequence_read
      );

    } else if (params->type == 'r') {
      res = direct_random_call(
          file_descr,
          &buf,
          params->block_size,
          params->block_count,
          params->range_min,
          params->range_max,
          buf_size,
          direct_sequence_read
      );
    }
    free(buf_raw);
    return res;
  }

  if (params->rw == 'w') {
    if (params->type == 's') {
      res = direct_sequence_call(
          file_descr,
          params->block_size,
          params->block_count,
          &buf,
          min_off,
          buf_size,
          direct_sequence_write
      );

    } else if (params->type == 'r') {
      res = direct_random_call(
          file_descr,
          &buf,
          params->block_size,
          params->block_count,
          params->range_min,
          params->range_max,
          buf_size,
          direct_sequence_write
      );
    }
    free(buf_raw);
    return res;
  }
  free(buf_raw);
  return true;
}

int main(int arg, char** args) {
  srand(time(NULL));

  if (args[1] == NULL || args[2] == NULL || args[3] == NULL ||
      args[4] == NULL || args[5] == NULL || args[6] == NULL ||
      args[7] == NULL) {
    printf("Not enough args\n");
    return 1;
  }

  struct params params = fill_params(args);
  // print_params(&params);

  if (!is_valid(&params)) {
    printf("Invalid args \n");
    return 1;
  }

  int flags = 0;
  if (params.rw == 'r')
    flags = O_RDONLY;
  if (params.rw == 'w' && params.direct)
    flags = O_RDWR | O_CREAT | O_TRUNC;
  if (params.rw == 'w' && !params.direct)
    flags = O_WRONLY | O_CREAT | O_TRUNC;
  if (params.direct)
    flags |= O_DIRECT;

  mode_t mode = 0644;
  int file_descr = open(params.file, flags, mode);
  if (file_descr < 0) {
    printf("%s\n", strerror(errno));
    return 1;
  }

  char* end;
  long iterations = 1;
  if (args[8] != NULL) {
    iterations = strtol(args[8], &end, 10);
    if (iterations < 1)
      iterations = 1;
  }

  for (long i = 0; i < iterations; i++) {
    if (params.direct) {
      rw_direct(file_descr, &params);
    } else {
      rw_simple(file_descr, &params);
    }
    printf("\n");
  }
  close(file_descr);
  return 0;
}

// io-loader rw=read block_size=2 block_count=3 file=io-loader/myfile.txt
// range=0-0 direct=on type=random