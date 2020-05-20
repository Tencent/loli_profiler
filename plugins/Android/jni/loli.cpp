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
#include <cstring>
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
#include <unwind.h>
#include <inttypes.h>
#include <jni.h>
#include <regex.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "xhook.h"
#include "lz4/lz4.h"

#include "spinlock.h"
#include "sampler.h"

namespace loli {
size_t capture(void** buffer, size_t max);
void dump(std::ostream& os, void** buffer, size_t count);
int serverStart(int port);
void serverShutdown();
}

enum class loliDataMode : std::uint8_t {
    STRICT = 0, 
    LOOSE, 
};

std::chrono::system_clock::time_point startTime_;
std::vector<std::string> cache_;
loli::spinlock cacheLock_;
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

inline void loliMaybeRecordAllocation(size_t size, void* addr, loliFlags flag) {
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
    loli::dump(oss, buffer, loli::capture(buffer, STACKBUFFERSIZE));
    {
        std::lock_guard<loli::spinlock> lock(cacheLock_);
        cache_.emplace_back(oss.str());
    }
}

void *loliMalloc(size_t size) {
    void* addr = malloc(size);
    loliMaybeRecordAllocation(size, addr, loliFlags::MALLOC_);
    return addr;
}

void loliFree(void* ptr) {
    if (ptr == nullptr) 
        return;
    std::ostringstream oss;
    oss << FREE_ << '\\' << ++callSeq_ << '\\' << ptr;
    {
        std::lock_guard<loli::spinlock> lock(cacheLock_);
        cache_.emplace_back(oss.str());
    }
    free(ptr);
}

void *loliCalloc(int n, int size) {
    void* addr = calloc(n, size);
    loliMaybeRecordAllocation(n * size, addr, loliFlags::CALLOC_);
    return addr;
}

void *loliMemalign(size_t alignment, size_t size) {
    void* addr = memalign(alignment, size);
    loliMaybeRecordAllocation(size, addr, loliFlags::MEMALIGN_);
    return addr;
}

void *loliRealloc(void *ptr, size_t new_size) {
    void* addr = realloc(ptr, new_size);
    if (addr != 0)
    {
        {
            std::ostringstream oss;
            oss << FREE_ << '\\' << ++callSeq_ << '\\' << ptr;
            std::lock_guard<loli::spinlock> lock(cacheLock_);
            cache_.emplace_back(oss.str());
        }
        loliMaybeRecordAllocation(new_size, addr, loliFlags::MALLOC_);
    }
    return addr;
}

int loliHook(std::unordered_set<std::string>& tokens) {
    xhook_enable_debug(1);
    xhook_clear();
    auto hookLibrary = [](const char* soRegex) -> bool {
        int ecode = xhook_register(soRegex, "malloc", (void*)loliMalloc, nullptr);
        if (ecode != 0) {
            __android_log_print(ANDROID_LOG_INFO, "Loli", "error hooking %s's malloc()", soRegex);
            return ecode;
        }
        ecode = xhook_register(soRegex, "free", (void*)loliFree, nullptr);
        if (ecode != 0) {
            __android_log_print(ANDROID_LOG_INFO, "Loli", "error hooking %s's free()", soRegex);
            return ecode;
        }
        ecode = xhook_register(soRegex, "calloc", (void*)loliCalloc, nullptr);
        if (ecode != 0) {
            __android_log_print(ANDROID_LOG_INFO, "Loli", "error hooking %s's calloc()", soRegex);
            return ecode;
        }
        ecode = xhook_register(soRegex, "memalign", (void*)loliMemalign, nullptr);
        if (ecode != 0) {
            __android_log_print(ANDROID_LOG_INFO, "Loli", "error hooking %s's memalign()", soRegex);
            return ecode;
        }
        ecode = xhook_register(soRegex, "realloc", (void*)loliRealloc, nullptr);
        if (ecode != 0) {
            __android_log_print(ANDROID_LOG_INFO, "Loli", "error hooking %s's realloc()", soRegex);
            return ecode;
        }
        return ecode;
    };
    if (isBlacklist_) {
        for (auto& token : tokens) {
            auto tokenRegex = ".*/" + token + "\\.so$";
            xhook_ignore(tokenRegex.c_str(), NULL);
        }
        int ecode = hookLibrary(".*\\.so$");
        if (ecode != 0) {
            return ecode;
        }
    } else {
        for (auto& token : tokens) {
            auto tokenRegex = ".*/" + token + "\\.so$";
            // __android_log_print(ANDROID_LOG_INFO, "Loli", "hooking %s", token.c_str());
            int ecode = hookLibrary(tokenRegex.c_str());
            if (ecode != 0) {
                return ecode;
            }
        }
    }
    xhook_refresh(0);
    return 0;
}

