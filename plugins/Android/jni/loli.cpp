#ifndef LOLI_CPP // Lightweight Opensource profiLing Instrument
#define LOLI_CPP

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <cassert>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <android/log.h>
#include <cxxabi.h>
#include <dlfcn.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <inttypes.h>
#include <jni.h>
#include <regex.h>

#include "lz4/lz4.h"
#include "wrapper/wrapper.h"
#include "loli_server.h"
#include "loli_utils.h"
#include "spinlock.h"
#include "sampler.h"
#include "xhook.h"

enum class loliDataMode : std::uint8_t {
    STRICT = 0, 
    LOOSE, 
    NOSTACK, 
};

std::chrono::system_clock::time_point startTime_;
int minRecSize_ = 0;
std::atomic<std::uint32_t> callSeq_;

loliDataMode mode_ = loliDataMode::STRICT;
bool isBlacklist_ = false;
loli::Sampler* sampler_ = nullptr;
loli::spinlock samplerLock_;

#define STACKBUFFERSIZE 128

enum loliFlags {
    FREE_ = 0, 
    MALLOC_ = 1, 
    CALLOC_ = 2, 
    MEMALIGN_ = 3, 
    REALLOC_ = 4, 
};

inline void loli_maybe_record_alloc(size_t size, void* addr, loliFlags flag) {
    bool bRecordAllocation = false;
    size_t recordSize = size;
    if (mode_ == loliDataMode::STRICT) {
        bRecordAllocation = size >= static_cast<size_t>(minRecSize_);
        recordSize = size;
    } else if(mode_ == loliDataMode::LOOSE) {
        {
            std::lock_guard<loli::spinlock> lock(samplerLock_);
            recordSize = sampler_->SampleSize(size);
        }
        bRecordAllocation = recordSize > 0;
    } else {
        assert(0);
    }
    if(!bRecordAllocation) {
        return;
    }
    static thread_local void* buffer[STACKBUFFERSIZE];
    std::ostringstream oss;
    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - startTime_).count();
    oss << flag << '\\' << ++callSeq_ << ',' << time << ',' << recordSize << ',' << addr << '\\';
    loli_dump(oss, buffer, loli_capture(buffer, STACKBUFFERSIZE));
    loli_server_send(oss.str().c_str());
}

void *loli_malloc(size_t size) {
    void* addr = malloc(size);
    loli_maybe_record_alloc(size, addr, loliFlags::MALLOC_);
    return addr;
}

void loli_free(void* ptr) {
    if (ptr == nullptr) 
        return;
    std::ostringstream oss;
    oss << FREE_ << '\\' << ++callSeq_ << '\\' << ptr;
    loli_server_send(oss.str().c_str());
    free(ptr);
}

void *loli_calloc(int n, int size) {
    void* addr = calloc(n, size);
    loli_maybe_record_alloc(n * size, addr, loliFlags::CALLOC_);
    return addr;
}

void *loli_memalign(size_t alignment, size_t size) {
    void* addr = memalign(alignment, size);
    loli_maybe_record_alloc(size, addr, loliFlags::MEMALIGN_);
    return addr;
}

void *loli_realloc(void *ptr, size_t new_size) {
    void* addr = realloc(ptr, new_size);
    if (addr != 0)
    {
        std::ostringstream oss;
        oss << FREE_ << '\\' << ++callSeq_ << '\\' << ptr;
        loli_server_send(oss.str().c_str());
        loli_maybe_record_alloc(new_size, addr, loliFlags::MALLOC_);
    }
    return addr;
}

void loli_hook_library(const char* regex) {
    xhook_register(regex, "malloc", (void*)loli_malloc, nullptr);
    xhook_register(regex, "free", (void*)loli_free, nullptr);
    xhook_register(regex, "calloc", (void*)loli_calloc, nullptr);
    xhook_register(regex, "memalign", (void*)loli_memalign, nullptr);
    xhook_register(regex, "realloc", (void*)loli_realloc, nullptr);
}

void loli_hook_blacklist(std::unordered_set<std::string>& blacklist) {
    for (auto& token : blacklist) {
        auto regex = ".*/" + token + "\\.so$";
        xhook_ignore(regex.c_str(), NULL);
    }
    loli_hook_library(".*\\.so$");
}

void loli_hook_whitelist(std::unordered_set<std::string>& whitelist) {
    for (auto& token : whitelist) {
        auto regex = ".*/" + token + "\\.so$";
        loli_hook_library(regex.c_str());
    }
}

void loli_hook(std::unordered_set<std::string>& tokens) {
    // xhook_enable_debug(1);
    xhook_clear();
    if (isBlacklist_) {
        loli_hook_blacklist(tokens);
    } else {
        loli_hook_whitelist(tokens);
    }
    xhook_refresh(0);
}

