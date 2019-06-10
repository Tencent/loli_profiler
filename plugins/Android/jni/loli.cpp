#ifndef LOLI_CPP // Lightweight Opensource profiLing Instrument
#define LOLI_CPP

#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <android/log.h>
#include <cxxabi.h>
#include <dlfcn.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unwind.h>
#include <xhook.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace trace {
size_t capture(void** buffer, size_t max);
void dump(std::ostream& os, void** buffer, size_t count);
}

namespace server {
bool started();
void sendMessage(const std::vector<std::string>& cache);
int start(int port);
void shundown();
}

std::chrono::system_clock::time_point hookTime_;
std::mutex cacheMutex_;
std::vector<std::string> cache_;
std::vector<std::string> cacheCopy_;

enum loliFlags {
    FREE_ = 0, 
    MALLOC_ = 1, 
    CALLOC_ = 2, 
};

void *loliMalloc(size_t size) {
    const size_t max = 30;
    void* buffer[max];
    std::ostringstream oss;
    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - hookTime_).count();
    auto mem = malloc(size);
    oss << MALLOC_ << '\\'<< time << ',' << size << ',' << mem << '\\';
    trace::dump(oss, buffer, trace::capture(buffer, max));
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cache_.emplace_back(oss.str());
    }
    return mem;
}

void loliFree(void* ptr) {
    std::ostringstream oss;
    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - hookTime_).count();
    oss << FREE_ << '\\'<< time << '\\' << ptr;
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cache_.emplace_back(oss.str());
    }
    free(ptr);
}

void *loliCalloc(int n, int size) {
    const size_t max = 30;
    void* buffer[max];
    std::ostringstream oss;
    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - hookTime_).count();
    auto mem = calloc(n, size);
    oss << CALLOC_ << '\\'<< time << ',' << size << ',' << mem << '\\';
    trace::dump(oss, buffer, trace::capture(buffer, max));
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cache_.emplace_back(oss.str());
    }
    return mem;
}

int loliHook(const char *soNames) {
    hookTime_ = std::chrono::system_clock::now();
    std::string names(soNames);
    std::string token;
    char delimiter = ',';
    std::size_t pos = 0;
    int ecode = 0;
    while ((pos = names.find(delimiter)) != std::string::npos) {
        token = names.substr(0, pos);
        auto soName = ".*/" + token + "\\.so$";
        ecode = xhook_register(soName.c_str(), "malloc", (void*)loliMalloc, nullptr);
        if (ecode == 0) 
            ecode = xhook_register(soName.c_str(), "free", (void*)loliFree, nullptr);
        if (ecode == 0)
            ecode = xhook_register(soName.c_str(), "calloc", (void*)loliCalloc, nullptr);
        names.erase(0, pos + 1);
    }
    xhook_refresh(0);
    auto svr = server::start(7100);
    __android_log_print(ANDROID_LOG_INFO, "Loli", "start status %i", svr);
    return ecode;
}

void loliTick() {
    if (!server::started()) 
        return;
    { // copy in memory is faster than write to file
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cacheCopy_ = std::move(cache_);
    }
    if (cacheCopy_.size() > 0) {
        server::sendMessage(cacheCopy_);
        cacheCopy_.clear();
    }
}

namespace trace { // begin trace

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
            if (info.dli_sname != nullptr) {
                auto demangled = __cxxabiv1::__cxa_demangle(info.dli_sname, 0, 0, &status);
                if (status == 0 && demangled != nullptr) {
                    os << demangled << '\\';
                } else {
                    os << info.dli_sname << '\\';
                }
                if (demangled != nullptr) free(demangled);
            } else {
                const void* reladdr = (void*)((_Unwind_Word)addr - (_Unwind_Word)info.dli_fbase);
                os << reladdr << '\\';
            }
        }
        prevAddr = addr;
    }
}

} // end back trace

namespace server { // begin server

char* buffer_ = NULL;
std::mutex sendCacheMutex_;
std::vector<std::string> sendCache_;
const std::size_t bandwidth_ = 500;
std::atomic<bool> serverRunning_ {true};
std::atomic<bool> hasClient_ {false};
std::thread socketThread_;
bool started_ = false;

bool started() {
    return started_;
}

void sendMessage(const std::vector<std::string>& cache) {
    std::lock_guard<std::mutex> lock(sendCacheMutex_);
    for (auto& str : cache)
        sendCache_.emplace_back(std::move(str));
}

void serverLoop(int sock) {
    std::vector<std::string> cacheCopy;
    struct timeval time;
    time.tv_usec = 33;
    fd_set fds;
    int clientSock = -1;
    int count = 0;
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
                std::lock_guard<std::mutex> lock(sendCacheMutex_);
                auto cacheSize = sendCache_.size();
                if (cacheSize <= bandwidth_) {
                    cacheCopy = std::move(sendCache_);
                } else {
                    cacheCopy.reserve(bandwidth_);
                    for (std::size_t i = cacheSize - bandwidth_; i < cacheSize; i++)
                        cacheCopy.emplace_back(std::move(sendCache_[i]));
                    sendCache_.erase(sendCache_.begin() + (cacheSize - bandwidth_), sendCache_.end());
                }
            }
            if (cacheCopy.size() > 0) {
                std::ostringstream stream;
                for (auto& str : cacheCopy) 
                    stream << str << std::endl;
                const auto& str = stream.str();
                std::uint32_t strSize = static_cast<std::uint32_t>(str.size());
                send(clientSock, &strSize, 4, 0); // send length first
                send(clientSock, str.c_str(), str.size(), 0); // then send data
                __android_log_print(ANDROID_LOG_INFO, "Loli", "send size %i, lineCount: %i", strSize, static_cast<int>(cacheCopy.size()));
                cacheCopy.clear();
            }
        }
    }
    close(sock);
    if (hasClient_)
        close(clientSock);
}

int start(int port = 8000) {
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
    socketThread_ = std::move(std::thread(serverLoop, sock));
    return 0;
}

void shundown() {
    if (!started_)
        return;
    serverRunning_ = false;
    hasClient_ = false;
    socketThread_.join();
    free(buffer_);
    buffer_ = NULL;
    started_ = false;
}

} // end server

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // LOLI_CPP