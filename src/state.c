/**
 * @file state.c
 * @brief Persistent reading state and bookmark management implementation.
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

BookState *state_load(const char *book_id) {
    BookState *st = (BookState *)xcalloc(1, sizeof(BookState));
    st->book_id = xstrdup(book_id ? book_id : "default");
    st->last_global_page = 1;
    st->theme = 0;
    st->column_width = 0; /* Auto / responsive */
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
        if (line[0] == '[' && line[strlen(line) - 1] == ']') {
            if (strcmp(line, target_header) == 0) {
                in_section = true;
            } else if (in_section) {
                break;
            }
            continue;
        }

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

void state_save(const BookState *state) {
    if (!state || !state->book_id) return;

    char *path = get_state_file_path();
    ensure_parent_dir(path);

    /* Read existing file lines into memory */
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

    /* Write updated state file */
    f = fopen(path, "w");
    free(path);
    if (!f) {
        for (size_t i = 0; i < line_count; i++) free(existing_lines[i]);
        free(existing_lines);
        return;
    }

    /* Write preserved other books */
    for (size_t i = 0; i < line_count; i++) {
        fputs(existing_lines[i], f);
        free(existing_lines[i]);
    }
    free(existing_lines);

    /* Write current book state */
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

void state_free(BookState *state) {
    if (!state) return;
    free(state->book_id);
    free(state);
}

bool state_toggle_bookmark(BookState *state, size_t global_page) {
    if (!state || global_page == 0) return false;

    for (size_t i = 0; i < state->bookmark_count; i++) {
        if (state->bookmarks[i] == global_page) {
            /* Remove bookmark */
            for (size_t j = i; j + 1 < state->bookmark_count; j++) {
                state->bookmarks[j] = state->bookmarks[j + 1];
            }
            state->bookmark_count--;
            return false;
        }
    }

    if (state->bookmark_count < MAX_BOOKMARKS) {
        state->bookmarks[state->bookmark_count++] = global_page;
        return true;
    }
    return false;
}

bool state_has_bookmark(const BookState *state, size_t global_page) {
    if (!state) return false;
    for (size_t i = 0; i < state->bookmark_count; i++) {
        if (state->bookmarks[i] == global_page) return true;
    }
    return false;
}
