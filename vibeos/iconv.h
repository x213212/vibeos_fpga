#ifndef ICONV_H
#define ICONV_H
#include <stddef.h>
typedef void* iconv_t;
static inline iconv_t iconv_open(const char *tocode, const char *fromcode) { return (iconv_t)-1; }
static inline size_t iconv(iconv_t cd, char **inbuf, size_t *inbytesleft, char **outbuf, size_t *outbytesleft) { return (size_t)-1; }
static inline int iconv_close(iconv_t cd) { return 0; }
#endif
