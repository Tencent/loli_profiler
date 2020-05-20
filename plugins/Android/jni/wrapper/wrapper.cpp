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

#include <string>

extern "C"
{
typedef void* (*MALLOC_FPTR)(size_t);
typedef void (*FREE_FPTR)(void*);
typedef void* (*CALLOC_FPTR)(int,int);
typedef void* (*MEMALIGN_FPTR)(size_t, size_t);
typedef void* (*REALLOC_FPTR)(void*, size_t);

typedef struct _hook_info{
    std::string so_name;
    MALLOC_FPTR malloc;
    FREE_FPTR free;
    CALLOC_FPTR calloc;
    MEMALIGN_FPTR memalign;
    REALLOC_FPTR realloc;
} HOOK_INFO;

void* index_malloc(size_t size, int index)
{
    //void* addr = malloc(size);
    printf("This is malloc %d body, size:%zu\n", index, size);
    //loliMaybeRecordAllocation(size, addr, loliFlags::MALLOC_);
    return NULL;
}

void index_free(void* ptr, int index)
{
    printf("This is free %d body, ptr:%p\n", index, ptr);
    //loliFree(ptr);
}

void *index_calloc(int n, int size, int index) {
    void* addr = NULL;//calloc(n, size);
    printf("This is calloc %d body, n:%d, size:%d\n", index, n, size);
    //loliMaybeRecordAllocation(n * size, addr, loliFlags::CALLOC_);
    return addr;
}

void *index_memalign(size_t alignment, size_t size, int index) {
    void* addr = NULL;// memalign(alignment, size);
    printf("This is memalign %d body, alignment:%zu, size:%zu\n", index, alignment, size);
    //loliMaybeRecordAllocation(size, addr, loliFlags::MEMALIGN_);
    return addr;
}

void *index_realloc(void *ptr, size_t new_size, int index) {
    void* addr = NULL;//realloc(ptr, new_size);
    printf("This is realloc %d body, ptr:%p, new_size:%zu\n", index, ptr, new_size);
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

#define NSLOT_MACRO(NUM)\
_##NUM##_MACRO(_MALLOC_WRAPPER, 0)\
_##NUM##_MACRO(_FREE_WRAPPER, 0)\
_##NUM##_MACRO(_CALLOC_WRAPPER, 0)\
_##NUM##_MACRO(_MEMALIGN_WRAPPER, 0)\
_##NUM##_MACRO(_REALLOC_WRAPPER, 0)

NSLOT_MACRO(128)

#define _REG_HOOK_INFO(INDEX)\
Reg_Hook_Info(INDEX, &_MALLOC##INDEX, &_FREE##INDEX, &_CALLOC##INDEX, &_MEMALIGN##INDEX, &_REALLOC##INDEX);

#define TEST_1_FUNC(INDEX, FUNC,...)\
(_##FUNC##INDEX(__VA_ARGS__));


static HOOK_INFO hk_infos[SLOT_NUM];

inline void Reg_Hook_Info(int index,MALLOC_FPTR p1, FREE_FPTR p2, CALLOC_FPTR p3, MEMALIGN_FPTR p4, REALLOC_FPTR p5)
{
    printf("Reg hook info %d\n", index);
    hk_infos[index].so_name="";
    hk_infos[index].malloc=p1;
    hk_infos[index].free=p2;
    hk_infos[index].calloc=p3;
    hk_infos[index].memalign=p4;
    hk_infos[index].realloc=p5;
}

bool Init()
{
    _128_MACRO(_REG_HOOK_INFO, 0)
    return true;
}

int test() {

//    _128_MACRO(TEST_1_FUNC, 0, MALLOC, 0);
//    _128_MACRO(TEST_1_FUNC, 0, FREE, NULL);
//    _128_MACRO(TEST_1_FUNC, 0, CALLOC, 0,0);
//    _128_MACRO(TEST_1_FUNC, 0, MEMALIGN, 0,0);
//    _128_MACRO(TEST_1_FUNC, 0, REALLOC, NULL,0);
    Init();
    
    for(int i=0; i<128; i++)
    {
        hk_infos[i].malloc(0);
        hk_infos[i].free(NULL);
        hk_infos[i].calloc(0,0);
        hk_infos[i].memalign(0,0);
        hk_infos[i].realloc(NULL,0);
    }
    return 0;
}
}
