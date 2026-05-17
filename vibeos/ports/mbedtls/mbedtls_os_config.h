#ifndef MBEDTLS_OS_CONFIG_H
#define MBEDTLS_OS_CONFIG_H

/*
 * Override the upstream Suite B config for the OS environment.
 * The OS does not provide POSIX sockets, so use custom transport callbacks.
 */

#undef MBEDTLS_NET_C
#undef MBEDTLS_SSL_SRV_C
#undef MBEDTLS_PSA_BUILTIN_GET_ENTROPY

#define MBEDTLS_PSA_DRIVER_GET_ENTROPY

#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS
#undef MBEDTLS_SSL_IN_CONTENT_LEN
#undef MBEDTLS_SSL_OUT_CONTENT_LEN
#define MBEDTLS_SSL_IN_CONTENT_LEN 16384
#define MBEDTLS_SSL_OUT_CONTENT_LEN 16384
#define MBEDTLS_PLATFORM_STD_SNPRINTF mbedtls_os_snprintf
#define MBEDTLS_PLATFORM_STD_VSNPRINTF mbedtls_os_vsnprintf

#define MBEDTLS_MD_CAN_SHA1
#define MBEDTLS_MD_CAN_SHA256
#define MBEDTLS_MD_CAN_SHA512
#define MBEDTLS_MD_CAN_MD5

#define MBEDTLS_AES_ROM_TABLES
#define MBEDTLS_AES_FEAT_WASM

#ifndef TF_PSA_CRYPTO_CONFIG_FILE
#define MBEDTLS_AES_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_CIPHER_MODE_CTR
#endif

#endif
