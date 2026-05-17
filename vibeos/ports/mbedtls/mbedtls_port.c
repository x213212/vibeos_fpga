#include "riscv.h"
#include "timer.h"
#include "string.h"
#include "mbedtls/platform.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/debug.h"
#include "psa/crypto.h"
#include <stdint.h>
#include <stdarg.h>

#ifndef SSH_DEBUG_LOG
#define SSH_DEBUG_LOG 0
#endif

#if !SSH_DEBUG_LOG
#define lib_printf(...) do { } while (0)
#endif

extern int libssh2_vibe_kex_stage;
extern int libssh2_vibe_kex_ret;
extern int libssh2_vibe_kex_aux0;
extern int libssh2_vibe_kex_aux1;
extern int libssh2_vibe_kex_aux2;

#define MBEDTLS_OS_POOL_SIZE (4u * 1024u)
#define MBEDTLS_OS_ALIGN 8u
#define MBEDTLS_OS_MIN_SPLIT 32u
#define MBEDTLS_OS_MAGIC 0x4d425448u

typedef struct mbedtls_os_block {
    uint32_t magic;
    uint32_t size;
    uint32_t free;
    uint32_t reserved;
    struct mbedtls_os_block *next;
    struct mbedtls_os_block *prev;
} mbedtls_os_block_t;

static unsigned char mbedtls_os_pool[MBEDTLS_OS_POOL_SIZE] __attribute__((aligned(16)));
static mbedtls_os_block_t *mbedtls_os_head;
static int mbedtls_os_pool_ready;
static int mbedtls_os_platform_ready;

static size_t mbedtls_os_align_up(size_t n)
{
    return (n + (MBEDTLS_OS_ALIGN - 1u)) & ~(size_t)(MBEDTLS_OS_ALIGN - 1u);
}

static size_t mbedtls_os_header_size(void)
{
    return mbedtls_os_align_up(sizeof(mbedtls_os_block_t));
}

static reg_t mbedtls_os_lock(void)
{
    reg_t mstatus = r_mstatus();
    w_mstatus(mstatus & ~MSTATUS_MIE);
    return mstatus;
}

static void mbedtls_os_unlock(reg_t mstatus)
{
    w_mstatus(mstatus);
}

static int mbedtls_os_ptr_in_pool(void *p)
{
    uintptr_t addr = (uintptr_t)p;
    uintptr_t start = (uintptr_t)mbedtls_os_pool;
    uintptr_t end = start + MBEDTLS_OS_POOL_SIZE;
    return addr >= start && addr < end;
}

static void mbedtls_os_pool_init_once(void)
{
    size_t hdr = mbedtls_os_header_size();

    if (mbedtls_os_pool_ready) {
        return;
    }

    mbedtls_os_head = (mbedtls_os_block_t *)mbedtls_os_pool;
    mbedtls_os_head->magic = MBEDTLS_OS_MAGIC;
    mbedtls_os_head->size = (uint32_t)(MBEDTLS_OS_POOL_SIZE - hdr);
    mbedtls_os_head->free = 1u;
    mbedtls_os_head->reserved = 0u;
    mbedtls_os_head->next = 0;
    mbedtls_os_head->prev = 0;
    mbedtls_os_pool_ready = 1;
}

static void mbedtls_os_pool_stats_unlocked(size_t *free_bytes,
                                           size_t *max_free_block)
{
    mbedtls_os_block_t *b = mbedtls_os_head;
    size_t total = 0;
    size_t max_block = 0;

    while (b) {
        if (b->magic != MBEDTLS_OS_MAGIC) {
            break;
        }
        if (b->free) {
            total += b->size;
            if (b->size > max_block) {
                max_block = b->size;
            }
        }
        b = b->next;
    }

    if (free_bytes) {
        *free_bytes = total;
    }
    if (max_free_block) {
        *max_free_block = max_block;
    }
}

static void mbedtls_os_record_alloc_fail(size_t requested)
{
    libssh2_vibe_kex_stage = 2291;
    libssh2_vibe_kex_ret = -141;
    libssh2_vibe_kex_aux0 = (int)requested;
    libssh2_vibe_kex_aux1 = 0;
    libssh2_vibe_kex_aux2 = 0;
}

