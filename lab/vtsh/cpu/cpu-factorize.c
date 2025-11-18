#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

bool is_prime(unsigned long long x) {
  unsigned long long i = 2;
  while (i * i <= x) {
    if (x % i == 0) {
      return false;
    }
    i++;
  }
  return true;
}

size_t count(unsigned long long** res, unsigned long long arg, size_t* len) {
  unsigned long long i = 2;
  size_t count = 0;
  bool prime = false;
  while (i * i <= arg) {
    if (is_prime(arg)) {
      prime = true;
      break;
    }
    if (arg % i == 0) {
      if (*len == count) {
        *len *= 2;
        *res = realloc(*res, *len * sizeof(unsigned long long));
        (*res)[count] = i;
        count++;
      } else {
        (*res)[count] = i;
        count++;
      }
      arg = arg / i;
      i = 1;
    }
    i++;
  }

  if (is_prime(arg)) {
    prime = true;
  }

  if (prime) {
    if (*len == count) {
      *len += 1;
      *res = realloc(*res, *len * sizeof(unsigned long long));
      (*res)[count] = arg;
      count++;
    } else {
      (*res)[count] = arg;
      count++;
    }
  }
  return count;
}

int main(int argc, char* argv[]) {
  char* end;
  if (argv[1] == NULL) {
    return 1;
  }
  long iterations = 1;
  if (argv[2] != NULL) {
    iterations = strtol(argv[2], &end, 10);
    if (iterations < 1)
      iterations = 1;
  }

  for (long i = 0; i < iterations; i++) {
    unsigned long long arg = strtoull(argv[1], &end, 10);
    unsigned long long* res = malloc(sizeof(unsigned long long) * 3);
    size_t len = 3;

    size_t res_len = count(&res, arg, &len);

    for (size_t i = 0; i < res_len; i++) {
      printf("%lld ", res[i]);
    }
    printf("\n");
    free(res);
  }
  return 0;
}

//  /Users/ekaterinakulesova/os-course/lab/vtsh/build/cpu/cpu-factorize
//  1125899906842624 5