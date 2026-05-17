/* Copyright (C) Art <https://github.com/wildart>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms,
 * with or without modification, are permitted provided
 * that the following conditions are met:
 *
 *   Redistributions of source code must retain the above
 *   copyright notice, this list of conditions and the
 *   following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following
 *   disclaimer in the documentation and/or other materials
 *   provided with the distribution.
 *
 *   Neither the name of the copyright holder nor the names
 *   of any other contributors may be used to endorse or
 *   promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "libssh2_priv.h"

#ifdef LIBSSH2_MBEDTLS

#include <stdlib.h>
#include <stdint.h>
#include "bignum_core.h"
#include "mbedtls/oid.h"

extern int lib_printf(const char *fmt, ...);
extern unsigned int get_millisecond_timer(void);
extern unsigned int get_wall_clock_seconds(void);
extern int libssh2_vibe_kex_stage;
extern int libssh2_vibe_kex_ret;
extern int libssh2_vibe_kex_aux0;
extern int libssh2_vibe_kex_aux1;
extern int libssh2_vibe_kex_aux2;

#ifndef SSH_DEBUG_LOG
#define SSH_DEBUG_LOG 0
#endif

static void
vibe_mbed_kex_record(int stage, int ret)
{
    libssh2_vibe_kex_stage = stage;
    libssh2_vibe_kex_ret = ret;
    libssh2_vibe_kex_aux0 = 0;
    libssh2_vibe_kex_aux1 = 0;
    libssh2_vibe_kex_aux2 = 0;
}

#if !SSH_DEBUG_LOG
#define lib_printf(...) do { } while (0)
#endif

#if MBEDTLS_VERSION_NUMBER < 0x03010000
#error "mbedTLS 3.1.0 or upper required"
#endif

/*******************************************************************/
/*
 * mbedTLS backend: Global context handles
 */

static mbedtls_entropy_context  _libssh2_mbedtls_entropy;
static mbedtls_ctr_drbg_context _libssh2_mbedtls_ctr_drbg;
static uint64_t _libssh2_vibe_rng_state;

/*******************************************************************/
/*
 * mbedTLS backend: Generic functions
 */

static uint64_t
vibe_mbedtls_mix64(uint64_t x)
{
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    return x * 2685821657736338717ULL;
}

static int
vibe_mbedtls_rng(void *ctx, unsigned char *buf, size_t len)
{
    uint64_t seed;
    size_t i = 0;

    (void)ctx;

    if(!buf && len)
        return -1;

    seed = ((uint64_t)get_millisecond_timer() << 32) ^
           (uint64_t)get_wall_clock_seconds() ^
           (uint64_t)(uintptr_t)buf ^
           (uint64_t)(uintptr_t)&seed ^
           _libssh2_vibe_rng_state;
    if(seed == 0)
        seed = 0x9e3779b97f4a7c15ULL;
    if(_libssh2_vibe_rng_state == 0)
        _libssh2_vibe_rng_state = seed ^ 0xd1b54a32d192ed03ULL;

    while(i < len) {
        _libssh2_vibe_rng_state =
            vibe_mbedtls_mix64(_libssh2_vibe_rng_state + seed + i);
        for(size_t j = 0; j < sizeof(_libssh2_vibe_rng_state) && i < len;
            ++j, ++i) {
            buf[i] = (unsigned char)(_libssh2_vibe_rng_state >> (8U * j));
        }
    }

    return 0;
}

static int
vibe_mbedtls_mpi_is_zero_simple(const mbedtls_mpi *x);

static int
vibe_mbedtls_mpi_cmp_abs_simple(const mbedtls_mpi *a, const mbedtls_mpi *b);

static int
vibe_mbedtls_ecp_gen_privkey_simple(const mbedtls_ecp_group *grp,
                                    mbedtls_mpi *d)
{
    unsigned char buf[80];
    mbedtls_mpi_uint *limbs_p;
    size_t nbytes;
    size_t nbits;
    size_t excess;
    size_t limb_count;
    size_t limb_bytes;
    int ret;

    if(!grp || !d)
        return -1;

    nbytes = mbedtls_mpi_size(&grp->N);
    nbits = mbedtls_mpi_bitlen(&grp->N);
    if(nbytes == 0 || nbytes > sizeof(buf))
        return -2;

    excess = (nbytes * 8U) - nbits;
    limb_bytes = sizeof(mbedtls_mpi_uint);
    limb_count = (nbytes + limb_bytes - 1U) / limb_bytes;
    if(limb_count == 0 || limb_count > 32)
        return -4;

    if(d->MBEDTLS_PRIVATE(p) == NULL ||
       d->MBEDTLS_PRIVATE(n) < limb_count) {
        if(d->MBEDTLS_PRIVATE(p) != NULL)
            mbedtls_free(d->MBEDTLS_PRIVATE(p));

        vibe_mbed_kex_record(223216, (int)(limb_count * limb_bytes));
        limbs_p = malloc(limb_count * limb_bytes);
        if(!limbs_p) {
            libssh2_vibe_kex_stage = 223217;
            libssh2_vibe_kex_ret = -141;
            libssh2_vibe_kex_aux0 = (int)(limb_count * limb_bytes);
            libssh2_vibe_kex_aux1 = 0;
            libssh2_vibe_kex_aux2 = 0;
            return -141;
        }
        d->MBEDTLS_PRIVATE(p) = limbs_p;
        d->MBEDTLS_PRIVATE(n) = (unsigned short)limb_count;
    }

    for(unsigned int attempt = 0; attempt < 64; ++attempt) {
        vibe_mbed_kex_record(223211, (int)attempt);
        ret = vibe_mbedtls_rng(NULL, buf, nbytes);
        if(ret) {
            vibe_mbed_kex_record(223212, ret);
            return ret;
        }

        if(excess > 0 && excess < 8)
            buf[0] &= (unsigned char)(0xffU >> excess);

        vibe_mbed_kex_record(223213, (int)nbytes);
        memset(d->MBEDTLS_PRIVATE(p), 0, d->MBEDTLS_PRIVATE(n) * limb_bytes);
        d->MBEDTLS_PRIVATE(s) = 1;
        for(size_t i = 0; i < nbytes; ++i) {
            size_t src = nbytes - 1U - i;
            size_t limb = i / limb_bytes;
            size_t shift = (i % limb_bytes) * 8U;
            d->MBEDTLS_PRIVATE(p)[limb] |=
                ((mbedtls_mpi_uint)buf[src]) << shift;
        }

        vibe_mbed_kex_record(223215, (int)d->MBEDTLS_PRIVATE(n));
        if(!vibe_mbedtls_mpi_is_zero_simple(d) &&
           vibe_mbedtls_mpi_cmp_abs_simple(d, &grp->N) < 0)
            return 0;
    }

    return -3;
}

static int
vibe_mbedtls_mpi_is_zero_simple(const mbedtls_mpi *x)
{
    if(!x || !x->MBEDTLS_PRIVATE(p))
        return 1;

    for(unsigned short i = 0; i < x->MBEDTLS_PRIVATE(n); ++i) {
        if(x->MBEDTLS_PRIVATE(p)[i] != 0)
            return 0;
    }

    return 1;
}

static int
vibe_mbedtls_mpi_cmp_abs_simple(const mbedtls_mpi *a, const mbedtls_mpi *b)
{
    unsigned short an = (a && a->MBEDTLS_PRIVATE(p)) ? a->MBEDTLS_PRIVATE(n) : 0;
    unsigned short bn = (b && b->MBEDTLS_PRIVATE(p)) ? b->MBEDTLS_PRIVATE(n) : 0;
    unsigned short n = an > bn ? an : bn;

    while(n > 0) {
        mbedtls_mpi_uint av = (n <= an) ? a->MBEDTLS_PRIVATE(p)[n - 1] : 0;
        mbedtls_mpi_uint bv = (n <= bn) ? b->MBEDTLS_PRIVATE(p)[n - 1] : 0;
        if(av > bv)
            return 1;
        if(av < bv)
            return -1;
        --n;
    }

    return 0;
}

void
_libssh2_mbedtls_init(void)
{
    int ret;

    mbedtls_entropy_init(&_libssh2_mbedtls_entropy);
    mbedtls_ctr_drbg_init(&_libssh2_mbedtls_ctr_drbg);

    ret = mbedtls_ctr_drbg_seed(&_libssh2_mbedtls_ctr_drbg,
                                mbedtls_entropy_func,
                                &_libssh2_mbedtls_entropy, NULL, 0);
    if(ret) {
        vibe_mbed_kex_record(180, ret);
        mbedtls_ctr_drbg_free(&_libssh2_mbedtls_ctr_drbg);
    }
}

void
_libssh2_mbedtls_free(void)
{
    mbedtls_ctr_drbg_free(&_libssh2_mbedtls_ctr_drbg);
    mbedtls_entropy_free(&_libssh2_mbedtls_entropy);
}

int
_libssh2_mbedtls_random(unsigned char *buf, size_t len)
{
    int ret;
    ret = vibe_mbedtls_rng(NULL, buf, len);
    return ret == 0 ? 0 : -1;
}

static void
_libssh2_mbedtls_safe_free(void *buf, size_t len)
{
    if(!buf)
        return;

    if(len > 0)
        _libssh2_explicit_zero(buf, len);

    mbedtls_free(buf);
}

