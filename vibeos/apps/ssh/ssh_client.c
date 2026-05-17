#include "user_internal.h"
#include "errno.h"
#include "libssh2.h"
#include "libssh2_sftp.h"
#include "lwip/altcp.h"
#include "lwip/altcp_tcp.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/timeouts.h"
#include "mbedtls_port.h"

extern int libssh2_vibe_kex_stage;
extern int libssh2_vibe_kex_ret;
extern int libssh2_vibe_kex_aux0;
extern int libssh2_vibe_kex_aux1;
extern int libssh2_vibe_kex_aux2;
extern char libssh2_vibe_kex_name[64];
extern char libssh2_vibe_hostkey_name[64];
extern char libssh2_vibe_crypt_name[32];
extern char libssh2_vibe_mac_name[32];
extern int libssh2_vibe_session_stage;
extern int libssh2_vibe_session_ret;
extern int libssh2_vibe_session_state;
extern int libssh2_vibe_packet_type;
extern int libssh2_vibe_packet_len;
extern int libssh2_vibe_packet_macstate;
extern unsigned int libssh2_vibe_local_seqno;
extern unsigned int libssh2_vibe_remote_seqno;
extern int libssh2_vibe_out_packet_type;
extern int libssh2_vibe_out_plain_len;
extern int libssh2_vibe_out_packet_len;
extern int libssh2_vibe_out_total_len;
extern int libssh2_vibe_out_ret;
extern int libssh2_vibe_out_encrypted;
extern int libssh2_vibe_out_padding;
extern unsigned int libssh2_vibe_out_seqno;

#ifdef FPGA_MINIMAL
extern void ps_gem_poll(void);
extern int ps_gem_has_rx_ready(void);
extern int ps_gem_rx_pending_count(void);
#endif

#ifndef SSH_DEBUG_LOG
#define SSH_DEBUG_LOG 0
#endif

#if !SSH_DEBUG_LOG
#define lib_printf(...) do { } while (0)
#endif

#define SSH_RX_BUF_SIZE (64 * 1024)
#define SSH_CONNECT_TIMEOUT_MS 60000U
#define SSH_AUTH_TIMEOUT_MS 15000U
#define SSH_EXEC_TIMEOUT_MS 20000U
#define SSH_DEFAULT_WRP_URL "http://192.168.123.100:9999"

static char ssh_user[32];
static char ssh_host[64];
static char ssh_target[96];
static uint16_t ssh_port = 22;
static char ssh_password[64];
static int ssh_password_set = 0;
static char ssh_wrp_url[160];
static char sftp_mount_root[160] = ".";
static int sftp_mounted = 0;
static char ssh_last_diag[768];
static uint32_t ssh_dbg_tcp_recv_bytes;
static uint32_t ssh_dbg_libssh2_send_bytes;
static uint32_t ssh_dbg_libssh2_recv_bytes;
static uint32_t ssh_dbg_send_eagain;
static uint32_t ssh_dbg_recv_eagain;
static int ssh_libssh2_ready;

struct ssh_transport {
    struct altcp_pcb *pcb;
    ip_addr_t addr;
    uint16_t port;
    int connected;
    int closed;
    int err;
    uint8_t rx_buf[SSH_RX_BUF_SIZE];
    uint32_t rx_start;
    uint32_t rx_len;
};

static void ssh_transport_destroy(struct ssh_transport *t);
static void ssh_transport_pump(void);
static int ssh_connect_transport(struct ssh_transport **out_t, const char *host, uint16_t port, char *out, int out_max);

static void ssh_set_msg(char *out, int out_max, const char *msg)
{
    if (!out || out_max <= 0) return;
    out[0] = '\0';
    if (!msg) return;
    lib_strcpy(out, msg);
}