static void *mbedtls_os_pool_alloc(size_t size)
{
    size_t hdr = mbedtls_os_header_size();
    size_t need = mbedtls_os_align_up(size ? size : 1u);
    mbedtls_os_block_t *b;
    reg_t irq;

    irq = mbedtls_os_lock();
    mbedtls_os_pool_init_once();

    for (b = mbedtls_os_head; b; b = b->next) {
        if (b->magic != MBEDTLS_OS_MAGIC) {
            break;
        }
        if (!b->free || b->size < need) {
            continue;
        }

        if ((size_t)b->size >= need + hdr + MBEDTLS_OS_MIN_SPLIT) {
            mbedtls_os_block_t *next =
                (mbedtls_os_block_t *)((unsigned char *)b + hdr + need);
            next->magic = MBEDTLS_OS_MAGIC;
            next->size = (uint32_t)((size_t)b->size - need - hdr);
            next->free = 1u;
            next->reserved = 0u;
            next->prev = b;
            next->next = b->next;
            if (b->next) {
                b->next->prev = next;
            }
            b->next = next;
            b->size = (uint32_t)need;
        }

        b->free = 0u;
        mbedtls_os_unlock(irq);
        return (unsigned char *)b + hdr;
    }

    mbedtls_os_unlock(irq);
    mbedtls_os_record_alloc_fail(need);
    return 0;
}

static void mbedtls_os_pool_merge_next(mbedtls_os_block_t *b)
{
    size_t hdr = mbedtls_os_header_size();
    mbedtls_os_block_t *next = b->next;

    if (!next || !next->free || next->magic != MBEDTLS_OS_MAGIC) {
        return;
    }

    b->size += (uint32_t)(hdr + next->size);
    b->next = next->next;
    if (b->next) {
        b->next->prev = b;
    }
}

static int mbedtls_os_pool_free(void *p)
{
    size_t hdr = mbedtls_os_header_size();
    mbedtls_os_block_t *b;
    reg_t irq;

    if (!mbedtls_os_ptr_in_pool(p)) {
        return 0;
    }

    b = (mbedtls_os_block_t *)((unsigned char *)p - hdr);
    if (b->magic != MBEDTLS_OS_MAGIC) {
        return 1;
    }

    irq = mbedtls_os_lock();
    b->free = 1u;
    mbedtls_os_pool_merge_next(b);
    if (b->prev && b->prev->free && b->prev->magic == MBEDTLS_OS_MAGIC) {
        mbedtls_os_pool_merge_next(b->prev);
    }
    mbedtls_os_unlock(irq);
    return 1;
}

static void *mbedtls_os_calloc(size_t n, size_t size)
{
    size_t total;
    void *p;

    if (n != 0 && size > ((size_t)-1) / n) {
        mbedtls_os_record_alloc_fail((size_t)-1);
        return 0;
    }

    total = n * size;
    p = calloc(1, total ? total : 1u);
    if (p == 0) {
        mbedtls_os_record_alloc_fail(total ? total : 1u);
        return 0;
    }
    return p;
}

static void mbedtls_os_free(void *p)
{
    if (p != 0) {
        free(p);
    }
}

mbedtls_time_t mbedtls_os_time(mbedtls_time_t *t);

void mbedtls_os_platform_init(void)
{
    /*
     * Keep this idempotent but always rewrite the callbacks.  If SSH enters
     * mbedTLS after a partial boot/reload, the default callback is
     * platform_calloc_uninit(), which makes ECP fail with -141.
     */
    mbedtls_os_platform_ready = 1;
    lib_printf("[BOOT] mbedtls: set calloc/free\n");
    mbedtls_platform_set_calloc_free(mbedtls_os_calloc, mbedtls_os_free);
}

void mbedtls_os_init(void)
{
    psa_status_t psa_status;

    mbedtls_os_platform_init();
    lib_printf("[BOOT] mbedtls: before psa_crypto_init\n");
    psa_status = psa_crypto_init();
    lib_printf("[BOOT] mbedtls: after psa_crypto_init status=%d\n", (int) psa_status);
}

mbedtls_time_t mbedtls_os_time(mbedtls_time_t *t)
{
    mbedtls_time_t now = (mbedtls_time_t)get_wall_clock_seconds();
    if (t != 0) {
        *t = now;
    }
    return now;
}

