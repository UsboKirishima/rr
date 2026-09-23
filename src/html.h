/**
 * @file html.h
 * @brief HTML/XHTML semantic parser and token stream generator for EPUB chapters.
 */

#ifndef RR_HTML_H
#define RR_HTML_H

#include "epub.h"
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Styling flags for individual text tokens.
 */
typedef enum {
    STYLE_NONE       = 0,
    STYLE_BOLD       = 1 << 0,
    STYLE_ITALIC     = 1 << 1,
    STYLE_UNDERLINE  = 1 << 2,
    STYLE_CODE       = 1 << 3,
    STYLE_DIM        = 1 << 4,
    STYLE_HEADING    = 1 << 5,
} TextStyle;

/**
 * @brief An indivisible text token for typesetting and layout.
 */
typedef struct {
    char *text;          /**< UTF-8 text string */
    int visual_width;    /**< True display width in terminal columns */
    int style;           /**< Bitmask of TextStyle flags */
    bool space_after;    /**< True if a space naturally follows this word */
} Word;

/**
 * @brief Type of semantic block element.
 */
typedef enum {
    BLOCK_PARAGRAPH,
    BLOCK_HEADING,
    BLOCK_BLOCKQUOTE,
    BLOCK_LIST_ITEM,
    BLOCK_HR,
    BLOCK_PRE,
} BlockType;

/**
 * @brief A semantic block containing formatted words.
 */
typedef struct {
    BlockType type;
    int heading_level;   /**< 1 to 6 for headings */
    char *anchor_id;     /**< Anchor ID / name for linking */
    char *section_title; /**< Section title text for headings */
    Word *words;
    size_t word_count;
    size_t word_cap;
} Block;

/**
 * @brief Complete parsed document representation for an EPUB chapter.
 */
typedef struct {
    size_t spine_index;
    char *href;
    char *title;
    Block *blocks;
    size_t block_count;
    size_t block_cap;
} ChapterDocument;

/**
 * @brief Parses an XHTML chapter buffer into a structured ChapterDocument.
 * @param xhtml_data Raw XHTML string buffer.
 * @param data_len Length of data buffer.
 * @param spine_index Spine sequence index.
 * @param href Source href path.
 * @param default_title Fallback title if document contains no headings.
 * @return Allocated ChapterDocument pointer, or NULL on error.
 */
ChapterDocument *html_parse_chapter(const char *xhtml_data, size_t data_len,
                                    size_t spine_index, const char *href, const char *default_title);

/**
 * @brief Frees all memory associated with a ChapterDocument.
 */
void chapter_document_free(ChapterDocument *doc);

#endif /* RR_HTML_H */