int
_libssh2_mbedtls_cipher_init(_libssh2_cipher_ctx *h,
                             _libssh2_cipher_type(algo),
                             unsigned char *iv,
                             unsigned char *secret,
                             int encrypt)
{
    const mbedtls_cipher_info_t *cipher_info;
    int ret, op;

    if(!h)
        return -1;

    op = encrypt ? MBEDTLS_ENCRYPT : MBEDTLS_DECRYPT;

    cipher_info = mbedtls_cipher_info_from_type(algo);
    if(!cipher_info)
    {
        lib_printf("[LIBSSH2][CIPHER] info missing algo=%d\n", (int)algo);
        return -1;
    }

    mbedtls_cipher_init(h);
    ret = mbedtls_cipher_setup(h, cipher_info);
    lib_printf("[LIBSSH2][CIPHER] setup algo=%d ret=%d ivlen=%u keybits=%u encrypt=%d\n",
               (int)algo, ret,
               (unsigned)mbedtls_cipher_info_get_iv_size(cipher_info),
               (unsigned)mbedtls_cipher_info_get_key_bitlen(cipher_info),
               encrypt);

    /* libssh2 computes and adds SSH packet padding itself, so for CBC
     * tell mbedTLS to expect no padding on the cipher layer. Only call
     * set_padding_mode for CBC ciphers since other modes (CTR, stream)
     * are not applicable and will cause an error. */
    if(!ret) {
        if(algo == MBEDTLS_CIPHER_AES_128_CBC ||
           algo == MBEDTLS_CIPHER_AES_192_CBC ||
           algo == MBEDTLS_CIPHER_AES_256_CBC ||
           algo == MBEDTLS_CIPHER_DES_EDE3_CBC) {
            ret = mbedtls_cipher_set_padding_mode(h, MBEDTLS_PADDING_NONE);
        }
    }

    if(!ret)
    {
        ret = mbedtls_cipher_setkey(h,
                  secret,
                  (int)mbedtls_cipher_info_get_key_bitlen(cipher_info),
                  op);
        lib_printf("[LIBSSH2][CIPHER] setkey ret=%d\n", ret);
    }

    if(!ret)
    {
        ret = mbedtls_cipher_set_iv(h, iv,
                  mbedtls_cipher_info_get_iv_size(cipher_info));
        lib_printf("[LIBSSH2][CIPHER] setiv ret=%d\n", ret);
    }

    if(!ret &&
       (algo == MBEDTLS_CIPHER_AES_128_CTR ||
        algo == MBEDTLS_CIPHER_AES_192_CTR ||
        algo == MBEDTLS_CIPHER_AES_256_CTR))
    {
        ret = mbedtls_cipher_reset(h);
        lib_printf("[LIBSSH2][CIPHER] ctr reset ret=%d\n", ret);
    }

    return ret == 0 ? 0 : -1;
}

int
_libssh2_mbedtls_cipher_crypt(_libssh2_cipher_ctx *ctx,
                              _libssh2_cipher_type(algo),
                              int encrypt,
                              unsigned char *block,
                              size_t blocksize, int firstlast)
{
    int ret;
    unsigned char *output;
    size_t osize, olen, finish_olen;

    (void)encrypt;
    (void)algo;
    (void)firstlast;

    osize = blocksize + mbedtls_cipher_get_block_size(ctx);

    output = (unsigned char *)mbedtls_calloc(osize, sizeof(char));
    if(output) {
        if(algo == MBEDTLS_CIPHER_AES_128_CTR ||
           algo == MBEDTLS_CIPHER_AES_192_CTR ||
           algo == MBEDTLS_CIPHER_AES_256_CTR) {
            ret = mbedtls_cipher_update(ctx, block, blocksize, output, &olen);
            finish_olen = 0;
        }
        else {
            ret = mbedtls_cipher_reset(ctx);

            if(!ret)
                ret = mbedtls_cipher_update(ctx, block, blocksize, output, &olen);

            if(!ret)
                ret = mbedtls_cipher_finish(ctx, output + olen, &finish_olen);
        }

        if(!ret) {
            olen += finish_olen;
            memcpy(block, output, olen);
        }

        _libssh2_mbedtls_safe_free(output, osize);
    }
    else
        ret = -1;

    return ret == 0 ? 0 : -1;
}

void
_libssh2_mbedtls_cipher_dtor(_libssh2_cipher_ctx *ctx)
{
    mbedtls_cipher_free(ctx);
}

int
_libssh2_mbedtls_hash_init(mbedtls_md_context_t *ctx,
                           mbedtls_md_type_t mdtype,
                           const unsigned char *key, size_t keylen)
{
    const mbedtls_md_info_t *md_info;
    int ret, hmac;

    md_info = mbedtls_md_info_from_type(mdtype);
    if(!md_info) {
        lib_printf("[LIBSSH2][HASH] md_info missing type=%d\n", (int)mdtype);
        return 0;
    }

    hmac = key ? 1 : 0;
    lib_printf("[LIBSSH2][HASH] init type=%d name=%s hmac=%d keylen=%u\n",
               (int)mdtype,
               mbedtls_md_get_name(md_info),
               hmac,
               (unsigned)keylen);

    mbedtls_md_init(ctx);
    ret = mbedtls_md_setup(ctx, md_info, 0);
    lib_printf("[LIBSSH2][HASH] setup ret=%d\n", ret);
    if(!ret) {
        if(hmac)
        {
            ret = mbedtls_md_hmac_starts(ctx, key, keylen);
            lib_printf("[LIBSSH2][HASH] hmac_starts ret=%d\n", ret);
        }
        else {
            ret = mbedtls_md_starts(ctx);
            lib_printf("[LIBSSH2][HASH] starts ret=%d\n", ret);
        }
    }

    return ret == 0 ? 1 : 0;
}

int
_libssh2_mbedtls_hash_final(mbedtls_md_context_t *ctx, unsigned char *hash)
{
    int ret;

    ret = mbedtls_md_finish(ctx, hash);
    mbedtls_md_free(ctx);

    return ret == 0 ? 1 : 0;
}

int
_libssh2_mbedtls_hash(const unsigned char *data, size_t datalen,
                      mbedtls_md_type_t mdtype, unsigned char *hash)
{
    const mbedtls_md_info_t *md_info;
    int ret;

    md_info = mbedtls_md_info_from_type(mdtype);
    if(!md_info)
        return 0;

    ret = mbedtls_md(md_info, data, datalen, hash);

    return ret == 0 ? 0 : -1;
}

int _libssh2_hmac_ctx_init(libssh2_hmac_ctx *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    lib_printf("[LIBSSH2][HMAC] ctx init\n");
    return 1;
}

static size_t
_libssh2_mbedtls_hmac_block_size(mbedtls_md_type_t mdtype)
{
    switch(mdtype) {
    case MBEDTLS_MD_SHA384:
    case MBEDTLS_MD_SHA512:
        return 128;
#if defined(MBEDTLS_SHA3_C)
    case MBEDTLS_MD_SHA3_384:
        return 104;
    case MBEDTLS_MD_SHA3_512:
        return 72;
#endif
    default:
        return 64;
    }
}

static int
_libssh2_mbedtls_hmac_prepare_key(libssh2_hmac_ctx *ctx,
                                  mbedtls_md_type_t mdtype,
                                  const unsigned char *key,
                                  size_t keylen)
{
    const mbedtls_md_info_t *md_info;
    unsigned char normalized[MBEDTLS_MD_MAX_SIZE];
    size_t mdlen;

    md_info = mbedtls_md_info_from_type(mdtype);
    if(!md_info) {
        lib_printf("[LIBSSH2][HMAC] md_info missing type=%d\n", (int)mdtype);
        return 0;
    }

    ctx->mdtype = mdtype;
    ctx->blocksize = _libssh2_mbedtls_hmac_block_size(mdtype);
    ctx->keylen = keylen;
    mdlen = mbedtls_md_get_size(md_info);

    if(keylen > ctx->blocksize) {
        int rc = _libssh2_mbedtls_hash(key, keylen, mdtype, normalized);
        if(rc) {
            lib_printf("[LIBSSH2][HMAC] key hash normalize failed type=%d rc=%d\n",
                       (int)mdtype, rc);
            _libssh2_explicit_zero(normalized, sizeof(normalized));
            return 0;
        }
        key = normalized;
        keylen = mdlen;
    }

    if(keylen > sizeof(ctx->key)) {
        lib_printf("[LIBSSH2][HMAC] key too long type=%d keylen=%u block=%u\n",
                   (int)mdtype, (unsigned)keylen, (unsigned)ctx->blocksize);
        _libssh2_explicit_zero(normalized, sizeof(normalized));
        return 0;
    }

    memset(ctx->key, 0, sizeof(ctx->key));
    memcpy(ctx->key, key, keylen);
    _libssh2_explicit_zero(normalized, sizeof(normalized));
    return 1;
}

static int
_libssh2_mbedtls_hmac_inner_start(libssh2_hmac_ctx *ctx)
{
    const mbedtls_md_info_t *md_info;
    unsigned char pad[LIBSSH2_HMAC_MAX_BLOCK_SIZE];
    size_t i;
    int ret;

    md_info = mbedtls_md_info_from_type(ctx->mdtype);
    if(!md_info)
        return 0;

    memset(pad, 0, sizeof(pad));
    for(i = 0; i < ctx->blocksize; ++i)
        pad[i] = (unsigned char)(ctx->key[i] ^ 0x36);

    mbedtls_md_init(&ctx->md_ctx);
    ret = mbedtls_md_setup(&ctx->md_ctx, md_info, 0);
    lib_printf("[LIBSSH2][HMAC] inner setup ret=%d\n", ret);
    if(!ret)
        ret = mbedtls_md_starts(&ctx->md_ctx);
    lib_printf("[LIBSSH2][HMAC] inner starts ret=%d\n", ret);
    if(!ret)
        ret = mbedtls_md_update(&ctx->md_ctx, pad, ctx->blocksize);
    lib_printf("[LIBSSH2][HMAC] inner ipad update ret=%d\n", ret);
    _libssh2_explicit_zero(pad, sizeof(pad));
    return ret == 0 ? 1 : 0;
}

