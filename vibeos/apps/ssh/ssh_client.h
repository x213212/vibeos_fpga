#ifndef SSH_CLIENT_H
#define SSH_CLIENT_H

void ssh_client_reset(void);
int ssh_client_set_target(const char *spec, char *out, int out_max);
int ssh_client_set_password(const char *password, char *out, int out_max);
int ssh_client_set_wrp_url(const char *url, char *out, int out_max);
int ssh_client_get_target(char *out, int out_max);
int ssh_client_has_password(void);
int ssh_client_get_wrp_url(char *out, int out_max);
int ssh_client_probe(const char *spec, char *out, int out_max);
int ssh_client_banner(const char *spec, char *out, int out_max);
int ssh_client_diag(char *out, int out_max);
int ssh_client_exec_remote(const char *cmd, char *out, int out_max);
int ssh_client_sftp_mount(const char *remote_root, char *out, int out_max);
int ssh_client_sftp_status(char *out, int out_max);
int ssh_client_sftp_ls(const char *remote_path, int all, char *out, int out_max);
int ssh_client_sftp_get(struct Window *w, const char *remote_path, const char *local_path, char *out, int out_max);
int ssh_client_sftp_put(struct Window *w, const char *local_path, const char *remote_path, char *out, int out_max);
int ssh_client_sftp_read_alloc(const char *remote_path, unsigned char **data, uint32_t *size, char *out, int out_max);
int ssh_client_sftp_write_bytes(const char *remote_path, const unsigned char *data, uint32_t size, char *out, int out_max);
int ssh_client_sftp_unlink(const char *remote_path, char *out, int out_max);
int ssh_client_sftp_rename(const char *src_path, const char *dst_path, char *out, int out_max);

#endif
