//
//  wrapper.c
//  TestMacro
//
//  Created by ashenzhou(周星) on 2020/5/17.
//  Copyright © 2020 ashenzhou(周星). All rights reserved.
//
#include "wrapper.h"
#include "loli.h"

#include <stdio.h>
#include <memory.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <android/log.h>

HOOK_INFO::~_hook_info() {
    if (so_name) {
        delete so_name;
        so_name = nullptr;
    }
}

#define SLOT_NUM 512

#define _8_MACRO(MACRO, INDEX,...)\
MACRO(INDEX##0,##__VA_ARGS__)\
MACRO(INDEX##1,##__VA_ARGS__)\
MACRO(INDEX##2,##__VA_ARGS__)\
MACRO(INDEX##3,##__VA_ARGS__)\
MACRO(INDEX##4,##__VA_ARGS__)\
MACRO(INDEX##5,##__VA_ARGS__)\
MACRO(INDEX##6,##__VA_ARGS__)\
MACRO(INDEX##7,##__VA_ARGS__)

#define _64_MACRO(MACRO, INDEX, ...)\
_8_MACRO(MACRO, INDEX##0,##__VA_ARGS__)\
_8_MACRO(MACRO, INDEX##1,##__VA_ARGS__)\
_8_MACRO(MACRO, INDEX##2,##__VA_ARGS__)\
_8_MACRO(MACRO, INDEX##3,##__VA_ARGS__)\
_8_MACRO(MACRO, INDEX##4,##__VA_ARGS__)\
_8_MACRO(MACRO, INDEX##5,##__VA_ARGS__)\
_8_MACRO(MACRO, INDEX##6,##__VA_ARGS__)\
_8_MACRO(MACRO, INDEX##7,##__VA_ARGS__)

#define _128_MACRO(MACRO, INDEX, ...)\
_64_MACRO(MACRO, INDEX##0,##__VA_ARGS__)\
_64_MACRO(MACRO, INDEX##1,##__VA_ARGS__)\

#define _256_MACRO(MACRO, INDEX, ...)\
_64_MACRO(MACRO, INDEX##0,##__VA_ARGS__)\
_64_MACRO(MACRO, INDEX##1,##__VA_ARGS__)\
_64_MACRO(MACRO, INDEX##2,##__VA_ARGS__)\
_64_MACRO(MACRO, INDEX##3,##__VA_ARGS__)\

#define _512_MACRO(MACRO, INDEX, ...)\
_64_MACRO(MACRO, INDEX##0,##__VA_ARGS__)\
_64_MACRO(MACRO, INDEX##1,##__VA_ARGS__)\
_64_MACRO(MACRO, INDEX##2,##__VA_ARGS__)\
_64_MACRO(MACRO, INDEX##3,##__VA_ARGS__)\
_64_MACRO(MACRO, INDEX##4,##__VA_ARGS__)\
_64_MACRO(MACRO, INDEX##5,##__VA_ARGS__)\
_64_MACRO(MACRO, INDEX##6,##__VA_ARGS__)\
_64_MACRO(MACRO, INDEX##7,##__VA_ARGS__)

#define _MALLOC_WRAPPER(INDEX)\
void *_MALLOC##INDEX(size_t size)\
{\
    return loli_index_malloc(size, INDEX);\
}

#define _CALLOC_WRAPPER(INDEX)\
void *_CALLOC##INDEX(int n, int size)\
{\
    return loli_index_calloc(n, size, INDEX);\
}

#define _MEMALIGN_WRAPPER(INDEX)\
void *_MEMALIGN##INDEX(size_t alignment, size_t size)\
{\
    return loli_index_memalign(alignment, size, INDEX);\
}

#define _REALLOC_WRAPPER(INDEX)\
void *_REALLOC##INDEX(void *ptr, size_t new_size)\
{\
    return loli_index_realloc(ptr, new_size, INDEX);\
}

#define NSLOT_MACRO(NUM)\
_##NUM##_MACRO(_MALLOC_WRAPPER, 0)\
_##NUM##_MACRO(_CALLOC_WRAPPER, 0)\
_##NUM##_MACRO(_MEMALIGN_WRAPPER, 0)\
_##NUM##_MACRO(_REALLOC_WRAPPER, 0)

NSLOT_MACRO(512)

#define _REG_HOOK_INFO(INDEX)\
Reg_Hook_Info(INDEX, &_MALLOC##INDEX, &_CALLOC##INDEX, &_MEMALIGN##INDEX, &_REALLOC##INDEX);

#define TEST_1_FUNC(INDEX, FUNC,...)\
(_##FUNC##INDEX(__VA_ARGS__));


static HOOK_INFO hk_infos[SLOT_NUM];
static int hk_info_index = -1;

inline void Reg_Hook_Info(int index,MALLOC_FPTR p1, CALLOC_FPTR p3, MEMALIGN_FPTR p4, REALLOC_FPTR p5) {
    // __android_log_print(ANDROID_LOG_INFO, "Loli", "Reg hook info %d", index);
    hk_infos[index].so_name = nullptr;
    hk_infos[index].malloc = p1;
    hk_infos[index].calloc = p3;
    hk_infos[index].memalign = p4;
    hk_infos[index].realloc = p5;
}

bool wrapper_init() {
    _512_MACRO(_REG_HOOK_INFO, 0)
    hk_info_index = -1;
    return true;
}

HOOK_INFO* wrapper_by_index(int index) {
    if (index < 0 || index > SLOT_NUM - 1) {
        return nullptr;
    }
    return &hk_infos[index];
}

HOOK_INFO* wrapper_by_name(const char* name) {
    for (int i = 0; i <= hk_info_index; i++) {
        auto curInfo = &hk_infos[i];
        if (strcmp(curInfo->so_name, name) == 0) {
            return curInfo;
        }
    }
    if (hk_info_index >= SLOT_NUM - 1) {
        return nullptr;
    }
    hk_info_index++;
    auto curInfo = &hk_infos[hk_info_index];
    curInfo->so_name = (char*)malloc(strlen(name) + 1);
    strncpy(curInfo->so_name, name, strlen(name) + 1);
    return curInfo;
}

// int test() {

// //    _128_MACRO(TEST_1_FUNC, 0, MALLOC, 0);
// //    _128_MACRO(TEST_1_FUNC, 0, FREE, NULL);
// //    _128_MACRO(TEST_1_FUNC, 0, CALLOC, 0,0);
// //    _128_MACRO(TEST_1_FUNC, 0, MEMALIGN, 0,0);
// //    _128_MACRO(TEST_1_FUNC, 0, REALLOC, NULL,0);
//     wrapperInit();
    
//     for(int i=0; i<128; i++)
//     {
//         hk_infos[i].malloc(0);
//         hk_infos[i].free(NULL);
//         hk_infos[i].calloc(0,0);
//         hk_infos[i].memalign(0,0);
//         hk_infos[i].realloc(NULL,0);
//     }
//     return 0;
// }

#ifdef __cplusplus
}
#endif // __cplusplus