#if LIBSSH2_MD5
int _libssh2_hmac_md5_init(libssh2_hmac_ctx *ctx,
                           void *key, size_t keylen)
{
    return _libssh2_mbedtls_hash_init(ctx, MBEDTLS_MD_MD5, key, keylen);
}
#endif

#if LIBSSH2_HMAC_RIPEMD
int _libssh2_hmac_ripemd160_init(libssh2_hmac_ctx *ctx,
                                 void *key, size_t keylen)
{
    return _libssh2_mbedtls_hash_init(ctx, MBEDTLS_MD_RIPEMD160, key, keylen);
}
#endif

int _libssh2_hmac_sha1_init(libssh2_hmac_ctx *ctx,
                            void *key, size_t keylen)
{
    int ret;

    ret = _libssh2_mbedtls_hmac_prepare_key(ctx, MBEDTLS_MD_SHA1, key, keylen);
    lib_printf("[LIBSSH2][HMAC] sha1 prep keylen=%u ret=%d\n",
               (unsigned)keylen, ret);
    if(!ret)
        return 0;

    ret = _libssh2_mbedtls_hmac_inner_start(ctx);
    lib_printf("[LIBSSH2][HMAC] sha1 inner ret=%d\n", ret);
    return ret;
}

int _libssh2_hmac_sha256_init(libssh2_hmac_ctx *ctx,
                              void *key, size_t keylen)
{
    int ret;

    ret = _libssh2_mbedtls_hmac_prepare_key(ctx, MBEDTLS_MD_SHA256, key, keylen);
    lib_printf("[LIBSSH2][HMAC] sha256 prep keylen=%u ret=%d\n",
               (unsigned)keylen, ret);
    if(!ret)
        return 0;

    ret = _libssh2_mbedtls_hmac_inner_start(ctx);
    lib_printf("[LIBSSH2][HMAC] sha256 inner ret=%d\n", ret);
    return ret;
}

int _libssh2_hmac_sha512_init(libssh2_hmac_ctx *ctx,
                              void *key, size_t keylen)
{
    int ret;

    ret = _libssh2_mbedtls_hmac_prepare_key(ctx, MBEDTLS_MD_SHA512, key, keylen);
    lib_printf("[LIBSSH2][HMAC] sha512 prep keylen=%u ret=%d\n",
               (unsigned)keylen, ret);
    if(!ret)
        return 0;

    ret = _libssh2_mbedtls_hmac_inner_start(ctx);
    lib_printf("[LIBSSH2][HMAC] sha512 inner ret=%d\n", ret);
    return ret;
}

int _libssh2_hmac_update(libssh2_hmac_ctx *ctx,
                         const void *data, size_t datalen)
{
    int ret = mbedtls_md_update(&ctx->md_ctx, data, datalen);
    lib_printf("[LIBSSH2][HMAC] update len=%u ret=%d\n",
               (unsigned)datalen, ret);

    return ret == 0 ? 1 : 0;
}

int _libssh2_hmac_final(libssh2_hmac_ctx *ctx, void *data)
{
    const mbedtls_md_info_t *md_info;
    unsigned char inner[MBEDTLS_MD_MAX_SIZE];
    unsigned char pad[LIBSSH2_HMAC_MAX_BLOCK_SIZE];
    mbedtls_md_context_t outer;
    size_t i, mdlen;
    int ret;

    md_info = mbedtls_md_info_from_type(ctx->mdtype);
    if(!md_info)
        return 0;

    mdlen = mbedtls_md_get_size(md_info);
    if(mdlen == 0 || mdlen > sizeof(inner))
        return 0;

    ret = mbedtls_md_finish(&ctx->md_ctx, inner);
    lib_printf("[LIBSSH2][HMAC] inner finish ret=%d\n", ret);
    if(ret != 0) {
        _libssh2_explicit_zero(inner, sizeof(inner));
        return 0;
    }

    memset(pad, 0, sizeof(pad));
    for(i = 0; i < ctx->blocksize; ++i)
        pad[i] = (unsigned char)(ctx->key[i] ^ 0x5c);

    mbedtls_md_init(&outer);
    ret = mbedtls_md_setup(&outer, md_info, 0);
    lib_printf("[LIBSSH2][HMAC] outer setup ret=%d\n", ret);
    if(!ret)
        ret = mbedtls_md_starts(&outer);
    lib_printf("[LIBSSH2][HMAC] outer starts ret=%d\n", ret);
    if(!ret)
        ret = mbedtls_md_update(&outer, pad, ctx->blocksize);
    lib_printf("[LIBSSH2][HMAC] outer ipad update ret=%d\n", ret);
    if(!ret)
        ret = mbedtls_md_update(&outer, inner, mdlen);
    lib_printf("[LIBSSH2][HMAC] outer inner update ret=%d\n", ret);
    if(!ret)
        ret = mbedtls_md_finish(&outer, data);
    lib_printf("[LIBSSH2][HMAC] outer finish ret=%d\n", ret);
    mbedtls_md_free(&outer);

    _libssh2_explicit_zero(inner, sizeof(inner));
    _libssh2_explicit_zero(pad, sizeof(pad));

    return ret == 0 ? 1 : 0;
}

void _libssh2_hmac_cleanup(libssh2_hmac_ctx *ctx)
{
    mbedtls_md_free(&ctx->md_ctx);
    _libssh2_explicit_zero(ctx, sizeof(*ctx));
}

/*******************************************************************/
/*
 * mbedTLS backend: BigNumber functions
 */

_libssh2_bn *
_libssh2_mbedtls_bignum_init(void)
{
    _libssh2_bn *bignum;

    bignum = (_libssh2_bn *)mbedtls_calloc(1, sizeof(_libssh2_bn));
    if(bignum) {
        mbedtls_mpi_init(bignum);
    }

    return bignum;
}

void
_libssh2_mbedtls_bignum_free(_libssh2_bn *bn)
{
    if(bn) {
        mbedtls_mpi_free(bn);
        mbedtls_free(bn);
    }
}

static int
_libssh2_mbedtls_bignum_random(_libssh2_bn *bn, int bits, int top, int bottom)
{
    size_t len;
    int err;
    size_t i;

    if(!bn || bits <= 0)
        return -1;

    len = (bits + 7) >> 3;
    err = mbedtls_mpi_fill_random(bn, len, vibe_mbedtls_rng, NULL);
    if(err)
        return -1;

    /* Zero unused bits above the most significant bit */
    for(i = len*8 - 1; (size_t)bits <= i; --i) {
        err = mbedtls_mpi_set_bit(bn, i, 0);
        if(err)
            return -1;
    }

    /* If `top` is -1, the most significant bit of the random number can be
       zero.  If top is 0, the most significant bit of the random number is
       set to 1, and if top is 1, the two most significant bits of the number
       will be set to 1, so that the product of two such random numbers will
       always have 2*bits length.
    */
    if(top >= 0) {
        for(i = 0; i <= (size_t)top; ++i) {
            err = mbedtls_mpi_set_bit(bn, bits-i-1, 1);
            if(err)
                return -1;
        }
    }

    /* make odd by setting first bit in least significant byte */
    if(bottom) {
        err = mbedtls_mpi_set_bit(bn, 0, 1);
        if(err)
            return -1;
    }

    return 0;
}

/*******************************************************************/
/*
 * mbedTLS backend: RSA functions
 */

int
_libssh2_mbedtls_rsa_new(libssh2_rsa_ctx **rsa,
                         const unsigned char *edata,
                         unsigned long elen,
                         const unsigned char *ndata,
                         unsigned long nlen,
                         const unsigned char *ddata,
                         unsigned long dlen,
                         const unsigned char *pdata,
                         unsigned long plen,
                         const unsigned char *qdata,
                         unsigned long qlen,
                         const unsigned char *e1data,
                         unsigned long e1len,
                         const unsigned char *e2data,
                         unsigned long e2len,
                         const unsigned char *coeffdata,
                         unsigned long coefflen)
{
    int ret;
    libssh2_rsa_ctx *ctx;

    ctx = (libssh2_rsa_ctx *) mbedtls_calloc(1, sizeof(libssh2_rsa_ctx));
    if(ctx)
        mbedtls_rsa_init(ctx);
    else
        return -1;

    lib_printf("[LIBSSH2][RSA] import pub e_len=%lu n_len=%lu d=%d\n",
               elen, nlen, ddata ? 1 : 0);

    ret = 0;
    if(mbedtls_mpi_read_binary(&(ctx->MBEDTLS_PRIVATE(E)),
                               edata, elen)) {
        lib_printf("[LIBSSH2][RSA] read E failed\n");
        ret = -1;
    }
    if(!ret && mbedtls_mpi_read_binary(&(ctx->MBEDTLS_PRIVATE(N)),
                                       ndata, nlen)) {
        lib_printf("[LIBSSH2][RSA] read N failed\n");
        ret = -1;
    }

    if(!ret) {
        ctx->MBEDTLS_PRIVATE(len) =
            mbedtls_mpi_size(&(ctx->MBEDTLS_PRIVATE(N)));
        lib_printf("[LIBSSH2][RSA] ctx len=%u nbits=%u ebits=%u\n",
                   (unsigned) ctx->MBEDTLS_PRIVATE(len),
                   (unsigned) mbedtls_mpi_bitlen(&(ctx->MBEDTLS_PRIVATE(N))),
                   (unsigned) mbedtls_mpi_bitlen(&(ctx->MBEDTLS_PRIVATE(E))));
        ret = mbedtls_mpi_core_get_mont_r2_unsafe(&(ctx->MBEDTLS_PRIVATE(RN)),
                                                  &(ctx->MBEDTLS_PRIVATE(N)));
        lib_printf("[LIBSSH2][RSA] rn precompute ret=%d\n", ret);
    }

    if(!ret && ddata) {
        if(mbedtls_mpi_read_binary(&(ctx->MBEDTLS_PRIVATE(D)),
                                   ddata, dlen) ||
           mbedtls_mpi_read_binary(&(ctx->MBEDTLS_PRIVATE(P)),
                                   pdata, plen) ||
           mbedtls_mpi_read_binary(&(ctx->MBEDTLS_PRIVATE(Q)),
                                   qdata, qlen) ||
           mbedtls_mpi_read_binary(&(ctx->MBEDTLS_PRIVATE(DP)),
                                   e1data, e1len) ||
           mbedtls_mpi_read_binary(&(ctx->MBEDTLS_PRIVATE(DQ)),
                                   e2data, e2len) ||
           mbedtls_mpi_read_binary(&(ctx->MBEDTLS_PRIVATE(QP)),
                                   coeffdata, coefflen)) {
            ret = -1;
        }
        else {
            ret = mbedtls_rsa_check_privkey(ctx);
            lib_printf("[LIBSSH2][RSA] privkey check ret=%d\n", ret);
        }
    }
    else if(!ret) {
        ret = mbedtls_rsa_check_pubkey(ctx);
        lib_printf("[LIBSSH2][RSA] pubkey check ret=%d\n", ret);
        if(ret != 0) {
            ret = 0;
        }
    }

    if(ret && ctx) {
        _libssh2_mbedtls_rsa_free(ctx);
        ctx = NULL;
    }
    *rsa = ctx;
    return ret;
}

