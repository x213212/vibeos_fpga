#include "riscv.h"
#include "string.h"
#include "mbedtls/entropy.h"

#ifndef SSH_DEBUG_LOG
#define SSH_DEBUG_LOG 0
#endif

#if !SSH_DEBUG_LOG
#define lib_printf(...) do { } while (0)
#endif

static uint64_t entropy_mix64(uint64_t x)
{
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    return x * 2685821657736338717ULL;
}

void mbedtls_entropy_init(mbedtls_entropy_context *ctx)
{
    lib_printf("[BOOT] compat_entropy_init ctx=%p\n", (void *) ctx);
    (void) ctx;
}

void mbedtls_entropy_free(mbedtls_entropy_context *ctx)
{
    lib_printf("[BOOT] compat_entropy_free ctx=%p\n", (void *) ctx);
    (void) ctx;
}

int mbedtls_entropy_func(void *data, unsigned char *output, size_t len)
{
    uint64_t state;
    size_t i;

    if (output == 0) {
        return -1;
    }

    state = ((uint64_t)get_millisecond_timer() << 32) ^
            (uint64_t)get_wall_clock_seconds() ^
            ((uint64_t)r_mhartid() << 48) ^
            (uint64_t)(uintptr_t)data ^
            (uint64_t)(uintptr_t)output;
    if (state == 0) {
        state = 0x6a09e667f3bcc909ULL;
    }

    lib_printf("[BOOT] compat_entropy_func data=%p out=%p len=%u\n",
               data, output, (unsigned) len);

    for (i = 0; i < len; ) {
        state = entropy_mix64(state ^ (uint64_t)i);
        for (size_t j = 0; j < sizeof(state) && i < len; ++j, ++i) {
            output[i] = (uint8_t) (state >> (8U * j));
        }
    }

    return 0;
}
