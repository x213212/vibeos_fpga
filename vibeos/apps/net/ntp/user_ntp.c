#include "os.h"
#include "timer.h"
#include "lwip/ip_addr.h"
#include "lwip/apps/sntp.h"

static int time_sync_started;
static int time_sync_done;
static int time_sync_stop_pending;
static int time_sync_stopped;
static uint32_t time_sync_start_ms;

void vibe_sntp_set_system_time(unsigned int unix_seconds)
{
    set_wall_clock_unix_seconds(unix_seconds);
    time_sync_done = 1;
    time_sync_stop_pending = 1;
    lib_printf("[TIME] SNTP synced once unix=%u\n", unix_seconds);
}

void vibe_time_sync_start(void)
{
    ip_addr_t server;

    if (time_sync_done) return;
    if (time_sync_started) return;
    time_sync_started = 1;
    time_sync_stop_pending = 0;
    time_sync_stopped = 0;
    time_sync_start_ms = get_millisecond_timer();

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    IP_ADDR4(&server, 118, 163, 81, 61);
    sntp_setserver(0, &server);

    sntp_init();
    lib_puts("[TIME] SNTP one-shot start: time.stdtime.gov.tw 118.163.81.61\n");
}

void vibe_time_sync_poll(void)
{
    if (!time_sync_started || time_sync_stopped) return;
    if (!time_sync_stop_pending &&
        (uint32_t)(get_millisecond_timer() - time_sync_start_ms) < 12000U) {
        return;
    }

    sntp_stop();
    time_sync_stopped = 1;
    if (time_sync_done) {
        lib_puts("[TIME] SNTP stopped after first sync\n");
    } else {
        lib_puts("[TIME] SNTP stopped without sync\n");
    }
}
