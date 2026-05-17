#include "user_internal.h"
#include <stdarg.h>

void lib_strcpy(char *dst, const char *src) { if(!dst || !src) return; while ((*dst++ = *src++)); }
void lib_strcat(char *dst, const char *src) { if(!dst || !src) return; while (*dst) dst++; while ((*dst++ = *src++)); }
void lib_strncat(char *dst, const char *src, int n) { if(!dst || !src) return; while (*dst) dst++; for (int i = 0; i < n && src[i]; i++) { if (src[i] >= 32 && src[i] <= 126) *dst++ = src[i]; else break; } *dst = '\0'; }
void lib_itoa(uint32_t n, char *s) { char tmp[12]; int i = 0; if (n == 0) tmp[i++] = '0'; while (n > 0) { tmp[i++] = (n % 10) + '0'; n /= 10; } int j = 0; while (i > 0) s[j++] = tmp[--i]; s[j] = '\0'; }

char* strcpy(char *dst, const char *src) { lib_strcpy(dst, src); return dst; }
char* strcat(char *dst, const char *src) { lib_strcat(dst, src); return dst; }
int strcmp(const char *s1, const char *s2) { while (*s1 && (*s1 == *s2)) { s1++; s2++; } return *(unsigned char *)s1 - *(unsigned char *)s2; }
uint32_t strlen(const char *s) { uint32_t n = 0; if(!s) return 0; while (*s++) n++; return n; }
char* strchr(const char *s, int c) {
    char ch = (char)c;
    while (*s) {
        if (*s == ch) return (char *)s;
        s++;
    }
    return ch == '\0' ? (char *)s : 0;
}
void* memchr(const void *s, int c, uint32_t n) { const unsigned char *p = (const unsigned char *)s; while (n--) { if (*p == (unsigned char)c) return (void *)p; p++; } return 0; }
int memcmp(const void *a, const void *b, size_t n) { const unsigned char *pa = (const unsigned char *)a; const unsigned char *pb = (const unsigned char *)b; while (n--) { if (*pa != *pb) return (int)*pa - (int)*pb; pa++; pb++; } return 0; }
char* strncpy(char *dest, const char *src, uint32_t n) { uint32_t i = 0; if(!dest || !src) return dest; for (; i < n && src[i]; i++) dest[i] = src[i]; for (; i < n; i++) dest[i] = '\0'; return dest; }
int strncmp(const char *s1, const char *s2, uint32_t n) { while (n--) { unsigned char c1 = (unsigned char)*s1++; unsigned char c2 = (unsigned char)*s2++; if (c1 != c2 || c1 == '\0') return (int)c1 - (int)c2; } return 0; }
int atoi(const char *nptr) { int sign = 1; int v = 0; if (!nptr) return 0; while (*nptr == ' ' || *nptr == '\t') nptr++; if (*nptr == '-') { sign = -1; nptr++; } else if (*nptr == '+') { nptr++; } while (*nptr >= '0' && *nptr <= '9') { v = v * 10 + (*nptr - '0'); nptr++; } return v * sign; }
char *strdup(const char *s) { if (!s) return 0; size_t n = strlen(s) + 1; char *p = (char *)malloc((uint32_t)n); if (!p) return 0; memcpy(p, s, n); return p; }
char* strstr(const char* haystack, const char* needle) { if (!*needle) return (char*)haystack; for (; *haystack; haystack++) { if (*haystack != *needle) continue; const char *h = haystack, *n = needle; while (*h && *n && *h == *n) { h++; n++; } if (!*n) return (char*)haystack; } return 0; }
int strncasecmp(const char *s1, const char *s2, uint32_t n) { while (n--) { unsigned char c1 = (unsigned char)tolower((int)*s1++); unsigned char c2 = (unsigned char)tolower((int)*s2++); if (c1 != c2 || c1 == '\0') return (int)c1 - (int)c2; } return 0; }
int tolower(int c) { if (c >= 'A' && c <= 'Z') return c + ('a' - 'A'); return c; }
int abs(int n) { return (n < 0) ? -n : n; }