int
_libssh2_mbedtls_rsa_new_private(libssh2_rsa_ctx **rsa,
                                 LIBSSH2_SESSION *session,
                                 const char *filename,
                                 const unsigned char *passphrase)
{
    int ret;
    mbedtls_pk_context pkey;
    mbedtls_rsa_context *pk_rsa;

    *rsa = (libssh2_rsa_ctx *) LIBSSH2_ALLOC(session, sizeof(libssh2_rsa_ctx));
    if(!*rsa)
        return -1;

    mbedtls_rsa_init(*rsa);
    mbedtls_pk_init(&pkey);

    ret = mbedtls_pk_parse_keyfile(&pkey, filename, (const char *)passphrase,
                                   vibe_mbedtls_rng, NULL);
    if(ret || mbedtls_pk_get_type(&pkey) != MBEDTLS_PK_RSA) {
        mbedtls_pk_free(&pkey);
        mbedtls_rsa_free(*rsa);
        LIBSSH2_FREE(session, *rsa);
        *rsa = NULL;
        return -1;
    }

    pk_rsa = mbedtls_pk_rsa(pkey);
    mbedtls_rsa_copy(*rsa, pk_rsa);
#if MBEDTLS_VERSION_NUMBER >= 0x03000000
    mbedtls_rsa_set_padding(*rsa, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_NONE);
#endif
    mbedtls_pk_free(&pkey);

    return 0;
}

int
_libssh2_mbedtls_rsa_new_private_frommemory(libssh2_rsa_ctx **rsa,
                                            LIBSSH2_SESSION *session,
                                            const char *filedata,
                                            size_t filedata_len,
                                            const unsigned char *passphrase)
{
    int ret;
    mbedtls_pk_context pkey;
    mbedtls_rsa_context *pk_rsa;
    void *filedata_nullterm;
    size_t pwd_len;

    *rsa = (libssh2_rsa_ctx *) mbedtls_calloc(1, sizeof(libssh2_rsa_ctx));
    if(!*rsa)
        return -1;

#if MBEDTLS_VERSION_NUMBER >= 0x03000000
    mbedtls_rsa_init(*rsa);
#else
    mbedtls_rsa_init(*rsa, MBEDTLS_RSA_PKCS_V15, 0);
#endif

    /*
    mbedtls checks in "mbedtls/pkparse.c:1184" if "key[keylen - 1] != '\0'"
    private-key from memory will fail if the last byte is not a null byte
    */
    filedata_nullterm = mbedtls_calloc(filedata_len + 1, 1);
    if(!filedata_nullterm) {
        return -1;
    }
    memcpy(filedata_nullterm, filedata, filedata_len);

    mbedtls_pk_init(&pkey);

    pwd_len = passphrase ? strlen((const char *)passphrase) : 0;
    ret = mbedtls_pk_parse_key(&pkey, (unsigned char *)filedata_nullterm,
                               filedata_len + 1,
                               passphrase, pwd_len,
                               vibe_mbedtls_rng, NULL);
    _libssh2_mbedtls_safe_free(filedata_nullterm, filedata_len);

    if(ret || mbedtls_pk_get_type(&pkey) != MBEDTLS_PK_RSA) {
        mbedtls_pk_free(&pkey);
        mbedtls_rsa_free(*rsa);
        LIBSSH2_FREE(session, *rsa);
        *rsa = NULL;
        return -1;
    }

    pk_rsa = mbedtls_pk_rsa(pkey);
    mbedtls_rsa_copy(*rsa, pk_rsa);
#if MBEDTLS_VERSION_NUMBER >= 0x03000000
    mbedtls_rsa_set_padding(*rsa, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_NONE);
#endif
    mbedtls_pk_free(&pkey);

    return 0;
}

int
_libssh2_mbedtls_rsa_sha2_verify(libssh2_rsa_ctx * rsactx,
                                 size_t hash_len,
                                 const unsigned char *sig,
                                 size_t sig_len,
                                 const unsigned char *m,
                                 size_t m_len)
{
    int ret;
    int md_type;
    unsigned char *hash;
    unsigned char *encoded = NULL;
    unsigned char *expected = NULL;
    const char *oid = NULL;
    size_t oid_len = 0;
    size_t digestinfo_len;
    size_t ps_len;
    mbedtls_mpi T;
    unsigned char *rsa_out = NULL;

    lib_printf("[LIBSSH2][RSA] verify enter hash_len=%u sig_len=%u key_len=%u\n",
               (unsigned)hash_len,
               (unsigned)sig_len,
               (unsigned)mbedtls_rsa_get_len(rsactx));
    if(sig_len < mbedtls_rsa_get_len(rsactx))
        return -1;

    hash = malloc(hash_len);
    if(!hash)
        return -1;

    if(hash_len == SHA_DIGEST_LENGTH) {
        md_type = MBEDTLS_MD_SHA1;
    }
    else if(hash_len == SHA256_DIGEST_LENGTH) {
        md_type = MBEDTLS_MD_SHA256;
    }
    else if(hash_len == SHA512_DIGEST_LENGTH) {
        md_type = MBEDTLS_MD_SHA512;
    }
    else{
        free(hash);
        return -1; /* unsupported digest */
    }
    ret = _libssh2_mbedtls_hash(m, m_len, md_type, hash);

    if(ret) {
        free(hash);
        return -1; /* failure */
    }

    if(hash_len == SHA_DIGEST_LENGTH) {
        oid = MBEDTLS_OID_DIGEST_ALG_SHA1;
    }
    else if(hash_len == SHA256_DIGEST_LENGTH) {
        oid = MBEDTLS_OID_DIGEST_ALG_SHA256;
    }
    else if(hash_len == SHA512_DIGEST_LENGTH) {
        oid = MBEDTLS_OID_DIGEST_ALG_SHA512;
    }
    else {
        free(hash);
        return -1;
    }

    oid_len = sizeof(MBEDTLS_OID_DIGEST_ALG_SHA1) - 1;
    if(hash_len == SHA256_DIGEST_LENGTH) {
        oid_len = sizeof(MBEDTLS_OID_DIGEST_ALG_SHA256) - 1;
    }
    else if(hash_len == SHA512_DIGEST_LENGTH) {
        oid_len = sizeof(MBEDTLS_OID_DIGEST_ALG_SHA512) - 1;
    }

    digestinfo_len = 10 + oid_len + hash_len;
    if(sig_len != mbedtls_rsa_get_len(rsactx) || sig_len < digestinfo_len + 11) {
        free(hash);
        return -1;
    }

    encoded = malloc(sig_len);
    expected = malloc(sig_len);
    rsa_out = malloc(sig_len);
    if(!encoded || !expected || !rsa_out) {
        free(hash);
        free(encoded);
        free(expected);
        free(rsa_out);
        return -1;
    }

    mbedtls_mpi_init(&T);
    ret = mbedtls_mpi_read_binary(&T, sig, sig_len);
    if(ret) {
        lib_printf("[LIBSSH2][RSA] read sig mpi ret=%d\n", ret);
        free(hash);
        free(encoded);
        free(expected);
        free(rsa_out);
        mbedtls_mpi_free(&T);
        return -1;
    }
    if(mbedtls_mpi_cmp_mpi(&T, &rsactx->MBEDTLS_PRIVATE(N)) >= 0) {
        lib_printf("[LIBSSH2][RSA] sig mpi >= N\n");
        free(hash);
        free(encoded);
        free(expected);
        free(rsa_out);
        mbedtls_mpi_free(&T);
        return -1;
    }
    ret = mbedtls_mpi_exp_mod_unsafe(&T, &T, &rsactx->MBEDTLS_PRIVATE(E),
                                     &rsactx->MBEDTLS_PRIVATE(N),
                                     &rsactx->MBEDTLS_PRIVATE(RN));
    if(ret) {
        lib_printf("[LIBSSH2][RSA] exp_mod sig ret=%d\n", ret);
        free(hash);
        free(encoded);
        free(expected);
        free(rsa_out);
        mbedtls_mpi_free(&T);
        return -1;
    }
    ret = mbedtls_mpi_write_binary(&T, rsa_out, sig_len);
    if(ret) {
        lib_printf("[LIBSSH2][RSA] write sig ret=%d\n", ret);
        free(hash);
        free(encoded);
        free(expected);
        free(rsa_out);
        mbedtls_mpi_free(&T);
        return -1;
    }

    memset(expected, 0xFF, sig_len);
    expected[0] = 0x00;
    expected[1] = 0x01;
    ps_len = sig_len - digestinfo_len - 3;
    if(ps_len < 8) {
        free(hash);
        free(encoded);
        free(expected);
        return -1;
    }
    memset(expected + 2, 0xFF, ps_len);
    expected[2 + ps_len] = 0x00;
    expected[3 + ps_len] = 0x30;
    expected[4 + ps_len] = (unsigned char)(8 + oid_len + hash_len);
    expected[5 + ps_len] = 0x30;
    expected[6 + ps_len] = (unsigned char)(4 + oid_len);
    expected[7 + ps_len] = 0x06;
    expected[8 + ps_len] = (unsigned char)oid_len;
    memcpy(expected + 9 + ps_len, oid, oid_len);
    expected[9 + ps_len + oid_len] = 0x05;
    expected[10 + ps_len + oid_len] = 0x00;
    expected[11 + ps_len + oid_len] = 0x04;
    expected[12 + ps_len + oid_len] = (unsigned char)hash_len;
    memcpy(expected + 13 + ps_len + oid_len, hash, hash_len);

    if(mbedtls_ct_memcmp(rsa_out, expected, sig_len) != 0) {
        ret = PSA_ERROR_INVALID_SIGNATURE;
    }
    else {
        ret = 0;
    }

    if(ret) {
        lib_printf("[LIBSSH2][RSA] verify ret=%d\n", ret);
    }
    else {
        lib_printf("[LIBSSH2][RSA] verify ok\n");
    }
    free(hash);
    free(encoded);
    free(expected);
    free(rsa_out);
    mbedtls_mpi_free(&T);

    return (ret == 0) ? 0 : -1;
}

