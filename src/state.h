/**
 * @file state.h
 * @brief Persistent reading state and bookmark management across sessions.
 */

#ifndef RR_STATE_H
#define RR_STATE_H

#include <stddef.h>
#include <stdbool.h>

#define MAX_BOOKMARKS 256

/**
 * @brief User preferences and book reading progress.
 */
typedef struct {
    char *book_id;            /**< File identifier / hash */
    size_t last_global_page;  /**< 1-based last read page */
    int theme;                /**< Active color theme index */
    int column_width;         /**< Preferred reading column width (0 = auto) */
    bool full_justify;        /**< True if full justification enabled */
    int paragraph_style;      /**< 0 = indent mode, 1 = spaced mode */

    size_t bookmarks[MAX_BOOKMARKS];
    size_t bookmark_count;
} BookState;

/**
 * @brief Loads the saved state for a book, or initializes default values.
 * @param book_id Unique identifier string for the book.
 * @return Allocated BookState pointer.
 */
BookState *state_load(const char *book_id);

/**
 * @brief Saves the book state to disk in XDG config directory.
 * @param state Pointer to BookState to persist.
 */
void state_save(const BookState *state);

/**
 * @brief Frees memory associated with BookState.
 */
void state_free(BookState *state);

/**
 * @brief Toggles a bookmark on a given global page.
 * @return True if bookmark was added, false if it was removed.
 */
bool state_toggle_bookmark(BookState *state, size_t global_page);

/**
 * @brief Checks if a page is bookmarked.
 */
bool state_has_bookmark(const BookState *state, size_t global_page);

#endif /* RR_STATE_H */
