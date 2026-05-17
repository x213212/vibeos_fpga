#include "user_wget.h"
#include "user.h"
#include "lib.h"
#include "string.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"

#define COL_TEXT UI_C_TEXT

enum {
    WGET_STAGE_IDLE = 0,
    WGET_STAGE_DNS,
    WGET_STAGE_CONNECT,
    WGET_STAGE_RECV,
    WGET_STAGE_SAVE,
};

const char *wget_stage_name(void) {
    switch (wget_job.stage) {
        case WGET_STAGE_DNS: return "DNS";
        case WGET_STAGE_CONNECT: return "CONNECT";
        case WGET_STAGE_RECV: return "RECV";
        case WGET_STAGE_SAVE: return "SAVE";
        default: return "IDLE";
    }
}

// 引用 user.c 中的全域視窗與臨時變數
extern struct Window wins[MAX_WINDOWS];
extern struct Window fs_tmp_window;
extern void wake_network_task(void);
extern void fs_free_chain(uint32_t bno);
extern uint32_t balloc(void);

// 全域變數定義
struct wget_state wget_job;
struct wget_save_state wget_save;
unsigned char wget_file_buf[WGET_MAX_FILE_SIZE];

static uint32_t wget_ui_last_update = 0;
static uint32_t wget_last_percent = 101;
int wget_save_turn = 0;
static struct altcp_tls_config *wget_tls_conf = 0;
static altcp_allocator_t wget_tls_allocator;

#define WGET_DEBUG 1
#if WGET_DEBUG
#define WGET_LOG(...) lib_printf(__VA_ARGS__)
#else
#define WGET_LOG(...) do { } while (0)
#endif

// --- 內部輔助函數 ---

static int wget_ensure_tls_allocator(void) {
    if (wget_tls_conf == 0) {
        wget_tls_conf = altcp_tls_create_config_client(0, 0);
        if (wget_tls_conf == 0) return -1;
    }
    wget_tls_allocator.alloc = altcp_tls_alloc;
    wget_tls_allocator.arg = wget_tls_conf;
    return 0;
}

static err_t wget_start_with_addr(const ip_addr_t *addr, const char *path, const char *filename, uint16_t port, int use_tls);
static void wget_dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *arg);

void wget_finish(int success, const char *msg) {
    lib_printf("[WGET] finish success=%d owner=%d kind=%d\n", success, wget_job.owner_win_id, wget_job.resource_kind);
    
    // Save state needed for callbacks
    int owner_win_id = wget_job.owner_win_id;
    int resource_kind = wget_job.resource_kind;
    int body_len = wget_job.body_len;
    char host[32], path[WGET_PATH_MAX];
    uint16_t port = wget_job.port;
    int use_tls = wget_job.use_tls;
    lib_strcpy(host, wget_job.host);
    lib_strcpy(path, wget_job.path);

    // Reset job state FIRST, so that callbacks can safely queue new requests
    wget_job.success = success;
    wget_job.done = 1;
    wget_job.active = 0;
    wget_job.stage = WGET_STAGE_IDLE;
    if (msg) copy_name20(wget_job.err, msg);
    WGET_LOG("[wget] finish success=%d msg=%s body=%u\n", success, msg ? msg : "-", body_len);

    if (success && owner_win_id >= 0) {
        struct Window *ow = &wins[owner_win_id];
        lib_printf("[WGET] finish: owner win kind=%d\n", ow->kind);
        if (resource_kind == 1) {
            char full_url[240];
            full_url[0] = '\0';
            lib_strcpy(full_url, use_tls ? "https://" : "http://");
            lib_strcat(full_url, host);
            if ((use_tls && port != 443) || (!use_tls && port != 80)) {
                char port_str[12];
                lib_strcat(full_url, ":");
                lib_itoa((uint32_t)port, port_str);
                lib_strcat(full_url, port_str);
            }
            lib_strcat(full_url, path);
            lib_printf("[WGET] finish: calling netsurf_receive_image for %s\n", full_url);
            extern void netsurf_receive_image(int win_id, const char *url, const uint8_t *data, size_t len);
            netsurf_receive_image(owner_win_id, full_url, wget_file_buf, body_len);
        } else if (resource_kind == 0) {
            if (ow->kind == 5) {
                lib_printf("[WGET] finish: calling netsurf_complete_data\n");
                extern void netsurf_complete_data(int win_id);
                netsurf_complete_data(owner_win_id);
            } else {
                lib_printf("[WGET] finish: owner not netsurf (kind %d)\n", ow->kind);
            }
        }
    }
}

