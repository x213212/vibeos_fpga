#ifndef __STDLIB_H__
#define __STDLIB_H__

#include "stddef.h"

void *malloc(size_t size);
void free(void *p);
void *calloc(size_t n, size_t size);
void *realloc(void *p, size_t size);

#endif
