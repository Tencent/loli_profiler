//
//  wrapper.c
//  TestMacro
//
//  Created by ashenzhou(周星) on 2020/5/17.
//  Copyright © 2020 ashenzhou(周星). All rights reserved.
//
#include "wrapper.h"

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

void* index_malloc(size_t size, int index)
{
    void* addr = malloc(size);
    printf("This is malloc %d body\n", index);
    //loliMaybeRecordAllocation(size, addr, loliFlags::MALLOC_);
    return addr;
}

void index_free(void* ptr, int index)
{
    printf("This is free %d body\n", index);
    //loliFree(ptr);
}

void *index_calloc(int n, int size, int index) {
    void* addr = NULL;//calloc(n, size);
    printf("This is calloc %d body\n", index);
    //loliMaybeRecordAllocation(n * size, addr, loliFlags::CALLOC_);
    return addr;
}

void *index_memalign(size_t alignment, size_t size, int index) {
    void* addr = NULL;// memalign(alignment, size);
    printf("This is memalign %d body\n", index);
    //loliMaybeRecordAllocation(size, addr, loliFlags::MEMALIGN_);
    return addr;
}

void *index_realloc(void *ptr, size_t new_size, int index) {
    void* addr = NULL;//realloc(ptr, new_size);
    printf("This is realloc %d body\n", index);
//    if (addr != 0)
//    {
//        {
//            std::ostringstream oss;
//            oss << FREE_ << '\\' << ++callSeq_ << '\\' << ptr;
//            std::lock_guard<loli::spinlock> lock(cacheLock_);
//            cache_.emplace_back(oss.str());
//        }
//        //loliMaybeRecordAllocation(new_size, addr, loliFlags::MALLOC_);
//    }
    return addr;
}

#define _MALLOC_WRAPPER(INDEX)\
void *_MALLOC##INDEX(size_t size)\
{\
    return index_malloc(size, INDEX);\
}

#define _FREE_WRAPPER(INDEX)\
void _FREE##INDEX(void* ptr)\
{\
    index_free(ptr, INDEX);\
}

#define _CALLOC_WRAPPER(INDEX)\
void *_CALLOC##INDEX(int n, int size)\
{\
    return index_calloc(n, size, INDEX);\
}

#define _MEMALIGN_WRAPPER(INDEX)\
void *_MEMALIGN##INDEX(size_t alignment, size_t size)\
{\
    return index_memalign(alignment, size, INDEX);\
}

#define _REALLOC_WRAPPER(INDEX)\
void *_REALLOC##INDEX(void *ptr, size_t new_size)\
{\
    return index_realloc(ptr, new_size, INDEX);\
}

_128_MACRO(_MALLOC_WRAPPER, 0)
_128_MACRO(_FREE_WRAPPER, 0)
_128_MACRO(_CALLOC_WRAPPER, 0)
_128_MACRO(_MEMALIGN_WRAPPER, 0)
_128_MACRO(_REALLOC_WRAPPER, 0)

#define TEST_1_FUNC(INDEX, FUNC,...)\
(_##FUNC##INDEX(__VA_ARGS__));

int main(int argc, const char * argv[]) {

    _128_MACRO(TEST_1_FUNC, 0, MALLOC, 0);
    _128_MACRO(TEST_1_FUNC, 0, FREE, NULL);
    _128_MACRO(TEST_1_FUNC, 0, CALLOC, 0,0);
    _128_MACRO(TEST_1_FUNC, 0, MEMALIGN, 0,0);
    _128_MACRO(TEST_1_FUNC, 0, REALLOC, NULL,0);
    char c[5] = "abcd";
    printf("Hello, World!%c\n",c[001]);
    return 0;
}
