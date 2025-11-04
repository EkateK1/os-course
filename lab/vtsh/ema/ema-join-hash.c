#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct table {
  struct element* elements;
  size_t len;
};

struct element {
  int id;
  char str[8];
};

uint32_t simple_hash(int x) {
  return (uint32_t)(x * 2654435761u);
}

struct table read_table(FILE* file) {
  size_t len;
  fscanf(file, "%zu", &len);

  struct element* table = malloc(sizeof(struct element) * len);

  for (int i = 0; i < len; i++) {
    fscanf(file, "%d", &table[i].id);
    fscanf(file, "%s", table[i].str);
    // table.id[i] = id;
    // table.string[i] = str;
  }
  struct table res_table = {.elements = table, .len = len};
  return res_table;
}

void print_table(struct table* table) {
  for (int i = 0; i < table->len; i++) {
    printf("%d %s\n", table->elements[i].id, table->elements[i].str);
  }
  printf("\n");
}

void free_table(struct table* table) {
  free(table->elements);
}

struct element* hashtable_creating(struct table* table) {
  struct element* hashtable = calloc(table->len, sizeof(struct element));
  if (hashtable == NULL) {
    return NULL;
  }

  for (size_t i = 0; i < table->len; i++) {
    struct element element = table->elements[i];
    uint32_t num = simple_hash(element.id) % table->len;

    if (hashtable[num].id == 0) {
      hashtable[num] = element;
    } else {
      uint32_t j = num;

      while (hashtable[j].id != 0) {
        j++;
        if (j == table->len) {
          j = 0;
        }
      }

      hashtable[j] = element;
    }
  }
  return hashtable;
}

char* compare(struct element* element, struct element* hashtable, size_t* len) {
  uint32_t num = simple_hash(element->id) % *len;
  if (hashtable[num].id != 0) {
    if (hashtable[num].id == element->id) {
      return hashtable[num].str;
    }
    uint32_t j = num;
    while (hashtable[j].id != element->id) {
      j++;
      if (j >= *len) {
        j = 0;
      }
      if (j == num) {
        return NULL;
      }
    }
    return hashtable[j].str;
  }
  return NULL;
}

int main(int arg, char* args[]) {
  if (args[1] == NULL || args[2] == NULL) {
    return 1;
  }

  char* end;
  long iterations = 1;
  if (args[3] != NULL) {
    iterations = strtol(args[3], &end, 10);
    if (iterations < 1)
      iterations = 1;
  }

  FILE* fptr;

  for (long i = 0; i < iterations; i++) {
    fptr = fopen(args[1], "r");
    if (fptr == NULL) {
      printf("Can not open file %s\n", args[1]);
      return 1;
    }

    struct table table1 = read_table(fptr);
    // print_table(&table1);
    fclose(fptr);

    fptr = fopen(args[2], "r");
    if (fptr == NULL) {
      printf("Can not open file %s\n", args[2]);
      return 1;
    }

    struct table table2 = read_table(fptr);
    // print_table(&table2);
    fclose(fptr);

    struct element* hashtable = hashtable_creating(&table1);
    if (hashtable == NULL) {
      printf("Calloc problem: %s\n", strerror(errno));
      return 1;
    }

    //   for (int i = 0; i < table1.len; i++) {
    //     printf("%d %d %s\n", i, hashtable[i].id, hashtable[i].str);
    //   }

    char* res_filename = "result.txt";
    fptr = fopen(res_filename, "w+");

    for (size_t i = 0; i < table2.len; i++) {
      struct element element = table2.elements[i];
      char* res_str = compare(&element, hashtable, &table1.len);
      if (res_str != NULL) {
        fprintf(fptr, "%s %s\n", res_str, element.str);
        printf("%s %s\n", res_str, element.str);
      }
    }

    free(hashtable);
    printf("\n");
  }

  return 0;
}

// /Users/ekaterinakulesova/os-course/lab/vtsh/build/ema/ema-join-hash
// /Users/ekaterinakulesova/os-course/lab/vtsh/ema/table1.txt
// /Users/ekaterinakulesova/os-course/lab/vtsh/ema/table2.txt