#ifndef OS_LIBSSH2_MBEDTLS_PK_WRAPPER_H
#define OS_LIBSSH2_MBEDTLS_PK_WRAPPER_H

#include "../../tf-psa-crypto/include/mbedtls/pk.h"

#ifdef LIBSSH2_MBEDTLS
typedef mbedtls_pk_sigalg_t mbedtls_pk_type_t;

#ifndef MBEDTLS_PK_NONE
#define MBEDTLS_PK_NONE       MBEDTLS_PK_SIGALG_NONE
#endif
#ifndef MBEDTLS_PK_RSA
#define MBEDTLS_PK_RSA        MBEDTLS_PK_SIGALG_RSA_PKCS1V15
#endif
#ifndef MBEDTLS_PK_RSASSA_PSS
#define MBEDTLS_PK_RSASSA_PSS  MBEDTLS_PK_SIGALG_RSA_PSS
#endif
#ifndef MBEDTLS_PK_ECDSA
#define MBEDTLS_PK_ECDSA      MBEDTLS_PK_SIGALG_ECDSA
#endif
#ifndef MBEDTLS_PK_ECKEY
#define MBEDTLS_PK_ECKEY      ((mbedtls_pk_sigalg_t)4)
#endif
#ifndef MBEDTLS_PK_ECKEY_DH
#define MBEDTLS_PK_ECKEY_DH   ((mbedtls_pk_sigalg_t)5)
#endif
#ifndef MBEDTLS_PK_OPAQUE
#define MBEDTLS_PK_OPAQUE     ((mbedtls_pk_sigalg_t)6)
#endif

static inline int os_mbedtls_pk_parse_key_compat(mbedtls_pk_context *ctx,
                                                 const unsigned char *key,
                                                 size_t keylen,
                                                 const unsigned char *pwd,
                                                 size_t pwdlen)
{
    return mbedtls_pk_parse_key(ctx, key, keylen, pwd, pwdlen);
}

/* Older libssh2 expects the pre-3.6 parse_key() signature with RNG args. */
#define mbedtls_pk_parse_key(ctx, key, keylen, pwd, pwdlen, ...) \
    os_mbedtls_pk_parse_key_compat((ctx), (key), (keylen), (pwd), (pwdlen))

mbedtls_pk_type_t mbedtls_pk_get_type(const mbedtls_pk_context *ctx);
int mbedtls_pk_can_do(const mbedtls_pk_context *ctx, mbedtls_pk_type_t type);
#endif /* LIBSSH2_MBEDTLS */

#endif