void loliProcMapsThread(const std::unordered_set<std::string>& libs) {
    char                                   line[512]; // proc/self/maps parsing code by xhook
    FILE                                  *fp;
    uintptr_t                              baseAddr;
    char                                   perm[5];
    unsigned long                          offset;
    int                                    pathNamePos;
    char                                  *pathName;
    size_t                                 pathNameLen;
    std::unordered_set<std::string>        loaded;
    std::unordered_set<std::string>        desired(libs);
    int                                    loadedDesiredCount = static_cast<int>(desired.size());
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
                loaded.insert(pathnameStr);
                if (isBlacklist_) {
                    for (auto& token : desired) {
                        if (pathnameStr.find(token) == std::string::npos) {
                            shouldHook = true;
                            break;
                        }
                    }
                } else {
                    for (auto& token : desired) {
                        if (pathnameStr.find(token) != std::string::npos) {
                            loadedDesiredCount--;
                            shouldHook = true;
                            __android_log_print(ANDROID_LOG_INFO, "Loli", "%s is loaded", token.c_str());
                        }
                    }
                }
            }
        }
        fclose(fp);
        if (shouldHook) 
            loliHook(desired);
        if (loadedDesiredCount <= 0) {
            __android_log_print(ANDROID_LOG_INFO, "Loli", "All desired libraries are loaded.");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void loliTrim(std::string &s) {
    s.erase(std::remove_if(s.begin(), s.end(), [](int ch) {
        return std::isspace(ch);
    }), s.end());
}

// str: 要分割的字符串
// result: 保存分割结果的字符串数组
// delim: 分隔字符串
void split(const std::string& str,
           std::vector<std::string>& tokens,
           const std::string delim = " ") {
    tokens.clear();

    char* buffer = new char[str.size() + 1];
    std::strcpy(buffer, str.c_str());

    char* tmp;
    char* p = strtok_r(buffer, delim.c_str(), &tmp);
    do {
        tokens.push_back(p);
    } while ((p = strtok_r(nullptr, delim.c_str(), &tmp)) != nullptr);

    delete[] buffer;
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    __android_log_print(ANDROID_LOG_INFO, "Loli", "JNI_OnLoad");
    JNIEnv* env;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR; // JNI version not supported.
    }

    int minRecSize = 512;
    std::string hookLibraries = "libil2cpp,libunity";
    mode_ = loliDataMode::STRICT;

    std::ifstream infile("/data/local/tmp/loli2.conf");
    std::string line;
    std::vector<std::string> words;
    while (std::getline(infile, line)) {
         split(line, words,":");
         if (words.size() < 2) {
            continue;
         }
         if (words[0] == "threshold") {
            std::istringstream iss(words[1]);
            iss >> minRecSize;
         } else if (words[0] == "libraries") {
            hookLibraries = words[1];
         } else if (words[0] == "mode") {
            if (words[1] == "loose")
                mode_ = loliDataMode::LOOSE;
            else 
                mode_ = loliDataMode::STRICT;
         } else if (words[0] == "type") {
            isBlacklist_ = words[1] == "black list";
         }
    }
    __android_log_print(ANDROID_LOG_INFO, "Loli", "mode: %i, minRecSize: %i, blacklist: %i, hookLibs: %s",
        static_cast<int>(mode_), minRecSize, isBlacklist_ ? 1 : 0, hookLibraries.c_str());
    // parse library tokens
    std::unordered_set<std::string> tokens;
    std::istringstream namess(hookLibraries);
    while (std::getline(namess, line, ',')) {
        loliTrim(line);
        tokens.insert(line);
    }
    // start tcp server
    minRecSize_ = minRecSize;
    sampler_ = new loli::Sampler(minRecSize_);
    callSeq_ = 0;
    startTime_ = std::chrono::system_clock::now();
    auto svr = loli::serverStart(7100);
    __android_log_print(ANDROID_LOG_INFO, "Loli", "loli start status %i", svr);
    // start proc/self/maps check thread
    std::thread(loliProcMapsThread, tokens).detach();
    return JNI_VERSION_1_6;
}

