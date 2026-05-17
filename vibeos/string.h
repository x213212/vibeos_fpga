#ifndef __STRING_H__
#define __STRING_H__

#include "stddef.h"

void *memset(void *, int, size_t);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
size_t strlen(const char *);
int strcmp(const char *, const char *);
int memcmp(const void *, const void *, size_t);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);

#endif
