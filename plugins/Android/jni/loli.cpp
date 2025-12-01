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
// #include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <cassert>

#include <android/log.h>
#include <cxxabi.h>
#include <dlfcn.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#include <inttypes.h>
#include <jni.h>
#include <regex.h>

#include "lz4/lz4.h"
#include "wrapper/wrapper.h"
#include "buffer.h"
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

enum class loliHookMode : std::uint8_t {
    MALLOC = 0, 
    MMAP, 
};

std::chrono::system_clock::time_point startTime_;
int minRecSize_ = 0;
std::atomic<std::uint32_t> callSeq_;

loliDataMode mode_ = loliDataMode::STRICT;
loliHookMode hookMode_ = loliHookMode::MALLOC;
bool isBlacklist_ = false;
bool isFramePointer_ = false;
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
    COMMAND_ = 255,
};

static thread_local bool ignore_current_ = false;
void toggle_ignore_current(bool value) {
    ignore_current_ = value;
}

inline void loli_maybe_record_alloc(size_t size, void* addr, loliFlags flag, int index) {
    if (ignore_current_ || size == 0) {
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

    static thread_local io::buffer obuffer(2048);
    obuffer.clear();
    // std::ostringstream oss;
    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now() - startTime_).count();
    if (mode_ == loliDataMode::NOSTACK) {
        std::string soname = std::string(hookInfo->so_name) + ".so";
        obuffer << static_cast<uint8_t>(flag) << static_cast<uint32_t>(++callSeq_) << static_cast<int64_t>(time) 
                << static_cast<uint32_t>(size) << reinterpret_cast<uint64_t>(addr) << static_cast<uint8_t>(0) << soname.c_str();
        // oss << flag << '\\' << ++callSeq_ << ',' << time << ',' << size << ',' << addr << '\\' 
        //     << hookInfo->so_name << ".so";
    } else {
        static thread_local void* buffer[STACKBUFFERSIZE];
        obuffer << static_cast<uint8_t>(flag) << static_cast<uint32_t>(++callSeq_) << static_cast<int64_t>(time) 
                << static_cast<uint32_t>(recordSize) << reinterpret_cast<uint64_t>(addr) << static_cast<uint8_t>(1);
        // oss << flag << '\\' << ++callSeq_ << ',' << time << ',' << recordSize << ',' << addr << '\\';
        if (isInstrumented_ && hookInfo->backtrace != nullptr) {
            loli_dump(obuffer, buffer, hookInfo->backtrace(buffer, STACKBUFFERSIZE));
        } else if (isFramePointer_) {
            loli_dump(obuffer, buffer, loli_fastcapture(buffer, STACKBUFFERSIZE));
        } else {
            loli_dump(obuffer, buffer, loli_capture(buffer, STACKBUFFERSIZE));
        }
    }
    loli_server_send(obuffer.data(), obuffer.size());
}

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

void loli_custom_free(void* ptr) {
    if (ptr == nullptr) 
        return;
    static thread_local io::buffer obuffer(128);
    obuffer.clear();
    obuffer << static_cast<uint8_t>(FREE_) << static_cast<uint32_t>(++callSeq_) << reinterpret_cast<uint64_t>(ptr);
    loli_server_send(obuffer.data(), obuffer.size());
}

void loli_free(void* ptr) {
    if (ptr == nullptr) 
        return;
    static thread_local io::buffer obuffer(128);
    obuffer.clear();
    obuffer << static_cast<uint8_t>(FREE_) << static_cast<uint32_t>(++callSeq_) << reinterpret_cast<uint64_t>(ptr);
    loli_server_send(obuffer.data(), obuffer.size());
    free(ptr);
}

