#include "time.h"
#include "timer.h"
#include "os.h" // r_mhartid, CSR helpers

#ifndef HOST_BUILD_HOUR
#define HOST_BUILD_HOUR 0
#endif
#ifndef HOST_BUILD_MIN
#define HOST_BUILD_MIN 0
#endif
#ifndef HOST_BUILD_SEC
#define HOST_BUILD_SEC 0
#endif

// QEMU virt CLINT mtime runs at 10MHz.  The FPGA minimal build drives the
// RISC-V timer from PL FCLK0; keep this synchronized with the loaded bitstream.
#ifdef FPGA_MINIMAL
#ifndef FPGA_MTIME_HZ
#define FPGA_MTIME_HZ 50000000ULL
#endif
#define MTIME_TICKS_PER_MS (FPGA_MTIME_HZ / 1000ULL)
#define INTERVAL (FPGA_MTIME_HZ / 100ULL)
#else
#define MTIME_TICKS_PER_MS 10000ULL
#define INTERVAL 100000ULL
#endif
static inline unsigned long long read_mtime_real(void) {
#ifdef FPGA_MINIMAL
    uint32_t hi, lo, hi2;
    do {
        asm volatile("csrr %0, 0xc81" : "=r"(hi));
        asm volatile("csrr %0, 0xc01" : "=r"(lo));
        asm volatile("csrr %0, 0xc81" : "=r"(hi2));
    } while (hi2 != hi);
    return ((unsigned long long)hi << 32) | (unsigned long long)lo;
#else
    volatile uint32_t *mtime = (volatile uint32_t *)(uintptr_t)CLINT_MTIME;
    uint32_t hi, lo;
    do {
        hi = mtime[1];
        lo = mtime[0];
    } while (mtime[1] != hi); // low wrap 改變 hi 的話 retry
    return ((unsigned long long)hi << 32) | (unsigned long long)lo;
#endif
}


// ---------- safe 寫 mtimecmp（RV32） ----------
static void set_next_timer(int id, unsigned long long next) {
#ifdef FPGA_MINIMAL
    (void)id;
    unsigned int old_mie = r_mie();
    unsigned int lo = (unsigned int)(next & 0xFFFFFFFFULL);

    w_mie(old_mie & ~MIE_MTIE);
    asm volatile("csrw 0x7c0, %0" :: "r"(lo) : "memory");
    w_mie(old_mie);
#else
    volatile unsigned int *mtimecmp = (volatile unsigned int *)CLINT_MTIMECMP(id);
    unsigned int hi = (unsigned int)(next >> 32);
    unsigned int lo = (unsigned int)(next & 0xFFFFFFFFULL);

    // Optional: 如果在 interrupt context 可能重入，可外層先關 MTIE
    unsigned int old_mie = r_mie();
    w_mie(old_mie & ~MIE_MTIE); // 暫時關 timer interrupt

    // safe update sequence: upper large, lower, then target upper
    mtimecmp[1] = 0xFFFFFFFF;
    mtimecmp[0] = lo;
    mtimecmp[1] = hi;

    w_mie(old_mie); // restore original mie (可能重新 enable)
#endif
}

// ---------- state ----------
static int timer_count = 0;
static uint32_t lwip_rand_state = 0x6d2b79f5U;
static uint32_t wall_clock_base_epoch = 0;
static uint32_t wall_clock_base_ms = 0;
static int wall_clock_initialized = 0;
static int wall_clock_synced = 0;

static int is_leap_year(unsigned int year)
{
    return ((year % 4U) == 0U && (year % 100U) != 0U) || ((year % 400U) == 0U);
}

