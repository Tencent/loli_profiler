#include "loli_utils.h"

#include <algorithm>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <string.h>
#include <unwind.h>

// https://github.com/root-project/root/blob/master/LICENSE
// https://github.com/root-project/root/blob/master/misc/memstat/src/TMemStatBacktrace.cxx#L52
#define G__builtin_return_address(N) \
    ((__builtin_frame_address(N) == NULL)  || \
     (__builtin_frame_address(N) < __builtin_frame_address(0))) ? \
    NULL : __builtin_return_address(N)
#define _RET_ADDR(x)   case x: return G__builtin_return_address(x);

static void *return_address(int _frame) {
   switch(_frame) {
      _128_MACRO(_RET_ADDR, 0)
      default:
         return 0;
   }
}

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
   size_t i(0);
   void *addr;
   for(i = 0; (i < max) && (addr = return_address(i)); ++i)
      buffer[i] = addr;
   return i;
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