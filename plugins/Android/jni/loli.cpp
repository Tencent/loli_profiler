#ifndef LOLI_CPP // Lightweight Opensource profiLing Instrument
#define LOLI_CPP

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

namespace loli {
size_t capture(void** buffer, size_t max);
void dump(std::ostream& os, void** buffer, size_t count);
int serverStart(int port);
void serverShutdown();
}

std::chrono::system_clock::time_point startTime_;
std::mutex cacheMutex_;
std::vector<std::string> cache_;
int minRecSize_ = 0;
std::atomic<std::uint32_t> callSeq_;

enum loliFlags {
    FREE_ = 0, 
    MALLOC_ = 1, 
    CALLOC_ = 2, 
    MEMALIGN_ = 3, 
    REALLOC_ = 4, 
};

void *loliMalloc(size_t size) {
    if (size < static_cast<size_t>(minRecSize_))
        return malloc(size);
    const size_t max = 60;
    void* buffer[max];
    std::ostringstream oss;
    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - startTime_).count();
    auto mem = malloc(size);
    oss << MALLOC_ << '\\' << ++callSeq_ << ',' << time << ',' << size << ',' << mem << '\\';
    loli::dump(oss, buffer, loli::capture(buffer, max));
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cache_.emplace_back(oss.str());
    }
    return mem;
}

void loliFree(void* ptr) {
    if (ptr == nullptr) 
        return;
    std::ostringstream oss;
    // auto time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - startTime_).count();
    oss << FREE_ << '\\' << ++callSeq_ << '\\' << ptr;
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cache_.emplace_back(oss.str());
    }
    free(ptr);
}

void *loliCalloc(int n, int size) {
    if (n * size < minRecSize_)
        return calloc(n, size);
    const size_t max = 60;
    void* buffer[max];
    std::ostringstream oss;
    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - startTime_).count();
    auto mem = calloc(n, size);
    oss << CALLOC_ << '\\' << ++callSeq_ << ','<< time << ',' << n * size << ',' << mem << '\\';
    loli::dump(oss, buffer, loli::capture(buffer, max));
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cache_.emplace_back(oss.str());
    }
    return mem;
}

void *loliMemalign(size_t alignment, size_t size) {
    if (size < static_cast<size_t>(minRecSize_))
        return memalign(alignment, size);
    const size_t max = 60;
    void* buffer[max];
    std::ostringstream oss;
    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - startTime_).count();
    auto mem = memalign(alignment, size);
    oss << MEMALIGN_ << '\\' << ++callSeq_ << ','<< time << ',' << size << ',' << mem << '\\';
    loli::dump(oss, buffer, loli::capture(buffer, max));
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cache_.emplace_back(oss.str());
    }
    return mem;
}

void *loliRealloc(void *ptr, size_t new_size) {
    if (new_size < static_cast<size_t>(minRecSize_))
        return realloc(ptr, new_size);
    const size_t max = 60;
    void* buffer[max];
    std::ostringstream oss;
    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - startTime_).count();
    auto mem = realloc(ptr, new_size);
    if (ptr == mem) {
        oss << FREE_ << '\\' << ++callSeq_ << '\\' << ptr;
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cache_.emplace_back(oss.str());
        oss.str(std::string());
        oss.clear();
    }
    oss << REALLOC_ << '\\' << ++callSeq_ << ',' << time << ',' << new_size << ',' << mem << '\\';
    loli::dump(oss, buffer, loli::capture(buffer, max));
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cache_.emplace_back(oss.str());
    }
    return mem;
}

