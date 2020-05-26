#pragma once

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

void *loli_index_malloc(size_t size, int index);
void *loli_index_calloc(int n, int size, int index);
void *loli_index_memalign(size_t alignment, size_t size, int index);
void *loli_index_realloc(void *ptr, size_t new_size, int index);

#ifdef __cplusplus
}
#endif // __cplusplus