inline void loli_nostack_maybe_record_alloc(size_t size, void* addr, loliFlags flag, int index) {
    if (size == 0 || addr == nullptr) {
        return;
    }
    auto hookInfo = wrapper_by_index(index);
    if (hookInfo == nullptr) {
        return;
    }
    std::ostringstream oss;
    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - startTime_).count();
    oss << flag << '\\' << ++callSeq_ << ',' << time << ',' << size << ',' << addr << '\\' 
        << hookInfo->so_name << ".so";
    loli_server_send(oss.str().c_str());
}

void *loli_index_malloc(size_t size, int index) {
    void* addr = malloc(size);
    loli_nostack_maybe_record_alloc(size, addr, loliFlags::MALLOC_, index);
    return addr;
}

void *loli_index_calloc(int n, int size, int index) {
    void* addr = calloc(n, size);
    loli_nostack_maybe_record_alloc(n * size, addr, loliFlags::CALLOC_, index);
    return addr;
}

void *loli_index_memalign(size_t alignment, size_t size, int index) {
    void* addr = memalign(alignment, size);
    loli_nostack_maybe_record_alloc(size, addr, loliFlags::MEMALIGN_, index);
    return addr;
}

void *loli_index_realloc(void *ptr, size_t new_size, int index) {
    void* addr = realloc(ptr, new_size);
    if (addr != 0)
    {
        std::ostringstream oss;
        oss << FREE_ << '\\' << ++callSeq_ << '\\' << ptr;
        loli_server_send(oss.str().c_str());
        loli_nostack_maybe_record_alloc(new_size, addr, loliFlags::MALLOC_, index);
    }
    return addr;
}

bool loli_nostack_hook_library(const char* library, std::unordered_map<std::string, uintptr_t>& baseAddrMap) {
    if (auto info = wrapper_by_name(library)) {
        info->so_baseaddr = baseAddrMap[std::string(info->so_name)];
        auto regex = std::string(".*/") + library + "\\.so$";
        xhook_register(regex.c_str(), "malloc", (void*)info->malloc, nullptr);
        xhook_register(regex.c_str(), "free", (void*)loli_free, nullptr);
        xhook_register(regex.c_str(), "calloc", (void*)info->calloc, nullptr);
        xhook_register(regex.c_str(), "memalign", (void*)info->memalign, nullptr);
        xhook_register(regex.c_str(), "realloc", (void*)info->realloc, nullptr);
        return true;
    } else {
        __android_log_print(ANDROID_LOG_INFO, "Loli", "Out of wrappers!");
        return false;
    }
}

void loli_nostack_hook_blacklist(const std::unordered_set<std::string>& blacklist, std::unordered_map<std::string, uintptr_t>& baseAddrMap) {
    for (auto& token : blacklist) {
        auto regex = ".*/" + token + "\\.so$";
        xhook_ignore(regex.c_str(), NULL);
    }
    for (auto& pair : baseAddrMap) {
        if (!loli_nostack_hook_library(pair.first.c_str(), baseAddrMap)) {
            return;
        }
    }
}

void loli_nostack_hook_whitelist(const std::unordered_set<std::string>& whitelist, std::unordered_map<std::string, uintptr_t>& baseAddrMap) {
    for (auto& token : whitelist) {
        if (!loli_nostack_hook_library(token.c_str(), baseAddrMap)) {
            return;
        }
    }
}

void loli_nostack_hook(const std::unordered_set<std::string>& tokens, std::unordered_map<std::string, uintptr_t> baseAddrMap) {
    // xhook_enable_debug(1);
    xhook_clear();
    // convert absolute path to relative ones, ie: system/lib/libc.so -> libc
    std::unordered_map<std::string, uintptr_t> demangledAddrMap;
    for (auto& pair : baseAddrMap) {
        auto origion = pair.first;
        if (origion.find(".so") == std::string::npos) {
            continue;
        }
        std::string demangled;
        loli_demangle(origion, demangled);
        demangledAddrMap[demangled] = pair.second;
    }
    if (isBlacklist_) {
        loli_nostack_hook_blacklist(tokens, demangledAddrMap);
    } else {
        loli_nostack_hook_whitelist(tokens, demangledAddrMap);
    }
    xhook_refresh(0);
}