static unsigned int days_before_month(unsigned int year, unsigned int month)
{
    static const unsigned char mdays[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    unsigned int days = 0;
    if (month < 1U) month = 1U;
    if (month > 12U) month = 12U;
    for (unsigned int m = 1; m < month; m++) {
        days += mdays[m - 1U];
        if (m == 2U && is_leap_year(year)) days++;
    }
    return days;
}

static uint32_t ymd_hms_to_unix(unsigned int year, unsigned int month, unsigned int day,
                                unsigned int hour, unsigned int min, unsigned int sec)
{
    uint64_t days = 0;
    if (year < 1970U) year = 1970U;
    if (day < 1U) day = 1U;
    for (unsigned int y = 1970U; y < year; y++) {
        days += is_leap_year(y) ? 366U : 365U;
    }
    days += days_before_month(year, month);
    days += day - 1U;
    return (uint32_t)(days * 86400ULL + (uint64_t)hour * 3600ULL +
                      (uint64_t)min * 60ULL + (uint64_t)sec);
}

static uint32_t build_time_unix(void)
{
    return ymd_hms_to_unix((unsigned int)HOST_BUILD_YEAR,
                           (unsigned int)HOST_BUILD_MONTH,
                           (unsigned int)HOST_BUILD_DAY,
                           (unsigned int)HOST_BUILD_HOUR,
                           (unsigned int)HOST_BUILD_MIN,
                           (unsigned int)HOST_BUILD_SEC);
}

static void wall_clock_init_if_needed(void)
{
    if (wall_clock_initialized) return;
    wall_clock_base_epoch = build_time_unix();
    wall_clock_base_ms = get_millisecond_timer();
    wall_clock_initialized = 1;
}

void timer_init()
{
#ifdef FPGA_MINIMAL
    int id = 0;
#else
    int id = r_mhartid();
#endif

    // 讀當前時間並 schedule 下一次
    unsigned long long now = read_mtime_real();
    set_next_timer(id, now + INTERVAL);

    // 啟用 machine timer interrupt source（假設 global MIE 先開）
    w_mie(r_mie() | MIE_MTIE);
}

void timer_handler()
{
#ifdef FPGA_MINIMAL
    int id = 0;
#else
    int id = r_mhartid();
#endif

    // 先讀時間
    unsigned long long now = read_mtime_real();
    timer_count++;

    // debug 可以暫時開
    // lib_printf("timer_handler #%d now=0x%llx\n", timer_count, now);

    // 安排下一次
    set_next_timer(id, now + INTERVAL);
}

// 轉毫秒（10MHz tick => ms = ticks / 10000）
unsigned int get_millisecond_timer(void) {
    unsigned long long mtime = read_mtime_real();
    return (unsigned int)(mtime / MTIME_TICKS_PER_MS);
}

unsigned int sys_now(void) {
    return get_millisecond_timer();
}

unsigned int get_wall_clock_seconds(void) {
    wall_clock_init_if_needed();
    return wall_clock_base_epoch + ((uint32_t)(get_millisecond_timer() - wall_clock_base_ms) / 1000U);
}

void set_wall_clock_unix_seconds(unsigned int unix_seconds) {
    wall_clock_base_epoch = unix_seconds;
    wall_clock_base_ms = get_millisecond_timer();
    wall_clock_initialized = 1;
    wall_clock_synced = 1;
}

int wall_clock_is_synced(void) {
    return wall_clock_synced;
}

time_t time(time_t *t) {
    time_t now = (time_t)get_wall_clock_seconds();
    if (t != 0) {
        *t = now;
    }
    return now;
}

long long difftime(time_t time1, time_t time0) {
    return (long long)(time1 - time0);
}

struct tm *gmtime_r(const time_t *timer, struct tm *result) {
    static const unsigned char mdays[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    uint64_t t;
    uint64_t days;
    unsigned int rem;
    unsigned int year = 1970U;
    unsigned int yday;
    unsigned int month = 0;
    unsigned int dim;

    if (!timer || !result) return 0;
    t = (*timer < 0) ? 0ULL : (uint64_t)(*timer);
    days = t / 86400ULL;
    rem = (unsigned int)(t % 86400ULL);

    result->tm_hour = (int)(rem / 3600U);
    rem %= 3600U;
    result->tm_min = (int)(rem / 60U);
    result->tm_sec = (int)(rem % 60U);
    result->tm_wday = (int)((days + 4ULL) % 7ULL);

    while (1) {
        unsigned int yd = is_leap_year(year) ? 366U : 365U;
        if (days < yd) break;
        days -= yd;
        year++;
    }
    yday = (unsigned int)days;
    while (month < 12U) {
        dim = mdays[month] + ((month == 1U && is_leap_year(year)) ? 1U : 0U);
        if (days < dim) break;
        days -= dim;
        month++;
    }

    result->tm_year = (int)year - 1900;
    result->tm_mon = (int)month;
    result->tm_mday = (int)days + 1;
    result->tm_yday = (int)yday;
    result->tm_isdst = 0;
    return result;
}

unsigned int lwip_rand(void) {
    uint32_t x = lwip_rand_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    lwip_rand_state = x ^ sys_now()
#ifndef FPGA_MINIMAL
        ^ ((uint32_t)r_mhartid() << 16)
#endif
        ^ (uint32_t)timer_count;
    return lwip_rand_state;
}