int
_libssh2_mbedtls_rsa_sha1_verify(libssh2_rsa_ctx * rsactx,
                                 const unsigned char *sig,
                                 size_t sig_len,
                                 const unsigned char *m,
                                 size_t m_len)
{
    return _libssh2_mbedtls_rsa_sha2_verify(rsactx, SHA_DIGEST_LENGTH,
                                            sig, sig_len, m, m_len);
}

int
_libssh2_mbedtls_rsa_sha2_sign(LIBSSH2_SESSION *session,
                               libssh2_rsa_ctx *rsactx,
                               const unsigned char *hash,
                               size_t hash_len,
                               unsigned char **signature,
                               size_t *signature_len)
{
    int ret;
    unsigned char *sig;
    size_t sig_len;
    int md_type;

    sig_len = mbedtls_rsa_get_len(rsactx);
    sig = LIBSSH2_ALLOC(session, sig_len);
    if(!sig) {
        return -1;
    }
    ret = 0;
    if(hash_len == SHA_DIGEST_LENGTH) {
        md_type = MBEDTLS_MD_SHA1;
    }
    else if(hash_len == SHA256_DIGEST_LENGTH) {
        md_type = MBEDTLS_MD_SHA256;
    }
    else if(hash_len == SHA512_DIGEST_LENGTH) {
        md_type = MBEDTLS_MD_SHA512;
    }
    else {
        _libssh2_error(session, LIBSSH2_ERROR_PROTO,
                       "Unsupported hash digest length");
        ret = -1;
    }
    if(ret == 0)
        ret = mbedtls_rsa_pkcs1_sign(rsactx,
                                     vibe_mbedtls_rng, NULL,
                                     md_type, (unsigned int)hash_len,
                                     hash, sig);
    if(ret) {
        LIBSSH2_FREE(session, sig);
        return -1;
    }

    *signature = sig;
    *signature_len = sig_len;

    return (ret == 0) ? 0 : -1;
}

int
_libssh2_mbedtls_rsa_sha1_sign(LIBSSH2_SESSION * session,
                               libssh2_rsa_ctx * rsactx,
                               const unsigned char *hash,
                               size_t hash_len,
                               unsigned char **signature,
                               size_t *signature_len)
{
    return _libssh2_mbedtls_rsa_sha2_sign(session, rsactx, hash, hash_len,
                                          signature, signature_len);
}

void
_libssh2_mbedtls_rsa_free(libssh2_rsa_ctx *ctx)
{
    mbedtls_rsa_free(ctx);
    mbedtls_free(ctx);
}

static unsigned char *
gen_publickey_from_rsa(LIBSSH2_SESSION *session,
                       mbedtls_rsa_context *rsa,
                       size_t *keylen)
{
    uint32_t e_bytes, n_bytes;
    uint32_t len;
    unsigned char *key;
    unsigned char *p;

    e_bytes = (uint32_t)mbedtls_mpi_size(&rsa->MBEDTLS_PRIVATE(E));
    n_bytes = (uint32_t)mbedtls_mpi_size(&rsa->MBEDTLS_PRIVATE(N)) + 1;

    /* Key form is "ssh-rsa" + e + n. */
    len = 4 + 7 + 4 + e_bytes + 4 + n_bytes;

    key = LIBSSH2_ALLOC(session, len);
    if(!key) {
        return NULL;
    }

    /* Process key encoding. */
    p = key;

    _libssh2_htonu32(p, 7);  /* Key type. */
    p += 4;
    /* NOLINTNEXTLINE(bugprone-not-null-terminated-result) */
    memcpy(p, "ssh-rsa", 7);
    p += 7;

    _libssh2_htonu32(p, e_bytes);
    p += 4;
    mbedtls_mpi_write_binary(&rsa->MBEDTLS_PRIVATE(E), p, e_bytes);
    p += e_bytes;   /* Increment write index after writing to buffer */

    _libssh2_htonu32(p, n_bytes);
    p += 4;
    mbedtls_mpi_write_binary(&rsa->MBEDTLS_PRIVATE(N), p, n_bytes);
    p += n_bytes;   /* Increment write index after writing to buffer */

    *keylen = (size_t)(p - key);
    return key;
}

static int
_libssh2_mbedtls_pub_priv_key(LIBSSH2_SESSION *session,
                              unsigned char **method,
                              size_t *method_len,
                              unsigned char **pubkeydata,
                              size_t *pubkeydata_len,
                              mbedtls_pk_context *pkey)
{
    unsigned char *key = NULL, *mth = NULL;
    size_t keylen = 0, mthlen = 0;
    int ret;
    mbedtls_rsa_context *rsa;

    if(mbedtls_pk_get_type(pkey) != MBEDTLS_PK_RSA) {
        mbedtls_pk_free(pkey);
        return _libssh2_error(session, LIBSSH2_ERROR_FILE,
                              "Key type not supported");
    }

    ret = 0;

    /* write method */
    mthlen = 7;
    mth = LIBSSH2_ALLOC(session, mthlen);
    if(mth) {
        memcpy(mth, "ssh-rsa", mthlen);
    }
    else {
        ret = -1;
    }

    rsa = mbedtls_pk_rsa(*pkey);
    key = gen_publickey_from_rsa(session, rsa, &keylen);
    if(!key) {
        ret = -1;
    }

    /* write output */
    if(ret) {
        if(mth)
            LIBSSH2_FREE(session, mth);
        if(key)
            LIBSSH2_FREE(session, key);
    }
    else {
        *method = mth;
        *method_len = mthlen;
        *pubkeydata = key;
        *pubkeydata_len = keylen;
    }

    return ret;
}

int
_libssh2_mbedtls_pub_priv_keyfile(LIBSSH2_SESSION *session,
                                  unsigned char **method,
                                  size_t *method_len,
                                  unsigned char **pubkeydata,
                                  size_t *pubkeydata_len,
                                  const char *privatekey,
                                  const char *passphrase)
{
    mbedtls_pk_context pkey;
    char buf[1024];
    int ret;

    mbedtls_pk_init(&pkey);
    ret = mbedtls_pk_parse_keyfile(&pkey, privatekey, passphrase,
                                   vibe_mbedtls_rng, NULL);
    if(ret) {
        mbedtls_strerror(ret, (char *)buf, sizeof(buf));
        mbedtls_pk_free(&pkey);
        return _libssh2_error_flags(session, LIBSSH2_ERROR_FILE, buf,
                                    LIBSSH2_ERR_FLAG_DUP);
    }

    ret = _libssh2_mbedtls_pub_priv_key(session, method, method_len,
                                        pubkeydata, pubkeydata_len, &pkey);

    mbedtls_pk_free(&pkey);

    return ret;
}

int
_libssh2_mbedtls_pub_priv_keyfilememory(LIBSSH2_SESSION *session,
                                        unsigned char **method,
                                        size_t *method_len,
                                        unsigned char **pubkeydata,
                                        size_t *pubkeydata_len,
                                        const char *privatekeydata,
                                        size_t privatekeydata_len,
                                        const char *passphrase)
{
    mbedtls_pk_context pkey;
    char buf[1024];
    int ret;
    void *privatekeydata_nullterm;
    size_t pwd_len;

    /*
    mbedtls checks in "mbedtls/pkparse.c:1184" if "key[keylen - 1] != '\0'"
    private-key from memory will fail if the last byte is not a null byte
    */
    privatekeydata_nullterm = mbedtls_calloc(privatekeydata_len + 1, 1);
    if(!privatekeydata_nullterm) {
        return -1;
    }
    memcpy(privatekeydata_nullterm, privatekeydata, privatekeydata_len);

    mbedtls_pk_init(&pkey);

    pwd_len = passphrase ? strlen((const char *)passphrase) : 0;
    ret = mbedtls_pk_parse_key(&pkey,
                               (unsigned char *)privatekeydata_nullterm,
                               privatekeydata_len + 1,
                               (const unsigned char *)passphrase, pwd_len,
                               vibe_mbedtls_rng, NULL);
    _libssh2_mbedtls_safe_free(privatekeydata_nullterm, privatekeydata_len);

    if(ret) {
        mbedtls_strerror(ret, (char *)buf, sizeof(buf));
        mbedtls_pk_free(&pkey);
        return _libssh2_error_flags(session, LIBSSH2_ERROR_FILE, buf,
                                    LIBSSH2_ERR_FLAG_DUP);
    }

    ret = _libssh2_mbedtls_pub_priv_key(session, method, method_len,
                                        pubkeydata, pubkeydata_len, &pkey);

    mbedtls_pk_free(&pkey);

    return ret;
}