void loli_index_custom_alloc(void* addr, size_t size, int index) {
    loli_maybe_record_alloc(size, addr, loliFlags::MALLOC_, index);
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

int loli_index_posix_memalign(void** ptr, size_t alignment, size_t size, int index) {
    int ecode = posix_memalign(ptr, alignment, size);
    if (ecode == 0) {
        loli_maybe_record_alloc(size, *ptr, loliFlags::MEMALIGN_, index);
    }
    return ecode;
}

void *loli_index_realloc(void *ptr, size_t new_size, int index) {
    void* addr = realloc(ptr, new_size);
    if (addr != 0) {
        static thread_local io::buffer obuffer(128);
        // std::ostringstream oss;
        obuffer.clear();
        obuffer << static_cast<uint8_t>(FREE_) << static_cast<uint32_t>(++callSeq_) << reinterpret_cast<uint64_t>(addr);
        // oss << FREE_ << '\\' << ++callSeq_ << '\\' << ptr;
        loli_server_send(obuffer.data(), obuffer.size());
        loli_maybe_record_alloc(new_size, addr, loliFlags::MALLOC_, index);
    }
    return addr;
}

void *loli_index_mmap(void *ptr, size_t length, int prot, int flags, int fd, off_t offset, int index) {
    auto addr = mmap(ptr, length, prot, flags, fd, offset);
    if (addr == MAP_FAILED) {
        return addr;
    }

    // Count for regions with MAP_ANONYMOUS or MAP_PRIVATE flag set.
    if (!(flags & MAP_ANON) && !(flags & MAP_PRIVATE)) {
        return addr;
    }

    size_t pagesize = 4096;
    size_t numpages = std::max((size_t)1, (length + pagesize - 1) / pagesize);

    // Since mmaped memory can be munmapped partially, 
    // we convert mmap length to page count to support this behaviour.
    uint64_t curaddr = reinterpret_cast<uint64_t>(addr);
    for (size_t i = 0; i < numpages; i++) {
        loli_index_custom_alloc(reinterpret_cast<void*>(curaddr), pagesize, index);
        curaddr += pagesize;
    }

    return addr;
}

void *loli_index_mmap64(void *ptr, size_t length, int prot, int flags, int fd, off64_t offset, int index) {
    auto addr = mmap64(ptr, length, prot, flags, fd, offset);
    if (addr == MAP_FAILED) {
        return addr;
    }

    // Count for regions with MAP_ANONYMOUS or MAP_PRIVATE flag set.
    if (!(flags & MAP_ANON) && !(flags & MAP_PRIVATE)) {
        return addr;
    }

    size_t pagesize = 4096;
    size_t numpages = std::max((size_t)1, (length + pagesize - 1) / pagesize);

    // Since mmaped memory can be munmapped partially, 
    // we convert mmap length to page count to support this behaviour.
    uint64_t curaddr = reinterpret_cast<uint64_t>(addr);
    for (size_t i = 0; i < numpages; i++) {
        loli_index_custom_alloc(reinterpret_cast<void*>(curaddr), pagesize, index);
        curaddr += pagesize;
    }

    return addr;
}

int loli_munmap(void *ptr, size_t length) {
    auto result = munmap(ptr, length);
    if (result != 0) {
        return result;
    }

    size_t pagesize = 4096;
    size_t numpages = std::max((size_t)1, (length + pagesize - 1) / pagesize);

    uint64_t curaddr = reinterpret_cast<uint64_t>(ptr);
    for (size_t i = 0; i < numpages; i++) {
        loli_custom_free(reinterpret_cast<void*>(curaddr));
        curaddr += pagesize;
    }

    return result;
}

#ifdef __cplusplus
}
#endif // __cplusplus

BACKTRACE_FPTR loli_get_backtrace(const char* path) {
    BACKTRACE_FPTR backtrace = nullptr;
    void *handler = fake_dlopen(path, RTLD_LAZY);
    if (handler) {
        void (*set_loli_ignore_func)(void (*funcPtr)(bool)) = nullptr;
        *(void **) (&set_loli_ignore_func) = fake_dlsym(handler, "set_loli_ignore_func");
        if (set_loli_ignore_func == nullptr) {
            fake_dlclose(handler);
            LOLILOGI("Error dlsym set_loli_ignore_func: %s", path);
            return backtrace;
        }
        (*set_loli_ignore_func)(toggle_ignore_current);
        *(void **) (&backtrace) = fake_dlsym(handler, "get_stack_backtrace");
        if (backtrace == nullptr) {
            LOLILOGI("Error dlsym get_stack_backtrace: %s", path);
        }
        fake_dlclose(handler);
    } else {
        LOLILOGI("Error dlopen: %s", path);
    }
    return backtrace;
}

typedef void (*LOLI_SET_ALLOCANDFREE_FPTR)(LOLI_ALLOC_FPTR, FREE_FPTR);

