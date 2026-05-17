#ifndef USER_WGET_H
#define USER_WGET_H

#include <stdint.h>
#include "lwip/altcp.h"
#include "lwip/altcp_tls.h"
#include "lwip/ip_addr.h"
#include "virtio.h"

#ifdef FPGA_MINIMAL
#define WGET_MAX_FILE_SIZE (256 * 1024)
#else
#define WGET_MAX_FILE_SIZE (1024 * 1024)
#endif
#define WGET_PATH_MAX 512
#define WGET_HEADER_MAX 4096

struct wget_state {
    struct altcp_pcb *pcb;
    char host[32];
    char path[WGET_PATH_MAX];
    char filename[20];
    uint16_t port;
    int use_tls;
    int stage;
    int resolve_pending;
    int resolve_in_progress;
    int resolved_addr_valid;
    ip_addr_t resolved_addr;
    uint32_t wait_started_ms;
    
    int active;
    int done;
    int success;
    int request_pending;
    int save_pending;
    
    int owner_win_id;
    int resource_kind;
    uint32_t progress_row;
    uint32_t target_cwd_bno;
    char target_cwd[32];

    char header[WGET_HEADER_MAX];
    uint32_t header_len;
    int header_done;
    int http_status;
    
    int has_content_len;
    uint32_t content_len;
    uint32_t body_len;
    
    char err[20];
};

struct wget_save_state {
    int active;
    int entry_idx;
    uint32_t first_bno;
    uint32_t written;
    int failed;
    struct blk pending_blk;
    uint32_t pending_bno;
    int pending_valid;
    int finalize_phase;
    uint32_t created_at;
    uint32_t old_bno;
};

// 全域狀態宣告
extern struct wget_state wget_job;
extern struct wget_save_state wget_save;
extern unsigned char wget_file_buf[WGET_MAX_FILE_SIZE];
extern int wget_save_turn;

// 對外接口
void wget_finish(int success, const char *msg);
void wget_close_pcb(int abort_conn);
void wget_reset_job(void);
void wget_queue_request(const char *host, const char *path, const char *filename, uint16_t port, int use_tls);
void wget_queue_request_ex(const char *host, const char *path, const char *filename, uint16_t port, int use_tls, int save_to_fs, int resource_kind, int owner_win_id);
void wget_kick_if_needed(void);
int wget_timeout_step(void);
int wget_progress_dirty(void);
const char *wget_stage_name(void);
void set_wget_progress_line(void *w_ptr, uint32_t row); // 用 void* 避免循環依賴
int wget_save_begin(void);
int wget_save_step(void);
int parse_wget_url(const char *url, char *host_out, uint16_t *port_out, char *path_out);

#endif