int
_libssh2_mbedtls_sk_pub_keyfilememory(LIBSSH2_SESSION *session,
                                      unsigned char **method,
                                      size_t *method_len,
                                      unsigned char **pubkeydata,
                                      size_t *pubkeydata_len,
                                      int *algorithm,
                                      unsigned char *flags,
                                      const char **application,
                                      const unsigned char **key_handle,
                                      size_t *handle_len,
                                      const char *privatekeydata,
                                      size_t privatekeydata_len,
                                      const char *passphrase)
{
    (void)method;
    (void)method_len;
    (void)pubkeydata;
    (void)pubkeydata_len;
    (void)algorithm;
    (void)flags;
    (void)application;
    (void)key_handle;
    (void)handle_len;
    (void)privatekeydata;
    (void)privatekeydata_len;
    (void)passphrase;

    return _libssh2_error(session, LIBSSH2_ERROR_FILE,
                    "Unable to extract public SK key from private key file: "
                    "Method unimplemented in mbedTLS backend");
}

void _libssh2_init_aes_ctr(void)
{
    /* no implementation */
}

/*******************************************************************/
/*
 * mbedTLS backend: Diffie-Hellman functions
 */

void
_libssh2_dh_init(_libssh2_dh_ctx *dhctx)
{
    *dhctx = _libssh2_mbedtls_bignum_init();    /* Random from client */
}

int
_libssh2_dh_key_pair(_libssh2_dh_ctx *dhctx, _libssh2_bn *public,
                     _libssh2_bn *g, _libssh2_bn *p, int group_order)
{
    unsigned char pubbuf[260];
    size_t pubbytes;
    int tries;
    int public_cmp_one;
    int public_cmp_p;

    lib_printf("[LIBSSH2][DH] enter group_order=%d gbits=%u pbits=%u psize=%u p0=%02x\n",
               group_order,
               (unsigned) mbedtls_mpi_bitlen(g),
               (unsigned) mbedtls_mpi_bitlen(p),
               (unsigned) mbedtls_mpi_size(p),
               (unsigned) (p->MBEDTLS_PRIVATE(p) != NULL ?
                           (p->MBEDTLS_PRIVATE(p)[0] & 0xFF) : 0));

    /* Generate x and e */
    for(tries = 0; tries < 8; tries++) {
        if(_libssh2_mbedtls_bignum_random(*dhctx, group_order * 8 - 1, 0, -1))
        {
            lib_printf("[LIBSSH2][DH] random failed try=%d\n", tries);
            return -1;
        }

        {
            int ret = mbedtls_mpi_exp_mod_unsafe(public, g, *dhctx, p, NULL);
            if(ret) {
                char errbuf[128];
            mbedtls_strerror(ret, errbuf, sizeof(errbuf));
            lib_printf("[LIBSSH2][DH] exp_mod_unsafe failed try=%d ret=%d err=%s\n",
                       tries, ret, errbuf);
            return -1;
            }
        }

        pubbytes = mbedtls_mpi_size(public);
        if(pubbytes > sizeof(pubbuf)) {
            pubbytes = sizeof(pubbuf);
        }
        if(pubbytes > 0) {
            memset(pubbuf, 0, sizeof(pubbuf));
            if(mbedtls_mpi_write_binary(public, pubbuf, pubbytes) == 0) {
                lib_printf("[LIBSSH2][DH] try=%d bits=%u bytes=%u first=%02x%02x%02x%02x\n",
                        tries,
                        (unsigned) mbedtls_mpi_bitlen(public),
                        (unsigned) pubbytes,
                        (unsigned) pubbuf[0],
                        (unsigned) (pubbytes > 1 ? pubbuf[1] : 0),
                        (unsigned) (pubbytes > 2 ? pubbuf[2] : 0),
                        (unsigned) (pubbytes > 3 ? pubbuf[3] : 0));
            }
        }

        /* Reject degenerate public values which some SSH servers refuse. */
        public_cmp_one = mbedtls_mpi_cmp_int(public, 1);
        public_cmp_p = mbedtls_mpi_cmp_mpi(public, p);
        if(public_cmp_one > 0 && public_cmp_p < 0) {
            return 0;
        }

        lib_printf("[LIBSSH2][DH] reject try=%d cmp_one=%d cmp_p=%d\n",
                tries, public_cmp_one, public_cmp_p);
    }

    lib_printf("[LIBSSH2][DH] failed after retries\n");
    return -1;
}

int
_libssh2_dh_secret(_libssh2_dh_ctx *dhctx, _libssh2_bn *secret,
                   _libssh2_bn *f, _libssh2_bn *p)
{
    /* Compute the shared secret */
    {
        int ret = mbedtls_mpi_exp_mod_unsafe(secret, f, *dhctx, p, NULL);
        if(ret) {
            char errbuf[128];
            mbedtls_strerror(ret, errbuf, sizeof(errbuf));
            lib_printf("[LIBSSH2][DH] secret exp_mod_unsafe failed ret=%d err=%s\n",
                       ret, errbuf);
        }
    }
    return 0;
}

void
_libssh2_dh_dtor(_libssh2_dh_ctx *dhctx)
{
    _libssh2_mbedtls_bignum_free(*dhctx);
    *dhctx = NULL;
}

#if LIBSSH2_ECDSA

/*******************************************************************/
/*
 * mbedTLS backend: ECDSA functions
 */

/*
 * _libssh2_ecdsa_create_key
 *
 * Creates a local private key based on input curve
 * and returns octal value and octal length
 *
 */
int
_libssh2_mbedtls_ecdsa_create_key(LIBSSH2_SESSION *session,
                                  _libssh2_ec_key **out_private_key,
                                  unsigned char **out_public_key_octal,
                                  size_t *out_public_key_octal_len,
                                  libssh2_curve_type curve_type)
{
    size_t plen = 0;
    int ret;

    if(out_private_key)
        *out_private_key = NULL;
    if(out_public_key_octal)
        *out_public_key_octal = NULL;
    if(out_public_key_octal_len)
        *out_public_key_octal_len = 0;

    *out_private_key = LIBSSH2_ALLOC(session, sizeof(mbedtls_ecp_keypair));

    if(!*out_private_key) {
        vibe_mbed_kex_record(2231, -1);
        goto failed;
    }

    mbedtls_ecdsa_init(*out_private_key);

    vibe_mbed_kex_record(22310, (int)curve_type);
    ret = mbedtls_ecp_group_load(&(*out_private_key)->MBEDTLS_PRIVATE(grp),
                                 (mbedtls_ecp_group_id)curve_type);
    if(ret) {
        vibe_mbed_kex_record(22311, ret);
        goto failed;
    }

    vibe_mbed_kex_record(22320, 0);
    ret = vibe_mbedtls_ecp_gen_privkey_simple(
            &(*out_private_key)->MBEDTLS_PRIVATE(grp),
            &(*out_private_key)->MBEDTLS_PRIVATE(d));
    if(ret) {
        if(!((libssh2_vibe_kex_stage >= 223211 &&
              libssh2_vibe_kex_stage <= 223999) ||
             (libssh2_vibe_kex_stage >= 2290 &&
              libssh2_vibe_kex_stage <= 2299))) {
            vibe_mbed_kex_record(22321, ret);
        }
        else {
            libssh2_vibe_kex_ret = ret;
        }
        goto failed;
    }

    vibe_mbed_kex_record(22330, 0);
    libssh2_vibe_kex_aux0 =
        (int)(*out_private_key)->MBEDTLS_PRIVATE(grp).nbits;
    libssh2_vibe_kex_aux1 =
        (int)(*out_private_key)->MBEDTLS_PRIVATE(d).MBEDTLS_PRIVATE(n);
    libssh2_vibe_kex_aux2 =
        (int)(*out_private_key)->MBEDTLS_PRIVATE(Q).MBEDTLS_PRIVATE(X).
            MBEDTLS_PRIVATE(n);
    ret = mbedtls_ecp_mul(&(*out_private_key)->MBEDTLS_PRIVATE(grp),
                          &(*out_private_key)->MBEDTLS_PRIVATE(Q),
                          &(*out_private_key)->MBEDTLS_PRIVATE(d),
                          &(*out_private_key)->MBEDTLS_PRIVATE(grp).G,
                          vibe_mbedtls_rng, NULL);
    if(ret) {
        int fail_stage = libssh2_vibe_kex_stage;
        int fail_aux0 = libssh2_vibe_kex_aux0;
        int fail_aux1 = libssh2_vibe_kex_aux1;
        int fail_aux2 = libssh2_vibe_kex_aux2;
        libssh2_vibe_kex_stage = 22331;
        libssh2_vibe_kex_ret = ret;
        if(fail_stage == 2291) {
            libssh2_vibe_kex_aux0 = fail_aux0;
            libssh2_vibe_kex_aux1 = fail_aux1;
            libssh2_vibe_kex_aux2 = fail_aux2;
        }
        else {
            libssh2_vibe_kex_aux0 =
                (int)(*out_private_key)->MBEDTLS_PRIVATE(grp).nbits;
            libssh2_vibe_kex_aux1 =
                (int)(*out_private_key)->MBEDTLS_PRIVATE(d).
                    MBEDTLS_PRIVATE(n);
            libssh2_vibe_kex_aux2 =
                (int)(*out_private_key)->MBEDTLS_PRIVATE(Q).
                    MBEDTLS_PRIVATE(X).MBEDTLS_PRIVATE(n);
        }
        goto failed;
    }

    plen = 2 * mbedtls_mpi_size(
        &(*out_private_key)->MBEDTLS_PRIVATE(grp).P) + 1;
    *out_public_key_octal = LIBSSH2_ALLOC(session, plen);

    if(!*out_public_key_octal) {
        vibe_mbed_kex_record(2233, -1);
        goto failed;
    }

    ret = mbedtls_ecp_point_write_binary(
          &(*out_private_key)->MBEDTLS_PRIVATE(grp),
          &(*out_private_key)->MBEDTLS_PRIVATE(Q),
          MBEDTLS_ECP_PF_UNCOMPRESSED,
          out_public_key_octal_len, *out_public_key_octal, plen);
    if(ret == 0) {
        vibe_mbed_kex_record(2230, 0);
        return 0;
    }

    vibe_mbed_kex_record(2234, ret);

failed:

    _libssh2_mbedtls_ecdsa_free(*out_private_key);
    _libssh2_mbedtls_safe_free(*out_public_key_octal, plen);
    *out_private_key = NULL;

    return -1;
}

