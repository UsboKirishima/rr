/* rr - Lightweight terminal EPUB reader
 *
 * Copyright (c) 2024, Usbo Kirishima <usbo at github>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the copyright holder nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
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

/* ==========================================================================
 * Memory allocation wrappers
 *
 * Like Redis's zmalloc, we wrap memory allocation primitives so that out-of-
 * memory conditions terminate the process immediately with an explanatory
 * error on stderr.
 *
 * In a user-facing terminal reader, running out of memory while rendering a
 * page or parsing a chapter is an unrecoverable condition. Attempting to unwind
 * complex layout structures during an allocation failure creates brittle,
 * hard-to-test code paths. Failing fast guarantees integrity and keeps the
 * rest of the codebase clean of cascading NULL-checks.
 * ========================================================================== */

/* Allocate `size` bytes of uninitialized heap memory.
 * If allocation fails, the reader prints a diagnostic and exits. */
void *xmalloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr && size > 0) {
        fprintf(stderr, "FATAL: Out of memory attempting to allocate %zu bytes\n", size);
        exit(EXIT_FAILURE);
    }
    return ptr;
}

/* Allocate zero-initialized memory for an array of `nmemb` elements
 * of `size` bytes each. Aborts on allocation failure. */
void *xcalloc(size_t nmemb, size_t size) {
    void *ptr = calloc(nmemb, size);
    if (!ptr && nmemb > 0 && size > 0) {
        fprintf(stderr, "FATAL: Out of memory attempting to allocate %zu x %zu bytes\n", nmemb, size);
        exit(EXIT_FAILURE);
    }
    return ptr;
}

/* Reallocate `ptr` to new size `size` bytes.
 * If reallocation fails, the program terminates immediately. */
void *xrealloc(void *ptr, size_t size) {
    void *new_ptr = realloc(ptr, size);
    if (!new_ptr && size > 0) {
        fprintf(stderr, "FATAL: Out of memory attempting to reallocate %zu bytes\n", size);
        exit(EXIT_FAILURE);
    }
    return new_ptr;
}

/* Duplicate string `s` using xmalloc. Returns NULL if `s` is NULL.
 * The returned buffer includes the trailing null terminator. */
char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *copy = (char *)xmalloc(len + 1);
    memcpy(copy, s, len + 1);
    return copy;
}

/* Synonym for xstrdup, providing semantic readability in string contexts. */
char *str_dup(const char *s) {
    if (!s) return NULL;
    return xstrdup(s);
}

/* ==========================================================================
 * String manipulation
 *
 * Lightweight string utilities designed for zero memory waste when parsing
 * XML attributes, TOC titles, and user inputs.
 * ========================================================================== */

/* Strip whitespace characters (spaces, tabs, newlines) from both ends
 * of string `s` in place.
 *
 * If the string contains leading whitespace, the remaining characters
 * are shifted left with memmove. A null terminator is written at the new end.
 * Returns the modified string pointer. */
char *str_trim(char *s) {
    if (!s) return NULL;

    /* Advance past leading whitespace */
    char *start = s;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }

    /* All-whitespace string */
    if (*start == '\0') {
        *s = '\0';
        return s;
    }

    /* Locate end of string and step backward past trailing whitespace */
    char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) {
        end--;
    }
    *(end + 1) = '\0';

    /* Shift content back to buffer start if leading whitespace was stripped */
    if (start != s) {
        memmove(s, start, (size_t)(end - start + 2));
    }
    return s;
}

/* Case-insensitive search: check if `needle` exists anywhere in `haystack`.
 * Uses POSIX strcasestr. Returns true on match or if `needle` is empty. */
bool str_case_contains(const char *haystack, const char *needle) {
    if (!haystack || !needle) return false;
    if (!*needle) return true;
    return (strcasestr(haystack, needle) != NULL);
}

/* Test whether `str` ends with `suffix`.
 * Returns false if suffix is longer than the string itself. */
bool str_has_suffix(const char *str, const char *suffix) {
    if (!str || !suffix) return false;
    size_t str_len = strlen(str);
    size_t suf_len = strlen(suffix);
    if (suf_len > str_len) return false;
    return (strcmp(str + (str_len - suf_len), suffix) == 0);
}

/* Test whether `str` starts with `prefix`. */
bool str_has_prefix(const char *str, const char *prefix) {
    if (!str || !prefix) return false;
    size_t pre_len = strlen(prefix);
    return (strncmp(str, prefix, pre_len) == 0);
}

