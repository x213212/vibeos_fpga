#include <stddef.h>
#include <string.h>

#include "mbedtls/private_access.h"
#include "mbedtls/pk.h"
#include "mbedtls/private/rsa.h"
#include "mbedtls/private/ecp.h"
#include "mbedtls/private/ecdsa.h"
#include "mbedtls/private/cipher.h"
#include "errno.h"
#include "stdio.h"
#include "sys/select.h"

#ifndef SSH_DEBUG_LOG
#define SSH_DEBUG_LOG 0
#endif

#if !SSH_DEBUG_LOG
#define lib_printf(...) do { } while (0)
#endif

int errno;
FILE *stderr;

/*
 * libssh2's mbedTLS backend still expects a few legacy helper entry points
 * that TF-PSA-Crypto no longer exposes publicly. For the SSH password-login
 * path we only need them to exist at link time; the actual key exchange path
 * is handled through libssh2's own primitives.
 */

const mbedtls_rsa_context *mbedtls_pk_rsa(const mbedtls_pk_context *ctx)
{
    (void)ctx;
    return 0;
}

const mbedtls_ecp_keypair *mbedtls_pk_ec(const mbedtls_pk_context *ctx)
{
    (void)ctx;
    return 0;
}

int mbedtls_pk_parse_keyfile(mbedtls_pk_context *ctx, const char *path,
                             const char *password)
{
    (void)ctx;
    (void)path;
    (void)password;
    return -1;
}

int mbedtls_pk_load_file(const char *path, unsigned char **buf, size_t *n)
{
    (void)path;
    if (buf != 0) {
        *buf = 0;
    }
    if (n != 0) {
        *n = 0;
    }
    return -1;
}

int mbedtls_ecdh_compute_shared(mbedtls_ecp_group *grp, mbedtls_mpi *z,
                                const mbedtls_ecp_point *Q,
                                const mbedtls_mpi *d,
                                int (*f_rng)(void *, unsigned char *, size_t),
                                void *p_rng)
{
    mbedtls_ecp_point secret;
    int ret;

    if (grp == 0 || z == 0 || Q == 0 || d == 0) {
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
    }

    ret = mbedtls_ecp_check_pubkey(grp, Q);
    if (ret != 0) {
        return ret;
    }

    mbedtls_ecp_point_init(&secret);

    ret = mbedtls_ecp_mul(grp, &secret, d, Q, f_rng, p_rng);
    if (ret != 0) {
        lib_printf("[LIBSSH2][ECDH] ecp_mul failed ret=%d\n", ret);
        goto cleanup;
    }

    ret = mbedtls_mpi_copy(z, &secret.MBEDTLS_PRIVATE(X));
    if (ret != 0) {
        lib_printf("[LIBSSH2][ECDH] mpi_copy failed ret=%d\n", ret);
        goto cleanup;
    }

    lib_printf("[LIBSSH2][ECDH] shared secret ready bits=%u bytes=%u\n",
               (unsigned) mbedtls_mpi_bitlen(z),
               (unsigned) mbedtls_mpi_size(z));

cleanup:
    mbedtls_ecp_point_free(&secret);
    return ret;
}

__attribute__((weak))
int mbedtls_cipher_set_padding_mode(mbedtls_cipher_context_t *ctx,
                                    int mode)
{
    (void)ctx;
    (void)mode;
    return 0;
}

int send(int sockfd, const void *buf, size_t len, int flags)
{
    (void)sockfd;
    (void)buf;
    (void)len;
    (void)flags;
    errno = EIO;
    return -1;
}

int recv(int sockfd, void *buf, size_t len, int flags)
{
    (void)sockfd;
    (void)buf;
    (void)len;
    (void)flags;
    errno = EIO;
    return -1;
}

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           struct timeval *timeout)
{
    (void)nfds;
    (void)readfds;
    (void)writefds;
    (void)exceptfds;
    (void)timeout;
    errno = EAGAIN;
    return 0;
}
