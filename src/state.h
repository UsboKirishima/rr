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

#ifndef RR_STATE_H
#define RR_STATE_H

#include <stddef.h>
#include <stdbool.h>

/* Maximum number of saved bookmarks allowed per book. */
#define MAX_BOOKMARKS 256

/* ==========================================================================
 * State persistence and bookmarks
 *
 * Reading is an iterative experience. When a reader opens an EPUB, rr
 * restores the exact reading position, color theme, column width, and
 * bookmarks from previous sessions without altering the original .epub file.
 *
 * State is stored in standard XDG configuration format:
 *   $XDG_CONFIG_HOME/rr/state  (typically ~/.config/rr/state)
 *
 * Format is human-readable and git-friendly INI syntax:
 *
 *   [BOOK:/home/user/books/iliad.epub:1048576:1711200000]
 *   last_page=42
 *   theme=1
 *   column_width=66
 *   full_justify=1
 *   paragraph_style=0
 *   bookmarks=12,42,108
 * ========================================================================== */

/* Persistent reading preferences and session progress for a single book. */
typedef struct {
    char *book_id;            /* Stable fingerprint key (path + size + mtime) */
    size_t last_global_page;  /* 1-based page number where reader left off */
    int theme;                /* Active color scheme index (0..4) */
    int column_width;         /* Reading column width preference (0 = auto) */
    bool full_justify;        /* True if full justification is enabled */
    int paragraph_style;      /* 0 = traditional indent, 1 = spaced lines */

    size_t bookmarks[MAX_BOOKMARKS]; /* Array of bookmarked global pages */
    size_t bookmark_count;           /* Number of saved bookmarks */
} BookState;

/* ==========================================================================
 * State management API
 * ========================================================================== */

/* Load saved session state for `book_id`.
 *
 * If no previous state exists in the config file, returns a BookState
 * initialized with sensible defaults (page 1, minimal theme, classic indent).
 * Always returns a valid heap-allocated BookState pointer. */
BookState *state_load(const char *book_id);

/* Persist the given BookState to disk.
 *
 * Reads the existing state file, replaces or appends the section corresponding
 * to this book while preserving records for all other books, and writes back
 * to $XDG_CONFIG_HOME/rr/state. */
void state_save(const BookState *state);

/* Free all memory associated with a BookState structure. */
void state_free(BookState *state);

/* Toggle a bookmark on `global_page`.
 *
 * If a bookmark exists for this page, it is removed. If not, and capacity
 * permits, it is added. Returns true if added, false if removed. */
bool state_toggle_bookmark(BookState *state, size_t global_page);

/* Check whether `global_page` is currently bookmarked. */
bool state_has_bookmark(const BookState *state, size_t global_page);

#endif /* RR_STATE_H */
