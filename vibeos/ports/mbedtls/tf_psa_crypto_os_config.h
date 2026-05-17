#ifndef TF_PSA_CRYPTO_OS_CONFIG_H
#define TF_PSA_CRYPTO_OS_CONFIG_H

#include "stdint.h"
#include "stddef.h"
#include "stdarg.h"

int mbedtls_os_snprintf(char *s, size_t n, const char *fmt, ...);
int mbedtls_os_vsnprintf(char *s, size_t n, const char *fmt, va_list ap);

/*
 * Override the upstream Suite B crypto config for the bare-metal kernel.
 * We keep the crypto surface small and provide random/time from the OS.
 */

#undef MBEDTLS_PSA_BUILTIN_GET_ENTROPY
#undef MBEDTLS_PLATFORM_STD_TIME
#undef MBEDTLS_PLATFORM_TIME_MACRO
#undef MBEDTLS_PLATFORM_TIME_TYPE_MACRO
#undef MBEDTLS_PLATFORM_MS_TIME_TYPE_MACRO
#undef MBEDTLS_PLATFORM_STD_CALLOC
#undef MBEDTLS_PLATFORM_STD_FREE
#undef MBEDTLS_PLATFORM_STD_SNPRINTF
#undef MBEDTLS_PLATFORM_STD_VSNPRINTF

#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS
#define PSA_WANT_KEY_TYPE_AES 1
#define PSA_WANT_ALG_CTR 1
#define PSA_WANT_ALG_SHA_1 1
#define MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG
#define MBEDTLS_PLATFORM_TIME_ALT
#define MBEDTLS_PLATFORM_MS_TIME_ALT
#define MBEDTLS_PLATFORM_SNPRINTF_ALT
#define MBEDTLS_PLATFORM_VSNPRINTF_ALT
#define MBEDTLS_PLATFORM_STD_SNPRINTF mbedtls_os_snprintf
#define MBEDTLS_PLATFORM_STD_VSNPRINTF mbedtls_os_vsnprintf

#endif
