#pragma once

#include <sstream>
#include <string>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <android/log.h>
#include <buffer.h>

#define LOLILOGI(...) __android_log_print(ANDROID_LOG_INFO, "Loli", __VA_ARGS__)
#define LOLILOGW(...) __android_log_print(ANDROID_LOG_WARN, "Loli", __VA_ARGS__)
#define LOLILOGE(...) __android_log_print(ANDROID_LOG_ERROR, "Loli", __VA_ARGS__)

#define _8_MACRO(MACRO, INDEX,...)\
MACRO(INDEX##0,##__VA_ARGS__)\
MACRO(INDEX##1,##__VA_ARGS__)\
MACRO(INDEX##2,##__VA_ARGS__)\
MACRO(INDEX##3,##__VA_ARGS__)\
MACRO(INDEX##4,##__VA_ARGS__)\
MACRO(INDEX##5,##__VA_ARGS__)\
MACRO(INDEX##6,##__VA_ARGS__)\
MACRO(INDEX##7,##__VA_ARGS__)

#define _64_MACRO(MACRO, INDEX, ...)\
_8_MACRO(MACRO, INDEX##0,##__VA_ARGS__)\
_8_MACRO(MACRO, INDEX##1,##__VA_ARGS__)\
_8_MACRO(MACRO, INDEX##2,##__VA_ARGS__)\
_8_MACRO(MACRO, INDEX##3,##__VA_ARGS__)\
_8_MACRO(MACRO, INDEX##4,##__VA_ARGS__)\
_8_MACRO(MACRO, INDEX##5,##__VA_ARGS__)\
_8_MACRO(MACRO, INDEX##6,##__VA_ARGS__)\
_8_MACRO(MACRO, INDEX##7,##__VA_ARGS__)

#define _128_MACRO(MACRO, INDEX, ...)\
_64_MACRO(MACRO, INDEX##0,##__VA_ARGS__)\
_64_MACRO(MACRO, INDEX##1,##__VA_ARGS__)\

#define _256_MACRO(MACRO, INDEX, ...)\
_64_MACRO(MACRO, INDEX##0,##__VA_ARGS__)\
_64_MACRO(MACRO, INDEX##1,##__VA_ARGS__)\
_64_MACRO(MACRO, INDEX##2,##__VA_ARGS__)\
_64_MACRO(MACRO, INDEX##3,##__VA_ARGS__)\

#define _512_MACRO(MACRO, INDEX, ...)\
_64_MACRO(MACRO, INDEX##0,##__VA_ARGS__)\
_64_MACRO(MACRO, INDEX##1,##__VA_ARGS__)\
_64_MACRO(MACRO, INDEX##2,##__VA_ARGS__)\
_64_MACRO(MACRO, INDEX##3,##__VA_ARGS__)\
_64_MACRO(MACRO, INDEX##4,##__VA_ARGS__)\
_64_MACRO(MACRO, INDEX##5,##__VA_ARGS__)\
_64_MACRO(MACRO, INDEX##6,##__VA_ARGS__)\
_64_MACRO(MACRO, INDEX##7,##__VA_ARGS__)

void loli_trim(std::string &str);

// str: 要分割的字符串
// result: 保存分割结果的字符串数组
// delim: 分隔字符串
void loli_split(const std::string& str, std::vector<std::string>& tokens, const std::string delim = " ");

void loli_demangle(const std::string& name, std::string& demangled);

size_t loli_fastcapture(void** buffer, size_t max);
size_t loli_capture(void** buffer, size_t max);
void loli_dump(io::buffer& obuffer, void** buffer, size_t count);

#ifdef __cplusplus
}
#endif // __cplusplus