/* _libssh2_ecdsa_curve_name_with_octal_new
 *
 * Creates a new public key given an octal string, length and type
 *
 */
int
_libssh2_mbedtls_ecdsa_curve_name_with_octal_new(libssh2_ecdsa_ctx **ec_ctx,
                                                 const unsigned char *k,
                                                 size_t k_len,
                                                 libssh2_curve_type curve)
{
    *ec_ctx = mbedtls_calloc(1, sizeof(mbedtls_ecp_keypair));

    if(!*ec_ctx)
        goto failed;

    mbedtls_ecdsa_init(*ec_ctx);

    if(mbedtls_ecp_group_load(&(*ec_ctx)->MBEDTLS_PRIVATE(grp),
                              (mbedtls_ecp_group_id)curve))
        goto failed;

    if(mbedtls_ecp_point_read_binary(&(*ec_ctx)->MBEDTLS_PRIVATE(grp),
                                     &(*ec_ctx)->MBEDTLS_PRIVATE(Q),
                                     k, k_len))
        goto failed;

    if(mbedtls_ecp_check_pubkey(&(*ec_ctx)->MBEDTLS_PRIVATE(grp),
                                &(*ec_ctx)->MBEDTLS_PRIVATE(Q)) == 0)
        return 0;

failed:

    _libssh2_mbedtls_ecdsa_free(*ec_ctx);
    *ec_ctx = NULL;

    return -1;
}

/* _libssh2_ecdh_gen_k
 *
 * Computes the shared secret K given a local private key,
 * remote public key and length
 */
int
_libssh2_mbedtls_ecdh_gen_k(_libssh2_bn **k,
                            _libssh2_ec_key *private_key,
                            const unsigned char *server_public_key,
                            size_t server_public_key_len)
{
    mbedtls_ecp_point pubkey;
    int rc = 0;
    int ret;

    if(!*k)
        return -1;

    mbedtls_ecp_point_init(&pubkey);

    if(mbedtls_ecp_point_read_binary(&private_key->MBEDTLS_PRIVATE(grp),
                                     &pubkey,
                                     server_public_key,
                                     server_public_key_len)) {
        lib_printf("[LIBSSH2][ECDH] read server pubkey failed len=%u\n",
                   (unsigned) server_public_key_len);
        rc = -1;
        goto cleanup;
    }
    lib_printf("[LIBSSH2][ECDH] read server pubkey ok len=%u\n",
               (unsigned) server_public_key_len);

    rc = mbedtls_ecp_check_pubkey(&private_key->MBEDTLS_PRIVATE(grp), &pubkey);
    lib_printf("[LIBSSH2][ECDH] check pubkey rc=%d\n", rc);
    if(rc != 0) {
        goto cleanup;
    }

    ret = mbedtls_ecdh_compute_shared(&private_key->MBEDTLS_PRIVATE(grp), *k,
                                      &pubkey,
                                      &private_key->MBEDTLS_PRIVATE(d),
                                      vibe_mbedtls_rng, NULL);
    if(ret) {
        char errbuf[128];
        mbedtls_strerror(ret, errbuf, sizeof(errbuf));
        lib_printf("[LIBSSH2][ECDH] compute_shared failed ret=%d err=%s\n",
                   ret, errbuf);
        rc = -1;
        goto cleanup;
    }

    lib_printf("[LIBSSH2][ECDH] compute_shared ok bits=%u bytes=%u\n",
               (unsigned) mbedtls_mpi_bitlen(*k),
               (unsigned) mbedtls_mpi_size(*k));

cleanup:

    mbedtls_ecp_point_free(&pubkey);

    return rc;
}