namespace loli { // begin loli

struct TraceState {
    void** current;
    void** end;
};

static _Unwind_Reason_Code unwind(struct _Unwind_Context* context, void* arg) {
    TraceState* state = static_cast<TraceState*>(arg);
    uintptr_t pc = _Unwind_GetIP(context);
    if (pc) {
        if (state->current == state->end) {
            return _URC_END_OF_STACK;
        } else {
            *state->current++ = reinterpret_cast<void*>(pc);
        }
    }
    return _URC_NO_REASON;
}

size_t capture(void** buffer, size_t max) {
    TraceState state = {buffer, buffer + max};
    _Unwind_Backtrace(unwind, &state);
    return state.current - buffer;
}

void dump(std::ostream& os, void** buffer, size_t count) {
    for (size_t idx = 1; idx < count; ++idx) { // idx = 1 to ignore loli's hook function
        const void* addr = buffer[idx];
        os << addr << '\\';
    }
}

// void dump(std::ostream& os, void** buffer, size_t count) {
//     const void* prevAddr = nullptr;
//     for (size_t idx = 1; idx < count; ++idx) { // idx = 1 to ignore loli's hook function
//         const void* addr = buffer[idx];
//         if (addr == prevAddr) // skip same addr
//             continue;
//         static thread_local std::unordered_map<const void*, Dl_info> dlInfoCache;
//         auto it = dlInfoCache.find(addr);
//         if (it == dlInfoCache.end()) {
//             Dl_info info;
//             if (dladdr(addr, &info)) {
//                 it = dlInfoCache.emplace(addr, info).first;
//             } else {
//                 continue;
//             }
//         }
//         Dl_info& info = it->second;
//         int status = 0;
//         {
//             auto demangled = __cxxabiv1::__cxa_demangle(info.dli_fname, 0, 0, &status);
//             const char* dlname = (status == 0 && demangled != nullptr) ? demangled : info.dli_fname;
//             auto shortdlname = strrchr(dlname, '/');
//             os << (shortdlname ? shortdlname + 1 : dlname) << '\\';
//             if (demangled != nullptr) free(demangled);
//         }
//         const void* reladdr = (void*)((_Unwind_Word)addr - (_Unwind_Word)info.dli_fbase);
//         os << reladdr << '\\';
//         prevAddr = addr;
//     }
// }

char* buffer_ = NULL;
const std::size_t bandwidth_ = 3000;
std::atomic<bool> serverRunning_ {true};
std::atomic<bool> hasClient_ {false};
std::thread socketThread_;
bool started_ = false;

bool serverStarted() {
    return started_;
}

void serverLoop(int sock) {
    std::vector<std::string> cacheCopy;
    std::vector<std::string> sendCache;
    uint32_t compressBufferSize = 1024;
    char* compressBuffer = new char[compressBufferSize];
    struct timeval time;
    time.tv_sec = 0; // must initialize this value to prevent uninitialised memory
    time.tv_usec = 100;
    fd_set fds;
    int clientSock = -1;
    auto lastTickTime = std::chrono::steady_clock::now();
    while (serverRunning_) {
        if (!serverRunning_)
            break;
        if (!hasClient_) { // handle new connection
            FD_ZERO(&fds);
            FD_SET(sock, &fds);
            if (select(sock + 1, &fds, NULL, NULL, &time) < 1)
                continue;
            if (FD_ISSET(sock, &fds)) {
                clientSock = accept(sock, NULL, NULL);
                if (clientSock >= 0) {
                    __android_log_print(ANDROID_LOG_INFO, "Loli", "Client connected");
                    hasClient_ = true;
                }
            }
        } else {
            // fill cached messages
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration<double, std::milli>(now - lastTickTime).count() > 66.6) {
                lastTickTime = now;
                std::lock_guard<loli::spinlock> lock(cacheLock_);
                if (sendCache.size() > 0) {
                    sendCache.insert(sendCache.end(), cache_.begin(), cache_.end());
                    cache_.clear();
                }
                else {
                    sendCache = std::move(cache_);
                }
            }
            // check for client connectivity
            FD_ZERO(&fds);
            FD_SET(clientSock, &fds);
            if (select(clientSock + 1, &fds, NULL, NULL, &time) > 0 && FD_ISSET(clientSock, &fds)) {
                int ecode = recv(clientSock, buffer_, BUFSIZ, 0);
                if (ecode <= 0) {
                    hasClient_ = false;
                    __android_log_print(ANDROID_LOG_INFO, "Loli", "Client disconnected, ecode: %i", ecode);
                    continue;
                }
            }
            // send cached messages with limited banwidth
            {
                auto cacheSize = sendCache.size();
                if (cacheSize <= bandwidth_) {
                    cacheCopy = std::move(sendCache);
                } else {
                    cacheCopy.reserve(bandwidth_);
                    for (std::size_t i = cacheSize - bandwidth_; i < cacheSize; i++)
                        cacheCopy.emplace_back(std::move(sendCache[i]));
                    sendCache.erase(sendCache.begin() + (cacheSize - bandwidth_), sendCache.end());
                }
            }
            if (cacheCopy.size() > 0) {
                std::ostringstream stream;
                for (auto& str : cacheCopy) 
                    stream << str << std::endl;
                const auto& str = stream.str();
                std::uint32_t srcSize = static_cast<std::uint32_t>(str.size());
                // lz4 compression
                uint32_t requiredSize = LZ4_compressBound(srcSize);
                if (requiredSize > compressBufferSize) { // enlarge compress buffer if necessary
                    compressBufferSize = static_cast<std::uint32_t>(requiredSize * 1.5f);
                    delete[] compressBuffer;
                    compressBuffer = new char[compressBufferSize];
                    // __android_log_print(ANDROID_LOG_INFO, "Loli", "Buffer exapnding: %i", static_cast<uint32_t>(compressBufferSize));
                }
                uint32_t compressSize = LZ4_compress_default(str.c_str(), compressBuffer, srcSize, requiredSize);
                if (compressSize == 0) {
                    __android_log_print(ANDROID_LOG_INFO, "Loli", "LZ4 compression failed!");
                } else {
                    compressSize += 4;
                    // send messages
                    send(clientSock, &compressSize, 4, 0); // send net buffer size
                    send(clientSock, &srcSize, 4, 0); // send uncompressed buffer size (for decompression)
                    send(clientSock, compressBuffer, compressSize - 4, 0); // then send data
                    // __android_log_print(ANDROID_LOG_INFO, "Loli", "send size %i, compressed size %i, lineCount: %i", srcSize, compressSize, static_cast<int>(cacheCopy.size()));
                }
                cacheCopy.clear();
            }
        }
    }
    delete[] compressBuffer;
    close(sock);
    if (hasClient_)
        close(clientSock);
}

