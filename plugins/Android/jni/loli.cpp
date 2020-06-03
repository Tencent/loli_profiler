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
#include "loli_dlfcn.h"
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
bool isInstrumented_ = false;
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

static thread_local bool ignore_current_ = false;
void toggle_ignore_current(bool value) {
    ignore_current_ = value;
}

inline void loli_maybe_record_alloc(size_t size, void* addr, loliFlags flag, int index) {
    if (ignore_current_) {
        return;
    }

    bool bRecordAllocation = false;
    size_t recordSize = size;
    if (mode_ == loliDataMode::STRICT) {
        bRecordAllocation = size >= static_cast<size_t>(minRecSize_);
    } else if(mode_ == loliDataMode::LOOSE) {
        {
            std::lock_guard<loli::spinlock> lock(samplerLock_);
            recordSize = sampler_->SampleSize(size);
        }
        bRecordAllocation = recordSize > 0;
    } else {
        bRecordAllocation = true;
    }

    if(!bRecordAllocation) {
        return;
    }

    auto hookInfo = wrapper_by_index(index);
    if (hookInfo == nullptr) {
        return;
    }

    std::ostringstream oss;
    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - startTime_).count();
    if (mode_ == loliDataMode::NOSTACK) {
        oss << flag << '\\' << ++callSeq_ << ',' << time << ',' << size << ',' << addr << '\\' 
            << hookInfo->so_name << ".so";
    } else {
        static thread_local void* buffer[STACKBUFFERSIZE];
        oss << flag << '\\' << ++callSeq_ << ',' << time << ',' << recordSize << ',' << addr << '\\';
        if (hookInfo->backtrace != nullptr) {
            loli_dump(oss, buffer, hookInfo->backtrace(buffer, STACKBUFFERSIZE));
        } else {
            loli_dump(oss, buffer, loli_capture(buffer, STACKBUFFERSIZE));
        }
    }
    loli_server_send(oss.str().c_str());
}

void *loli_index_malloc(size_t size, int index) {
    void* addr = malloc(size);
    loli_maybe_record_alloc(size, addr, loliFlags::MALLOC_, index);
    return addr;
}

void *loli_index_calloc(int n, int size, int index) {
    void* addr = calloc(n, size);
    loli_maybe_record_alloc(n * size, addr, loliFlags::CALLOC_, index);
    return addr;
}

void *loli_index_memalign(size_t alignment, size_t size, int index) {
    void* addr = memalign(alignment, size);
    loli_maybe_record_alloc(size, addr, loliFlags::MEMALIGN_, index);
    return addr;
}