#define LIBSSH2_MBEDTLS_ECDSA_VERIFY(digest_type)                             \
    do {                                                                      \
        unsigned char hsh[SHA##digest_type##_DIGEST_LENGTH];                  \
                                                                              \
        if(libssh2_sha##digest_type(m, m_len, hsh) == 0) {                    \
            rc = mbedtls_ecdsa_verify(&ec_ctx->MBEDTLS_PRIVATE(grp), hsh,     \
                                      SHA##digest_type##_DIGEST_LENGTH,       \
                                      &ec_ctx->MBEDTLS_PRIVATE(Q), &pr, &ps); \
        }                                                                     \
    } while(0)

/* _libssh2_ecdsa_verify
 *
 * Verifies the ECDSA signature of a hashed message
 *
 */
int
_libssh2_mbedtls_ecdsa_verify(libssh2_ecdsa_ctx *ec_ctx,
                              const unsigned char *r, size_t r_len,
                              const unsigned char *s, size_t s_len,
                              const unsigned char *m, size_t m_len)
{
    mbedtls_mpi pr, ps;
    int rc = -1;
    int ret = -1;
    char errbuf[128];

    mbedtls_mpi_init(&pr);
    mbedtls_mpi_init(&ps);

    lib_printf("[LIBSSH2][ECDSA] verify enter r_len=%u s_len=%u hash_len=%u curve=%d\n",
               (unsigned)r_len, (unsigned)s_len, (unsigned)m_len,
               (int)_libssh2_ecdsa_get_curve_type(ec_ctx));

    if(mbedtls_mpi_read_binary(&pr, r, r_len))
    {
        lib_printf("[LIBSSH2][ECDSA] read r failed\n");
        goto cleanup;
    }

    if(mbedtls_mpi_read_binary(&ps, s, s_len))
    {
        lib_printf("[LIBSSH2][ECDSA] read s failed\n");
        goto cleanup;
    }

    lib_printf("[LIBSSH2][ECDSA] r bits=%u s bits=%u\n",
               (unsigned)mbedtls_mpi_bitlen(&pr),
               (unsigned)mbedtls_mpi_bitlen(&ps));

    switch(_libssh2_ecdsa_get_curve_type(ec_ctx)) {
    case LIBSSH2_EC_CURVE_NISTP256:
        LIBSSH2_MBEDTLS_ECDSA_VERIFY(256);
        break;
    case LIBSSH2_EC_CURVE_NISTP384:
        LIBSSH2_MBEDTLS_ECDSA_VERIFY(384);
        break;
    case LIBSSH2_EC_CURVE_NISTP521:
        LIBSSH2_MBEDTLS_ECDSA_VERIFY(512);
        break;
    default:
        rc = -1;
    }

    if(rc != 0) {
        mbedtls_strerror(rc, errbuf, sizeof(errbuf));
        lib_printf("[LIBSSH2][ECDSA] verify ret=%d err=%s\n", rc, errbuf);
    }
    else {
        lib_printf("[LIBSSH2][ECDSA] verify ok\n");
    }

cleanup:

    mbedtls_mpi_free(&pr);
    mbedtls_mpi_free(&ps);

    return (rc == 0) ? 0 : -1;
}

static int
_libssh2_mbedtls_parse_eckey(libssh2_ecdsa_ctx **ctx,
                             mbedtls_pk_context *pkey,
                             LIBSSH2_SESSION *session,
                             const unsigned char *data,
                             size_t data_len,
                             const unsigned char *pwd)
{
    size_t pwd_len;

    pwd_len = pwd ? strlen((const char *) pwd) : 0;

    if(mbedtls_pk_parse_key(pkey, data, data_len, pwd, pwd_len,
                            vibe_mbedtls_rng, NULL))

        goto failed;

    if(mbedtls_pk_get_type(pkey) != MBEDTLS_PK_ECKEY)
        goto failed;

    *ctx = LIBSSH2_ALLOC(session, sizeof(libssh2_ecdsa_ctx));

    if(!*ctx)
        goto failed;

    mbedtls_ecdsa_init(*ctx);

    if(mbedtls_ecdsa_from_keypair(*ctx, mbedtls_pk_ec(*pkey)) == 0)
        return 0;

failed:

    _libssh2_mbedtls_ecdsa_free(*ctx);
    *ctx = NULL;

    return -1;
}

static int
_libssh2_mbedtls_parse_openssh_key(libssh2_ecdsa_ctx **ctx,
                                   LIBSSH2_SESSION *session,
                                   const unsigned char *data,
                                   size_t data_len,
                                   const unsigned char *pwd)
{
    libssh2_curve_type type;
    unsigned char *name = NULL;
    struct string_buf *decrypted = NULL;
    size_t curvelen, exponentlen, pointlen;
    unsigned char *curve, *exponent, *point_buf;

    if(_libssh2_openssh_pem_parse_memory(session, pwd,
                                         (const char *)data, data_len,
                                         &decrypted))
        goto failed;

    if(_libssh2_get_string(decrypted, &name, NULL))
        goto failed;

    if(_libssh2_mbedtls_ecdsa_curve_type_from_name((const char *)name,
                                                   &type))
        goto failed;

    if(_libssh2_get_string(decrypted, &curve, &curvelen))
        goto failed;

    if(_libssh2_get_string(decrypted, &point_buf, &pointlen))
        goto failed;

    if(_libssh2_get_bignum_bytes(decrypted, &exponent, &exponentlen))
        goto failed;

    *ctx = LIBSSH2_ALLOC(session, sizeof(libssh2_ecdsa_ctx));

    if(!*ctx)
        goto failed;

    mbedtls_ecdsa_init(*ctx);

    if(mbedtls_ecp_group_load(&(*ctx)->MBEDTLS_PRIVATE(grp),
                              (mbedtls_ecp_group_id)type))
        goto failed;

    if(mbedtls_mpi_read_binary(&(*ctx)->MBEDTLS_PRIVATE(d),
                               exponent, exponentlen))
        goto failed;

    if(mbedtls_ecp_mul(&(*ctx)->MBEDTLS_PRIVATE(grp),
                       &(*ctx)->MBEDTLS_PRIVATE(Q),
                       &(*ctx)->MBEDTLS_PRIVATE(d),
                       &(*ctx)->MBEDTLS_PRIVATE(grp).G,
                       vibe_mbedtls_rng, NULL))
        goto failed;

    if(mbedtls_ecp_check_privkey(&(*ctx)->MBEDTLS_PRIVATE(grp),
                                 &(*ctx)->MBEDTLS_PRIVATE(d)) == 0)
        goto cleanup;

failed:

    _libssh2_mbedtls_ecdsa_free(*ctx);
    *ctx = NULL;

cleanup:

    if(decrypted) {
        _libssh2_string_buf_free(session, decrypted);
    }

    return *ctx ? 0 : -1;
}

/* Force-expose internal mbedTLS function */
#if MBEDTLS_VERSION_NUMBER >= 0x03060000
int mbedtls_pk_load_file(const char *path, unsigned char **buf, size_t *n);
#endif

/* _libssh2_ecdsa_new_private
 *
 * Creates a new private key given a file path and password
 *
 */
int
_libssh2_mbedtls_ecdsa_new_private(libssh2_ecdsa_ctx **ec_ctx,
                                   LIBSSH2_SESSION *session,
                                   const char *filename,
                                   const unsigned char *passphrase)
{
    mbedtls_pk_context pkey;
    unsigned char *data = NULL;
    size_t data_len = 0;

    mbedtls_pk_init(&pkey);

    /* FIXME: Reimplement this functionality via a public API. */
    if(mbedtls_pk_load_file(filename, &data, &data_len))
        goto cleanup;

    if(_libssh2_mbedtls_parse_eckey(ec_ctx, &pkey, session,
                                    data, data_len, passphrase) == 0)
        goto cleanup;

    _libssh2_mbedtls_parse_openssh_key(ec_ctx, session, data, data_len,
                                       passphrase);

cleanup:

    mbedtls_pk_free(&pkey);

    _libssh2_mbedtls_safe_free(data, data_len);

    return *ec_ctx ? 0 : -1;
}

/* _libssh2_ecdsa_new_private
 *
 * Creates a new private key given a file data and password
 *
 */
int
_libssh2_mbedtls_ecdsa_new_private_frommemory(libssh2_ecdsa_ctx **ec_ctx,
                                              LIBSSH2_SESSION *session,
                                              const char *filedata,
                                              size_t filedata_len,
                                              const unsigned char *passphrase)
{
    unsigned char *ntdata;
    mbedtls_pk_context pkey;

    mbedtls_pk_init(&pkey);

    ntdata = LIBSSH2_ALLOC(session, filedata_len + 1);

    if(!ntdata)
        goto cleanup;

    memcpy(ntdata, filedata, filedata_len);

    if(_libssh2_mbedtls_parse_eckey(ec_ctx, &pkey, session,
                                    ntdata, filedata_len + 1, passphrase) == 0)
        goto cleanup;

    _libssh2_mbedtls_parse_openssh_key(ec_ctx, session,
                                       ntdata, filedata_len + 1, passphrase);

cleanup:

    mbedtls_pk_free(&pkey);

    _libssh2_mbedtls_safe_free(ntdata, filedata_len);

    return *ec_ctx ? 0 : -1;
}

static unsigned char *
_libssh2_mbedtls_mpi_write_binary(unsigned char *buf,
                                  const mbedtls_mpi *mpi,
                                  size_t bytes)
{
    unsigned char *p = buf;
    uint32_t size = (uint32_t)bytes;

    if(sizeof(&p) / sizeof(p[0]) < 4) {
        goto done;
    }

    p += 4;
    *p = 0;

    if(size > 0) {
        mbedtls_mpi_write_binary(mpi, p + 1, size - 1);
    }

    if(size > 0 && !(*(p + 1) & 0x80)) {
        memmove(p, p + 1, --size);
    }

    _libssh2_htonu32(p - 4, size);

done:

    return p + size;
}

/* _libssh2_ecdsa_sign
 *
 * Computes the ECDSA signature of a previously-hashed message
 *
 */
int
_libssh2_mbedtls_ecdsa_sign(LIBSSH2_SESSION *session,
                            libssh2_ecdsa_ctx *ec_ctx,
                            const unsigned char *hash,
                            size_t hash_len,
                            unsigned char **signature,
                            size_t *signature_len)
{
    size_t r_len, s_len, tmp_sign_len = 0;
    unsigned char *sp, *tmp_sign = NULL;
    mbedtls_mpi pr, ps;

    mbedtls_mpi_init(&pr);
    mbedtls_mpi_init(&ps);

    if(mbedtls_ecdsa_sign(&ec_ctx->MBEDTLS_PRIVATE(grp), &pr, &ps,
                          &ec_ctx->MBEDTLS_PRIVATE(d),
                          hash, hash_len,
                          vibe_mbedtls_rng, NULL))
        goto cleanup;

    r_len = mbedtls_mpi_size(&pr) + 1;
    s_len = mbedtls_mpi_size(&ps) + 1;
    tmp_sign_len = r_len + s_len + 8;

    tmp_sign = LIBSSH2_CALLOC(session, tmp_sign_len);

    if(!tmp_sign)
        goto cleanup;

    sp = tmp_sign;
    sp = _libssh2_mbedtls_mpi_write_binary(sp, &pr, r_len);
    sp = _libssh2_mbedtls_mpi_write_binary(sp, &ps, s_len);

    *signature_len = (size_t)(sp - tmp_sign);

    *signature = LIBSSH2_CALLOC(session, *signature_len);

    if(!*signature)
        goto cleanup;

    memcpy(*signature, tmp_sign, *signature_len);

cleanup:

    mbedtls_mpi_free(&pr);
    mbedtls_mpi_free(&ps);

    _libssh2_mbedtls_safe_free(tmp_sign, tmp_sign_len);

    return *signature ? 0 : -1;
}

/* _libssh2_ecdsa_get_curve_type
 *
 * returns key curve type that maps to libssh2_curve_type
 *
 */
libssh2_curve_type
_libssh2_mbedtls_ecdsa_get_curve_type(libssh2_ecdsa_ctx *ec_ctx)
{
    return (libssh2_curve_type)ec_ctx->MBEDTLS_PRIVATE(grp).id;
}

/* _libssh2_ecdsa_curve_type_from_name
 *
 * returns 0 for success, key curve type that maps to libssh2_curve_type
 *
 */
int
_libssh2_mbedtls_ecdsa_curve_type_from_name(const char *name,
                                            libssh2_curve_type *out_type)
{
    int ret = 0;
    libssh2_curve_type type;

    if(!name || strlen(name) != 19)
        return -1;

    if(strcmp(name, "ecdsa-sha2-nistp256") == 0)
        type = LIBSSH2_EC_CURVE_NISTP256;
    else if(strcmp(name, "ecdsa-sha2-nistp384") == 0)
        type = LIBSSH2_EC_CURVE_NISTP384;
    else if(strcmp(name, "ecdsa-sha2-nistp521") == 0)
        type = LIBSSH2_EC_CURVE_NISTP521;
    else {
        ret = -1;
    }

    if(ret == 0 && out_type) {
        *out_type = type;
    }

    return ret;
}

void
_libssh2_mbedtls_ecdsa_free(libssh2_ecdsa_ctx *ctx)
{
    mbedtls_ecdsa_free(ctx);
    mbedtls_free(ctx);
}
#endif /* LIBSSH2_ECDSA */

/* _libssh2_supported_key_sign_algorithms
 *
 * Return supported key hash algo upgrades, see crypto.h
 *
 */
const char *
_libssh2_supported_key_sign_algorithms(LIBSSH2_SESSION *session,
                                       unsigned char *key_method,
                                       size_t key_method_len)
{
    (void)session;

#if LIBSSH2_RSA_SHA2
    if(key_method_len == 7 &&
       memcmp(key_method, "ssh-rsa", key_method_len) == 0) {
        return "rsa-sha2-512,rsa-sha2-256"
#if LIBSSH2_RSA_SHA1
            ",ssh-rsa"
#endif
            ;
    }
#endif

    return NULL;
}

#endif /* LIBSSH2_MBEDTLS */
