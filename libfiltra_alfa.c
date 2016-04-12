#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "filtrar.h"

int tratar(char* buffer_in, char* buffer_out, int size) {
  int i, out_size;
  out_size = 0;
  for (i = 0; i < size; i++) {
    if (!isalpha(buffer_in[i])) {
      buffer_out[out_size] = buffer_in[i];
      out_size++;
    }
  }
  return out_size;
}