void wget_close_pcb(int abort_conn) {
    struct altcp_pcb *pcb = wget_job.pcb;
    if (!pcb) return;
    altcp_arg(pcb, NULL);
    altcp_recv(pcb, NULL);
    altcp_sent(pcb, NULL);
    altcp_err(pcb, NULL);
    wget_job.pcb = 0;
    if (abort_conn) altcp_abort(pcb);
    else altcp_close(pcb);
}

static int parse_http_status_code(const char *hdr) {
    const char *p = hdr;
    while (*p && *p != ' ') p++;
    if (*p != ' ') return -1;
    p++;
    if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9' || p[2] < '0' || p[2] > '9') return -1;
    return (p[0] - '0') * 100 + (p[1] - '0') * 10 + (p[2] - '0');
}

static int parse_content_length(const char *hdr) {
    const char *p = strstr(hdr, "\nContent-Length:");
    if (!p) {
        if (strncmp(hdr, "Content-Length:", 15) == 0) p = hdr - 1;
        else return -1;
    }
    p += 16;
    while (*p == ' ') p++;
    int n = 0;
    while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); p++; }
    return n;
}

static int str_starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

static void copy_range_str(char *dst, int dst_size, const char *begin, const char *end) {
    int i = 0;
    if (dst_size <= 0) return;
    while (begin < end && i < dst_size - 1) dst[i++] = *begin++;
    dst[i] = '\0';
}

static int parse_port_number(const char *s, uint16_t *port_out) {
    unsigned int port = 0;
    if (*s == '\0') return 0;
    while (*s) {
        if (*s < '0' || *s > '9') return 0;
        port = port * 10U + (unsigned int)(*s - '0');
        if (port > 65535U) return 0;
        s++;
    }
    *port_out = (uint16_t)port;
    return 1;
}

int parse_wget_url(const char *url, char *host_out, uint16_t *port_out, char *path_out) {
    const char *p = url;
    const char *scheme = 0;
    if (str_starts_with(url, "http://")) {
        scheme = "http://";
        *port_out = 80;
    } else if (str_starts_with(url, "https://")) {
        scheme = "https://";
        *port_out = 443;
    } else {
        return 0;
    }

    p += strlen(scheme);
    if (*p == '\0') return 0;

    const char *authority = p;
    while (*p && *p != '/') p++; 
    const char *authority_end = p;
    const char *colon = 0;
    for (const char *q = authority; q < authority_end; q++) {
        if (*q == ':') colon = q;
    }

    if (colon) {
        copy_range_str(host_out, 32, authority, colon);
        char pstr[8];
        copy_range_str(pstr, 8, colon + 1, authority_end);
        *port_out = (uint16_t)atoi(pstr);
    } else {
        copy_range_str(host_out, 32, authority, authority_end);
    }
    
    if (host_out[0] == '\0') return 0;

    if (*p == '\0') {
        lib_strcpy(path_out, "/");
    } else {
        lib_strcpy(path_out, p);
    }
    return 1;
}

// --- 回調函數 ---

