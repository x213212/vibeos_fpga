#ifndef NETSURF_PORT_H
#define NETSURF_PORT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define __LITTLE_ENDIAN 1234
#define __BYTE_ORDER __LITTLE_ENDIAN

extern void *malloc(uint32_t size);
extern void free(void *ptr);
extern void *realloc(void *ptr, uint32_t size);
extern void *calloc(uint32_t nmemb, uint32_t size);
extern void *memcpy(void *dest, const void *src, uint32_t n);
extern void *memset(void *s, int c, uint32_t n);
extern uint32_t strlen(const char *s);
extern int strcmp(const char *s1, const char *s2);
extern int strncmp(const char *s1, const char *s2, uint32_t n);
extern char *strcpy(char *dest, const char *src);
extern char *strdup(const char *s);
extern int atoi(const char *nptr);
extern char *strstr(const char *haystack, const char *needle);
extern int abs(int n);
extern int lib_printf(const char *fmt, ...);
extern void panic(const char *s);

// 只在非 lwIP 的地方替換 printf
#ifndef LWIP_HDR_ALTCP_H
#define printf lib_printf
#endif

// 不要巨集化 abort 和 fflush，直接宣告函數，讓 linker 去找 user_utils.c 裡的實作
#endif