static void ssh_copy_bounded(char *dst, int dst_max, const char *src)
{
    int i = 0;
    if (!dst || dst_max <= 0) return;
    if (!src) src = "";
    while (i < dst_max - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void ssh_copy_n_bounded(char *dst, int dst_max, const char *src, int src_len)
{
    int i = 0;
    if (!dst || dst_max <= 0) return;
    if (!src || src_len <= 0) {
        dst[0] = '\0';
        return;
    }
    while (i < dst_max - 1 && i < src_len && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void ssh_append_bounded(char *dst, int dst_max, const char *src)
{
    int used;
    int i = 0;
    if (!dst || dst_max <= 0 || !src) return;
    used = (int)strlen(dst);
    if (used >= dst_max - 1) return;
    while (used + i < dst_max - 1 && src[i]) {
        dst[used + i] = src[i];
        i++;
    }
    dst[used + i] = '\0';
}

static void ssh_diag_save(const char *msg)
{
    ssh_copy_bounded(ssh_last_diag, sizeof(ssh_last_diag), msg);
}

static void ssh_append_selected_methods(LIBSSH2_SESSION *session, char *out, int out_max)
{
    const char *kex;
    const char *hostkey;
    const char *crypt_cs;
    const char *mac_cs;
    char tail[256];

    if (!session || !out || out_max <= 0) return;
    kex = libssh2_session_methods(session, LIBSSH2_METHOD_KEX);
    hostkey = libssh2_session_methods(session, LIBSSH2_METHOD_HOSTKEY);
    crypt_cs = libssh2_session_methods(session, LIBSSH2_METHOD_CRYPT_CS);
    mac_cs = libssh2_session_methods(session, LIBSSH2_METHOD_MAC_CS);
    snprintf(tail, sizeof(tail), " methods: kex=%s hk=%s c=%s mac=%s.",
             kex ? kex : "-",
             hostkey ? hostkey : "-",
             crypt_cs ? crypt_cs : "-",
             mac_cs ? mac_cs : "-");
    ssh_append_bounded(out, out_max, tail);
}

static void ssh_append_kex_global_diag(char *out, int out_max)
{
    char tail[256];
    if (!out || out_max <= 0) return;
    snprintf(tail, sizeof(tail),
             " kexdbg: st=%d ret=%d a0=%d a1=%d a2=%d kex=%s hk=%s c=%s mac=%s.",
             libssh2_vibe_kex_stage,
             libssh2_vibe_kex_ret,
             libssh2_vibe_kex_aux0,
             libssh2_vibe_kex_aux1,
             libssh2_vibe_kex_aux2,
             libssh2_vibe_kex_name[0] ? libssh2_vibe_kex_name : "-",
             libssh2_vibe_hostkey_name[0] ? libssh2_vibe_hostkey_name : "-",
             libssh2_vibe_crypt_name[0] ? libssh2_vibe_crypt_name : "-",
             libssh2_vibe_mac_name[0] ? libssh2_vibe_mac_name : "-");
    ssh_append_bounded(out, out_max, tail);
}

static void ssh_append_transport_diag(char *out, int out_max)
{
    char tail[160];
    if (!out || out_max <= 0) return;
    snprintf(tail, sizeof(tail),
             " netdbg: tcp_rx=%u ssh_tx=%u ssh_rx=%u seagain=%u reagain=%u.",
             (unsigned)ssh_dbg_tcp_recv_bytes,
             (unsigned)ssh_dbg_libssh2_send_bytes,
             (unsigned)ssh_dbg_libssh2_recv_bytes,
             (unsigned)ssh_dbg_send_eagain,
             (unsigned)ssh_dbg_recv_eagain);
    ssh_append_bounded(out, out_max, tail);
}

static void ssh_append_session_diag(char *out, int out_max)
{
    char tail[320];
    if (!out || out_max <= 0) return;
    snprintf(tail, sizeof(tail),
             " sshdbg: sst=%d sret=%d nb=%d pkt=%d plen=%d mac=%d lseq=%u rseq=%u opkt=%d oseq=%u oplen=%d opad=%d ototal=%d oret=%d oenc=%d.",
             libssh2_vibe_session_stage,
             libssh2_vibe_session_ret,
             libssh2_vibe_session_state,
             libssh2_vibe_packet_type,
             libssh2_vibe_packet_len,
             libssh2_vibe_packet_macstate,
             libssh2_vibe_local_seqno,
             libssh2_vibe_remote_seqno,
             libssh2_vibe_out_packet_type,
             libssh2_vibe_out_seqno,
             libssh2_vibe_out_plain_len,
             libssh2_vibe_out_padding,
             libssh2_vibe_out_total_len,
             libssh2_vibe_out_ret,
             libssh2_vibe_out_encrypted);
    ssh_append_bounded(out, out_max, tail);
}

static void ssh_log(const char *msg)
{
    if (msg) lib_printf("[SSH] %s\n", msg);
}

static void ssh_log_u32(const char *tag, uint32_t v)
{
    if (tag) lib_printf("[SSH] %s=%u\n", tag, (unsigned)v);
}

static void ssh_log_ptr(const char *tag, const void *p)
{
    if (tag) lib_printf("[SSH] %s=%lx\n", tag, (unsigned long)p);
}

static void ssh_log_build_stamp(void)
{
    const char *ver = libssh2_version(0);
    lib_printf("[SSH] build stamp %s %s %s libssh2=%s\n",
               __DATE__, __TIME__, __FILE__, ver ? ver : "-");
}

static int ssh_runtime_init(char *out, int out_max)
{
    int rc;

    mbedtls_os_platform_init();
    if (ssh_libssh2_ready) return 0;

    rc = libssh2_init(0);
    if (rc != 0) {
        snprintf(out, out_max, "ERR: libssh2 init failed rc=%d.", rc);
        return -1;
    }
    ssh_libssh2_ready = 1;
    return 0;
}

static void ssh_runtime_release(void)
{
    /*
     * Keep libssh2 initialized for the bare-metal process lifetime.
     * Repeated init/exit caused the next SSH command to get stuck in
     * handshake on this target after a successful first command.
     */
}

static void ssh_trace_handler(LIBSSH2_SESSION *session, void *context, const char *message, size_t length)
{
    char buf[256];
    size_t n;

    (void)session;
    (void)context;
    if (!message || length == 0) return;
    n = length;
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    memcpy(buf, message, n);
    buf[n] = '\0';
    lib_printf("[SSH][TRACE] %s\n", buf);
}

static void ssh_log_session_error(LIBSSH2_SESSION *session, const char *stage)
{
    char *errmsg = 0;
    int errmsg_len = 0;
    int last_errno = -1;

    if (!stage) stage = "session";
    if (session) {
        last_errno = libssh2_session_last_error(session, &errmsg, &errmsg_len, 0);
    }
    lib_printf("[SSH] %s last_errno=%d errmsg=%s len=%d\n",
               stage,
               last_errno,
               errmsg ? errmsg : "-",
               errmsg_len);
}

static void ssh_set_session_error_msg(LIBSSH2_SESSION *session, char *out, int out_max, const char *stage, int rc)
{
    char *errmsg = 0;
    int errmsg_len = 0;
    int last_errno = -1;

    if (!out || out_max <= 0) return;
    if (!stage) stage = "SSH failed";
    if (session) {
        last_errno = libssh2_session_last_error(session, &errmsg, &errmsg_len, 0);
    }
    if (errmsg && errmsg_len > 0) {
        char err_copy[160];
        ssh_copy_n_bounded(err_copy, sizeof(err_copy), errmsg, errmsg_len);
        snprintf(out, (uint32_t)out_max, "ERR: %s rc=%d libssh2=%d %s.",
                 stage, rc, last_errno, err_copy);
    } else {
        snprintf(out, (uint32_t)out_max, "ERR: %s rc=%d libssh2=%d.",
                 stage, rc, last_errno);
    }
    ssh_append_kex_global_diag(out, out_max);
    ssh_append_session_diag(out, out_max);
    ssh_append_transport_diag(out, out_max);
    ssh_diag_save(out);
}

int ssh_client_diag(char *out, int out_max)
{
    if (!out || out_max <= 0) return -1;
    if (ssh_last_diag[0] == '\0') {
        ssh_set_msg(out, out_max, "ssh diag: no session error yet.");
        ssh_append_kex_global_diag(out, out_max);
        ssh_append_session_diag(out, out_max);
        ssh_append_transport_diag(out, out_max);
        return 0;
    }
    ssh_copy_bounded(out, out_max, ssh_last_diag);
    return 0;
}

static int ssh_configure_session_methods(LIBSSH2_SESSION *session)
{
    int rc;
    if (!session) return -1;
    rc = libssh2_session_method_pref(session,
                                     LIBSSH2_METHOD_KEX,
                                     "ecdh-sha2-nistp256");
    if (rc != 0) {
        lib_printf("[SSH] method_pref kex rc=%d\n", rc);
        return rc;
    }
    rc = libssh2_session_method_pref(session,
                                     LIBSSH2_METHOD_HOSTKEY,
                                     "ecdsa-sha2-nistp256");
    if (rc != 0) {
        lib_printf("[SSH] method_pref hostkey rc=%d\n", rc);
        return rc;
    }
    rc = libssh2_session_method_pref(session,
                                     LIBSSH2_METHOD_SIGN_ALGO,
                                     "rsa-sha2-256,ssh-rsa");
    if (rc != 0) {
        lib_printf("[SSH] method_pref sign_algo rc=%d\n", rc);
        return rc;
    }
    rc = libssh2_session_method_pref(session, LIBSSH2_METHOD_CRYPT_CS, "aes128-ctr");
    if (rc != 0) {
        lib_printf("[SSH] method_pref crypt_cs rc=%d\n", rc);
        return rc;
    }
    rc = libssh2_session_method_pref(session, LIBSSH2_METHOD_CRYPT_SC, "aes128-ctr");
    if (rc != 0) {
        lib_printf("[SSH] method_pref crypt_sc rc=%d\n", rc);
        return rc;
    }
    rc = libssh2_session_method_pref(session, LIBSSH2_METHOD_MAC_CS,
                                     "hmac-sha2-256");
    if (rc != 0) {
        lib_printf("[SSH] method_pref mac_cs rc=%d\n", rc);
        return rc;
    }
    rc = libssh2_session_method_pref(session, LIBSSH2_METHOD_MAC_SC,
                                     "hmac-sha2-256");
    if (rc != 0) {
        lib_printf("[SSH] method_pref mac_sc rc=%d\n", rc);
        return rc;
    }
    lib_printf("[SSH] method preferences configured\n");
    return 0;
}

static void ssh_rebuild_target(void)
{
    ssh_target[0] = '\0';
    if (ssh_user[0]) {
        lib_strcpy(ssh_target, ssh_user);
        lib_strcat(ssh_target, "@");
    }
    lib_strcat(ssh_target, ssh_host);
    if (ssh_port != 22) {
        char portbuf[16];
        lib_strcat(ssh_target, ":");
        lib_itoa((uint32_t)ssh_port, portbuf);
        lib_strcat(ssh_target, portbuf);
    }
}

static int ssh_parse_target(const char *spec, char *out, int out_max)
{
    const char *p = spec;
    const char *at;
    const char *colon;
    char user[32];
    char host[64];
    uint16_t port = 22;

    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0') {
        ssh_set_msg(out, out_max, "ERR: ssh set <user@host[:port]>.");
        return -1;
    }

    user[0] = '\0';
    host[0] = '\0';
    at = strchr(p, '@');
    if (at) {
        int n = (int)(at - p);
        if (n <= 0 || n >= (int)sizeof(user)) {
            ssh_set_msg(out, out_max, "ERR: bad ssh user.");
            return -1;
        }
        memcpy(user, p, (uint32_t)n);
        user[n] = '\0';
        p = at + 1;
    }
    else {
        lib_strcpy(user, "root");
    }

    colon = strchr(p, ':');
    if (colon) {
        int n = (int)(colon - p);
        if (n <= 0 || n >= (int)sizeof(host)) {
            ssh_set_msg(out, out_max, "ERR: bad ssh host.");
            return -1;
        }
        memcpy(host, p, (uint32_t)n);
        host[n] = '\0';
        port = (uint16_t)atoi(colon + 1);
        if (port == 0) port = 22;
    }
    else {
        if ((int)strlen(p) >= (int)sizeof(host)) {
            ssh_set_msg(out, out_max, "ERR: bad ssh host.");
            return -1;
        }
        lib_strcpy(host, p);
    }

    if (host[0] == '\0') {
        ssh_set_msg(out, out_max, "ERR: bad ssh host.");
        return -1;
    }

    lib_strcpy(ssh_user, user);
    lib_strcpy(ssh_host, host);
    ssh_port = port;
    ssh_rebuild_target();

    if (ssh_wrp_url[0] == '\0') {
        lib_strcpy(ssh_wrp_url, "http://");
        lib_strcat(ssh_wrp_url, ssh_host);
        lib_strcat(ssh_wrp_url, ":8080");
    }

    ssh_set_msg(out, out_max, "OK: ssh target stored.");
    return 0;
}

static int ssh_parse_probe_target(const char *spec, char *host, int host_max, uint16_t *port, char *out, int out_max)
{
    const char *p = spec;
    const char *at;
    const char *colon;
    uint16_t parsed_port = 22;

    if (!spec || !host || host_max <= 0 || !port) {
        ssh_set_msg(out, out_max, "ERR: ssh probe <host[:port]>.");
        return -1;
    }

    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0') {
        ssh_set_msg(out, out_max, "ERR: ssh probe <host[:port]>.");
        return -1;
    }

    at = strchr(p, '@');
    if (at) p = at + 1;

    colon = strchr(p, ':');
    if (colon) {
        int n = (int)(colon - p);
        if (n <= 0 || n >= host_max) {
            ssh_set_msg(out, out_max, "ERR: bad ssh host.");
            return -1;
        }
        memcpy(host, p, (uint32_t)n);
        host[n] = '\0';
        parsed_port = (uint16_t)atoi(colon + 1);
        if (parsed_port == 0) parsed_port = 22;
    }
    else {
        if ((int)strlen(p) >= host_max) {
            ssh_set_msg(out, out_max, "ERR: bad ssh host.");
            return -1;
        }
        lib_strcpy(host, p);
    }

    if (host[0] == '\0') {
        ssh_set_msg(out, out_max, "ERR: bad ssh host.");
        return -1;
    }

    *port = parsed_port;
    return 0;
}

static void ssh_transport_trim(struct ssh_transport *t)
{
    if (!t) return;
    if (t->rx_start == 0) return;
    if (t->rx_start >= t->rx_len) {
        t->rx_start = 0;
        t->rx_len = 0;
        return;
    }
    memmove(t->rx_buf, t->rx_buf + t->rx_start, t->rx_len - t->rx_start);
    t->rx_len -= t->rx_start;
    t->rx_start = 0;
}

static uint32_t ssh_transport_free_space(const struct ssh_transport *t)
{
    if (!t) return 0;
    return (uint32_t)sizeof(t->rx_buf) - t->rx_len;
}

static void ssh_transport_push(struct ssh_transport *t, const uint8_t *data, uint32_t len)
{
    if (!t || !data || len == 0) return;
    if (ssh_transport_free_space(t) < len) {
        ssh_transport_trim(t);
    }
    if (ssh_transport_free_space(t) < len) {
        t->err = 1;
        return;
    }
    memcpy(t->rx_buf + t->rx_len, data, len);
    t->rx_len += len;
}

static err_t ssh_altcp_connected_cb(void *arg, struct altcp_pcb *pcb, err_t err)
{
    struct ssh_transport *t = (struct ssh_transport *)arg;
    (void)pcb;
    if (!t) return ERR_ARG;
    lib_printf("[SSH] tcp connected err=%d transport=%lx\n", (int)err, (unsigned long)t);
    if (err == ERR_OK) {
        t->connected = 1;
    }
    else {
        t->err = err;
    }
    wake_network_task();
    return ERR_OK;
}

static err_t ssh_altcp_recv_cb(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err)
{
    struct ssh_transport *t = (struct ssh_transport *)arg;
    (void)pcb;
    if (!t) {
        if (p) pbuf_free(p);
        return ERR_ARG;
    }
    if (err != ERR_OK) {
        if (p) pbuf_free(p);
        t->err = err;
        lib_printf("[SSH] tcp recv err=%d transport=%lx\n", (int)err, (unsigned long)t);
        wake_network_task();
        return ERR_ABRT;
    }
    if (p == NULL) {
        t->closed = 1;
        lib_printf("[SSH] tcp recv eof transport=%lx\n", (unsigned long)t);
        wake_network_task();
        return ERR_OK;
    }

    for (struct pbuf *tp = p; tp != NULL; tp = tp->next) {
        if (tp->len > ssh_transport_free_space(t)) {
            ssh_transport_trim(t);
        }
        if (tp->len > ssh_transport_free_space(t)) {
            t->err = ERR_MEM;
            break;
        }
        ssh_transport_push(t, (const uint8_t *)tp->payload, (uint32_t)tp->len);
    }
    altcp_recved(pcb, (u16_t)p->tot_len);
    ssh_dbg_tcp_recv_bytes += (uint32_t)p->tot_len;
    pbuf_free(p);
    lib_printf("[SSH] tcp recv len=%u transport=%lx\n", (unsigned)p->tot_len, (unsigned long)t);
    wake_network_task();
    return ERR_OK;
}

static err_t ssh_altcp_sent_cb(void *arg, struct altcp_pcb *pcb, u16_t len)
{
    (void)arg;
    (void)pcb;
    (void)len;
    wake_network_task();
    return ERR_OK;
}

static void ssh_altcp_err_cb(void *arg, err_t err)
{
    struct ssh_transport *t = (struct ssh_transport *)arg;
    if (!t) return;
    t->closed = 1;
    t->err = err;
    lib_printf("[SSH] tcp err=%d transport=%lx\n", (int)err, (unsigned long)t);
    wake_network_task();
}

static ssize_t ssh_libssh2_send(libssh2_socket_t socket, const void *buffer, size_t length, int flags, void **abstract)
{
    struct ssh_transport *t;
    u16_t chunk;
    err_t rc;

    (void)socket;
    (void)flags;
    t = (struct ssh_transport *)(*abstract);
    lib_printf("[SSH] send socket=%d len=%u abstract=%lx t=%lx conn=%d closed=%d err=%d\n",
               (int)socket, (unsigned)length, (unsigned long)(*abstract), (unsigned long)t,
               t ? t->connected : -1, t ? t->closed : -1, t ? t->err : -1);
    if (!t || !t->pcb) {
        ssh_dbg_send_eagain++;
        return -EAGAIN;
    }
    if (!t->connected) {
        ssh_dbg_send_eagain++;
        return -EAGAIN;
    }
    if (t->closed || t->err) return -EPIPE;

    chunk = (u16_t)length;
    if (chunk == 0) return 0;
    rc = altcp_write(t->pcb, buffer, chunk, 1);
    if (rc != ERR_OK) {
        if (rc == ERR_MEM || rc == ERR_TIMEOUT) {
            ssh_dbg_send_eagain++;
            return -EAGAIN;
        }
        return -EIO;
    }
    rc = altcp_output(t->pcb);
    if (rc != ERR_OK) {
        if (rc == ERR_MEM || rc == ERR_TIMEOUT) {
            ssh_dbg_send_eagain++;
            return -EAGAIN;
        }
        return -EIO;
    }
    ssh_dbg_libssh2_send_bytes += (uint32_t)chunk;
    wake_network_task();
    return (ssize_t)chunk;
}

static ssize_t ssh_libssh2_recv(libssh2_socket_t socket, void *buffer, size_t length, int flags, void **abstract)
{
    struct ssh_transport *t;
    size_t available;
    size_t n;

    (void)socket;
    (void)flags;
    t = (struct ssh_transport *)(*abstract);
    lib_printf("[SSH] recv socket=%d len=%u abstract=%lx t=%lx conn=%d closed=%d err=%d rx=%u/%u\n",
               (int)socket, (unsigned)length, (unsigned long)(*abstract), (unsigned long)t,
               t ? t->connected : -1, t ? t->closed : -1, t ? t->err : -1,
               t ? (unsigned)t->rx_start : 0, t ? (unsigned)t->rx_len : 0);
    if (!t) {
        ssh_dbg_recv_eagain++;
        return -EAGAIN;
    }

    if (t->rx_start >= t->rx_len) {
        t->rx_start = 0;
        t->rx_len = 0;
        if (t->closed) return 0;
        if (t->err) return -EPIPE;
        ssh_dbg_recv_eagain++;
        return -EAGAIN;
    }

    available = (size_t)(t->rx_len - t->rx_start);
    n = (available < length) ? available : length;
    memcpy(buffer, t->rx_buf + t->rx_start, (uint32_t)n);
    t->rx_start += (uint32_t)n;
    if (t->rx_start >= t->rx_len) {
        t->rx_start = 0;
        t->rx_len = 0;
    }
    ssh_dbg_libssh2_recv_bytes += (uint32_t)n;
    return (ssize_t)n;
}

static void ssh_transport_pump(void)
{
#ifdef FPGA_MINIMAL
    /*
     * SSH runs from a terminal worker and waits in tight non-blocking
     * libssh2 loops.  Waking network_task is not enough here: if the worker
     * gets scheduled back before network_task drains GEM RX, the handshake can
     * sit on EAGAIN until the timeout.  Poll GEM directly for a small bounded
     * budget, then yield to keep UI/USB responsive.
     */
    int budget = ps_gem_rx_pending_count();
    if (budget < 1) budget = 1;
    if (budget > 8) budget = 8;
    while (budget-- > 0) {
        ps_gem_poll();
        if (!ps_gem_has_rx_ready()) break;
    }
    sys_check_timeouts();
#endif
    wake_network_task();
    task_os();
}

static int ssh_transport_wait_ready(struct ssh_transport *t, uint32_t timeout_ms, char *out, int out_max, const char *what)
{
    uint32_t start = sys_now();
    while (!t->connected && !t->closed && !t->err) {
        if (timeout_ms && (uint32_t)(sys_now() - start) > timeout_ms) {
            if (out) {
                lib_strcpy(out, "ERR: ");
                lib_strcat(out, what);
                lib_strcat(out, " timeout.");
            }
            return -1;
        }
        ssh_transport_pump();
    }
    if (!t->connected || t->err || t->closed) {
        if (out && out[0] == '\0') {
            lib_strcpy(out, "ERR: ");
            lib_strcat(out, what);
            lib_strcat(out, " failed.");
        }
        return -1;
    }
    return 0;
}

void ssh_client_reset(void)
{
    ssh_user[0] = '\0';
    ssh_host[0] = '\0';
    ssh_target[0] = '\0';
    ssh_port = 22;
    ssh_password[0] = '\0';
    ssh_password_set = 0;
    ssh_wrp_url[0] = '\0';
    lib_strcpy(sftp_mount_root, ".");
    sftp_mounted = 0;
}

int ssh_client_set_target(const char *spec, char *out, int out_max)
{
    if (!spec || !*spec) {
        ssh_set_msg(out, out_max, "ERR: ssh set <user@host[:port]>.");
        return -1;
    }
    return ssh_parse_target(spec, out, out_max);
}

int ssh_client_set_password(const char *password, char *out, int out_max)
{
    if (!password || !*password) {
        ssh_set_msg(out, out_max, "ERR: ssh auth <password>.");
        return -1;
    }
    if ((int)strlen(password) >= (int)sizeof(ssh_password)) {
        ssh_set_msg(out, out_max, "ERR: password too long.");
        return -1;
    }
    lib_strcpy(ssh_password, password);
    ssh_password_set = 1;
    ssh_set_msg(out, out_max, "OK: ssh password stored.");
    return 0;
}

int ssh_client_set_wrp_url(const char *url, char *out, int out_max)
{
    if (!url || !*url) {
        ssh_set_msg(out, out_max, "ERR: wrp set <http://host:8080>.");
        return -1;
    }
    ssh_wrp_url[0] = '\0';
    lib_strcpy(ssh_wrp_url, url);
    ssh_set_msg(out, out_max, "OK: wrp url stored.");
    return 0;
}

int ssh_client_get_target(char *out, int out_max)
{
    (void)out_max;
    if (!out) return -1;
    if (ssh_target[0] == '\0') {
        out[0] = '\0';
        return -1;
    }
    lib_strcpy(out, ssh_target);
    return 0;
}

int ssh_client_has_password(void)
{
    return ssh_password_set;
}

int ssh_client_get_wrp_url(char *out, int out_max)
{
    (void)out_max;
    if (!out) return -1;
    if (ssh_wrp_url[0] == '\0') {
        lib_strcpy(out, SSH_DEFAULT_WRP_URL);
        return 0;
    }
    lib_strcpy(out, ssh_wrp_url);
    return 0;
}

int ssh_client_probe(const char *spec, char *out, int out_max)
{
    struct ssh_transport *transport = NULL;
    char host[64];
    uint16_t port;
    char portbuf[16];
    int rc;

    if (!out || out_max <= 0) return -1;
    out[0] = '\0';

    if (ssh_parse_probe_target(spec, host, sizeof(host), &port, out, out_max) != 0) {
        return -1;
    }

    rc = ssh_connect_transport(&transport, host, port, out, out_max);
    if (rc != 0) {
        if (out[0] == '\0') ssh_set_msg(out, out_max, "ERR: ssh probe connect failed.");
        return -1;
    }

    ssh_transport_destroy(transport);

    lib_strcpy(out, "OK: tcp ");
    lib_strcat(out, host);
    lib_strcat(out, ":");
    lib_itoa((uint32_t)port, portbuf);
    lib_strcat(out, portbuf);
    lib_strcat(out, " connected.");
    return 0;
}

static int ssh_connect_transport(struct ssh_transport **out_t, const char *host, uint16_t port, char *out, int out_max)
{
    struct ssh_transport *t;
    ip_addr_t tmp_addr;
    err_t rc;
    uint32_t start;

    t = (struct ssh_transport *)malloc(sizeof(struct ssh_transport));
    if (!t) {
        ssh_set_msg(out, out_max, "ERR: No Memory.");
        return -1;
    }
    memset(t, 0, sizeof(*t));
    ssh_dbg_tcp_recv_bytes = 0;
    ssh_dbg_libssh2_send_bytes = 0;
    ssh_dbg_libssh2_recv_bytes = 0;
    ssh_dbg_send_eagain = 0;
    ssh_dbg_recv_eagain = 0;
    t->port = port;
    if (!ipaddr_aton(host, &tmp_addr)) {
        free(t);
        ssh_set_msg(out, out_max, "ERR: ssh host must be numeric IPv4.");
        return -1;
    }
    t->addr = tmp_addr;
    lib_printf("[SSH] connect host=%s port=%u transport=%lx\n", host, (unsigned)port, (unsigned long)t);

    t->pcb = altcp_tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!t->pcb) {
        free(t);
        ssh_set_msg(out, out_max, "ERR: No TCP PCB.");
        return -1;
    }

    altcp_arg(t->pcb, t);
    altcp_recv(t->pcb, ssh_altcp_recv_cb);
    altcp_sent(t->pcb, ssh_altcp_sent_cb);
    altcp_err(t->pcb, ssh_altcp_err_cb);

    rc = altcp_connect(t->pcb, &t->addr, t->port, ssh_altcp_connected_cb);
    lib_printf("[SSH] altcp_connect rc=%d transport=%lx\n", (int)rc, (unsigned long)t);
    if (rc != ERR_OK && rc != ERR_INPROGRESS) {
        ssh_transport_destroy(t);
        ssh_set_msg(out, out_max, "ERR: SSH connect failed.");
        return -1;
    }

    start = sys_now();
    while (!t->connected && !t->closed && !t->err) {
        if ((uint32_t)(sys_now() - start) > SSH_CONNECT_TIMEOUT_MS) {
            ssh_transport_destroy(t);
            ssh_set_msg(out, out_max, "ERR: SSH connect timeout.");
            return -1;
        }
        ssh_transport_pump();
    }

    if (t->err || t->closed || !t->connected) {
        ssh_transport_destroy(t);
        ssh_set_msg(out, out_max, "ERR: SSH connect failed.");
        return -1;
    }

    *out_t = t;
    return 0;
}

static void ssh_transport_destroy(struct ssh_transport *t)
{
    struct altcp_pcb *pcb;
    err_t rc;

    if (!t) return;
    if (t->pcb) {
        pcb = t->pcb;
        t->pcb = NULL;
        altcp_arg(pcb, NULL);
        altcp_recv(pcb, NULL);
        altcp_sent(pcb, NULL);
        altcp_err(pcb, NULL);
        rc = altcp_close(pcb);
        if (rc != ERR_OK) {
            altcp_abort(pcb);
        }
    }
    free(t);
}

static int ssh_session_handshake_nonblock(LIBSSH2_SESSION *session, char *out, int out_max)
{
    int rc;
    uint32_t start = sys_now();
    lib_printf("[SSH] handshake begin session=%lx\n", (unsigned long)session);
    for (;;) {
        rc = libssh2_session_handshake(session, LIBSSH2_INVALID_SOCKET);
        lib_printf("[SSH] handshake rc=%d session=%lx\n", rc, (unsigned long)session);
        if (rc == LIBSSH2_ERROR_EAGAIN) {
            if ((uint32_t)(sys_now() - start) > SSH_CONNECT_TIMEOUT_MS) {
                ssh_set_msg(out, out_max, "ERR: SSH handshake timeout.");
                ssh_log_session_error(session, "handshake timeout");
                ssh_append_selected_methods(session, out, out_max);
                ssh_append_kex_global_diag(out, out_max);
                ssh_append_session_diag(out, out_max);
                ssh_append_transport_diag(out, out_max);
                ssh_diag_save(out);
                return -1;
            }
            ssh_transport_pump();
            continue;
        }
        if (rc != 0) {
            ssh_set_session_error_msg(session, out, out_max, "SSH handshake failed", rc);
            ssh_log_session_error(session, "handshake fail");
            return -1;
        }
        ssh_log_session_error(session, "handshake ok");
        return 0;
    }
}

static int ssh_userauth_password_nonblock(LIBSSH2_SESSION *session, const char *user, const char *pass, char *out, int out_max)
{
    int rc;
    uint32_t start = sys_now();
    lib_printf("[SSH] auth begin user=%s session=%lx\n", user ? user : "-", (unsigned long)session);
    for (;;) {
        rc = libssh2_userauth_password_ex(session, user, (unsigned int)strlen(user), pass, (unsigned int)strlen(pass), NULL);
        lib_printf("[SSH] auth rc=%d user=%s session=%lx\n", rc, user ? user : "-", (unsigned long)session);
        if (rc == LIBSSH2_ERROR_EAGAIN) {
            if ((uint32_t)(sys_now() - start) > SSH_AUTH_TIMEOUT_MS) {
                ssh_set_msg(out, out_max, "ERR: SSH auth timeout.");
                ssh_log_session_error(session, "auth timeout");
                ssh_append_session_diag(out, out_max);
                ssh_append_transport_diag(out, out_max);
                ssh_diag_save(out);
                return -1;
            }
            ssh_transport_pump();
            continue;
        }
        if (rc != 0) {
            ssh_set_session_error_msg(session, out, out_max, "SSH auth failed", rc);
            ssh_log_session_error(session, "auth fail");
            return -1;
        }
        ssh_log_session_error(session, "auth ok");
        return 0;
    }
}

static int ssh_open_authenticated_session(LIBSSH2_SESSION **out_session,
                                          struct ssh_transport **out_transport,
                                          char *out,
                                          int out_max)
{
    LIBSSH2_SESSION *session = NULL;
    struct ssh_transport *transport = NULL;
    void **abstract_slot;
    int rc;

    if (!out_session || !out_transport) return -1;
    *out_session = NULL;
    *out_transport = NULL;

    if (ssh_target[0] == '\0' || ssh_host[0] == '\0') {
        ssh_set_msg(out, out_max, "ERR: ssh target not set.");
        return -1;
    }
    if (!ssh_password_set) {
        ssh_set_msg(out, out_max, "ERR: ssh auth <password> first.");
        return -1;
    }

    ssh_log_build_stamp();
    if (ssh_runtime_init(out, out_max) != 0) return -1;

    rc = ssh_connect_transport(&transport, ssh_host, ssh_port, out, out_max);
    if (rc != 0) {
        ssh_runtime_release();
        return -1;
    }

    session = libssh2_session_init_ex(NULL, NULL, NULL, NULL);
    if (!session) {
        ssh_transport_destroy(transport);
        ssh_runtime_release();
        ssh_set_msg(out, out_max, "ERR: SSH session alloc failed.");
        return -1;
    }

    libssh2_session_set_blocking(session, 0);
    abstract_slot = libssh2_session_abstract(session);
    *abstract_slot = transport;
    libssh2_session_callback_set2(session, LIBSSH2_CALLBACK_SEND, (libssh2_cb_generic *)ssh_libssh2_send);
    libssh2_session_callback_set2(session, LIBSSH2_CALLBACK_RECV, (libssh2_cb_generic *)ssh_libssh2_recv);

    if (ssh_configure_session_methods(session) != 0) {
        ssh_set_msg(out, out_max, "ERR: SSH method pref failed.");
        libssh2_session_free(session);
        ssh_transport_destroy(transport);
        ssh_runtime_release();
        return -1;
    }

    rc = ssh_session_handshake_nonblock(session, out, out_max);
    if (rc != 0) {
        libssh2_session_free(session);
        ssh_transport_destroy(transport);
        ssh_runtime_release();
        return -1;
    }

    rc = ssh_userauth_password_nonblock(session, ssh_user[0] ? ssh_user : "root", ssh_password, out, out_max);
    if (rc != 0) {
        libssh2_session_disconnect_ex(session, SSH_DISCONNECT_BY_APPLICATION, "auth failed", "");
        libssh2_session_free(session);
        ssh_transport_destroy(transport);
        ssh_runtime_release();
        return -1;
    }

    *out_session = session;
    *out_transport = transport;
    return 0;
}

static void ssh_close_authenticated_session(LIBSSH2_SESSION *session, struct ssh_transport *transport)
{
    if (session) {
        uint32_t start = sys_now();
        for (;;) {
            int rc = libssh2_session_disconnect_ex(session, SSH_DISCONNECT_BY_APPLICATION, "bye", "");
            if (rc == LIBSSH2_ERROR_EAGAIN) {
                if ((uint32_t)(sys_now() - start) > 1000U) break;
                ssh_transport_pump();
                continue;
            }
            break;
        }
        libssh2_session_free(session);
    }
    ssh_transport_destroy(transport);
    ssh_runtime_release();
}

static LIBSSH2_SFTP *ssh_sftp_init_nonblock(LIBSSH2_SESSION *session, char *out, int out_max)
{
    LIBSSH2_SFTP *sftp;
    uint32_t start = sys_now();

    for (;;) {
        sftp = libssh2_sftp_init(session);
        if (sftp) return sftp;
        if (libssh2_session_last_errno(session) != LIBSSH2_ERROR_EAGAIN) {
            ssh_set_msg(out, out_max, "ERR: SFTP init failed.");
            ssh_log_session_error(session, "sftp init fail");
            return NULL;
        }
        if ((uint32_t)(sys_now() - start) > SSH_CONNECT_TIMEOUT_MS) {
            ssh_set_msg(out, out_max, "ERR: SFTP init timeout.");
            return NULL;
        }
        ssh_transport_pump();
    }
}

static void ssh_sftp_shutdown_nonblock(LIBSSH2_SFTP *sftp)
{
    if (!sftp) return;
    while (libssh2_sftp_shutdown(sftp) == LIBSSH2_ERROR_EAGAIN) {
        ssh_transport_pump();
    }
}

static void sftp_join_path(const char *base, const char *path, char *out, int out_max)
{
    int len;
    if (!out || out_max <= 0) return;
    out[0] = '\0';
    if (path && path[0] == '/') {
        lib_strcpy(out, path);
        return;
    }
    if (!base || !*base) base = ".";
    lib_strcpy(out, base);
    len = strlen(out);
    if (path && *path) {
        if (len > 0 && out[len - 1] != '/') lib_strcat(out, "/");
        lib_strcat(out, path);
    }
}

static int sftp_basename_to_local(const char *remote, char *local, int local_max)
{
    const char *p;
    (void)local_max;
    if (!remote || !*remote || !local) return -1;
    p = strrchr(remote, '/');
    p = p ? p + 1 : remote;
    if (!*p) return -1;
    copy_name20(local, p);
    return 0;
}

static int ssh_channel_exec_nonblock(LIBSSH2_SESSION *session, const char *cmd, char *out, int out_max)
{
    LIBSSH2_CHANNEL *channel = NULL;
    int rc;
    uint32_t start = sys_now();
    lib_printf("[SSH] channel exec begin cmd='%s' session=%lx\n", cmd ? cmd : "-", (unsigned long)session);

    for (;;) {
        channel = libssh2_channel_open_session(session);
        lib_printf("[SSH] open_channel rc_channel=%lx session=%lx\n", (unsigned long)channel, (unsigned long)session);
        if (channel) break;
        if (libssh2_session_last_errno(session) != LIBSSH2_ERROR_EAGAIN) {
            ssh_set_msg(out, out_max, "ERR: SSH channel open failed.");
            ssh_log_session_error(session, "channel open fail");
            return -1;
        }
        if ((uint32_t)(sys_now() - start) > SSH_EXEC_TIMEOUT_MS) {
            ssh_set_msg(out, out_max, "ERR: SSH channel open timeout.");
            ssh_log_session_error(session, "channel open timeout");
            ssh_append_session_diag(out, out_max);
            ssh_append_transport_diag(out, out_max);
            ssh_diag_save(out);
            return -1;
        }
        ssh_transport_pump();
    }

    for (;;) {
        rc = libssh2_channel_exec(channel, cmd);
        lib_printf("[SSH] channel_exec rc=%d cmd='%s' channel=%lx\n", rc, cmd ? cmd : "-", (unsigned long)channel);
        if (rc == LIBSSH2_ERROR_EAGAIN) {
            if ((uint32_t)(sys_now() - start) > SSH_EXEC_TIMEOUT_MS) {
                libssh2_channel_free(channel);
                ssh_set_msg(out, out_max, "ERR: SSH exec timeout.");
                ssh_log_session_error(session, "channel exec timeout");
                ssh_append_session_diag(out, out_max);
                ssh_append_transport_diag(out, out_max);
                ssh_diag_save(out);
                return -1;
            }
            ssh_transport_pump();
            continue;
        }
        if (rc != 0) {
            libssh2_channel_free(channel);
            ssh_set_msg(out, out_max, "ERR: SSH exec failed.");
            ssh_log_session_error(session, "channel exec fail");
            return -1;
        }
        break;
    }

    out[0] = '\0';
    while (1) {
        char buf[128];
        ssize_t n = libssh2_channel_read_ex(channel, 0, buf, sizeof(buf) - 1);
        lib_printf("[SSH] channel_read n=%ld channel=%lx\n", (long)n, (unsigned long)channel);
        if (n == LIBSSH2_ERROR_EAGAIN) {
            ssh_transport_pump();
            continue;
        }
        if (n < 0) {
            libssh2_channel_free(channel);
            ssh_set_msg(out, out_max, "ERR: SSH read failed.");
            ssh_log_session_error(session, "channel read fail");
            return -1;
        }
        if (n == 0) {
            if (libssh2_channel_eof(channel)) break;
            ssh_transport_pump();
            continue;
        }
        if ((int)strlen(out) + (int)n >= out_max) {
            break;
        }
        buf[n] = '\0';
        lib_strcat(out, buf);
    }

    if (out[0] == '\0') {
        lib_strcpy(out, ">> SSH Exec Done.");
    }

    for (;;) {
        rc = libssh2_channel_close(channel);
        if (rc == LIBSSH2_ERROR_EAGAIN) {
            ssh_transport_pump();
            continue;
        }
        break;
    }
    libssh2_channel_free(channel);
    ssh_log_session_error(session, "channel exec done");
    return 0;
}

int ssh_client_sftp_mount(const char *remote_root, char *out, int out_max)
{
    if (!remote_root || !*remote_root) remote_root = ".";
    if ((int)strlen(remote_root) >= (int)sizeof(sftp_mount_root)) {
        ssh_set_msg(out, out_max, "ERR: sftp mount path too long.");
        return -1;
    }
    lib_strcpy(sftp_mount_root, remote_root);
    sftp_mounted = 1;
    if (out && out_max > 0) {
        lib_strcpy(out, "OK: sftp mounted at ");
        lib_strcat(out, sftp_mount_root);
    }
    return 0;
}

int ssh_client_sftp_status(char *out, int out_max)
{
    if (!out || out_max <= 0) return -1;
    out[0] = '\0';
    lib_strcat(out, "sftp: ");
    lib_strcat(out, sftp_mounted ? sftp_mount_root : "(not mounted)");
    lib_strcat(out, " target: ");
    lib_strcat(out, ssh_target[0] ? ssh_target : "(unset)");
    return 0;
}

static void sftp_mode_to_str(uint32_t perms, int is_dir, char *out, int out_sz)
{
    if (!out || out_sz < 5) return;
    out[0] = is_dir ? 'd' : '-';
    out[1] = (perms & 0400U) ? 'r' : '-';
    out[2] = (perms & 0200U) ? 'w' : '-';
    out[3] = (perms & 0100U) ? 'x' : '-';
    out[4] = '\0';
}

static void sftp_format_size_human(unsigned long bytes, char *out, int out_sz)
{
    if (!out || out_sz <= 0) return;
    if (bytes < 1024ul) {
        unsigned long v = bytes;
        char tmp[16];
        int i = 0, j = 0;
        if (v == 0) tmp[i++] = '0';
        while (v > 0 && i < (int)sizeof(tmp) - 1) {
            tmp[i++] = (char)('0' + (v % 10ul));
            v /= 10ul;
        }
        while (i > 0 && j < out_sz - 2) out[j++] = tmp[--i];
        out[j++] = 'B';
        out[j] = '\0';
    } else if (bytes < 1024ul * 1024ul) {
        unsigned long v = bytes / 1024ul;
        char tmp[16];
        int i = 0, j = 0;
        if (v == 0) tmp[i++] = '0';
        while (v > 0 && i < (int)sizeof(tmp) - 1) {
            tmp[i++] = (char)('0' + (v % 10ul));
            v /= 10ul;
        }
        while (i > 0 && j < out_sz - 2) out[j++] = tmp[--i];
        out[j++] = 'K';
        out[j] = '\0';
    } else {
        unsigned long v = bytes / (1024ul * 1024ul);
        char tmp[16];
        int i = 0, j = 0;
        if (v == 0) tmp[i++] = '0';
        while (v > 0 && i < (int)sizeof(tmp) - 1) {
            tmp[i++] = (char)('0' + (v % 10ul));
            v /= 10ul;
        }
        while (i > 0 && j < out_sz - 2) out[j++] = tmp[--i];
        out[j++] = 'M';
        out[j] = '\0';
    }
}

static void sftp_append_u2(char *out, unsigned int v)
{
    out[0] = (char)('0' + ((v / 10U) % 10U));
    out[1] = (char)('0' + (v % 10U));
}

static void sftp_append_u4(char *out, unsigned int v)
{
    out[0] = (char)('0' + ((v / 1000U) % 10U));
    out[1] = (char)('0' + ((v / 100U) % 10U));
    out[2] = (char)('0' + ((v / 10U) % 10U));
    out[3] = (char)('0' + (v % 10U));
}

static void sftp_format_utc_datetime(unsigned long total, char *out, int out_sz)
{
    unsigned long days;
    unsigned long sod;
    unsigned int hour, min, sec;
    long z, era, doe, yoe, y, doy, mp, d, m;
    unsigned int year;

    if (!out || out_sz < 20) {
        if (out && out_sz > 0) out[0] = '\0';
        return;
    }

    days = total / 86400ul;
    sod = total % 86400ul;
    hour = (unsigned int)(sod / 3600ul);
    min = (unsigned int)((sod / 60ul) % 60ul);
    sec = (unsigned int)(sod % 60ul);

    z = (long)days + 719468L;
    era = (z >= 0 ? z : z - 146096L) / 146097L;
    doe = z - era * 146097L;
    yoe = (doe - doe / 1460L + doe / 36524L - doe / 146096L) / 365L;
    y = yoe + era * 400L;
    doy = doe - (365L * yoe + yoe / 4L - yoe / 100L);
    mp = (5L * doy + 2L) / 153L;
    d = doy - (153L * mp + 2L) / 5L + 1L;
    m = mp + (mp < 10L ? 3L : -9L);
    year = (unsigned int)(y + (m <= 2L));

    sftp_append_u4(out, year);
    out[4] = '-';
    sftp_append_u2(out + 5, (unsigned int)m);
    out[7] = '-';
    sftp_append_u2(out + 8, (unsigned int)d);
    out[10] = ' ';
    sftp_append_u2(out + 11, hour);
    out[13] = ':';
    sftp_append_u2(out + 14, min);
    out[16] = ':';
    sftp_append_u2(out + 17, sec);
    out[19] = '\0';
}

static void sftp_append_str(char *out, int out_max, int *pos, const char *s)
{
    if (!out || !pos || !s || *pos >= out_max - 1) return;
    while (*s && *pos < out_max - 1) {
        out[*pos] = *s;
        (*pos)++;
        s++;
    }
    out[*pos] = '\0';
}

static void sftp_append_pad(char *out, int out_max, int *pos, const char *s, int width)
{
    int used = 0;
    if (width < 0) width = 0;
    while (s && *s && *pos < out_max - 1 && used < width) {
        out[*pos] = *s;
        (*pos)++;
        s++;
        used++;
    }
    while (*pos < out_max - 1 && used < width) {
        out[*pos] = ' ';
        (*pos)++;
        used++;
    }
    out[*pos] = '\0';
}

int ssh_client_sftp_ls(const char *remote_path, int all, char *out, int out_max)
{
    LIBSSH2_SESSION *session = NULL;
    struct ssh_transport *transport = NULL;
    LIBSSH2_SFTP *sftp = NULL;
    LIBSSH2_SFTP_HANDLE *dir = NULL;
    char full[192];
    uint32_t start;
    uint32_t read_start;
    int pos = 0;
    int rc = -1;

    if (!out || out_max <= 0) return -1;
    out[0] = '\0';
    if (!sftp_mounted) ssh_client_sftp_mount(".", NULL, 0);
    sftp_join_path(sftp_mount_root, remote_path && *remote_path ? remote_path : ".", full, sizeof(full));

    if (ssh_open_authenticated_session(&session, &transport, out, out_max) != 0) return -1;
    sftp = ssh_sftp_init_nonblock(session, out, out_max);
    if (!sftp) goto done;

    start = sys_now();
    for (;;) {
        dir = libssh2_sftp_opendir(sftp, full);
        if (dir) break;
        if (libssh2_session_last_errno(session) != LIBSSH2_ERROR_EAGAIN) {
            ssh_set_msg(out, out_max, "ERR: SFTP opendir failed.");
            goto done;
        }
        if ((uint32_t)(sys_now() - start) > SSH_EXEC_TIMEOUT_MS) {
            ssh_set_msg(out, out_max, "ERR: SFTP opendir timeout.");
            goto done;
        }
        ssh_transport_pump();
    }

    out[0] = '\0';
    read_start = sys_now();
    for (;;) {
        char name[96];
        LIBSSH2_SFTP_ATTRIBUTES attrs;
        int n = libssh2_sftp_readdir(dir, name, sizeof(name) - 1, &attrs);
        if (n == LIBSSH2_ERROR_EAGAIN) {
            if ((uint32_t)(sys_now() - read_start) > SSH_EXEC_TIMEOUT_MS) {
                ssh_set_msg(out, out_max, "ERR: SFTP readdir timeout.");
                goto done;
            }
            ssh_transport_pump();
            continue;
        }
        if (n <= 0) break;
        read_start = sys_now();
        name[n] = '\0';
        if (!all && ((name[0] == '.' && name[1] == '\0') ||
                     (name[0] == '.' && name[1] == '.' && name[2] == '\0'))) {
            continue;
        }
        {
            int is_dir = ((attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) &&
                          LIBSSH2_SFTP_S_ISDIR(attrs.permissions));
            int wrote = 0;
            if (all) {
                char mode[8];
                char sz[16];
                char ts[24];
                char kind[2];
                unsigned long mtime = (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) ? attrs.mtime : 0ul;
                unsigned long fsize = (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) ? (unsigned long)attrs.filesize : 0ul;
                sftp_mode_to_str(attrs.permissions, is_dir, mode, sizeof(mode));
                sftp_format_size_human(fsize, sz, sizeof(sz));
                sftp_format_utc_datetime(mtime, ts, sizeof(ts));
                kind[0] = is_dir ? 'd' : 'f';
                kind[1] = '\0';
                sftp_append_pad(out, out_max, &pos, mode, 5);
                sftp_append_str(out, out_max, &pos, " ");
                sftp_append_pad(out, out_max, &pos, sz, 8);
                sftp_append_str(out, out_max, &pos, " ");
                sftp_append_pad(out, out_max, &pos, ts, 19);
                sftp_append_str(out, out_max, &pos, " ");
                sftp_append_str(out, out_max, &pos, kind);
                sftp_append_str(out, out_max, &pos, " ");
                sftp_append_str(out, out_max, &pos, name);
                sftp_append_str(out, out_max, &pos, "\n");
                wrote = 1;
            } else {
                wrote = snprintf(out + pos, out_max - pos, "%c %s\n", is_dir ? 'd' : 'f', name);
            }
            if (wrote < 0) break;
            if (!all && wrote >= out_max - pos) {
                pos = out_max - 1;
                out[pos] = '\0';
                break;
            }
            if (!all) pos += wrote;
        }
    }
    if (out[0] == '\0') lib_strcpy(out, "(empty)");
    rc = 0;

done:
    if (dir) while (libssh2_sftp_closedir(dir) == LIBSSH2_ERROR_EAGAIN) ssh_transport_pump();
    ssh_sftp_shutdown_nonblock(sftp);
    ssh_close_authenticated_session(session, transport);
    return rc;
}

int ssh_client_sftp_get(struct Window *w, const char *remote_path, const char *local_path, char *out, int out_max)
{
    LIBSSH2_SESSION *session = NULL;
    struct ssh_transport *transport = NULL;
    LIBSSH2_SFTP *sftp = NULL;
    LIBSSH2_SFTP_HANDLE *fh = NULL;
    unsigned char *buf = NULL;
    uint32_t cap = 4096, len = 0;
    char full[192], local[32];
    int rc = -1;

    if (!out || out_max <= 0) return -1;
    out[0] = '\0';
    if (!remote_path || !*remote_path) {
        ssh_set_msg(out, out_max, "ERR: sftp get <remote> [local].");
        return -1;
    }
    if (!sftp_mounted) ssh_client_sftp_mount(".", NULL, 0);
    sftp_join_path(sftp_mount_root, remote_path, full, sizeof(full));
    if (local_path && *local_path) copy_name20(local, local_path);
    else if (sftp_basename_to_local(remote_path, local, sizeof(local)) != 0) {
        ssh_set_msg(out, out_max, "ERR: bad local filename.");
        return -1;
    }

    buf = (unsigned char *)malloc(cap);
    if (!buf) {
        ssh_set_msg(out, out_max, "ERR: no memory.");
        return -1;
    }
    if (ssh_open_authenticated_session(&session, &transport, out, out_max) != 0) goto done;
    sftp = ssh_sftp_init_nonblock(session, out, out_max);
    if (!sftp) goto done;

    for (;;) {
        fh = libssh2_sftp_open(sftp, full, LIBSSH2_FXF_READ, 0);
        if (fh) break;
        if (libssh2_session_last_errno(session) != LIBSSH2_ERROR_EAGAIN) {
            ssh_set_msg(out, out_max, "ERR: SFTP open failed.");
            goto done;
        }
        ssh_transport_pump();
    }

    for (;;) {
        char tmp[1024];
        ssize_t n = libssh2_sftp_read(fh, tmp, sizeof(tmp));
        if (n == LIBSSH2_ERROR_EAGAIN) {
            ssh_transport_pump();
            continue;
        }
        if (n < 0) {
            ssh_set_msg(out, out_max, "ERR: SFTP read failed.");
            goto done;
        }
        if (n == 0) break;
        if (len + (uint32_t)n > WGET_MAX_FILE_SIZE) {
            ssh_set_msg(out, out_max, "ERR: file too large.");
            goto done;
        }
        if (len + (uint32_t)n > cap) {
            uint32_t ncap = cap * 2;
            unsigned char *nbuf;
            while (ncap < len + (uint32_t)n) ncap *= 2;
            nbuf = (unsigned char *)realloc(buf, ncap);
            if (!nbuf) {
                ssh_set_msg(out, out_max, "ERR: no memory.");
                goto done;
            }
            buf = nbuf;
            cap = ncap;
        }
        memcpy(buf + len, tmp, (uint32_t)n);
        len += (uint32_t)n;
    }

    if (store_file_bytes(w, local, buf, len) != 0) {
        ssh_set_msg(out, out_max, "ERR: local save failed.");
        goto done;
    }
    snprintf(out, out_max, "OK: sftp get %s -> %s (%u bytes)", full, local, len);
    rc = 0;

done:
    if (fh) while (libssh2_sftp_close(fh) == LIBSSH2_ERROR_EAGAIN) ssh_transport_pump();
    ssh_sftp_shutdown_nonblock(sftp);
    ssh_close_authenticated_session(session, transport);
    if (buf) free(buf);
    return rc;
}

int ssh_client_sftp_read_alloc(const char *remote_path, unsigned char **data, uint32_t *size, char *out, int out_max)
{
    LIBSSH2_SESSION *session = NULL;
    struct ssh_transport *transport = NULL;
    LIBSSH2_SFTP *sftp = NULL;
    LIBSSH2_SFTP_HANDLE *fh = NULL;
    unsigned char *buf = NULL;
    uint32_t cap = 4096, len = 0;
    char full[192];
    int rc = -1;

    if (data) *data = NULL;
    if (size) *size = 0;
    if (!data || !size) return -1;
    if (out && out_max > 0) out[0] = '\0';
    if (!remote_path || !*remote_path) {
        ssh_set_msg(out, out_max, "ERR: sftp read <remote>.");
        return -1;
    }
    if (!sftp_mounted) ssh_client_sftp_mount(".", NULL, 0);
    sftp_join_path(sftp_mount_root, remote_path, full, sizeof(full));

    buf = (unsigned char *)malloc(cap);
    if (!buf) {
        ssh_set_msg(out, out_max, "ERR: no memory.");
        return -1;
    }
    if (ssh_open_authenticated_session(&session, &transport, out, out_max) != 0) goto done;
    sftp = ssh_sftp_init_nonblock(session, out, out_max);
    if (!sftp) goto done;

    for (;;) {
        fh = libssh2_sftp_open(sftp, full, LIBSSH2_FXF_READ, 0);
        if (fh) break;
        if (libssh2_session_last_errno(session) != LIBSSH2_ERROR_EAGAIN) {
            ssh_set_msg(out, out_max, "ERR: SFTP open failed.");
            goto done;
        }
        ssh_transport_pump();
    }

    for (;;) {
        char tmp[1024];
        ssize_t n = libssh2_sftp_read(fh, tmp, sizeof(tmp));
        if (n == LIBSSH2_ERROR_EAGAIN) {
            ssh_transport_pump();
            continue;
        }
        if (n < 0) {
            ssh_set_msg(out, out_max, "ERR: SFTP read failed.");
            goto done;
        }
        if (n == 0) break;
        if (len + (uint32_t)n > WGET_MAX_FILE_SIZE) {
            ssh_set_msg(out, out_max, "ERR: file too large.");
            goto done;
        }
        if (len + (uint32_t)n > cap) {
            uint32_t ncap = cap * 2;
            unsigned char *nbuf;
            while (ncap < len + (uint32_t)n) ncap *= 2;
            nbuf = (unsigned char *)realloc(buf, ncap);
            if (!nbuf) {
                ssh_set_msg(out, out_max, "ERR: no memory.");
                goto done;
            }
            buf = nbuf;
            cap = ncap;
        }
        memcpy(buf + len, tmp, (uint32_t)n);
        len += (uint32_t)n;
    }

    *data = buf;
    *size = len;
    buf = NULL;
    rc = 0;

done:
    if (fh) while (libssh2_sftp_close(fh) == LIBSSH2_ERROR_EAGAIN) ssh_transport_pump();
    ssh_sftp_shutdown_nonblock(sftp);
    ssh_close_authenticated_session(session, transport);
    if (buf) free(buf);
    return rc;
}

int ssh_client_sftp_put(struct Window *w, const char *local_path, const char *remote_path, char *out, int out_max)
{
    LIBSSH2_SESSION *session = NULL;
    struct ssh_transport *transport = NULL;
    LIBSSH2_SFTP *sftp = NULL;
    LIBSSH2_SFTP_HANDLE *fh = NULL;
    unsigned char *buf = NULL;
    uint32_t size = 0, off = 0;
    char full[192];
    int rc = -1;

    if (!out || out_max <= 0) return -1;
    out[0] = '\0';
    if (!local_path || !*local_path) {
        ssh_set_msg(out, out_max, "ERR: sftp put <local> [remote].");
        return -1;
    }
    if (load_file_bytes_alloc(w, local_path, &buf, &size) != 0 || !buf) {
        ssh_set_msg(out, out_max, "ERR: local read failed.");
        return -1;
    }
    if (!sftp_mounted) ssh_client_sftp_mount(".", NULL, 0);
    sftp_join_path(sftp_mount_root, remote_path && *remote_path ? remote_path : local_path, full, sizeof(full));

    if (ssh_open_authenticated_session(&session, &transport, out, out_max) != 0) goto done;
    sftp = ssh_sftp_init_nonblock(session, out, out_max);
    if (!sftp) goto done;

    for (;;) {
        fh = libssh2_sftp_open(sftp, full,
                               LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
                               LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR |
                               LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);
        if (fh) break;
        if (libssh2_session_last_errno(session) != LIBSSH2_ERROR_EAGAIN) {
            ssh_set_msg(out, out_max, "ERR: SFTP create failed.");
            goto done;
        }
        ssh_transport_pump();
    }

    while (off < size) {
        uint32_t chunk = size - off;
        ssize_t n;
        if (chunk > 1024) chunk = 1024;
        n = libssh2_sftp_write(fh, (const char *)buf + off, chunk);
        if (n == LIBSSH2_ERROR_EAGAIN) {
            ssh_transport_pump();
            continue;
        }
        if (n <= 0) {
            ssh_set_msg(out, out_max, "ERR: SFTP write failed.");
            goto done;
        }
        off += (uint32_t)n;
    }

    snprintf(out, out_max, "OK: sftp put %s -> %s (%u bytes)", local_path, full, size);
    rc = 0;

done:
    if (fh) while (libssh2_sftp_close(fh) == LIBSSH2_ERROR_EAGAIN) ssh_transport_pump();
    ssh_sftp_shutdown_nonblock(sftp);
    ssh_close_authenticated_session(session, transport);
    if (buf) free(buf);
    return rc;
}

int ssh_client_sftp_write_bytes(const char *remote_path, const unsigned char *data, uint32_t size, char *out, int out_max)
{
    LIBSSH2_SESSION *session = NULL;
    struct ssh_transport *transport = NULL;
    LIBSSH2_SFTP *sftp = NULL;
    LIBSSH2_SFTP_HANDLE *fh = NULL;
    uint32_t off = 0;
    char full[192];
    int rc = -1;

    if (!out || out_max <= 0) return -1;
    out[0] = '\0';
    if (!remote_path || !*remote_path) {
        ssh_set_msg(out, out_max, "ERR: bad remote path.");
        return -1;
    }
    if (!data && size) {
        ssh_set_msg(out, out_max, "ERR: no data.");
        return -1;
    }
    if (!sftp_mounted) ssh_client_sftp_mount(".", NULL, 0);
    sftp_join_path(sftp_mount_root, remote_path, full, sizeof(full));

    if (ssh_open_authenticated_session(&session, &transport, out, out_max) != 0) goto done;
    sftp = ssh_sftp_init_nonblock(session, out, out_max);
    if (!sftp) goto done;

    for (;;) {
        fh = libssh2_sftp_open(sftp, full,
                               LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
                               LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR |
                               LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);
        if (fh) break;
        if (libssh2_session_last_errno(session) != LIBSSH2_ERROR_EAGAIN) {
            ssh_set_msg(out, out_max, "ERR: SFTP create failed.");
            goto done;
        }
        ssh_transport_pump();
    }

    while (off < size) {
        uint32_t chunk = size - off;
        ssize_t n;
        if (chunk > 1024) chunk = 1024;
        n = libssh2_sftp_write(fh, (const char *)data + off, chunk);
        if (n == LIBSSH2_ERROR_EAGAIN) {
            ssh_transport_pump();
            continue;
        }
        if (n <= 0) {
            ssh_set_msg(out, out_max, "ERR: SFTP write failed.");
            goto done;
        }
        off += (uint32_t)n;
    }

    snprintf(out, out_max, "OK: sftp write %s (%u bytes)", full, size);
    rc = 0;

done:
    if (fh) while (libssh2_sftp_close(fh) == LIBSSH2_ERROR_EAGAIN) ssh_transport_pump();
    ssh_sftp_shutdown_nonblock(sftp);
    ssh_close_authenticated_session(session, transport);
    return rc;
}

int ssh_client_sftp_unlink(const char *remote_path, char *out, int out_max)
{
    LIBSSH2_SESSION *session = NULL;
    struct ssh_transport *transport = NULL;
    LIBSSH2_SFTP *sftp = NULL;
    char full[192];
    int rc = -1;

    if (!out || out_max <= 0) return -1;
    out[0] = '\0';
    if (!remote_path || !*remote_path) {
        ssh_set_msg(out, out_max, "ERR: bad remote path.");
        return -1;
    }
    if (!sftp_mounted) ssh_client_sftp_mount(".", NULL, 0);
    sftp_join_path(sftp_mount_root, remote_path, full, sizeof(full));

    if (ssh_open_authenticated_session(&session, &transport, out, out_max) != 0) goto done;
    sftp = ssh_sftp_init_nonblock(session, out, out_max);
    if (!sftp) goto done;

    for (;;) {
        int rr = libssh2_sftp_unlink(sftp, full);
        if (rr == 0) {
            snprintf(out, out_max, "OK: sftp rm %s", full);
            rc = 0;
            break;
        }
        if (rr == LIBSSH2_ERROR_EAGAIN) {
            ssh_transport_pump();
            continue;
        }
        ssh_set_msg(out, out_max, "ERR: SFTP unlink failed.");
        break;
    }

done:
    ssh_sftp_shutdown_nonblock(sftp);
    ssh_close_authenticated_session(session, transport);
    return rc;
}

int ssh_client_sftp_rename(const char *src_path, const char *dst_path, char *out, int out_max)
{
    LIBSSH2_SESSION *session = NULL;
    struct ssh_transport *transport = NULL;
    LIBSSH2_SFTP *sftp = NULL;
    char src_full[192], dst_full[192];
    int rc = -1;

    if (!out || out_max <= 0) return -1;
    out[0] = '\0';
    if (!src_path || !*src_path || !dst_path || !*dst_path) {
        ssh_set_msg(out, out_max, "ERR: bad remote path.");
        return -1;
    }
    if (!sftp_mounted) ssh_client_sftp_mount(".", NULL, 0);
    sftp_join_path(sftp_mount_root, src_path, src_full, sizeof(src_full));
    sftp_join_path(sftp_mount_root, dst_path, dst_full, sizeof(dst_full));

    if (ssh_open_authenticated_session(&session, &transport, out, out_max) != 0) goto done;
    sftp = ssh_sftp_init_nonblock(session, out, out_max);
    if (!sftp) goto done;

    for (;;) {
        int rr = libssh2_sftp_rename(sftp, src_full, dst_full);
        if (rr == 0) {
            snprintf(out, out_max, "OK: sftp mv %s -> %s", src_full, dst_full);
            rc = 0;
            break;
        }
        if (rr == LIBSSH2_ERROR_EAGAIN) {
            ssh_transport_pump();
            continue;
        }
        ssh_set_msg(out, out_max, "ERR: SFTP rename failed.");
        break;
    }

done:
    ssh_sftp_shutdown_nonblock(sftp);
    ssh_close_authenticated_session(session, transport);
    return rc;
}

int ssh_client_exec_remote(const char *cmd, char *out, int out_max)
{
    LIBSSH2_SESSION *session = NULL;
    struct ssh_transport *transport = NULL;
    int rc;
    void **abstract_slot;

    if (!out || out_max <= 0) return -1;
    if (ssh_target[0] == '\0' || ssh_host[0] == '\0') {
        ssh_set_msg(out, out_max, "ERR: ssh target not set.");
        return -1;
    }
    if (!ssh_password_set) {
        ssh_set_msg(out, out_max, "ERR: ssh auth <password> first.");
        return -1;
    }
    if (!cmd || !*cmd) {
        ssh_set_msg(out, out_max, "ERR: ssh exec <command>.");
        return -1;
    }

    ssh_log_build_stamp();
    lib_printf("[SSH] exec begin target='%s' cmd='%s'\n", ssh_target, cmd);
    if (ssh_runtime_init(out, out_max) != 0) return -1;

    rc = ssh_connect_transport(&transport, ssh_host, ssh_port, out, out_max);
    if (rc != 0) {
        ssh_runtime_release();
        return -1;
    }

    session = libssh2_session_init_ex(NULL, NULL, NULL, NULL);
    if (!session) {
        ssh_transport_destroy(transport);
        ssh_runtime_release();
        ssh_set_msg(out, out_max, "ERR: SSH session alloc failed.");
        return -1;
    }
    ssh_log_ptr("session", session);

    libssh2_session_set_blocking(session, 0);
    abstract_slot = libssh2_session_abstract(session);
    *abstract_slot = transport;
    ssh_log_ptr("abstract_slot", abstract_slot);
    ssh_log_ptr("transport", transport);
    libssh2_session_callback_set2(session, LIBSSH2_CALLBACK_SEND, (libssh2_cb_generic *)ssh_libssh2_send);
    libssh2_session_callback_set2(session, LIBSSH2_CALLBACK_RECV, (libssh2_cb_generic *)ssh_libssh2_recv);
    if (ssh_configure_session_methods(session) != 0) {
        ssh_set_msg(out, out_max, "ERR: SSH method pref failed.");
        libssh2_session_free(session);
        ssh_transport_destroy(transport);
        ssh_runtime_release();
        return -1;
    }

    rc = ssh_session_handshake_nonblock(session, out, out_max);
    if (rc != 0) {
        ssh_log("handshake failed");
        libssh2_session_free(session);
        ssh_transport_destroy(transport);
        ssh_runtime_release();
        return -1;
    }

    rc = ssh_userauth_password_nonblock(session, ssh_user[0] ? ssh_user : "root", ssh_password, out, out_max);
    if (rc != 0) {
        ssh_log("auth failed");
        libssh2_session_disconnect_ex(session, SSH_DISCONNECT_BY_APPLICATION, "auth failed", "");
        libssh2_session_free(session);
        ssh_transport_destroy(transport);
        ssh_runtime_release();
        return -1;
    }

    rc = ssh_channel_exec_nonblock(session, cmd, out, out_max);
    lib_printf("[SSH] exec rc=%d\n", rc);

    {
        uint32_t close_start = sys_now();
        for (;;) {
            int fr = libssh2_session_disconnect_ex(session, SSH_DISCONNECT_BY_APPLICATION, "bye", "");
            lib_printf("[SSH] disconnect rc=%d\n", fr);
            if (fr == LIBSSH2_ERROR_EAGAIN) {
                if ((uint32_t)(sys_now() - close_start) > 1000U) break;
                ssh_transport_pump();
                continue;
            }
            break;
        }
    }
    libssh2_session_free(session);
    ssh_transport_destroy(transport);
    ssh_runtime_release();
    return rc;
}