static err_t wget_recv_cb(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err) {
    lib_printf("WGET: Bottom-level packet received, len %d\n", (p ? p->tot_len : 0));
    (void)arg;
    if (err != ERR_OK) {
        if (p) pbuf_free(p);
        wget_close_pcb(1);
        wget_finish(0, "RECV ERR");
        return ERR_ABRT;
    }

    if (p == NULL) {
        wget_close_pcb(0); // Graceful close to avoid crashing simple servers
        if (!wget_job.header_done) {
            wget_finish(0, "BAD HTTP");
        } else if (wget_job.resource_kind == 0 && wget_job.owner_win_id >= 0 && wins[wget_job.owner_win_id].kind == 5) {
            // For NetSurf real-time feed, we skip full header parsing, so assume success
            wget_finish(1, 0);
        } else if (wget_job.http_status != 200) {
            wget_finish(0, "HTTP FAIL");
        } else {
            wget_finish(1, 0);
        }
        return ERR_OK;
    }

    /* --- NETSURF DATA FEED START --- */
    if (wget_job.active && wget_job.owner_win_id >= 0 && wget_job.resource_kind == 0) {
        struct Window *ow = &wins[wget_job.owner_win_id];
        if (ow->kind == 5) {
            for (struct pbuf *tp = p; tp != NULL; tp = tp->next) {
                unsigned char *d = (unsigned char *)tp->payload;
                int l = tp->len;
                if (!wget_job.header_done) {
                    for (int i = 0; i < l - 3; i++) {
                        if (d[i]=='\r' && d[i+1]=='\n' && d[i+2]=='\r' && d[i+3]=='\n') {
                            wget_job.header_done = 1;
                            if (i + 4 < l) {
                                extern void netsurf_feed_data(int, const uint8_t*, size_t);
                                netsurf_feed_data(wget_job.owner_win_id, d + i + 4, l - (i + 4));
                            }
                            break;
                        }
                    }
                } else {
                    extern void netsurf_feed_data(int, const uint8_t*, size_t);
                    netsurf_feed_data(wget_job.owner_win_id, d, l);
                }
            }
        }
    }
    /* --- NETSURF DATA FEED END --- */

    struct pbuf *q = p;
    while (q != 0 && !wget_job.done) {
        unsigned char *src = (unsigned char *)q->payload;
        uint16_t i = 0;
        for (; i < q->len && !wget_job.done; i++) {
            if (!wget_job.header_done) {
                if (wget_job.header_len + 1 >= sizeof(wget_job.header)) {
                    wget_close_pcb(1); wget_finish(0, "HDR TOO BIG"); break;
                }
                wget_job.header[wget_job.header_len++] = (char)src[i];
                wget_job.header[wget_job.header_len] = '\0';
                if (wget_job.header_len >= 4 &&
                    wget_job.header[wget_job.header_len - 4] == '\r' &&
                    wget_job.header[wget_job.header_len - 3] == '\n' &&
                    wget_job.header[wget_job.header_len - 2] == '\r' &&
                    wget_job.header[wget_job.header_len - 1] == '\n') {
                    wget_job.header_done = 1;
                    wget_job.http_status = parse_http_status_code(wget_job.header);
                    int clen = parse_content_length(wget_job.header);
                    if (clen >= 0) { wget_job.has_content_len = 1; wget_job.content_len = (uint32_t)clen; }
                }
            } else {
                uint16_t remain = q->len - i;
                if (wget_job.body_len + remain > WGET_MAX_FILE_SIZE) {
                    wget_close_pcb(1); wget_finish(0, "FILE TOO BIG"); break;
                }
                memcpy(wget_file_buf + wget_job.body_len, src + i, remain);
                wget_job.body_len += remain;
                i = q->len; // Skip current pbuf inner loop

                // Check for early completion via Content-Length
                if (wget_job.has_content_len && wget_job.body_len >= wget_job.content_len) {
                    wget_close_pcb(0);
                    wget_finish(1, 0);
                    pbuf_free(p);
                    return ERR_OK;
                }
            }
        }
        q = q->next;
    }

    if (pcb != 0) {
        altcp_recved(pcb, p->tot_len);
        if (!wget_job.done) altcp_output(pcb);
    }
    pbuf_free(p);
    return ERR_OK;
}
static void wget_err_cb(void *arg, err_t err) {
    lib_printf("WGET: TCP Error occurred, code=%d\n", err);
    (void)arg;
    wget_job.pcb = 0;
    if (!wget_job.done) {
        char msg[20];
        lib_strcpy(msg, "TCPERR ");
        char code_str[8];
        lib_itoa((uint32_t)(err < 0 ? -err : err), code_str);
        if (err < 0) lib_strcat(msg, "-");
        lib_strcat(msg, code_str);
        wget_finish(0, msg);
    }
}