mbedtls_ms_time_t mbedtls_ms_time(void)
{
    return (mbedtls_ms_time_t)get_millisecond_timer();
}

#if defined(MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG)
static uint64_t rng_mix64(uint64_t x)
{
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    return x * 2685821657736338717ULL;
}

psa_status_t mbedtls_psa_external_get_random(
    mbedtls_psa_external_random_context_t *context,
    uint8_t *output, size_t output_size, size_t *output_length)
{
    uint64_t state;
    uint64_t seed;

    if (context == 0 || output == 0 || output_length == 0) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    seed = ((uint64_t)get_millisecond_timer() << 32) ^
           (uint64_t)get_wall_clock_seconds() ^
           ((uint64_t)r_mhartid() << 48) ^
           (uint64_t)(uintptr_t)context ^
           (uint64_t)(uintptr_t)&seed;
    if (seed == 0) {
        seed = 0x9e3779b97f4a7c15ULL;
    }

    state = ((uint64_t)context->MBEDTLS_PRIVATE(opaque)[0] << 32) ^
            (uint64_t)context->MBEDTLS_PRIVATE(opaque)[1];
    if (state == 0) {
        state = seed;
    }

    for (size_t i = 0; i < output_size; ) {
        state = rng_mix64(state ^ seed ^ (uint64_t)i);
        for (size_t j = 0; j < sizeof(state) && i < output_size; ++j, ++i) {
            output[i] = (uint8_t) (state >> (8U * j));
        }
    }

    context->MBEDTLS_PRIVATE(opaque)[0] = (uintptr_t)(state >> 32);
    context->MBEDTLS_PRIVATE(opaque)[1] = (uintptr_t)(state & 0xffffffffu);
    *output_length = output_size;
    return PSA_SUCCESS;
}
#endif

#undef MBEDTLS_PLATFORM_STD_VSNPRINTF
#undef MBEDTLS_PLATFORM_STD_SNPRINTF

int mbedtls_os_vsnprintf(char *s, size_t n, const char *fmt, va_list ap)
{
    return lib_vsnprintf(s, n, fmt, ap);
}

int mbedtls_os_snprintf(char *s, size_t n, const char *fmt, ...)
{
    int ret;
    va_list ap;
    va_start(ap, fmt);
    ret = lib_vsnprintf(s, n, fmt, ap);
    va_end(ap);
    return ret;
}

int MBEDTLS_PLATFORM_STD_VSNPRINTF(char *s, size_t n, const char *fmt, va_list ap)
{
    return lib_vsnprintf(s, n, fmt, ap);
}

int MBEDTLS_PLATFORM_STD_SNPRINTF(char *s, size_t n, const char *fmt, ...)
{
    int ret;
    va_list ap;
    va_start(ap, fmt);
    ret = lib_vsnprintf(s, n, fmt, ap);
    va_end(ap);
    return ret;
}

#if defined(MBEDTLS_PSA_DRIVER_GET_ENTROPY)
int mbedtls_platform_get_entropy(psa_driver_get_entropy_flags_t flags,
                                 size_t *estimate_bits,
                                 unsigned char *output,
                                 size_t output_size)
{
    unsigned int tick = get_millisecond_timer();
    unsigned long long mix = ((unsigned long long)tick << 32) ^
                             (unsigned long long)r_mhartid() ^
                             (unsigned long long)(uintptr_t)output ^
                             (unsigned long long)(uintptr_t)&tick;

    if (flags != 0) {
        return PSA_ERROR_NOT_SUPPORTED;
    }

    if (output == 0 || estimate_bits == 0) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    lib_printf("[BOOT] mbedtls_platform_get_entropy: size=%u tick=%u\n",
               (unsigned) output_size, tick);

    for (size_t i = 0; i < output_size; ) {
        mix ^= (mix << 13);
        mix ^= (mix >> 7);
        mix ^= (mix << 17);
        size_t chunk = output_size - i;
        if (chunk > sizeof(mix)) chunk = sizeof(mix);
        memcpy(output + i, &mix, chunk);
        i += chunk;
    }

    *estimate_bits = output_size * 8U;
    lib_printf("[BOOT] mbedtls_platform_get_entropy: done bits=%u\n",
               (unsigned) *estimate_bits);
    return 0;
}
#endif