int loliHook(std::unordered_set<std::string>& tokens) {
    xhook_enable_debug(1);
    xhook_clear();
    int ecode = 0;
    for (auto& token : tokens) {
        auto soReg = ".*/" + token + "\\.so$";
        // __android_log_print(ANDROID_LOG_INFO, "Loli", "hooking %s", token.c_str());
        ecode = xhook_register(soReg.c_str(), "malloc", (void*)loliMalloc, nullptr);
        if (ecode != 0) {
            __android_log_print(ANDROID_LOG_INFO, "Loli", "error hooking %s's malloc()", token.c_str());
            return ecode;
        }
        ecode = xhook_register(soReg.c_str(), "free", (void*)loliFree, nullptr);
        if (ecode != 0) {
            __android_log_print(ANDROID_LOG_INFO, "Loli", "error hooking %s's free()", token.c_str());
            return ecode;
        }
        ecode = xhook_register(soReg.c_str(), "calloc", (void*)loliCalloc, nullptr);
        if (ecode != 0) {
            __android_log_print(ANDROID_LOG_INFO, "Loli", "error hooking %s's calloc()", token.c_str());
            return ecode;
        }
        ecode = xhook_register(soReg.c_str(), "memalign", (void*)loliMemalign, nullptr);
        if (ecode != 0) {
            __android_log_print(ANDROID_LOG_INFO, "Loli", "error hooking %s's memalign()", token.c_str());
            return ecode;
        }
        ecode = xhook_register(soReg.c_str(), "realloc", (void*)loliRealloc, nullptr);
        if (ecode != 0) {
            __android_log_print(ANDROID_LOG_INFO, "Loli", "error hooking %s's realloc()", token.c_str());
            return ecode;
        }
    }
    xhook_refresh(0);
    return ecode;
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
                for (auto& token : desired) {
                    if (pathnameStr.find(token) != std::string::npos) {
                        loadedDesiredCount--;
                        shouldHook = true;
                        __android_log_print(ANDROID_LOG_INFO, "Loli", "%s is loaded", token.c_str());
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

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    __android_log_print(ANDROID_LOG_INFO, "Loli", "JNI_OnLoad");
    JNIEnv* env;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR; // JNI version not supported.
    }
    std::ifstream infile("/data/local/tmp/loli.conf");
    std::string line;
    int count = 0;
    int minRecSize = 512;
    std::string hookLibraries = "libil2cpp,libunity";
    while (std::getline(infile, line)) {
        if (count == 0) {
            std::istringstream iss(line);
            iss >> minRecSize;
        } else if (count == 1) {
            hookLibraries = line;
        } else {
            break;
        }
        count++;
    }
    __android_log_print(ANDROID_LOG_INFO, "Loli", "minRecSize:: %i, hookLibs: %s", minRecSize, hookLibraries.c_str());
    // parse library tokens
    std::unordered_set<std::string> tokens;
    std::istringstream namess(hookLibraries);
    while (std::getline(namess, line, ',')) {
        tokens.insert(line);
    }
    // start tcp server
    minRecSize_ = minRecSize;
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
    const void* prevAddr = nullptr;
    for (size_t idx = 0; idx < count; ++idx) {
        const void* addr = buffer[idx];
        if (addr == prevAddr) // skip same addr
            continue;
        Dl_info info;
        if (dladdr(addr, &info)) {
            int status = 0;
            {
                auto demangled = __cxxabiv1::__cxa_demangle(info.dli_fname, 0, 0, &status);
                const char* dlname = (status == 0 && demangled != nullptr) ? demangled : info.dli_fname;
                auto shortdlname = strrchr(dlname, '/');
                os << (shortdlname ? shortdlname + 1 : dlname) << '\\';
                if (demangled != nullptr) free(demangled);
            }
            // don't demangle function names, because they might be very loooooong
            // if (info.dli_sname != nullptr) {
            //     auto demangled = __cxxabiv1::__cxa_demangle(info.dli_sname, 0, 0, &status);
            //     if (status == 0 && demangled != nullptr) {
            //         os << demangled << '\\';
            //     } else {
            //         os << info.dli_sname << '\\';
            //     }
            //     if (demangled != nullptr) free(demangled);
            // } else {
                const void* reladdr = (void*)((_Unwind_Word)addr - (_Unwind_Word)info.dli_fbase);
                os << reladdr << '\\';
            // }
        }
        prevAddr = addr;
    }
}

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
    time.tv_usec = 33;
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
                    hasClient_ = true;
                }
            }
        } else {
            // fill cached messages
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration<double, std::milli>(now - lastTickTime).count() > 66.6) {
                lastTickTime = now;
                std::lock_guard<std::mutex> lock(cacheMutex_);
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