static err_t wget_connected_cb(void *arg, struct altcp_pcb *pcb, err_t err) {
    (void)arg;
    if (err != ERR_OK) {
        wget_close_pcb(1); wget_finish(0, "CONNECT FAIL"); return err;
    }
    wget_job.stage = WGET_STAGE_RECV;
    char req[768];
    lib_strcpy(req, "GET ");
    lib_strcat(req, wget_job.path);
    lib_strcat(req, " HTTP/1.0\r\nHost: ");
    lib_strcat(req, wget_job.host);
    lib_strcat(req, "\r\nConnection: close\r\n\r\n");
    altcp_recv(pcb, wget_recv_cb);
    altcp_err(pcb, wget_err_cb);
    altcp_write(pcb, req, strlen(req), TCP_WRITE_FLAG_COPY);
    altcp_output(pcb);
    return ERR_OK;
}

// --- 對外接口實現 ---

void wget_reset_job(void) {
    memset(&wget_job, 0, sizeof(wget_job));
    wget_ui_last_update = 0;
    wget_last_percent = 101;
    wget_save_turn = 0;
    wget_job.stage = WGET_STAGE_IDLE;
    wget_job.wait_started_ms = 0;
}

static err_t wget_start_with_addr(const ip_addr_t *addr, const char *path, const char *filename, uint16_t port, int use_tls) {
    if (wget_job.active) {
        lib_printf("[WGET] start_with_addr: already active\n");
        return -1;
    }
    // 排隊時已經填好大部分欄位，這裡只做初始化連線
    wget_job.active = 1;
    wget_job.stage = WGET_STAGE_CONNECT;
    wget_job.wait_started_ms = sys_now();
    if (use_tls) {
        if (wget_ensure_tls_allocator() != 0) { 
            lib_printf("[WGET] start_with_addr: TLS INIT FAIL\n");
            wget_finish(0, "TLS INIT FAIL"); return -3; 
        }
        wget_job.pcb = altcp_new(&wget_tls_allocator);
    } else {
        wget_job.pcb = altcp_new(NULL);
    }
    
    if (!wget_job.pcb) { 
        lib_printf("[WGET] start_with_addr: NO PCB\n");
        wget_finish(0, "NO PCB"); return -3; 
    }
    
    static uint16_t local_port = 10000;
    altcp_bind(wget_job.pcb, IP_ANY_TYPE, local_port++);
    if (local_port > 60000) local_port = 10000;

    altcp_arg(wget_job.pcb, &wget_job);
    altcp_err(wget_job.pcb, wget_err_cb);
    // Delay connection to prevent TCP fast-reconnect rejection from simple servers
    if (use_tls) {
        lib_delay(1000);
    }
    err_t err = altcp_connect(wget_job.pcb, addr, port, wget_connected_cb);
    if (err != ERR_OK) {
        lib_printf("[WGET] start_with_addr: CONNECT FAIL err=%d\n", err);
        wget_close_pcb(1); wget_finish(0, "CONNECT FAIL"); return -4;
    }
    lib_printf("[WGET] start_with_addr: SUCCESS\n");
    return ERR_OK;
}

