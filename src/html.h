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

#ifndef RR_HTML_H
#define RR_HTML_H

#include "css.h"
#include <stddef.h>
#include <stdbool.h>

/* ==========================================================================
 * Semantic HTML typesetting tokens
 *
 * EPUB chapters are encoded in XHTML. Rather than rendering raw HTML trees
 * directly, rr parses the document into a high-level semantic representation:
 *
 *   ChapterDocument -> [ Block ] -> [ Word ]
 *
 * Each Word is an atomic layout unit storing its UTF-8 text, pre-calculated
 * visual width, and active typography style flags (bold, italic, etc.).
 *
 * This decomposition isolates XML/HTML parsing from the typesetting engine,
 * enabling fast, flexible re-wrapping when the user resizes the terminal or
 * alters margins.
 *
 * The parse also consults the book's stylesheets (see css.h). Real books mark
 * their chapter titles with <p> plus a class such as `calibre2 { font-size:
 * 1.25em }`, never with <h1>, so the typographic facts resolved from CSS are
 * recorded alongside the markup and used to promote those paragraphs to
 * headings once the whole block has been read.
 * ========================================================================== */

/* Typography and styling bitmask flags applied to individual words. */
typedef enum {
    STYLE_NONE       = 0,
    STYLE_BOLD       = 1 << 0,  /* Bold / strong text */
    STYLE_ITALIC     = 1 << 1,  /* Italic / emphasized text */
    STYLE_UNDERLINE  = 1 << 2,  /* Underlined text */
    STYLE_CODE       = 1 << 3,  /* Inline code or preformatted text */
    STYLE_DIM        = 1 << 4,  /* Dim / secondary text */
    STYLE_HEADING    = 1 << 5,  /* Heading styling (bold + color) */
} TextStyle;

/* An indivisible word token for typesetting.
 *
 * Stores the UTF-8 text string, its terminal column display width,
 * styling flags, and whether a whitespace character originally followed it. */
typedef struct {
    char *text;          /* UTF-8 string (dynamically allocated) */
    int visual_width;    /* Number of terminal columns needed to render */
    int style;           /* Bitmask of TextStyle flags */
    bool space_after;    /* True if followed by whitespace in source */
} Word;

/* Semantic block classification. */
typedef enum {
    BLOCK_PARAGRAPH,     /* Standard text paragraph (<p>) */
    BLOCK_HEADING,       /* Chapter or section header (<h1> - <h6>) */
    BLOCK_BLOCKQUOTE,    /* Indented quotation (<blockquote>) */
    BLOCK_LIST_ITEM,     /* Bulleted or numbered item (<li>) */
    BLOCK_HR,            /* Visual section break (<hr>) */
    BLOCK_PRE,           /* Monospaced preformatted text (<pre>) */
} BlockType;

/* A semantic block element holding an ordered sequence of words.
 *
 * Represents paragraph-level structures in the original document. Also records
 * any HTML anchor identifier attached to this element so that TOC links can
 * jump directly to it. */
typedef struct {
    BlockType type;      /* Paragraph, heading, blockquote, etc. */
    int heading_level;   /* 1 to 6 for headings; 0 for other blocks */
    char *anchor_id;     /* HTML id or name attribute for anchor navigation */
    char *section_title; /* Synthesized plain-text title for headings */
    Word *words;         /* Array of word tokens */
    size_t word_count;   /* Number of words in this block */
    size_t word_cap;     /* Allocated capacity of words array */

    /* Typographic evidence gathered from the book's stylesheets while the
     * block was being read. These fields are what lets rr recognise a heading
     * in a book that never used an <h1> tag; see html.h's module comment. */
    bool css_heading;     /* Promoted from BLOCK_PARAGRAPH to BLOCK_HEADING */
    bool css_page_break;  /* The element requested a break before it */
    bool css_centered;    /* Some element inside the block centres its text */
    float css_font_ratio; /* Largest font-size multiplier inside the block */
} Block;

/* In-memory parsed representation of an entire EPUB chapter document. */
typedef struct {
    size_t spine_index;  /* 0-based index in the linear reading spine */
    char *href;          /* Source href filename within the EPUB archive */
    char *title;         /* Chapter title extracted from first heading */
    Block *blocks;       /* Array of semantic content blocks */
    size_t block_count;  /* Number of content blocks */
    size_t block_cap;    /* Allocated capacity of blocks array */
} ChapterDocument;

/* ==========================================================================
 * HTML parsing API
 * ========================================================================== */

/* Parse a raw XHTML chapter buffer into a structured ChapterDocument.
 *
 * Traverses the DOM tree using libxml2, extracts text nodes, normalizes
 * whitespace, applies inline typography styles, captures anchor IDs, and
 * filters out non-content elements (scripts, styles, SVGs).
 *
 * `css` may be NULL, in which case only presentational markup (<b>, <i>, <h1>
 * ...) is honoured. When it is supplied, the book's stylesheets decide which
 * paragraphs are really headings, which words are really bold, and where the
 * original page breaks fell.
 *
 * This function does structural work only. It does NOT run
 * html_prune_document_noise or html_classify_headings, because both are
 * whole-book decisions that the caller must make in order, after every spine
 * document has been read. See html_classify_headings for why the order is not
 * interchangeable.
 *
 * On success, returns an allocated ChapterDocument pointer.
 * On failure, returns NULL. */
ChapterDocument *html_parse_chapter(const char *xhtml_data, size_t data_len,
                                    size_t spine_index, const char *href,
                                    const char *default_title, const CssIndex *css);

/* Promote paragraphs that the stylesheets mark as display type to headings.
 *
 * A converted book almost never tags its chapter titles with <h1>; it styles
 * an ordinary <p> instead. This pass is where rr recovers the intent: a short
 * paragraph that is bold throughout - or set in a larger, centred face - is
 * reclassified as a heading, given a synthetic anchor so that table of contents
 * navigation can reach it, and given a title synthesized from its own words.
 *
 * ORDERING IS PART OF THE CONTRACT. This must run after
 * html_prune_document_noise, never before. These converters write a running
 * head in exactly the same markup as a chapter title - a bold span in the body
 * face - so the two are separable only by the repetition that pruning
 * measures. Worse, promoting a running head would give it a synthetic anchor,
 * and an anchored block is one that pruning deliberately spares. Classifying
 * first would make the noise untouchable.
 *
 * Safe to call more than once; already-promoted blocks are left alone. */
void html_classify_headings(ChapterDocument *doc);

/* Strip conversion debris from a whole book: printed page numbers and running
 * heads (the book title repeated in the margin of every page).
 *
 * Both artefacts are indistinguishable from prose when a single chapter is
 * read on its own, so the decision needs the whole book as evidence: a short
 * paragraph only counts as a running head once it recurs many times, and a
 * paragraph only loses a trailing number once the book is known to carry
 * folios at all. Blocks that carry a navigation anchor are never removed.
 *
 * Must run before html_classify_headings: see the note there. */
void html_prune_document_noise(ChapterDocument **docs, size_t doc_count);

/* Choose the chapter's displayed title once noise has been pruned.
 *
 * Prefers the first surviving heading's text, so a chapter is named after the
 * title printed in it rather than after its position in the archive, and falls
 * back to `default_title` for spine items with no headings of their own. */
void html_finalize_title(ChapterDocument *doc, const char *default_title);

/* Release all memory associated with a ChapterDocument, including all its
 * child blocks, words, text buffers, and anchor strings. */
void chapter_document_free(ChapterDocument *doc);

#endif /* RR_HTML_H */