int serverStart(int port = 8000) {
    if (started_)
        return 0;
    // allocate buffer
    buffer_ = (char*)malloc(BUFSIZ);
    memset(buffer_, 0, BUFSIZ);
    // setup server addr
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);
    // create socket
    int sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        __android_log_print(ANDROID_LOG_INFO, "Loli", "start.socket %i", sock);
        return -1;
    }
    // bind address
    int ecode = bind(sock, (struct sockaddr*)&serverAddr, sizeof(struct sockaddr));
    if (ecode < 0) {
        __android_log_print(ANDROID_LOG_INFO, "Loli", "start.bind %i", ecode);
        return -1;
    }
    // set max send buffer
    int sendbuff = 327675;
    ecode = setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sendbuff, sizeof(sendbuff));
    if (ecode < 0) {
        __android_log_print(ANDROID_LOG_INFO, "Loli", "start.setsockopt %i", ecode);
        return -1;
    }
    // listen for incomming connections
    ecode = listen(sock, 2);
    if (ecode < 0) {
        __android_log_print(ANDROID_LOG_INFO, "Loli", "start.listen %i", ecode);
        return -1;
    }
    started_ = true;
    serverRunning_ = true;
    hasClient_ = false;
    socketThread_ = std::thread(serverLoop, sock);
    return 0;
}

void serverShutdown() {
    if (!started_)
        return;
    serverRunning_ = false;
    hasClient_ = false;
    socketThread_.join();
    free(buffer_);
    buffer_ = NULL;
    started_ = false;
}

} // end loli

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // LOLI_CPP