#ifndef _STDINT_H
#define _STDINT_H

#include "stddef.h"

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long long int64_t;

typedef uint32_t uintptr_t;
typedef int32_t intptr_t;

#define UINT8_MAX   0xffU
#define UINT16_MAX  0xffffU
#define UINT32_MAX  0xffffffffU
#define UINT64_MAX  0xffffffffffffffffULL

#define INT8_MAX    0x7f
#define INT16_MAX   0x7fff
#define INT32_MAX   0x7fffffff
#define INT64_MAX   0x7fffffffffffffffLL

#define SIZE_MAX    0xffffffffU

#endif