static void wget_dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *arg) {
    (void)name;
    (void)arg;
    wget_job.resolve_in_progress = 0;
    if (!wget_job.request_pending || wget_job.done) return;
    if (ipaddr == NULL) {
        wget_job.resolve_pending = 0;
        wget_job.request_pending = 0;
        wget_finish(0, "DNS FAIL");
        wake_network_task();
        return;
    }
    wget_job.resolved_addr = *ipaddr;
    wget_job.resolved_addr_valid = 1;
    wget_job.resolve_pending = 0;
    wake_network_task();
}

int wget_start(const char *host, const char *path, const char *filename, uint16_t port, int use_tls) {
    ip_addr_t addr;
    if (wget_job.active) return -1;
    if (!ipaddr_aton(host, &addr)) return -2;
    return (wget_start_with_addr(&addr, path, filename, port, use_tls) == ERR_OK) ? 0 : -3;
}

void wget_queue_request(const char *host, const char *path, const char *filename, uint16_t port, int use_tls) {
    wget_queue_request_ex(host, path, filename, port, use_tls, 1, 0, -1);
}

void wget_queue_request_ex(const char *host, const char *path, const char *filename, uint16_t port, int use_tls, int save_to_fs, int resource_kind, int owner_win_id) {
    lib_printf("[WGET] queue host=%s path=%s file=%s port=%u tls=%d save=%d kind=%d owner=%d\n",
               host ? host : "-", path ? path : "-", filename ? filename : "-",
               (unsigned)port, use_tls, save_to_fs, resource_kind, owner_win_id);
    wget_reset_job();
    copy_name20(wget_job.host, host);
    lib_strcpy(wget_job.path, path);
    copy_name20(wget_job.filename, filename);
    wget_job.port = port ? port : (use_tls ? 443 : 80);
    wget_job.use_tls = use_tls;
    wget_job.resource_kind = resource_kind;
    wget_job.owner_win_id = owner_win_id;
    wget_job.resolve_pending = 0;
    wget_job.resolve_in_progress = 0;
    wget_job.resolved_addr_valid = 0;
    if (!ipaddr_aton(host, &wget_job.resolved_addr)) {
        wget_job.resolve_pending = 1;
        wget_job.stage = WGET_STAGE_DNS;
        wget_job.wait_started_ms = sys_now();
    } else {
        wget_job.resolved_addr_valid = 1;
    }
    wget_job.request_pending = 1;
    wget_job.save_pending = save_to_fs;
    
    // 立即嘗試啟動任務
    extern void wget_kick_if_needed(void);
    wget_kick_if_needed();
    
    wake_network_task();
}

void wget_kick_if_needed(void) {
    if (!wget_job.request_pending || wget_job.active) {
        lib_printf("[WGET] kick ignored: pending=%d active=%d\n", wget_job.request_pending, wget_job.active);
        return;
    }
    lib_printf("[WGET] kicking job, resolve_pending=%d\n", wget_job.resolve_pending);
    if (wget_job.resolve_pending) {
        err_t err;
        if (wget_job.resolve_in_progress) return;
        wget_job.resolve_in_progress = 1;
        wget_job.stage = WGET_STAGE_DNS;
        if (wget_job.wait_started_ms == 0) wget_job.wait_started_ms = sys_now();
        err = dns_gethostbyname_addrtype(wget_job.host, &wget_job.resolved_addr,
                                         wget_dns_found_cb, NULL, LWIP_DNS_ADDRTYPE_DEFAULT);
        if (err == ERR_OK) {
            wget_job.resolve_in_progress = 0;
            wget_job.resolve_pending = 0;
            wget_job.resolved_addr_valid = 1;
            wget_job.stage = WGET_STAGE_CONNECT;
        } else if (err == ERR_INPROGRESS) {
            return;
        } else {
            wget_job.resolve_in_progress = 0;
            wget_job.resolve_pending = 0;
            wget_job.request_pending = 0;
            wget_finish(0, "DNS FAIL");
            return;
        }
    }
    if (!wget_job.resolved_addr_valid) {
        wget_job.request_pending = 0;
        wget_finish(0, "DNS FAIL");
        return;
    }
    wget_job.request_pending = 0;
    wget_start_with_addr(&wget_job.resolved_addr, wget_job.path, wget_job.filename, wget_job.port, wget_job.use_tls);
}

