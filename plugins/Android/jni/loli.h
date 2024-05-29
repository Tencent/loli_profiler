#pragma once

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

void loli_index_custom_alloc(void* addr, size_t size, int index);
void *loli_index_malloc(size_t size, int index);
void *loli_index_calloc(int n, int size, int index);
void *loli_index_memalign(size_t alignment, size_t size, int index);
int loli_index_posix_memalign(void** ptr, size_t alignment, size_t size, int index);
void *loli_index_realloc(void *ptr, size_t new_size, int index);
void *loli_index_mmap(void *ptr, size_t length, int prot, int flags, int fd, off_t offset, int index);

#ifdef __cplusplus
}
#endif // __cplusplus