//
//  wrapper.h
//  TestMacro
//
//  Created by ashenzhou(周星) on 2020/5/19.
//  Copyright © 2020 ashenzhou(周星). All rights reserved.
//

#ifndef wrapper_h
#define wrapper_h

#define SLOT_NUM 128

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
_512_MACRO(MACRO, INDEX##0,##__VA_ARGS__)\
_512_MACRO(MACRO, INDEX##1,##__VA_ARGS__)\
_512_MACRO(MACRO, INDEX##2,##__VA_ARGS__)\
_512_MACRO(MACRO, INDEX##3,##__VA_ARGS__)\
_512_MACRO(MACRO, INDEX##4,##__VA_ARGS__)\
_512_MACRO(MACRO, INDEX##5,##__VA_ARGS__)\
_512_MACRO(MACRO, INDEX##6,##__VA_ARGS__)\
_512_MACRO(MACRO, INDEX##7,##__VA_ARGS__)


extern "C"
{
int test();
}
#endif /* wrapper_h */