void loli_smaps_thread(std::unordered_set<std::string> libs) {
    char                                        line[512]; // proc/self/maps parsing code by xhook
    FILE                                       *fp;
    uintptr_t                                   baseAddr;
    char                                        perm[5];
    unsigned long                               offset;
    int                                         pathNamePos;
    char                                       *pathName;
    size_t                                      pathNameLen;
    std::unordered_set<std::string>             loaded;
    std::unordered_set<std::string>             desired(libs);
    std::unordered_map<std::string, uintptr_t>  libBaseAddrMap;
    int                                         loadedDesiredCount = static_cast<int>(desired.size());
    while (true) {
        if(NULL == (fp = fopen("/proc/self/maps", "r"))) {
            continue;
        }
        bool shouldHook = false;
        while(fgets(line, sizeof(line), fp)) {
            if(sscanf(line, "%" PRIxPTR"-%*lx %4s %lx %*x:%*x %*d%n", &baseAddr, perm, &offset, &pathNamePos) != 3) continue;
            // check permission & offset
            if(perm[0] != 'r') continue;
            if(perm[3] != 'p') continue; // do not touch the shared memory
            if(0 != offset) continue;
            // get pathname
            while(isspace(line[pathNamePos]) && pathNamePos < (int)(sizeof(line) - 1))
                pathNamePos += 1;
            if(pathNamePos >= (int)(sizeof(line) - 1)) continue;
            pathName = line + pathNamePos;
            pathNameLen = strlen(pathName);
            if(0 == pathNameLen) continue;
            if(pathName[pathNameLen - 1] == '\n') {
                pathName[pathNameLen - 1] = '\0';
                pathNameLen -= 1;
            }
            if(0 == pathNameLen) continue;
            if('[' == pathName[0]) continue;
            // check path
            auto pathnameStr = std::string(pathName);
            if (loaded.find(pathnameStr) == loaded.end()) {
                libBaseAddrMap[pathnameStr] = baseAddr;
                // path in loaded is full path to so library
                loaded.insert(pathnameStr);
                if (isBlacklist_) {
                    shouldHook = true;
                } else {
                    for (auto& token : desired) {
                        if (pathnameStr.find(token) != std::string::npos) {
                            shouldHook = true;
                            loadedDesiredCount--;
                            __android_log_print(ANDROID_LOG_INFO, "Loli", "%s (%s) is loaded", token.c_str(), pathnameStr.c_str());
                        }
                    }
                }
            }
        }
        fclose(fp);
        if (shouldHook) {
            if (mode_ == loliDataMode::NOSTACK) {
                loli_nostack_hook(desired, libBaseAddrMap);
            } else {
                loli_hook(desired);
            }
        }
        if (loadedDesiredCount <= 0) {
            __android_log_print(ANDROID_LOG_INFO, "Loli", "All desired libraries are loaded.");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    __android_log_print(ANDROID_LOG_INFO, "Loli", "JNI_OnLoad");
    JNIEnv* env;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR; // JNI version not supported.
    }

    if (!wrapper_init()) {
        __android_log_print(ANDROID_LOG_INFO, "Loli", "wrapper_init failed!");
        return JNI_VERSION_1_6;
    }

    int minRecSize = 512;
    std::string hookLibraries = "libil2cpp,libunity";
    mode_ = loliDataMode::STRICT;

    std::ifstream infile("/data/local/tmp/loli2.conf");
    std::string line;
    std::vector<std::string> words;
    while (std::getline(infile, line)) {
        loli_split(line, words, ":");
        if (words.size() < 2) {
            continue;
        }
        // remove unnecessary characters like \n \t
        loli_trim(words[1]);
        if (words[0] == "threshold") {
            std::istringstream iss(words[1]);
            iss >> minRecSize;
        } else if (words[0] == "libraries") {
            hookLibraries = words[1];
        } else if (words[0] == "mode") {
            if (words[1] == "loose") {
                mode_ = loliDataMode::LOOSE;
            } else if (words[1] == "strict") {
                mode_ = loliDataMode::STRICT;
            } else {
                mode_ = loliDataMode::NOSTACK;
            }
        } else if (words[0] == "type") {
            isBlacklist_ = words[1] == "blacklist";
        }
    }
    __android_log_print(ANDROID_LOG_INFO, "Loli", "mode: %i, minRecSize: %i, blacklist: %i, hookLibs: %s",
        static_cast<int>(mode_), minRecSize, isBlacklist_ ? 1 : 0, hookLibraries.c_str());
    // parse library tokens
    std::unordered_set<std::string> tokens;
    std::istringstream namess(hookLibraries);
    while (std::getline(namess, line, ',')) {
        tokens.insert(line);
    }
    if (isBlacklist_) {
        tokens.insert("libloli");
    }
    // start tcp server
    minRecSize_ = minRecSize;
    sampler_ = new loli::Sampler(minRecSize_);
    callSeq_ = 0;
    startTime_ = std::chrono::system_clock::now();
    auto svr = loli_server_start(7100);
    __android_log_print(ANDROID_LOG_INFO, "Loli", "loli start status %i", svr);
    // start proc/self/maps check thread
    std::thread(loli_smaps_thread, tokens).detach();
    return JNI_VERSION_1_6;
}

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // LOLI_CPP