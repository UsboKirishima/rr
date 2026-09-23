/**
 * @file util.h
 * @brief Common utility functions for rr (memory, UTF-8 string operations, path manipulation).
 */

#ifndef RR_UTIL_H
#define RR_UTIL_H

#include <stddef.h>
#include <stdbool.h>

/* Safe memory allocation wrappers that terminate on failure */
void *xmalloc(size_t size);
void *xcalloc(size_t nmemb, size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *s);

/* String utilities */
char *str_trim(char *s);
char *str_dup(const char *s);
bool str_case_contains(const char *haystack, const char *needle);
bool str_has_suffix(const char *str, const char *suffix);
bool str_has_prefix(const char *str, const char *prefix);

/* Path manipulation utilities */
char *path_dirname(const char *path);
char *path_join(const char *dir, const char *rel);
void path_normalize(char *path);
char *url_decode(const char *src);
void split_url_fragment(const char *url, char **out_path, char **out_fragment);

/* UTF-8 and visual column width utilities */
size_t utf8_char_len(unsigned char c);
int utf8_char_width(const char *s, size_t *out_bytes);
int utf8_strwidth(const char *s);
void utf8_truncate(const char *s, int max_width, char *out_buf, size_t out_buf_size);
void utf8_pad(char *dest, size_t dest_size, const char *src, int target_width, int align);

/* Time formatting (e.g. "2:34am" or "14:34") */
void get_current_time_str(char *buf, size_t buf_size);

/* Stable hash string of a file (path + mtime + size) for state persistence */
char *get_file_identifier(const char *filepath);

#endif /* RR_UTIL_H */