int wget_timeout_step(void) {
    uint32_t now = sys_now();
    if (wget_job.done) return 0;
    if ((wget_job.resolve_pending || wget_job.resolve_in_progress) && wget_job.wait_started_ms != 0) {
        if ((uint32_t)(now - wget_job.wait_started_ms) > 5000U) {
            wget_job.resolve_pending = 0;
            wget_job.resolve_in_progress = 0;
            wget_job.request_pending = 0;
            wget_finish(0, "DNS TIMEOUT");
            return 1;
        }
    }
    if (wget_job.active && wget_job.wait_started_ms != 0 &&
        wget_job.stage == WGET_STAGE_CONNECT &&
        (uint32_t)(now - wget_job.wait_started_ms) > 10000U) {
        wget_close_pcb(1);
        wget_finish(0, wget_job.use_tls ? "TLS TIMEOUT" : "TCP TIMEOUT");
        return 1;
    }
    return 0;
}

int wget_progress_dirty(void) {
    uint32_t now = sys_now();
    if (wget_job.request_pending) return 1;
    if (wget_job.done && wget_job.success && wget_save.active) {
        if (now - wget_ui_last_update >= 120) { wget_ui_last_update = now; return 1; }
        return 0;
    }
    if (wget_job.done && wget_job.success) {
        if (wget_last_percent != 100) { wget_last_percent = 100; return 1; }
        return 0;
    }
    if (wget_job.active && wget_job.has_content_len && wget_job.content_len > 0) {
        uint32_t percent = (wget_job.body_len * 100) / wget_job.content_len;
        if (percent != wget_last_percent || now - wget_ui_last_update >= 120) {
            wget_last_percent = percent; wget_ui_last_update = now; return 1;
        }
    } else if (wget_job.active && now - wget_ui_last_update >= 120) {
        wget_ui_last_update = now; return 1;
    }
    return 0;
}

void set_wget_progress_line(void *w_ptr, uint32_t row) {
    struct Window *w = (struct Window *)w_ptr;
    char line[COLS], got[16], total[16];
    static const char spin[4] = {'|', '/', '-', '\\'};
    char sp = spin[(sys_now() / 100U) & 3U];
    lib_strcpy(line, "wget ");
    lib_strcat(line, wget_job.filename[0] ? wget_job.filename : "-");
    lib_strcat(line, " ");

    if (wget_job.resolve_pending || wget_job.resolve_in_progress) {
        lib_strcat(line, "DNS ");
        lib_strcat(line, (char[]){sp, '\0'});
    } else if (wget_job.active && wget_job.has_content_len) {
        uint32_t percent = (wget_job.body_len * 100) / wget_job.content_len;
        lib_strcat(line, "[");
        int filled = (percent * 20) / 100;
        for (int i = 0; i < 20; i++) lib_strcat(line, (i < filled) ? "=" : " ");
        lib_strcat(line, "] ");
        format_size_human(wget_job.body_len, got);
        format_size_human(wget_job.content_len, total);
        lib_strcat(line, got); lib_strcat(line, "/"); lib_strcat(line, total);
    } else if (wget_job.done) {
        lib_strcat(line, wget_job.success ? "DONE" : wget_job.err);
    } else {
        lib_strcat(line, (char[]){sp, '\0'});
    }
    
    int char_w = terminal_char_w(w);
    int char_h = terminal_char_h(w);
    int tx = w->x + 10;
    int ty = w->y + 36 + row * (char_h + 2);
    draw_text_scaled(tx, ty, line, COL_TEXT, terminal_font_scale(w));
}

// --- 檔案儲存邏輯 ---

