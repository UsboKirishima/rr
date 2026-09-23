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

#ifndef RR_UTIL_H
#define RR_UTIL_H

#include <stddef.h>
#include <stdbool.h>

/* ==========================================================================
 * Memory allocation wrappers
 *
 * In an interactive terminal reader, handling out-of-memory errors by trying
 * to roll back complex state is impractical and error-prone. These wrappers
 * guarantee that allocations either succeed or abort immediately with an
 * informative message, simplifying error handling throughout the codebase.
 * ========================================================================== */

/* Allocate `size` bytes of memory. Aborts if allocation fails. */
void *xmalloc(size_t size);

/* Allocate an array of `nmemb` elements each of `size` bytes, cleared to zero. */
void *xcalloc(size_t nmemb, size_t size);

/* Resize allocated block `ptr` to `size` bytes. Aborts on failure. */
void *xrealloc(void *ptr, size_t size);

/* Duplicate string `s` using xmalloc. Returns NULL if `s` is NULL. */
char *xstrdup(const char *s);

/* ==========================================================================
 * String manipulation
 * ========================================================================== */

/* Trim leading and trailing whitespace from string `s` in-place.
 * Returns the modified pointer `s`. */
char *str_trim(char *s);

/* Duplicate string `s` (synonym for xstrdup for semantic clarity). */
char *str_dup(const char *s);

/* Case-insensitive substring search. Returns true if needle is found. */
bool str_case_contains(const char *haystack, const char *needle);

/* Returns true if `str` ends with `suffix`. */
bool str_has_suffix(const char *str, const char *suffix);

/* Returns true if `str` begins with `prefix`. */
bool str_has_prefix(const char *str, const char *prefix);

/* ==========================================================================
 * Path and URL utilities
 *
 * EPUB archives contain internal paths and relative URLs that may use
 * percent-encoding, relative parent traversals ("../"), and fragment anchors.
 * ========================================================================== */

/* Return the directory component of a path, allocated with xmalloc.
 * Trailing slashes are stripped. Returns empty string if no slash exists. */
char *path_dirname(const char *path);

/* Join two path segments `dir` and `rel`, normalizing the resulting path.
 * Returns an allocated string. */
char *path_join(const char *dir, const char *rel);

/* Normalize a file path in-place by resolving '.' and '..' segments. */
void path_normalize(char *path);

/* Decode a percent-encoded URL string (e.g. "%20" -> ' ', "+" -> ' ').
 * Returns a newly allocated null-terminated string. */
char *url_decode(const char *src);

/* Split a URL into its path component and fragment anchor (the part after '#').
 * Both returned pointers are newly allocated, or NULL if absent. */
void split_url_fragment(const char *url, char **out_path, char **out_fragment);

/* ==========================================================================
 * UTF-8 and terminal display width
 *
 * Terminal typography requires knowing the visual column width of characters,
 * not merely their byte count. Multi-byte UTF-8 sequences and full-width
 * CJK or emoji glyphs occupy different amounts of screen columns.
 * ========================================================================== */

/* Returns the expected byte length of a UTF-8 character based on its first byte. */
size_t utf8_char_len(unsigned char c);

/* Compute the visual column width of the first UTF-8 character in `s`.
 * If `out_bytes` is non-NULL, it receives the number of bytes consumed. */
int utf8_char_width(const char *s, size_t *out_bytes);

/* Compute the total visual terminal width of a UTF-8 string. */
int utf8_strwidth(const char *s);

/* Truncate `s` to fit within `max_width` visual columns, appending a UTF-8
 * ellipsis ("…") if truncation was necessary. Writes result into `out_buf`. */
void utf8_truncate(const char *s, int max_width, char *out_buf, size_t out_buf_size);

/* Pad or align `src` within `target_width` columns (0: left, 1: center, 2: right).
 * Result is written into `dest`. */
void utf8_pad(char *dest, size_t dest_size, const char *src, int target_width, int align);

/* ==========================================================================
 * System and time helpers
 * ========================================================================== */

/* Format current local time into a compact string like "2:34am" or "11:05pm". */
void get_current_time_str(char *buf, size_t buf_size);

/* Generate a stable identifier string for a file (real path, size, mtime)
 * used as a key to persist reading progress and preferences across sessions. */
char *get_file_identifier(const char *filepath);

#endif /* RR_UTIL_H */