LOLI_SET_ALLOCANDFREE_FPTR loli_get_allocandfree(const char* path) {
    void *handler = fake_dlopen(path, RTLD_LAZY);
    if (handler) {
        LOLI_SET_ALLOCANDFREE_FPTR ptr = nullptr;
        *(void **) (&ptr) = fake_dlsym(handler, "loli_set_allocandfree");
        fake_dlclose(handler);
        return ptr;
    } else {
        LOLILOGI("Error dlopen: %s", path);
    }
    return nullptr;
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
        if (auto set_allocandfree = loli_get_allocandfree(brief.first.c_str())) {
            set_allocandfree(info->custom_alloc, loli_custom_free);
        }
        auto regex = std::string(".*/") + library + "\\.so$";
        if (hookMode_ == loliHookMode::MMAP) {
            xhook_register(regex.c_str(), "mmap", (void*)info->mmap, nullptr);
            xhook_register(regex.c_str(), "mmap64", (void*)info->mmap, nullptr);
            xhook_register(regex.c_str(), "munmap", (void*)loli_munmap, nullptr);
        } else {
            xhook_register(regex.c_str(), "malloc", (void*)info->malloc, nullptr);
            xhook_register(regex.c_str(), "free", (void*)loli_free, nullptr);
            xhook_register(regex.c_str(), "calloc", (void*)info->calloc, nullptr);
            xhook_register(regex.c_str(), "memalign", (void*)info->memalign, nullptr);
            xhook_register(regex.c_str(), "aligned_alloc", (void*)info->memalign, nullptr);
            xhook_register(regex.c_str(), "posix_memalign", (void*)info->posix_memalign, nullptr);
            xhook_register(regex.c_str(), "realloc", (void*)info->realloc, nullptr);
        }
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
        if (blacklist.find(pair.first) != blacklist.end()) {
            continue;
        }
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
    xhook_enable_debug(1);
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
    std::unordered_set<std::string>             matchedDesiredTokens;
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
            // Always keep the smallest base address observed for the same path (safer if maps order varies).
            auto it = libBaseAddrMap.find(pathnameStr);
            if (it == libBaseAddrMap.end()) {
                libBaseAddrMap[pathnameStr] = baseAddr;
            } else {
                it->second = std::min(it->second, baseAddr);
            }
            // path in loaded is full path to so library
            if (loaded.find(pathnameStr) == loaded.end()) {
                loaded.insert(pathnameStr);
                if (isBlacklist_) {
                    shouldHook = true;
                } else {
                    for (auto& token : desired) {
                        if (pathnameStr.find(token) != std::string::npos) {
                            shouldHook = true;
                            // Only decrement once per token to avoid double counting when a token matches multiple paths.
                            if (matchedDesiredTokens.find(token) == matchedDesiredTokens.end()) {
                                matchedDesiredTokens.insert(token);
                                if (loadedDesiredCount > 0) {
                                    loadedDesiredCount--;
                                }
                                LOLILOGI("%s (%s) is loaded", token.c_str(), pathnameStr.c_str());
                            } else {
                                LOLILOGI("%s (%s) matched but token already accounted for", token.c_str(), pathnameStr.c_str());
                            }
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
    std::string whitelist, blacklist, buildtype;
    mode_ = loliDataMode::STRICT;

    std::ifstream infile("/data/local/tmp/loli3.conf");
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
        } else if (words[0] == "hook") {
            if (words[1] == "mmap") {
                hookMode_ = loliHookMode::MMAP;
            } else {
                hookMode_ = loliHookMode::MALLOC;
            }
        } else if (words[0] == "type") {
            isBlacklist_ = words[1] == "blacklist";
        } else if (words[0] == "build") {
            buildtype = words[1];
            isFramePointer_ = words[1] == "framepointer";
            isInstrumented_ = words[1] == "instrumented";
        } else if (words[0] == "saved") {
            break;
        }
    }
    hookLibraries = isBlacklist_ ? blacklist : whitelist;
    LOLILOGI("mode: %i, build: %s, minRecSize: %i, blacklist: %i, hookLibs: %s",
        static_cast<int>(mode_), buildtype.c_str(), minRecSize, isBlacklist_ ? 1 : 0, hookLibraries.c_str());
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

#endif // LOLI_CPP