/* ==========================================================================
 * Path and URL utilities
 *
 * EPUB archives follow the Open Container Format (OCF) where file references
 * are relative to the OPF package root or to the referring XHTML file.
 * We must resolve relative paths, collapse parent directory navigations,
 * and strip fragment hashes when fetching entries from the zip file.
 * ========================================================================== */

/* Extract the directory portion of a file path.
 *
 * For example:
 *   "OEBPS/text/chap1.xhtml" -> "OEBPS/text"
 *   "chap1.xhtml"            -> ""
 *   "/OEBPS/package.opf"     -> "/OEBPS"
 *
 * Returns a newly allocated string that the caller is responsible for freeing. */
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

/* Canonicalize a path string in-place by resolving '.' and '..' segments.
 *
 * This algorithm splits the path into tokens, uses an array stack to
 * resolve parent traversals, and joins them back without allocating extra
 * memory buffers.
 *
 * Example:
 *   "OEBPS/text/../images/cover.jpg" -> "OEBPS/images/cover.jpg" */
void path_normalize(char *path) {
    if (!path || !*path) return;

    char *segments[128];
    int count = 0;
    char *p = path;
    bool is_abs = (*p == '/');

    /* Skip leading slashes */
    while (*p == '/') p++;

    /* Tokenize segments and resolve relative markers */
    char *token = strtok(p, "/");
    while (token) {
        if (strcmp(token, ".") == 0) {
            /* Current directory: ignore */
        } else if (strcmp(token, "..") == 0) {
            /* Parent directory: pop the previous segment if available */
            if (count > 0) {
                count--;
            }
        } else {
            /* Standard directory or file component: push onto stack */
            if (count < 128) {
                segments[count++] = token;
            }
        }
        token = strtok(NULL, "/");
    }

    /* Reconstruct canonical path into the original buffer */
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

/* Join a base directory and a relative path segment into a normalized path.
 *
 * If `rel` is absolute (starts with '/'), it overrides `dir`.
 * If either argument is empty, a copy of the other is returned.
 * Returns a newly allocated normalized path. */
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

/* Decode percent-encoded characters in a URL string.
 *
 * For instance, converts "Chapter%201.xhtml" to "Chapter 1.xhtml" and
 * translates '+' into spaces according to standard form URL encoding.
 * Returns a newly allocated null-terminated string. */
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

/* Split a URL into its resource path and fragment identifier.
 *
 * In EPUB TOC and spine navigation, URLs often point to specific anchors:
 *   "chapter01.xhtml#section_2" -> path="chapter01.xhtml", fragment="section_2"
 *
 * Both output pointers are allocated with xmalloc. If no '#' exists,
 * *out_fragment is set to NULL. */
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

/* ==========================================================================
 * UTF-8 and terminal display width
 *
 * Terminal columns do not correspond 1:1 with byte lengths or even Unicode
 * code points. Multi-byte sequences take 2 to 4 bytes in memory, combining
 * characters take zero visual cells, and wide East Asian characters or
 * emojis take two visual columns.
 *
 * These functions use POSIX `mbrtowc` and `wcwidth` to ensure flawless
 * typesetting on modern Unicode-capable terminal emulators.
 * ========================================================================== */

/* Determine the expected byte length of a UTF-8 character from its leading byte.
 * Valid return values are 1, 2, 3, or 4. Returns 1 as a fallback for malformed bytes. */
size_t utf8_char_len(unsigned char c) {
    if ((c & 0x80) == 0x00) return 1; /* 1-byte ASCII (0xxxxxxx) */
    if ((c & 0xE0) == 0xC0) return 2; /* 2-byte sequence (110xxxxx) */
    if ((c & 0xF0) == 0xE0) return 3; /* 3-byte sequence (1110xxxx) */
    if ((c & 0xF8) == 0xF0) return 4; /* 4-byte sequence (11110xxx) */
    return 1;                         /* Fallback for invalid sequence */
}

/* Calculate the visual column width of the single UTF-8 character starting at `s`.
 * If `out_bytes` is provided, the byte count of the character is stored there.
 * Returns 0 for non-printing characters, 1 for normal glyphs, 2 for wide glyphs. */
int utf8_char_width(const char *s, size_t *out_bytes) {
    if (!s || !*s) {
        if (out_bytes) *out_bytes = 0;
        return 0;
    }

    mbstate_t mbs;
    memset(&mbs, 0, sizeof(mbs));
    wchar_t wc = 0;
    size_t len = mbrtowc(&wc, s, MB_CUR_MAX, &mbs);

    /* Handle invalid or incomplete multi-byte sequence */
    if (len == (size_t)-1 || len == (size_t)-2 || len == 0) {
        if (out_bytes) *out_bytes = 1;
        return 1;
    }

    if (out_bytes) *out_bytes = len;
    int w = wcwidth(wc);
    return (w >= 0) ? w : 0;
}

/* Compute the total visual width in terminal cells needed to render string `s`.
 * Iterates through all multi-byte characters and accumulates their `wcwidth`. */
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
            /* Invalid byte: treat as 1 visual cell and advance 1 byte */
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

/* Truncate a UTF-8 string to fit within `max_width` visual columns.
 *
 * If the string exceeds `max_width`, it is truncated cleanly at a character
 * boundary and a UTF-8 ellipsis ("…", U+2026) is appended to indicate truncation.
 *
 * The destination buffer is guaranteed to be null-terminated. */
void utf8_truncate(const char *s, int max_width, char *out_buf, size_t out_buf_size) {
    if (!s || max_width <= 0 || out_buf_size == 0) {
        if (out_buf_size > 0) out_buf[0] = '\0';
        return;
    }

    /* If the entire string fits comfortably, copy directly */
    if (utf8_strwidth(s) <= max_width) {
        snprintf(out_buf, out_buf_size, "%s", s);
        return;
    }

    /* Target max_width - 1 columns to make room for the single-column ellipsis "…" */
    int target = max_width - 1;
    if (target < 0) target = 0;

    int current_width = 0;
    mbstate_t mbs;
    memset(&mbs, 0, sizeof(mbs));
    const char *p = s;
    size_t out_idx = 0;

    /* Copy characters until target column width is reached */
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

    /* Append UTF-8 horizontal ellipsis "…" (U+2026: 0xE2 0x80 0xA6) */
    if (out_idx + 4 < out_buf_size) {
        out_buf[out_idx++] = (char)0xE2;
        out_buf[out_idx++] = (char)0x80;
        out_buf[out_idx++] = (char)0xA6;
    }
    out_buf[out_idx] = '\0';
}

/* Pad or align `src` within `target_width` terminal columns.
 *
 * Alignment parameter:
 *   0 = Left align (pad with spaces on the right)
 *   1 = Center align (pad equally on both sides)
 *   2 = Right align (pad with spaces on the left)
 *
 * The result is stored in `dest`, bounded by `dest_size`. */
void utf8_pad(char *dest, size_t dest_size, const char *src, int target_width, int align) {
    if (!dest || dest_size == 0) return;
    int src_w = utf8_strwidth(src);
    int pad = target_width - src_w;
    if (pad < 0) pad = 0;

    int left_pad = 0;
    int right_pad = 0;
    if (align == 0) {
        /* Left aligned: text on left, padding on right */
        right_pad = pad;
    } else if (align == 1) {
        /* Center aligned */
        left_pad = pad / 2;
        right_pad = pad - left_pad;
    } else {
        /* Right aligned: padding on left, text on right */
        left_pad = pad;
    }

    size_t idx = 0;

    /* Write leading padding spaces */
    for (int i = 0; i < left_pad && idx + 1 < dest_size; i++) {
        dest[idx++] = ' ';
    }

    /* Copy source string bytes */
    if (src) {
        size_t slen = strlen(src);
        for (size_t i = 0; i < slen && idx + 1 < dest_size; i++) {
            dest[idx++] = src[i];
        }
    }

    /* Write trailing padding spaces */
    for (int i = 0; i < right_pad && idx + 1 < dest_size; i++) {
        dest[idx++] = ' ';
    }
    dest[idx] = '\0';
}

/* ==========================================================================
 * System and time helpers
 * ========================================================================== */

/* Retrieve the current local time formatted as a compact 12-hour string,
 * for example "2:34am" or "10:15pm", suitable for reading status bars. */
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

/* Generate a persistent, stable identifier for an EPUB file.
 *
 * To track bookmarks and reading progress without modifying the original EPUB
 * file, we compute a compound key combining the canonicalized file path, its
 * size in bytes, and its modification timestamp.
 *
 * Format: "/path/to/book.epub:1048576:1711200000"
 *
 * If the file is moved or symlinked, realpath resolves its identity. If the file
 * is updated or edited, the changed size or mtime creates a distinct entry. */
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
