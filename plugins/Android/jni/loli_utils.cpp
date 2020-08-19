#include "loli_utils.h"

#include <algorithm>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <string.h>
#include <unwind.h>

// framepointer stack trace references:
// https://chromium.googlesource.com/chromium/src/base/+/master/debug/stack_trace.cc
#include <pthread.h>
#include <unistd.h>

#if defined(__arm__) && defined(__GNUC__) && !defined(__clang__)
// GCC and LLVM generate slightly different frames on ARM, see
// https://llvm.org/bugs/show_bug.cgi?id=18505 - LLVM generates
// x86-compatible frame, while GCC needs adjustment.
constexpr size_t kStackFrameAdjustment = sizeof(uintptr_t);
#else
constexpr size_t kStackFrameAdjustment = 0;
#endif

uintptr_t loli_get_nextstackframe(uintptr_t fp) {
    const uintptr_t* fp_addr = reinterpret_cast<const uintptr_t*>(fp);
    return fp_addr[0] - kStackFrameAdjustment;
}

uintptr_t loli_get_stackframepc(uintptr_t fp) {
    const uintptr_t* fp_addr = reinterpret_cast<const uintptr_t*>(fp);
    return fp_addr[1];
}

bool loli_is_stackframe_valid(uintptr_t fp, uintptr_t prev_fp, uintptr_t stack_end) {
    // With the stack growing downwards, older stack frame must be
    // at a greater address that the current one.
    if (fp <= prev_fp) return false;
    // Assume huge stack frames are bogus.
    if (fp - prev_fp > 100000) return false;
    // Check alignment.
    if (fp & (sizeof(uintptr_t) - 1)) return false;
    if (stack_end) {
        // Both fp[0] and fp[1] must be within the stack.
        if (fp > stack_end - 2 * sizeof(uintptr_t)) return false;
        // Additional check to filter out false positives.
        if (loli_get_stackframepc(fp) < 32768) return false;
    }
    return true;
}

uintptr_t loli_get_stackend() {
    // Bionic reads proc/maps on every call to pthread_getattr_np() when called
    // from the main thread. So we need to cache end of stack in that case to get
    // acceptable performance.
    // For all other threads pthread_getattr_np() is fast enough as it just reads
    // values from its pthread_t argument.
    static uintptr_t main_stack_end = 0;
    bool is_main_thread = getpid() == gettid();
    if (is_main_thread && main_stack_end) {
        return main_stack_end;
    }
    uintptr_t stack_begin = 0;
    size_t stack_size = 0;
    pthread_attr_t attributes;
    int error = pthread_getattr_np(pthread_self(), &attributes);
    if (!error) {
        error = pthread_attr_getstack(
            &attributes, reinterpret_cast<void**>(&stack_begin), &stack_size);
        pthread_attr_destroy(&attributes);
    }
    uintptr_t stack_end = stack_begin + stack_size;
    if (is_main_thread) {
        main_stack_end = stack_end;
    }
    return stack_end;  // 0 in case of error
}

size_t loli_trace_stackframepointers(void** out_trace, size_t max_depth, size_t skip_initial) {
    // Usage of __builtin_frame_address() enables frame pointers in this
    // function even if they are not enabled globally. So 'fp' will always
    // be valid.
    uintptr_t fp = reinterpret_cast<uintptr_t>(__builtin_frame_address(0)) -
                        kStackFrameAdjustment;
    uintptr_t stack_end = loli_get_stackend();
    size_t depth = 0;
    while (depth < max_depth) {
        if (skip_initial != 0) {
        skip_initial--;
        } else {
        out_trace[depth++] = reinterpret_cast<void*>(loli_get_stackframepc(fp));
        }
        uintptr_t next_fp = loli_get_nextstackframe(fp);
        if (loli_is_stackframe_valid(next_fp, fp, stack_end)) {
        fp = next_fp;
        continue;
        }
        // Failed to find next frame.
        break;
    }
    return depth;
}

// https://github.com/root-project/root/blob/master/LICENSE
// https://github.com/root-project/root/blob/master/misc/memstat/src/TMemStatBacktrace.cxx#L52
// #define G__builtin_return_address(N) \
//     ((__builtin_frame_address(N) == NULL)  || \
//      (__builtin_frame_address(N) < __builtin_frame_address(0))) ? \
//     NULL : __builtin_return_address(N)
// #define _RET_ADDR(x)   case x: return G__builtin_return_address(x);

// static void *return_address(int _frame) {
//    switch(_frame) {
//       _128_MACRO(_RET_ADDR, 0)
//       default:
//          return 0;
//    }
// }

void loli_trim(std::string &str) {
    str.erase(std::remove_if(str.begin(), str.end(), [](int ch) {
        return std::isspace(ch);
    }), str.end());
}

// str: 要分割的字符串
// result: 保存分割结果的字符串数组
// delim: 分隔字符串
void loli_split(const std::string& str,
           std::vector<std::string>& tokens,
           const std::string delim) {
    tokens.clear();

    char* buffer = new char[str.size() + 1];
    strcpy(buffer, str.c_str());

    char* tmp;
    char* p = strtok_r(buffer, delim.c_str(), &tmp);
    do {
        tokens.push_back(p);
    } while ((p = strtok_r(nullptr, delim.c_str(), &tmp)) != nullptr);

    delete[] buffer;
}

void loli_demangle(const std::string& name, std::string& demangled) {
    auto slashIndex = name.find_last_of('/');
    demangled = name;
    if (slashIndex != std::string::npos) {
        demangled = name.substr(slashIndex + 1);
    }
    auto dotIndex = demangled.find_last_of('.');
    if (dotIndex != std::string::npos) {
        demangled = demangled.substr(0, dotIndex);
    }
}

struct TraceState {
    void** current;
    void** end;
};

static _Unwind_Reason_Code loli_unwind(struct _Unwind_Context* context, void* arg) {
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

size_t loli_fastcapture(void **buffer, size_t max) {
    return loli_trace_stackframepointers(buffer, max, 0);
   // size_t i(0);
   // void *addr;
   // for(i = 0; (i < max) && (addr = return_address(i)); ++i)
   //    buffer[i] = addr;
   // return i;
}

size_t loli_capture(void** buffer, size_t max) {
    TraceState state = {buffer, buffer + max};
    _Unwind_Backtrace(loli_unwind, &state);
    return state.current - buffer;
}

void loli_dump(io::buffer& obuffer, void** buffer, size_t count) {
    for (size_t idx = 2; idx < count; ++idx) { // idx = 1 to ignore loli's hook function
        const void* addr = buffer[idx];
        obuffer << reinterpret_cast<uint64_t>(addr);
    }
}

#ifdef __cplusplus
}
#endif // __cplusplus