static int ns_is_unreserved(char ch) {
    return (ch >= 'A' && ch <= 'Z') ||
           (ch >= 'a' && ch <= 'z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '-' || ch == '_' || ch == '.' || ch == '~';
}

static int ns_trimmed_len(const char *s) {
    int n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    while (n > 0 && s[n - 1] == '/') n--;
    return n;
}

static int ns_url_is_wrp_base(const char *url, const char *base) {
    int ul, bl;

    if (!url || !base) return 0;
    ul = ns_trimmed_len(url);
    bl = ns_trimmed_len(base);
    if (ul <= 0 || bl <= 0) return 0;
    if (ul < bl) return 0;
    if (strncmp(url, base, (uint32_t)bl) != 0) return 0;
    if (ul == bl) return 1;
    return (url[bl] == '/' || url[bl] == '?' || url[bl] == '#' || url[bl] == '&');
}

static int ns_append_char(char *out, int out_max, int *pos, char ch) {
    if (!out || !pos || out_max <= 0) return -1;
    if (*pos >= out_max - 1) return -1;
    out[*pos] = ch;
    (*pos)++;
    out[*pos] = '\0';
    return 0;
}

static int ns_append_str(char *out, int out_max, int *pos, const char *s) {
    if (!out || !pos || !s) return -1;
    while (*s) {
        if (ns_append_char(out, out_max, pos, *s++) < 0) return -1;
    }
    return 0;
}

static int ns_hex_val(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
    if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
    return -1;
}

static void ns_percent_decode(const char *src, char *dst, int dst_max) {
    int i = 0;
    int j = 0;
    if (!dst || dst_max <= 0) return;
    dst[0] = '\0';
    if (!src) return;
    while (src[i] && j < dst_max - 1) {
        if (src[i] == '%' && src[i + 1] && src[i + 2]) {
            int hi = ns_hex_val(src[i + 1]);
            int lo = ns_hex_val(src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                dst[j++] = (char)((hi << 4) | lo);
                i += 3;
                continue;
            }
        }
        if (src[i] == '+') {
            dst[j++] = ' ';
            i++;
            continue;
        }
        dst[j++] = src[i++];
    }
    dst[j] = '\0';
}

void netsurf_normalize_target_url(const char *src, char *dst, int dst_max) {
    const char *p = src;
    const char *q;
    char encoded[256];
    int i = 0;

    if (!dst || dst_max <= 0) return;
    dst[0] = '\0';
    if (!src || !*src) return;

    // 關鍵修復：如果是內部 WRP 或 Map 請求，絕對不要重新標準化
    if (strstr(src, "m=ismap") || strstr(src, "/map/") || strstr(src, "/img/")) {
        int slen = strlen(src);
        if (slen > dst_max - 1) slen = dst_max - 1;
        memcpy(dst, src, slen);
        dst[slen] = '\0';
        return;
    }

    while (*p == ' ' || *p == '\t') p++;
    q = strstr(p, "url=");
    if (q) {
        q += 4;
        while (*q == ' ' || *q == '\t') q++;
        while (*q && *q != '&' && *q != '\n' && *q != '\r' && i < (int)sizeof(encoded) - 1) {
            encoded[i++] = *q++;
        }
        encoded[i] = '\0';
        ns_percent_decode(encoded, dst, dst_max);
        return;
    }
    
    // 移除開頭的亂碼或非 URL 字元
    for (; *p; p++) {
        if (strncmp(p, "http://", 7) == 0 || strncmp(p, "https://", 8) == 0) break;
    }
    if (!*p) p = src;

    while (*p == ' ' || *p == '\t') p++;
    while (*p && *p != '\n' && *p != '\r' && *p != '&' && i < dst_max - 1) {
        // 只允許合法字元
        if ((unsigned char)*p >= 32 && (unsigned char)*p < 128) {
            dst[i++] = *p;
        }
        p++;
    }
    dst[i] = '\0';
}

static const char *ns_wrp_mode_query(void) {
    return "m=html&url=";
}

static int ns_append_encoded(char *out, int out_max, int *pos, const char *s) {
    int i;
    for (i = 0; s && s[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ns_is_unreserved((char)ch)) {
            if (ns_append_char(out, out_max, pos, (char)ch) < 0) return -1;
        } else {
            static const char hex[] = "0123456789ABCDEF";
            if (ns_append_char(out, out_max, pos, '%') < 0) return -1;
            if (ns_append_char(out, out_max, pos, hex[(ch >> 4) & 0xF]) < 0) return -1;
            if (ns_append_char(out, out_max, pos, hex[ch & 0xF]) < 0) return -1;
        }
    }
    return 0;
}

static uint16_t ns_read_le16(const unsigned char *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static int ns_gif_skip_subblocks(const unsigned char *buf, uint32_t size, uint32_t *pos) {
    while (*pos < size) {
        uint8_t n = buf[(*pos)++];
        if (n == 0) return 0;
        if (*pos + n > size) return -1;
        *pos += n;
    }
    return -1;
}

static int ns_gif_expand_code(int code, int clear_code, uint16_t *prefix, uint8_t *suffix, uint8_t *rev, int *first_char) {
    int sp = 0;
    int cur = code;

    if (code < clear_code) {
        rev[sp++] = (uint8_t)code;
        if (first_char) *first_char = code;
        return sp;
    }
    while (cur >= clear_code && cur < 4096 && prefix[cur] != 0xFFFF) {
        if (sp >= 4095) return -1;
        rev[sp++] = suffix[cur];
        cur = prefix[cur];
    }
    if (sp >= 4095) return -1;
    rev[sp++] = (uint8_t)cur;
    if (first_char) *first_char = cur;
    return sp;
}

static int ns_gif_first_char(int code, int clear_code, const uint16_t *prefix) {
    int cur = code;
    while (cur >= clear_code && cur < 4096) {
        cur = prefix[cur];
    }
    return cur;
}

typedef struct {
    const uint8_t *buf;
    uint32_t size;
    uint32_t pos;
    uint8_t block_rem;
    uint32_t acc;
    int bits;
} ns_gif_bitstream;

static int ns_gif_bitstream_read(ns_gif_bitstream *s, int n) {
    while (s->bits < n) {
        if (s->block_rem == 0) {
            if (s->pos >= s->size) break;
            s->block_rem = s->buf[s->pos++];
            if (s->block_rem == 0) break;
        }
        if (s->pos >= s->size) break;
        s->acc |= ((uint32_t)s->buf[s->pos++]) << s->bits;
        s->bits += 8;
        s->block_rem--;
    }
    int code = (int)(s->acc & ((1u << n) - 1u));
    s->acc >>= n;
    s->bits -= n;
    return code;
}

static int ns_gif_decode_to_rgb565(const unsigned char *buf, uint32_t size, uint16_t *dst, int dst_max_w, int dst_max_h, int *out_w, int *out_h) {
    uint16_t canvas_w, canvas_h;
    uint8_t packed, bg_index;
    uint32_t pos = 0;
    uint16_t gpal[256];
    int gpal_count = 0;
    uint16_t bg_color = 0xFFFF; // Default to White
    int trans_idx = -1;

    if (!buf || !dst || !out_w || !out_h || size < 13) return -1;
    if (memcmp(buf, "GIF87a", 6) != 0 && memcmp(buf, "GIF89a", 6) != 0) return -1;

    canvas_w = ns_read_le16(buf + 6);
    canvas_h = ns_read_le16(buf + 8);
    packed = buf[10]; bg_index = buf[11]; pos = 13;

    if (packed & 0x80) {
        gpal_count = 1 << ((packed & 0x07) + 1);
        if (packed == 0x88 && pos + 256U * 3U <= size) {
            gpal_count = 256;
        }
        if (pos + (uint32_t)gpal_count * 3U > size) return -1;
        for (int i = 0; i < gpal_count; i++) {
            // Swap R and B to fix pinkish-yellow distortion
            gpal[i] = rgb565_from_rgb(buf[pos + 2], buf[pos + 1], buf[pos]);
            pos += 3;
        }
        if (bg_index < (uint8_t)gpal_count) bg_color = gpal[bg_index];
    }

    // 不要在此處 memset 整個 dst，因為它可能很大且由調用者管理
    // 我們只在渲染時寫入必要的像素

    while (pos < size) {
        uint8_t block = buf[pos++];
        if (block == 0x3B) break; // EOF
        if (block == 0x21) { // Extension
            if (pos >= size) return -1;
            uint8_t label = buf[pos++];
            if (label == 0xF9) { // Graphics Control Extension
                if (pos + 1 > size) return -1;
                uint8_t gce_size = buf[pos++];
                if (pos + gce_size > size) return -1;
                uint8_t gce_packed = buf[pos++];
                pos += 2; // skip delay
                if (gce_packed & 0x01) trans_idx = (int)buf[pos++];
                else pos++;
                pos++; // skip block terminator
            } else {
                if (ns_gif_skip_subblocks(buf, size, &pos) < 0) return -1;
            }
            continue;
        }
        if (block != 0x2C) {
             // If we get unexpected block, try to find next image descriptor or terminator
             continue; 
        }
        if (pos + 9 > size) return -1;

        uint16_t left = ns_read_le16(buf + pos);
        uint16_t top = ns_read_le16(buf + pos + 2);
        uint16_t frame_w = ns_read_le16(buf + pos + 4);
        uint16_t frame_h = ns_read_le16(buf + pos + 6);
        uint8_t img_packed = buf[pos + 8];
        pos += 9;

        uint16_t *pal = gpal;
        int pal_count = gpal_count;
        uint16_t lpal[256];
        if (img_packed & 0x80) {
            pal_count = 1 << ((img_packed & 0x07) + 1);
            if (pos + (uint32_t)pal_count * 3U > size) return -1;
            for (int i = 0; i < pal_count; i++) {
                // Swap R and B here too
                lpal[i] = rgb565_from_rgb(buf[pos + 2], buf[pos + 1], buf[pos]);
                pos += 3;
            }
            pal = lpal;
        }

        if (pos >= size) return -1;
        int lzw_min = buf[pos++];
        ns_gif_bitstream stream = { buf, size, pos, 0, 0, 0 };

        static uint16_t prefix[4096];
        static uint8_t suffix[4096];
        static uint8_t stack[4096];

        int clear_code = 1 << lzw_min;
        int end_code = clear_code + 1;
        int code_size = lzw_min + 1;
        int next_code = end_code + 1;
        int old_code = -1;
        int pixel_count = (int)frame_w * (int)frame_h;
        int current_pixel = 0;

        int interlace = (img_packed & 0x40);
        int pass = 0, pass_y = 0, pass_step = 8;
        int out_x = left;
        int out_y = top;

        while (current_pixel < pixel_count) {
            int code = ns_gif_bitstream_read(&stream, code_size);
            if (code == end_code || code < 0) break;
            if (code == clear_code) {
                code_size = lzw_min + 1;
                next_code = end_code + 1;
                old_code = -1;
                continue;
            }

            int sp = 0;
            if (old_code == -1) {
                if (code < clear_code) {
                    stack[sp++] = (uint8_t)code;
                    old_code = code;
                } else break;
            } else {
                int curr = code;
                if (code >= next_code) {
                    stack[sp++] = (uint8_t)ns_gif_first_char(old_code, clear_code, prefix);
                    curr = old_code;
                }
                while (curr >= clear_code && curr < 4096) {
                    stack[sp++] = suffix[curr];
                    curr = prefix[curr];
                }
                stack[sp++] = (uint8_t)curr;

                if (next_code < 4096) {
                    prefix[next_code] = (uint16_t)old_code;
                    suffix[next_code] = (uint8_t)curr;
                    next_code++;
                    if (next_code == (1 << code_size) && code_size < 12) code_size++;
                }
                old_code = code;
            }

            while (sp > 0) {
                uint8_t idx = stack[--sp];
                int x, y;
                if (interlace) {
                    x = (current_pixel % frame_w) + left;
                    y = pass_y + top;
                } else {
                    x = out_x;
                    y = out_y;
                    out_x++;
                    if (out_x >= (int)left + (int)frame_w) {
                        out_x = left;
                        out_y++;
                    }
                }

                if (x >= 0 && x < dst_max_w && y >= 0 && y < dst_max_h && idx < pal_count) {
                    if ((int)idx != trans_idx) {
                        dst[y * dst_max_w + x] = pal[idx];
                    }
                }
                current_pixel++;
                if (interlace && current_pixel < pixel_count && (current_pixel % frame_w == 0)) {
                    pass_y += pass_step;
                    while (pass_y >= frame_h && pass < 3) {
                        pass++;
                        switch (pass) {
                            case 1: pass_y = 4; pass_step = 8; break;
                            case 2: pass_y = 2; pass_step = 4; break;
                            case 3: pass_y = 1; pass_step = 2; break;
                        }
                    }
                }
                if (current_pixel >= pixel_count) break;
            }
        }

        pos = stream.pos;
        if (stream.block_rem > 0) pos += stream.block_rem;
        while (pos < size && buf[pos] != 0) { 
            uint8_t n = buf[pos++];
            pos += n;
        }
        if (pos < size && buf[pos] == 0) pos++;

        *out_w = (canvas_w > (uint16_t)dst_max_w) ? dst_max_w : canvas_w;
        *out_h = (canvas_h > (uint16_t)dst_max_h) ? dst_max_h : canvas_h;
        return 0;
    }
    return -1;
}

int decode_image_to_rgb565(const unsigned char *buf, uint32_t size, uint16_t *dst, int dst_max_w, int dst_max_h, int *out_w, int *out_h) {
    if (!buf || !dst || !out_w || !out_h) return -1;
    if (size >= 6 && (memcmp(buf, "GIF87a", 6) == 0 || memcmp(buf, "GIF89a", 6) == 0)) {
        return ns_gif_decode_to_rgb565(buf, size, dst, dst_max_w, dst_max_h, out_w, out_h);
    }
    if (size >= sizeof(struct bmp_file_header) + sizeof(struct bmp_info_header)) {
        const struct bmp_file_header *fh = (const struct bmp_file_header *)buf;
        const struct bmp_info_header *ih = (const struct bmp_info_header *)(buf + sizeof(struct bmp_file_header));
        if (fh->type == 0x4D42 && ih->size >= sizeof(struct bmp_info_header) &&
            ih->planes == 1 && ih->bit_count == 24 && ih->compression == 0 &&
            ih->width > 0 && ih->height != 0) {
            int src_w = ih->width;
            int src_h = (ih->height > 0) ? ih->height : -ih->height;
            int draw_w = (src_w > dst_max_w) ? dst_max_w : src_w;
            int draw_h = (src_h > dst_max_h) ? dst_max_h : src_h;
            uint32_t row_stride = (uint32_t)(((src_w * 3) + 3) & ~3);
            if (fh->off_bits >= size) return -1;
            if (fh->off_bits + row_stride * (uint32_t)src_h > size) return -1;
            memset(dst, 0, (size_t)dst_max_w * (size_t)dst_max_h * sizeof(uint16_t));
            for (int y = 0; y < draw_h; y++) {
                int src_y = (ih->height > 0) ? (src_h - 1 - y) : y;
                const unsigned char *row = buf + fh->off_bits + row_stride * (uint32_t)src_y;
                for (int x = 0; x < draw_w; x++) {
                    const unsigned char *px = row + x * 3;
                    dst[y * dst_max_w + x] = rgb565_from_rgb(px[2], px[1], px[0]);
                }
            }
            *out_w = draw_w;
            *out_h = draw_h;
            return 0;
        }
    }
    return -1;
}

int netsurf_prepare_launch_url(const char *url, int viewport_w, int viewport_h, char *out, int out_max) {
    char wrp_base[160];
    char wrp_norm[160];
    char clean_url[256];
    int pos = 0;
    int i;
    int wrp_len;
    const char *sep;

    if (!out || out_max <= 0) return -1;
    out[0] = '\0';
    netsurf_normalize_target_url(url, clean_url, sizeof(clean_url));
    url = clean_url;
    if (!url || !*url) return -1;

    if (ssh_client_get_wrp_url(wrp_base, sizeof(wrp_base)) < 0 || wrp_base[0] == '\0') {
        lib_strcpy(out, url);
        return 0;
    }

    if (strncmp(wrp_base, "https://", 8) == 0) {
        lib_strcpy(wrp_norm, "http://");
        lib_strcat(wrp_norm, wrp_base + 8);
    } else if (strncmp(wrp_base, "http://", 7) == 0) {
        lib_strcpy(wrp_norm, wrp_base);
    } else {
        lib_strcpy(wrp_norm, "http://");
        lib_strcat(wrp_norm, wrp_base);
    }

    if (ns_url_is_wrp_base(url, wrp_norm)) {
        lib_strcpy(out, url);
        return 0;
    }

    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        lib_strcpy(out, url);
        return 0;
    }

    wrp_len = (int)strlen(wrp_norm);
    lib_strcpy(out, wrp_norm);
    pos = wrp_len;

    if (strchr(wrp_norm, '?')) {
        sep = "&m=ismap&t=gip&";
    } else if (wrp_len > 0 && wrp_norm[wrp_len - 1] == '/') {
        sep = "?m=ismap&t=gip&";
    } else {
        sep = "/?m=ismap&t=gip&";
    }

    if (ns_append_str(out, out_max, &pos, sep) < 0) return -1;
    if (viewport_w > 0 && viewport_h > 0) {
        char num[16];
        if (ns_append_str(out, out_max, &pos, "w=") < 0) return -1;
        lib_itoa((uint32_t)viewport_w, num);
        if (ns_append_str(out, out_max, &pos, num) < 0) return -1;
        if (ns_append_str(out, out_max, &pos, "&h=") < 0) return -1;
        lib_itoa((uint32_t)viewport_h, num);
        if (ns_append_str(out, out_max, &pos, num) < 0) return -1;
    }
    if (ns_append_str(out, out_max, &pos, "&url=") < 0) return -1;
    for (i = 0; url[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)url[i];
        if (ns_is_unreserved((char)ch)) {
            if (ns_append_char(out, out_max, &pos, (char)ch) < 0) return -1;
        } else {
            static const char hex[] = "0123456789ABCDEF";
            if (ns_append_char(out, out_max, &pos, '%') < 0) return -1;
            if (ns_append_char(out, out_max, &pos, hex[(ch >> 4) & 0xF]) < 0) return -1;
            if (ns_append_char(out, out_max, &pos, hex[ch & 0xF]) < 0) return -1;
        }
    }
    return 0;
}

int netsurf_queue_wrapped_request(const char *url, int viewport_w, int viewport_h, int owner_win_id) {
    char wrp_base[160];
    char wrp_norm[160];
    char clean_url[256];
    char host[32];
    char base_path[96];
    char req_path[WGET_PATH_MAX];
    uint16_t port = 0;
    int pos = 0;
    const char *sep;

    netsurf_normalize_target_url(url, clean_url, sizeof(clean_url));
    url = clean_url;
    if (!url || !*url) return -1;
    if (ssh_client_get_wrp_url(wrp_base, sizeof(wrp_base)) < 0 || wrp_base[0] == '\0') return -1;
    if (strncmp(wrp_base, "https://", 8) == 0) {
        lib_strcpy(wrp_norm, "http://");
        lib_strcat(wrp_norm, wrp_base + 8);
    } else if (strncmp(wrp_base, "http://", 7) == 0) {
        lib_strcpy(wrp_norm, wrp_base);
    } else {
        lib_strcpy(wrp_norm, "http://");
        lib_strcat(wrp_norm, wrp_base);
    }
    if (!parse_wget_url(wrp_norm, host, &port, base_path)) return -1;

    lib_strcpy(req_path, base_path);
    pos = (int)strlen(req_path);
    if (strchr(base_path, '?')) {
        sep = "&m=ismap&t=gip&";
    } else if (pos > 0 && req_path[pos - 1] == '/') {
        sep = "?m=ismap&t=gip&";
    } else {
        sep = "/?m=ismap&t=gip&";
    }
    if (ns_append_str(req_path, sizeof(req_path), &pos, sep) < 0) return -1;
    if (viewport_w > 0 && viewport_h > 0) {
        char num[16];
        if (ns_append_str(req_path, sizeof(req_path), &pos, "w=") < 0) return -1;
        lib_itoa((uint32_t)viewport_w, num);
        if (ns_append_str(req_path, sizeof(req_path), &pos, num) < 0) return -1;
        if (ns_append_str(req_path, sizeof(req_path), &pos, "&h=") < 0) return -1;
        lib_itoa((uint32_t)viewport_h, num);
        if (ns_append_str(req_path, sizeof(req_path), &pos, num) < 0) return -1;
    }
    if (ns_append_str(req_path, sizeof(req_path), &pos, "&url=") < 0) return -1;
    if (ns_append_encoded(req_path, sizeof(req_path), &pos, url) < 0) return -1;

    lib_printf("[NET] queue host=%s port=%u path=%s file=index.html tls=0 owner=%d\n",
               host, (unsigned)port, req_path, owner_win_id);
    wget_queue_request_ex(host, req_path, "index.html", port, 0, 0, 0, owner_win_id);
    return 0;
}

void* calloc(uint32_t nmemb, uint32_t size) { uint32_t total = nmemb * size; void *ptr = malloc(total); if (ptr) memset(ptr, 0, total); return ptr; }
void* realloc(void *ptr, uint32_t size) {
    if (!ptr) return malloc(size);
    struct alloc_header { uint32_t magic, npages, req_size, reserved; };
    struct alloc_header *h = (struct alloc_header *)((uint8_t *)ptr - 16);
    uint32_t old_size = (h->magic == 0x4d415030u) ? h->req_size : 1024;
    if (size <= old_size) return ptr;
    void *new_ptr = malloc(size);
    if (new_ptr) { memcpy(new_ptr, ptr, old_size); free(ptr); }
    return new_ptr;
}

#undef printf
int lib_printf(const char *fmt, ...) {
#ifdef FPGA_MINIMAL
    (void)fmt;
    return 0;
#else
    va_list args; va_start(args, fmt);
    const char *p = fmt; char buf[32];
    while (*p) {
        if (*p == '%') {
            p++; int is_long = 0; if (*p == 'l') { is_long = 1; p++; }
            switch (*p) {
            case 'u': case 'd': {
                uint32_t num = is_long ? (uint32_t)va_arg(args, unsigned long) : (uint32_t)va_arg(args, int);
                if (*p == 'd' && (int)num < 0) { uart_putc('-'); num = -(int)num; }
                int i = 0; if (num == 0) buf[i++] = '0';
                while (num > 0) { buf[i++] = (num % 10) + '0'; num /= 10; }
                while (i > 0) uart_putc(buf[--i]); break;
            }
            case 'x': {
                uint32_t num = is_long ? (uint32_t)va_arg(args, unsigned long) : (uint32_t)va_arg(args, unsigned int);
                int started = 0; for (int i = 28; i >= 0; i -= 4) {
                    int digit = (num >> i) & 0xF;
                    if (digit != 0 || started || i == 0) { uart_putc(digit < 10 ? digit + '0' : digit - 10 + 'a'); started = 1; }
                } break;
            }
            case 's': { char *s = va_arg(args, char *); if(s) { while (*s) uart_putc(*s++); } break; }
            case 'c': { uart_putc(va_arg(args, int)); break; }
            }
        } else uart_putc(*p); p++;
    }
    va_end(args); return 0;
#endif
}
int printf(const char *fmt, ...) { return 0; }
int fprintf(FILE *fp, const char *fmt, ...) { (void)fp; (void)fmt; return 0; }
void panic(const char *s) { lib_printf("PANIC: %s\n", s); while(1); }
void abort(void) { panic("ABORT"); }
int fflush(void *s) { return 0; }
