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
         _RET_ADDR(0);
         _RET_ADDR(1);
         _RET_ADDR(2);
         _RET_ADDR(3);
         _RET_ADDR(4);
         _RET_ADDR(5);
         _RET_ADDR(6);
         _RET_ADDR(7);
         _RET_ADDR(8);
         _RET_ADDR(9);
         _RET_ADDR(10);
         _RET_ADDR(11);
         _RET_ADDR(12);
         _RET_ADDR(13);
         _RET_ADDR(14);
         _RET_ADDR(15);
         _RET_ADDR(16);
         _RET_ADDR(17);
         _RET_ADDR(18);
         _RET_ADDR(19);
         _RET_ADDR(20);
         _RET_ADDR(21);
         _RET_ADDR(22);
         _RET_ADDR(23);
         _RET_ADDR(24);
         _RET_ADDR(25);
         _RET_ADDR(26);
         _RET_ADDR(27);
         _RET_ADDR(28);
         _RET_ADDR(29);
         _RET_ADDR(30);
         _RET_ADDR(31);
         _RET_ADDR(32);
         _RET_ADDR(33);
         _RET_ADDR(34);
         _RET_ADDR(35);
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

void loli_dump(std::ostream& os, void** buffer, size_t count) {
    for (size_t idx = 1; idx < count; ++idx) { // idx = 1 to ignore loli's hook function
        const void* addr = buffer[idx];
        os << addr << '\\';
    }
}

#ifdef __cplusplus
}
#endif // __cplusplus