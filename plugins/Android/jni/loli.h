#pragma once

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

void *loliIndexMalloc(size_t size, int index);
void *loliIndexCalloc(int n, int size, int index);
void *loliIndexMemalign(size_t alignment, size_t size, int index);
void *loliIndexRealloc(void *ptr, size_t new_size, int index);

int loliHook(int minRecSize, const char *soNames);

#ifdef __cplusplus
}
#endif // __cplusplus