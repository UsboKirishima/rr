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

#include "state.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>

/* ==========================================================================
 * Filesystem and configuration paths
 * ========================================================================== */

/* Ensure all parent directories along `path` exist, creating them with
 * permissions 0755 if necessary (like 'mkdir -p'). */
static void ensure_parent_dir(const char *path) {
    char *dir = path_dirname(path);
    if (!dir || !*dir) {
        free(dir);
        return;
    }

    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    free(dir);

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

/* Resolve the absolute path to the persistent state file according to the
 * XDG Base Directory specification.
 *
 * Checks $XDG_CONFIG_HOME first; falls back to $HOME/.config/rr/state. */
static char *get_state_file_path(void) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    char path[PATH_MAX];
    if (xdg && *xdg) {
        snprintf(path, sizeof(path), "%s/rr/state", xdg);
    } else {
        const char *home = getenv("HOME");
        if (!home) home = ".";
        snprintf(path, sizeof(path), "%s/.config/rr/state", home);
    }
    return xstrdup(path);
}

/* ==========================================================================
 * State loading and serialization
 * ========================================================================== */

/* Load session state and reading progress for `book_id`.
 *
 * If the config file does not exist or contains no record for this book,
 * initializes a clean BookState with default reading settings. */
BookState *state_load(const char *book_id) {
    BookState *st = (BookState *)xcalloc(1, sizeof(BookState));
    st->book_id = xstrdup(book_id ? book_id : "default");
    st->last_global_page = 1;
    st->theme = 0;
    st->column_width = 0; /* Auto responsive width */
    st->full_justify = true;
    st->paragraph_style = 0; /* Classic book indent */

    char *path = get_state_file_path();
    FILE *f = fopen(path, "r");
    free(path);

    if (!f) return st;

    char line[4096];
    char target_header[2048];
    snprintf(target_header, sizeof(target_header), "[BOOK:%s]", st->book_id);

    bool in_section = false;
    while (fgets(line, sizeof(line), f)) {
        str_trim(line);

        /* Section header matching */
        if (line[0] == '[' && line[strlen(line) - 1] == ']') {
            if (strcmp(line, target_header) == 0) {
                in_section = true;
            } else if (in_section) {
                /* Exited our book's section */
                break;
            }
            continue;
        }

        /* Parse key=value directives within target section */
        if (in_section) {
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = '\0';
                char *key = str_trim(line);
                char *val = str_trim(eq + 1);

                if (strcmp(key, "last_page") == 0) {
                    st->last_global_page = (size_t)atol(val);
                    if (st->last_global_page == 0) st->last_global_page = 1;
                } else if (strcmp(key, "theme") == 0) {
                    st->theme = atoi(val);
                } else if (strcmp(key, "column_width") == 0) {
                    st->column_width = atoi(val);
                } else if (strcmp(key, "full_justify") == 0) {
                    st->full_justify = (atoi(val) != 0);
                } else if (strcmp(key, "paragraph_style") == 0) {
                    st->paragraph_style = atoi(val);
                } else if (strcmp(key, "bookmarks") == 0) {
                    char *token = strtok(val, ",");
                    while (token && st->bookmark_count < MAX_BOOKMARKS) {
                        size_t bm = (size_t)atol(token);
                        if (bm > 0) {
                            st->bookmarks[st->bookmark_count++] = bm;
                        }
                        token = strtok(NULL, ",");
                    }
                }
            }
        }
    }

    fclose(f);
    return st;
}

/* Save the reading state to disk.
 *
 * To avoid corrupting or discarding progress for other books, this function:
 *   1. Reads all existing lines from the state file.
 *   2. Filters out the section matching the current book.
 *   3. Rewrites the preserved lines followed by the updated book section. */
void state_save(const BookState *state) {
    if (!state || !state->book_id) return;

    char *path = get_state_file_path();
    ensure_parent_dir(path);

    /* Buffer existing lines from config file */
    char **existing_lines = NULL;
    size_t line_count = 0;
    size_t line_cap = 0;

    FILE *f = fopen(path, "r");
    if (f) {
        char buf[4096];
        char target_header[2048];
        snprintf(target_header, sizeof(target_header), "[BOOK:%s]", state->book_id);

        bool skipping = false;
        while (fgets(buf, sizeof(buf), f)) {
            char trimmed[4096];
            snprintf(trimmed, sizeof(trimmed), "%s", buf);
            str_trim(trimmed);

            if (trimmed[0] == '[' && trimmed[strlen(trimmed) - 1] == ']') {
                if (strcmp(trimmed, target_header) == 0) {
                    skipping = true;
                    continue;
                } else {
                    skipping = false;
                }
            }

            if (!skipping) {
                if (line_count >= line_cap) {
                    line_cap = line_cap ? line_cap * 2 : 128;
                    existing_lines = (char **)xrealloc(existing_lines, line_cap * sizeof(char *));
                }
                existing_lines[line_count++] = xstrdup(buf);
            }
        }
        fclose(f);
    }

    /* Open file for writing */
    f = fopen(path, "w");
    free(path);
    if (!f) {
        for (size_t i = 0; i < line_count; i++) free(existing_lines[i]);
        free(existing_lines);
        return;
    }

    /* Write preserved sections for other books */
    for (size_t i = 0; i < line_count; i++) {
        fputs(existing_lines[i], f);
        free(existing_lines[i]);
    }
    free(existing_lines);

    /* Write updated section for current book */
    fprintf(f, "\n[BOOK:%s]\n", state->book_id);
    fprintf(f, "last_page=%zu\n", state->last_global_page);
    fprintf(f, "theme=%d\n", state->theme);
    fprintf(f, "column_width=%d\n", state->column_width);
    fprintf(f, "full_justify=%d\n", state->full_justify ? 1 : 0);
    fprintf(f, "paragraph_style=%d\n", state->paragraph_style);

    fprintf(f, "bookmarks=");
    for (size_t i = 0; i < state->bookmark_count; i++) {
        fprintf(f, "%zu%s", state->bookmarks[i], (i + 1 < state->bookmark_count) ? "," : "");
    }
    fprintf(f, "\n");

    fclose(f);
}

/* Free memory allocated for a BookState structure. */
void state_free(BookState *state) {
    if (!state) return;
    free(state->book_id);
    free(state);
}

/* ==========================================================================
 * Bookmark operations
 * ========================================================================== */

/* Add or remove a bookmark on `global_page`.
 * Returns true if the bookmark was added, false if it was removed. */
bool state_toggle_bookmark(BookState *state, size_t global_page) {
    if (!state || global_page == 0) return false;

    /* If page is already bookmarked, delete it by shifting subsequent items */
    for (size_t i = 0; i < state->bookmark_count; i++) {
        if (state->bookmarks[i] == global_page) {
            for (size_t j = i; j + 1 < state->bookmark_count; j++) {
                state->bookmarks[j] = state->bookmarks[j + 1];
            }
            state->bookmark_count--;
            return false;
        }
    }

    /* Append new bookmark if space permits */
    if (state->bookmark_count < MAX_BOOKMARKS) {
        state->bookmarks[state->bookmark_count++] = global_page;
        return true;
    }
    return false;
}

/* Test whether `global_page` has a bookmark. */
bool state_has_bookmark(const BookState *state, size_t global_page) {
    if (!state) return false;
    for (size_t i = 0; i < state->bookmark_count; i++) {
        if (state->bookmarks[i] == global_page) return true;
    }
    return false;
}
