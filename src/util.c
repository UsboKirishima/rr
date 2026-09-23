/**
 * @file util.c
 * @brief Common utility functions implementation for rr.
 */

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <wchar.h>
#include <locale.h>
#include <time.h>
#include <sys/stat.h>
#include <limits.h>

void *xmalloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr && size > 0) {
        fprintf(stderr, "FATAL: Out of memory attempting to allocate %zu bytes\n", size);
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void *xcalloc(size_t nmemb, size_t size) {
    void *ptr = calloc(nmemb, size);
    if (!ptr && nmemb > 0 && size > 0) {
        fprintf(stderr, "FATAL: Out of memory attempting to allocate %zu x %zu bytes\n", nmemb, size);
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void *xrealloc(void *ptr, size_t size) {
    void *new_ptr = realloc(ptr, size);
    if (!new_ptr && size > 0) {
        fprintf(stderr, "FATAL: Out of memory attempting to reallocate %zu bytes\n", size);
        exit(EXIT_FAILURE);
    }
    return new_ptr;
}

char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *copy = (char *)xmalloc(len + 1);
    memcpy(copy, s, len + 1);
    return copy;
}

char *str_dup(const char *s) {
    if (!s) return NULL;
    return xstrdup(s);
}

char *str_trim(char *s) {
    if (!s) return NULL;
    char *start = s;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    if (*start == '\0') {
        *s = '\0';
        return s;
    }
    char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) {
        end--;
    }
    *(end + 1) = '\0';
    if (start != s) {
        memmove(s, start, (size_t)(end - start + 2));
    }
    return s;
}

bool str_case_contains(const char *haystack, const char *needle) {
    if (!haystack || !needle) return false;
    if (!*needle) return true;
    return (strcasestr(haystack, needle) != NULL);
}

bool str_has_suffix(const char *str, const char *suffix) {
    if (!str || !suffix) return false;
    size_t str_len = strlen(str);
    size_t suf_len = strlen(suffix);
    if (suf_len > str_len) return false;
    return (strcmp(str + (str_len - suf_len), suffix) == 0);
}

bool str_has_prefix(const char *str, const char *prefix) {
    if (!str || !prefix) return false;
    size_t pre_len = strlen(prefix);
    return (strncmp(str, prefix, pre_len) == 0);
}

char *path_dirname(const char *path) {
    if (!path || !*path) return xstrdup("");
    const char *last_slash = strrchr(path, '/');
    if (!last_slash) {
        return xstrdup("");
    }
    size_t len = (size_t)(last_slash - path);
    char *dir = (char *)xmalloc(len + 1);
    memcpy(dir, path, len);
    dir[len] = '\0';
    return dir;
}

void path_normalize(char *path) {
    if (!path || !*path) return;
    char *segments[128];
    int count = 0;
    char *p = path;
    bool is_abs = (*p == '/');
    while (*p == '/') p++;

    char *token = strtok(p, "/");
    while (token) {
        if (strcmp(token, ".") == 0) {
            /* Skip current directory markers */
        } else if (strcmp(token, "..") == 0) {
            if (count > 0) {
                count--;
            }
        } else {
            if (count < 128) {
                segments[count++] = token;
            }
        }
        token = strtok(NULL, "/");
    }

    char *out = path;
    if (is_abs) *out++ = '/';
    for (int i = 0; i < count; i++) {
        size_t len = strlen(segments[i]);
        memcpy(out, segments[i], len);
        out += len;
        if (i + 1 < count) *out++ = '/';
    }
    *out = '\0';
}

char *path_join(const char *dir, const char *rel) {
    if (!dir || !dir[0]) {
        char *res = xstrdup(rel ? rel : "");
        path_normalize(res);
        return res;
    }
    if (!rel || !rel[0]) {
        char *res = xstrdup(dir);
        path_normalize(res);
        return res;
    }
    if (rel[0] == '/') {
        char *res = xstrdup(rel);
        path_normalize(res);
        return res;
    }
    size_t dlen = strlen(dir);
    size_t rlen = strlen(rel);
    char *buf = (char *)xmalloc(dlen + rlen + 2);
    sprintf(buf, "%s/%s", dir, rel);
    path_normalize(buf);
    return buf;
}

char *url_decode(const char *src) {
    if (!src) return NULL;
    size_t len = strlen(src);
    char *out = (char *)xmalloc(len + 1);
    char *d = out;
    for (size_t i = 0; i < len; i++) {
        if (src[i] == '%' && i + 2 < len) {
            char hex[3] = {src[i + 1], src[i + 2], '\0'};
            char *endptr = NULL;
            long val = strtol(hex, &endptr, 16);
            if (endptr == hex + 2) {
                *d++ = (char)val;
                i += 2;
                continue;
            }
        } else if (src[i] == '+') {
            *d++ = ' ';
            continue;
        }
        *d++ = src[i];
    }
    *d = '\0';
    return out;
}

void split_url_fragment(const char *url, char **out_path, char **out_fragment) {
    if (!url) {
        *out_path = NULL;
        *out_fragment = NULL;
        return;
    }
    const char *hash = strchr(url, '#');
    if (hash) {
        size_t plen = (size_t)(hash - url);
        char *p = (char *)xmalloc(plen + 1);
        memcpy(p, url, plen);
        p[plen] = '\0';
        *out_path = p;
        *out_fragment = xstrdup(hash + 1);
    } else {
        *out_path = xstrdup(url);
        *out_fragment = NULL;
    }
}

size_t utf8_char_len(unsigned char c) {
    if ((c & 0x80) == 0x00) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1; /* Fallback for invalid sequence */
}

int utf8_char_width(const char *s, size_t *out_bytes) {
    if (!s || !*s) {
        if (out_bytes) *out_bytes = 0;
        return 0;
    }
    mbstate_t mbs;
    memset(&mbs, 0, sizeof(mbs));
    wchar_t wc = 0;
    size_t len = mbrtowc(&wc, s, MB_CUR_MAX, &mbs);
    if (len == (size_t)-1 || len == (size_t)-2 || len == 0) {
        if (out_bytes) *out_bytes = 1;
        return 1;
    }
    if (out_bytes) *out_bytes = len;
    int w = wcwidth(wc);
    return (w >= 0) ? w : 0;
}

int utf8_strwidth(const char *s) {
    if (!s) return 0;
    int total = 0;
    mbstate_t mbs;
    memset(&mbs, 0, sizeof(mbs));
    const char *p = s;
    while (*p) {
        wchar_t wc = 0;
        size_t len = mbrtowc(&wc, p, MB_CUR_MAX, &mbs);
        if (len == (size_t)-1 || len == (size_t)-2) {
            total += 1;
            p++;
            memset(&mbs, 0, sizeof(mbs));
        } else if (len == 0) {
            break;
        } else {
            int w = wcwidth(wc);
            total += (w > 0) ? w : 0;
            p += len;
        }
    }
    return total;
}

void utf8_truncate(const char *s, int max_width, char *out_buf, size_t out_buf_size) {
    if (!s || max_width <= 0 || out_buf_size == 0) {
        if (out_buf_size > 0) out_buf[0] = '\0';
        return;
    }
    if (utf8_strwidth(s) <= max_width) {
        snprintf(out_buf, out_buf_size, "%s", s);
        return;
    }

    /* Target max_width - 1 to leave room for ellipsis "…" */
    int target = max_width - 1;
    if (target < 0) target = 0;

    int current_width = 0;
    mbstate_t mbs;
    memset(&mbs, 0, sizeof(mbs));
    const char *p = s;
    size_t out_idx = 0;

    while (*p && current_width < target && out_idx + 4 < out_buf_size) {
        wchar_t wc = 0;
        size_t len = mbrtowc(&wc, p, MB_CUR_MAX, &mbs);
        if (len == (size_t)-1 || len == (size_t)-2) {
            if (current_width + 1 > target) break;
            out_buf[out_idx++] = *p++;
            current_width++;
            memset(&mbs, 0, sizeof(mbs));
        } else if (len == 0) {
            break;
        } else {
            int w = wcwidth(wc);
            int cw = (w > 0) ? w : 0;
            if (current_width + cw > target) break;
            for (size_t i = 0; i < len && out_idx + 1 < out_buf_size; i++) {
                out_buf[out_idx++] = p[i];
            }
            current_width += cw;
            p += len;
        }
    }

    /* Append UTF-8 ellipsis "…" (U+2026: 0xE2 0x80 0xA6) */
    if (out_idx + 4 < out_buf_size) {
        out_buf[out_idx++] = (char)0xE2;
        out_buf[out_idx++] = (char)0x80;
        out_buf[out_idx++] = (char)0xA6;
    }
    out_buf[out_idx] = '\0';
}

void utf8_pad(char *dest, size_t dest_size, const char *src, int target_width, int align) {
    if (!dest || dest_size == 0) return;
    int src_w = utf8_strwidth(src);
    int pad = target_width - src_w;
    if (pad < 0) pad = 0;

    int left_pad = 0;
    int right_pad = 0;
    if (align == 0) {
        /* Left align */
        right_pad = pad;
    } else if (align == 1) {
        /* Center */
        left_pad = pad / 2;
        right_pad = pad - left_pad;
    } else {
        /* Right align */
        left_pad = pad;
    }

    size_t idx = 0;
    for (int i = 0; i < left_pad && idx + 1 < dest_size; i++) {
        dest[idx++] = ' ';
    }
    if (src) {
        size_t slen = strlen(src);
        for (size_t i = 0; i < slen && idx + 1 < dest_size; i++) {
            dest[idx++] = src[i];
        }
    }
    for (int i = 0; i < right_pad && idx + 1 < dest_size; i++) {
        dest[idx++] = ' ';
    }
    dest[idx] = '\0';
}

void get_current_time_str(char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return;
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    if (!tm_info) {
        snprintf(buf, buf_size, "--:--");
        return;
    }
    int hour12 = tm_info->tm_hour % 12;
    if (hour12 == 0) hour12 = 12;
    const char *ampm = (tm_info->tm_hour >= 12) ? "pm" : "am";
    snprintf(buf, buf_size, "%d:%02d%s", hour12, tm_info->tm_min, ampm);
}

char *get_file_identifier(const char *filepath) {
    if (!filepath) return xstrdup("unknown");
    char real[PATH_MAX];
    const char *resolved = realpath(filepath, real) ? real : filepath;

    struct stat st;
    if (stat(resolved, &st) == 0) {
        char buf[PATH_MAX + 64];
        snprintf(buf, sizeof(buf), "%s:%ld:%ld", resolved, (long)st.st_size, (long)st.st_mtime);
        return xstrdup(buf);
    }
    return xstrdup(resolved);
}
