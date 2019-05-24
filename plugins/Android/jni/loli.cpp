#ifndef LOLI_CPP // Lightweight Opensource profiLing Instrument
#define LOLI_CPP

#include <string>
#include <mutex>

#include <chrono>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <android/log.h>
#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <xhook.h>
#include <unwind.h>
#include <dlfcn.h>
#include <cxxabi.h>

namespace internal {
size_t captureBacktrace(void** buffer, size_t max);
void dumpBacktrace(std::ostream& os, void** buffer, size_t count);
}

std::chrono::system_clock::time_point hookTime_;
std::mutex cacheMutex_;
std::vector<std::string> cache_;

int loliCacheCount() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return cache_.size();
}

void loliDump(bool append, const char* path) {
    std::ofstream file(path, append ? std::ios::app : std::ios::trunc);
    std::vector<std::string> cacheCopy;
    { // copy in memory is faster than write to file
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cacheCopy = std::move(cache_);
    }
    for (auto&& cache : cacheCopy) 
        file << cache << '\n';
    file.close();
    // __android_log_print(ANDROID_LOG_INFO, "app_name", "%s", oss.str().c_str());
}

void *loliMalloc(size_t size) {
    const size_t max = 30;
    void* buffer[max];
    std::ostringstream oss;
    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - hookTime_).count();
    oss << time << '|' << size << ',';
    internal::dumpBacktrace(oss, buffer, internal::captureBacktrace(buffer, max));
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cache_.emplace_back(oss.str());
    }
    return malloc(size);
}

int loliHook() {
    hookTime_ = std::chrono::system_clock::now();
    int ecode = xhook_register(".*/libil2cpp\\.so$", "malloc", (void*)loliMalloc, nullptr);
    xhook_refresh(0);
    return ecode;
}

namespace internal { // stack trace

struct BacktraceState {
    void** current;
    void** end;
};

static _Unwind_Reason_Code unwindCallback(struct _Unwind_Context* context, void* arg) {
    BacktraceState* state = static_cast<BacktraceState*>(arg);
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

size_t captureBacktrace(void** buffer, size_t max) {
    BacktraceState state = {buffer, buffer + max};
    _Unwind_Backtrace(unwindCallback, &state);
    return state.current - buffer;
}

void dumpBacktrace(std::ostream& os, void** buffer, size_t count) {
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
                os << (shortdlname ? shortdlname + 1 : dlname) << ',';
                if (demangled != nullptr) free(demangled);
            }
            if (info.dli_sname != nullptr) {
                auto demangled = __cxxabiv1::__cxa_demangle(info.dli_sname, 0, 0, &status);
                if (status == 0 && demangled != nullptr) {
                    os << demangled << ',';
                } else {
                    os << info.dli_sname << ',';
                }
                if (demangled != nullptr) free(demangled);
            } else {
                const void* reladdr = (void*)((_Unwind_Word)addr - (_Unwind_Word)info.dli_fbase);
                os << reladdr << ',';
            }
        }
        prevAddr = addr;
    }
}

} // end stack trace

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // LOLI_CPP