int wget_save_begin(void) {
    struct dir_block *db;
    fs_tmp_window.cwd_bno = wget_job.target_cwd_bno;
    lib_strcpy(fs_tmp_window.cwd, wget_job.target_cwd);
    db = load_current_dir(&fs_tmp_window);
    if (!db) return -1;

    memset(&wget_save, 0, sizeof(wget_save));
    wget_save.active = 1;
    wget_save.entry_idx = find_entry_index(db, wget_job.filename, -1);
    if (wget_save.entry_idx != -1 && db->entries[wget_save.entry_idx].type == 1) return -2;
    wget_save.old_bno = (wget_save.entry_idx != -1) ? db->entries[wget_save.entry_idx].bno : 0;
    if (wget_save.entry_idx == -1) wget_save.entry_idx = find_free_entry_index(db);
    if (wget_save.entry_idx == -1) return -3;
    wget_save.created_at = (wget_save.entry_idx >= 0 && db->entries[wget_save.entry_idx].name[0]) ?
                           db->entries[wget_save.entry_idx].ctime : current_fs_time();
    return 0;
}

int wget_save_step(void) {
    if (!wget_save.active) return 1;
    struct dir_block *db = (struct dir_block *)fs_tmp_window.local_b.data;
    uint32_t available = wget_job.body_len - wget_save.written;

    if (available >= FS_DATA_PAYLOAD || (wget_job.done && available > 0)) {
        uint32_t this_bno = balloc();
        struct blk blkbuf;
        struct data_block *dblk;
        uint32_t chunk = available;
        if (chunk > FS_DATA_PAYLOAD) chunk = FS_DATA_PAYLOAD;

        memset(&blkbuf, 0, sizeof(blkbuf));
        blkbuf.blockno = this_bno;
        dblk = (struct data_block *)blkbuf.data;
        dblk->magic = FS_DATA_MAGIC;
        dblk->next_bno = 0;
        memcpy(dblk->data, wget_file_buf + wget_save.written, chunk);
        
        if (wget_save.pending_valid) {
            struct data_block *prev = (struct data_block *)wget_save.pending_blk.data;
            prev->next_bno = this_bno;
            virtio_disk_rw(&wget_save.pending_blk, 1);
        } else {
            wget_save.first_bno = this_bno;
        }
        memcpy(&wget_save.pending_blk, &blkbuf, sizeof(struct blk));
        wget_save.pending_bno = this_bno;
        wget_save.pending_valid = 1;
        wget_save.written += chunk;
    }

    if (!wget_job.done) return 0;
    if (wget_job.body_len == 0 && wget_save.first_bno == 0) {
        uint32_t bno = balloc();
        struct blk empty_blk;
        memset(&empty_blk, 0, sizeof(empty_blk));
        empty_blk.blockno = bno;
        ((struct data_block *)empty_blk.data)->magic = FS_DATA_MAGIC;
        virtio_disk_rw(&empty_blk, 1);
        wget_save.first_bno = bno;
    }

    if (wget_save.written != wget_job.body_len) return 0;

    if (wget_save.pending_valid && wget_save.finalize_phase == 0) {
        virtio_disk_rw(&wget_save.pending_blk, 1);
        wget_save.pending_valid = 0;
        wget_save.finalize_phase = 1;
    }

    if (wget_save.finalize_phase == 1) {
        db->entries[wget_save.entry_idx].bno = wget_save.first_bno;
        db->entries[wget_save.entry_idx].size = wget_job.body_len;
        copy_name20(db->entries[wget_save.entry_idx].name, wget_job.filename);
        db->entries[wget_save.entry_idx].ctime = wget_save.created_at;
        db->entries[wget_save.entry_idx].mtime = current_fs_time();
        db->entries[wget_save.entry_idx].type = 0;
        virtio_disk_rw(&fs_tmp_window.local_b, 1);
        if (wget_save.old_bno != 0) fs_free_chain(wget_save.old_bno);
        wget_save.finalize_phase = 2;
        return 0;
    }

    wget_save.active = 0;
    return 1;
}
