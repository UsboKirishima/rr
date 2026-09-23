/**
 * @file layout.h
 * @brief Typesetting, word-wrapping, full-justification, and pagination engine.
 */

#ifndef RR_LAYOUT_H
#define RR_LAYOUT_H

#include "epub.h"
#include "html.h"
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief A single typeset line ready for terminal rendering.
 */
typedef struct {
    Word *words;               /**< Pointer to array of words on this line */
    int word_count;            /**< Number of words */
    int *spaces_after;         /**< Number of spaces following each word (for justification) */
    int indent_spaces;         /**< Left margin / paragraph indentation spaces */
    int total_width;           /**< Visual column width */
    bool is_heading;           /**< True if heading line */
    int heading_level;         /**< Heading level 1..6 */
    bool is_hr;                /**< True if horizontal divider */
    bool is_blank;             /**< True if empty / paragraph separator line */
    bool is_centered;          /**< True if content should be centered */
    int block_index;           /**< Source block index in ChapterDocument */
    const char *section_title; /**< Section title active at this line */
    const char *anchor_id;     /**< Anchor ID associated with this line, or NULL */
} LayoutLine;

/**
 * @brief A single screen page containing a slice of chapter lines.
 */
typedef struct {
    size_t start_line;         /**< First line index in chapter */
    size_t line_count;         /**< Number of lines displayed on this page */
    size_t chapter_index;      /**< 0-based spine chapter index */
    size_t page_in_chapter;    /**< 1-based page number within chapter */
    size_t global_page;        /**< 1-based global page number across book */
    const char *section_title; /**< Active chapter/section title for header */
} LayoutPage;

/**
 * @brief Complete layout for a single chapter.
 */
typedef struct {
    size_t chapter_index;
    ChapterDocument *doc;      /**< Parsed chapter document */
    LayoutLine *lines;
    size_t line_count;
    size_t line_cap;
    LayoutPage *pages;
    size_t page_count;
    size_t page_cap;
} ChapterLayout;

/**
 * @brief Global book layout containing all chapters and page mappings.
 */
typedef struct {
    EpubBook *book;
    ChapterLayout *chapters;
    size_t chapter_count;

    size_t total_pages;        /**< Total pages across all chapters */
    int column_width;          /**< Formatted text column width */
    int page_height;           /**< Available text lines per screen page */
    bool full_justify;         /**< True for full justification, false for ragged right */
    int paragraph_style;       /**< 0 = first-line indent, 1 = blank line spacing */

    /* Quick lookup: global_page (1-based) -> (chapter_index, page_in_chapter) */
    size_t *page_to_chapter;
    size_t *page_to_local_page;
} BookLayout;

/**
 * @brief Builds or rebuilds the complete book layout for given terminal constraints.
 * @param book Pointer to opened EpubBook.
 * @param col_width Desired width of the text column in characters.
 * @param page_height Number of text lines available per page.
 * @param full_justify Whether to full-justify text lines.
 * @param paragraph_style 0 for indent mode, 1 for blank line mode.
 * @return Allocated BookLayout pointer.
 */
BookLayout *layout_build(EpubBook *book, int col_width, int page_height,
                         bool full_justify, int paragraph_style);

/**
 * @brief Frees all memory associated with a BookLayout.
 */
void layout_free(BookLayout *layout);

/**
 * @brief Resolves a global page index to its LayoutPage pointer.
 */
const LayoutPage *layout_get_page(const BookLayout *layout, size_t global_page);

/**
 * @brief Finds the global page corresponding to a TOC item.
 */
size_t layout_find_toc_page(const BookLayout *layout, const EpubTocItem *toc);

/**
 * @brief Finds the first global page for a spine chapter.
 */
size_t layout_get_chapter_first_page(const BookLayout *layout, size_t chapter_index);

#endif /* RR_LAYOUT_H */
