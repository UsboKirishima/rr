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

#ifndef RR_UI_H
#define RR_UI_H

#include "epub.h"
#include "layout.h"
#include "state.h"
#include <stdbool.h>

/* Number of aesthetic color themes supported by rr. */
#define THEME_COUNT 5

/* ==========================================================================
 * Terminal user interface and interactive modals
 *
 * rr presents a minimalist, distraction-free reading experience inspired by
 * fine paperback book design.
 *
 * Visual Anatomy of a Page:
 *
 *   Row 0:           Running Header (Book Title ......... Section Title)
 *   Row 1:           Blank breathing space
 *   Rows 2 .. H-3:   Typeset text column with centered margins
 *   Row H-2:         Blank breathing space
 *   Row H-1:         Status footer (← Pag. X/Y →    [ 38% ]    2:34am)
 *
 * Modals (TOC, Bookmarks, Search, Keybindings) are rendered directly on top
 * of the typeset reading surface within rounded Unicode frames (╭─╮, ╰─╯),
 * creating an elegant overlay effect without destroying the reader's visual
 * context.
 * ========================================================================== */

/* Location coordinates of a search match occurrence within the book. */
typedef struct {
    size_t global_page;        /* 1-based page where match appears */
    size_t chapter_index;      /* Spine chapter index */
    size_t line_index;         /* Index in chapter lines array */
    int word_index;            /* Word index on the line */
} SearchMatch;

/* Active UI runtime context.
 * Holds layout references, screen dimensions, theme state, and search state. */
typedef struct {
    BookLayout *layout;        /* Active typeset book layout */
    BookState *state;          /* Reading state and preferences */
    size_t current_page;       /* 1-based current global page number */
    int term_w;                /* Terminal width in columns */
    int term_h;                /* Terminal height in rows */
    int active_theme;          /* Selected color scheme index (0..4) */

    char status_msg[128];      /* Toast status message string */
    int status_ticks;          /* Remaining display frames before toast expires */

    char search_query[128];    /* Current search query */
    SearchMatch *matches;      /* Array of all matches across book */
    size_t match_count;        /* Total number of matches */
    size_t current_match_idx;  /* Index of currently selected match */
    bool search_active;        /* True if search highlighting is enabled */
} UIState;

/* ==========================================================================
 * Curses lifecycle & theme management
 * ========================================================================== */

/* Initialize curses, configure mouse events, set up color pairs, and hide cursor.
 * Returns true on success, false on failure. */
bool ui_init(void);

/* Restore terminal state and shut down curses. */
void ui_cleanup(void);

/* Apply color palette corresponding to `theme_index` (0..4). */
void ui_set_theme(int theme_index);

/* Retrieve human-readable name of color scheme. */
const char *ui_get_theme_name(int theme_index);

/* ==========================================================================
 * Screen rendering & status notifications
 * ========================================================================== */

/* Render the full reader screen for `ui->current_page`.
 * Clears the screen, prints header, typesets body text, and draws footer. */
void ui_render(UIState *ui);

/* Display a temporary toast notification in the center of the status footer.
 * Formats like printf. Toast automatically fades after a few interactions. */
void ui_set_status(UIState *ui, const char *fmt, ...);

/* ==========================================================================
 * Interactive modal dialogs
 * ========================================================================== */

/* Display the Table of Contents modal selector.
 *
 * Displays a scrollable, hierarchical list of chapters with dot leaders and
 * page numbers. Arrow keys or j/k navigate; Enter selects.
 *
 * Returns the target 1-based global page to jump to, or 0 if cancelled. */
size_t ui_show_toc_modal(UIState *ui);

/* Display the Bookmarks management modal.
 *
 * Shows all bookmarked pages with chapter titles. Pressing 'd' removes a bookmark.
 * Returns the target global page to jump to, or 0 if dismissed. */
size_t ui_show_bookmarks_modal(UIState *ui);

/* Display the keyboard controls and features cheatsheet overlay. */
void ui_show_help_modal(UIState *ui);

/* Prompt the user in the footer row for a page number to jump to.
 * Returns the target global page, or 0 if cancelled. */
size_t ui_prompt_goto_page(UIState *ui);

/* ==========================================================================
 * Text search operations
 * ========================================================================== */

/* Prompt the user for a search query and scan the entire book.
 * Highlights matches on the current page and jumps to the first match. */
void ui_prompt_search(UIState *ui);

/* Advance to the next search occurrence across the book. */
void ui_search_next(UIState *ui);

/* Move to the previous search occurrence across the book. */
void ui_search_prev(UIState *ui);

/* Clear search query and remove match highlights. */
void ui_search_clear(UIState *ui);

#endif /* RR_UI_H */
