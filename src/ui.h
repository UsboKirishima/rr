/**
 * @file ui.h
 * @brief Terminal User Interface rendering, themes, and interactive modals for rr.
 */

#ifndef RR_UI_H
#define RR_UI_H

#include "epub.h"
#include "layout.h"
#include "state.h"
#include <stdbool.h>

#define THEME_COUNT 5

/**
 * @brief Search match location in book.
 */
typedef struct {
    size_t global_page;
    size_t chapter_index;
    size_t line_index;
    int word_index;
} SearchMatch;

/**
 * @brief Active UI runtime state.
 */
typedef struct {
    BookLayout *layout;
    BookState *state;
    size_t current_page;       /**< 1-based current global page */
    int term_w;                /**< Terminal width */
    int term_h;                /**< Terminal height */
    int active_theme;          /**< Theme index (0..4) */

    char status_msg[128];      /**< Temporary status message / toast */
    int status_ticks;          /**< Remaining display frames for status */

    char search_query[128];    /**< Active search query */
    SearchMatch *matches;      /**< Array of search matches */
    size_t match_count;
    size_t current_match_idx;
    bool search_active;
} UIState;

/**
 * @brief Initializes curses terminal and color pairs.
 */
bool ui_init(void);

/**
 * @brief Shuts down curses and restores terminal state.
 */
void ui_cleanup(void);

/**
 * @brief Applies a color theme by index.
 */
void ui_set_theme(int theme_index);

/**
 * @brief Retrieves human-readable name of color theme.
 */
const char *ui_get_theme_name(int theme_index);

/**
 * @brief Renders the reader interface for the current page.
 */
void ui_render(UIState *ui);

/**
 * @brief Sets a temporary status toast in the UI.
 */
void ui_set_status(UIState *ui, const char *fmt, ...);

/**
 * @brief Opens the interactive Table of Contents modal.
 * @return Target global page to jump to, or 0 if cancelled.
 */
size_t ui_show_toc_modal(UIState *ui);

/**
 * @brief Opens the interactive Bookmarks modal.
 * @return Target global page to jump to, or 0 if cancelled.
 */
size_t ui_show_bookmarks_modal(UIState *ui);

/**
 * @brief Opens the Help / Keybindings modal.
 */
void ui_show_help_modal(UIState *ui);

/**
 * @brief Prompts user for a global page number to jump to.
 * @return Target global page, or 0 if cancelled.
 */
size_t ui_prompt_goto_page(UIState *ui);

/**
 * @brief Prompts user for a search query and finds all matches.
 */
void ui_prompt_search(UIState *ui);

/**
 * @brief Advances to next search match.
 */
void ui_search_next(UIState *ui);

/**
 * @brief Moves to previous search match.
 */
void ui_search_prev(UIState *ui);

/**
 * @brief Clears active search.
 */
void ui_search_clear(UIState *ui);

#endif /* RR_UI_H */