void *loli_index_realloc(void *ptr, size_t new_size, int index) {
    void* addr = realloc(ptr, new_size);
    if (addr != 0)
    {
        std::ostringstream oss;
        oss << FREE_ << '\\' << ++callSeq_ << '\\' << ptr;
        loli_server_send(oss.str().c_str());
        loli_maybe_record_alloc(new_size, addr, loliFlags::MALLOC_, index);
    }
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

BACKTRACE_FPTR loli_get_backtrace(const char* path) {
    BACKTRACE_FPTR backtrace = nullptr;
    void *handle = fake_dlopen(path, RTLD_LAZY);
    if (handle) {
        void (*set_loli_ignore_func)(void (*funcPtr)(bool)) = nullptr;
        *(void **) (&set_loli_ignore_func) = fake_dlsym(handle, "set_loli_ignore_func");
         if (set_loli_ignore_func == nullptr) {
            LOLILOGI("Error dlsym set_loli_ignore_func: %s", path);
            return backtrace;
        }
        (*set_loli_ignore_func)(toggle_ignore_current);
        *(void **) (&backtrace) = fake_dlsym(handle, "get_stack_backtrace");
        if (backtrace == nullptr) {
            LOLILOGI("Error dlsym get_stack_backtrace: %s", path);
        }
    } else {
        LOLILOGI("Error dlopen: %s", path);
    }
    return backtrace;
}

// demangled name, <full name, base address>
using so_info_map = std::unordered_map<std::string, std::pair<std::string, uintptr_t>>;

bool loli_hook_library(const char* library, so_info_map& infoMap) {
    if (auto info = wrapper_by_name(library)) {
        auto brief = infoMap[std::string(info->so_name)];
        info->so_baseaddr = brief.second;
        if (mode_ != loliDataMode::NOSTACK && isInstrumented_) {
            info->backtrace = loli_get_backtrace(brief.first.c_str());
        }
        auto regex = std::string(".*/") + library + "\\.so$";
        xhook_register(regex.c_str(), "malloc", (void*)info->malloc, nullptr);
        xhook_register(regex.c_str(), "free", (void*)loli_free, nullptr);
        xhook_register(regex.c_str(), "calloc", (void*)info->calloc, nullptr);
        xhook_register(regex.c_str(), "memalign", (void*)info->memalign, nullptr);
        xhook_register(regex.c_str(), "realloc", (void*)info->realloc, nullptr);
        return true;
    } else {
        LOLILOGE("Out of wrappers!");
        return false;
    }
}

void loli_hook_blacklist(const std::unordered_set<std::string>& blacklist, so_info_map& infoMap) {
    for (auto& token : blacklist) {
        auto regex = ".*/" + token + "\\.so$";
        xhook_ignore(regex.c_str(), NULL);
    }
    for (auto& pair : infoMap) {
        if (!loli_hook_library(pair.first.c_str(), infoMap)) {
            return;
        }
    }
}

void loli_hook_whitelist(const std::unordered_set<std::string>& whitelist, so_info_map& infoMap) {
    for (auto& token : whitelist) {
        if (!loli_hook_library(token.c_str(), infoMap)) {
            return;
        }
    }
}

void loli_hook(const std::unordered_set<std::string>& tokens, std::unordered_map<std::string, uintptr_t> infoMap) {
    // xhook_enable_debug(1);
    xhook_clear();
    // convert absolute path to relative ones, ie: system/lib/libc.so -> libc
    so_info_map demangledMap;
    for (auto& pair : infoMap) {
        auto origion = pair.first;
        if (origion.find(".so") == std::string::npos) {
            continue;
        }
        std::string demangled;
        loli_demangle(origion, demangled);
        demangledMap[demangled] = std::make_pair(origion, pair.second);
    }
    if (isBlacklist_) {
        loli_hook_blacklist(tokens, demangledMap);
    } else {
        loli_hook_whitelist(tokens, demangledMap);
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
                            LOLILOGI("%s (%s) is loaded", token.c_str(), pathnameStr.c_str());
                        }
                    }
                }

            }
        }
        fclose(fp);
        if (shouldHook) {
            loli_hook(desired, libBaseAddrMap);
        }
        if (loadedDesiredCount <= 0) {
            LOLILOGI("All desired libraries are loaded.");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    LOLILOGI("JNI_OnLoad");
    JNIEnv* env;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR; // JNI version not supported.
    }

    if (!wrapper_init()) {
        LOLILOGI("wrapper_init failed!");
        return JNI_VERSION_1_6;
    }

    int minRecSize = 512;
    std::string hookLibraries = "libil2cpp,libunity";
    std::string whitelist, blacklist;
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
        } else if (words[0] == "whitelist") {
            whitelist = words[1];
        } else if (words[0] == "blacklist") {
            blacklist = words[1];
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
        } else if (words[0] == "build") {
            isInstrumented_ = words[1] != "default";
        }
    }
    hookLibraries = isBlacklist_ ? blacklist : whitelist;
    LOLILOGI("mode: %i, minRecSize: %i, blacklist: %i, hookLibs: %s",
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
    LOLILOGI("loli start status %i", svr);
    // start proc/self/maps check thread
    std::thread(loli_smaps_thread, tokens).detach();
    return JNI_VERSION_1_6;
}

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // LOLI_CPP