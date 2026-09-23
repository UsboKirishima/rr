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

#ifndef RR_LAYOUT_H
#define RR_LAYOUT_H

#include "epub.h"
#include "html.h"
#include <stddef.h>
#include <stdbool.h>

/* ==========================================================================
 * Typography and layout engine
 *
 * The layout engine transforms semantic ChapterDocument structures into
 * geometric screen pages formatted for terminal rendering:
 *
 *   Word Stream ---> Typeset Lines ---> Paginated Screens
 *
 * It solves three distinct typesetting problems:
 *
 * 1. Word wrapping & Line Breaking:
 *    Groups words into lines that strictly obey the configured column width,
 *    taking into account UTF-8 visual column metrics rather than byte counts.
 *
 * 2. Full Justification:
 *    Distributes surplus column whitespace evenly across inter-word gaps.
 *    To prevent the typographic flaw known as "rivers of white space"
 *    (noticeable vertical alignment of gaps across consecutive lines),
 *    the distribution of fractional remainder spaces alternates direction
 *    between odd and even lines.
 *
 * 3. Pagination & Widow/Orphan Control:
 *    Slices formatted lines into screen-height pages. Strips leading and
 *    trailing blank lines from pages and ensures headings are not stranded
 *    as single-line orphans at the bottom of a screen.
 * ========================================================================== */

/* A single typeset line ready for direct rendering in the terminal.
 *
 * Rather than storing a flattened string, LayoutLine preserves pointers to
 * the constituent Word tokens and maintains an array of exact inter-word
 * space counts (`spaces_after`). This allows the rendering pass to draw words
 * with their individual ANSI / curses styling without string parsing. */
typedef struct {
    Word *words;               /* Pointer into block's words array */
    int word_count;            /* Number of words on this line */
    int *spaces_after;         /* Number of space characters after each word */
    int indent_spaces;         /* Left margin indentation (e.g. 4 for paragraphs) */
    int total_width;           /* Total visual width in terminal columns */
    bool is_heading;           /* True if line contains heading text */
    int heading_level;         /* Heading level 1..6 */
    bool is_hr;                /* True if visual thematic divider line */
    bool is_blank;             /* True if vertical spacing blank line */
    bool is_centered;          /* True if line content should be centered */
    int block_index;           /* Index of source block in ChapterDocument */
    const char *section_title; /* Active section title at this line position */
    const char *anchor_id;     /* HTML target anchor attached to line, or NULL */
} LayoutLine;

/* A single screen page containing a vertical slice of chapter lines. */
typedef struct {
    size_t start_line;         /* First line index within chapter's lines array */
    size_t line_count;         /* Number of lines displayed on this screen */
    size_t chapter_index;      /* 0-based spine chapter index */
    size_t page_in_chapter;    /* 1-based local page index within chapter */
    size_t global_page;        /* 1-based global page number across entire book */
    const char *section_title; /* Active section or chapter title for header */
} LayoutPage;

/* Formatted layout for an individual chapter. */
typedef struct {
    size_t chapter_index;      /* Spine sequence index */
    ChapterDocument *doc;      /* Parsed semantic document for this chapter */
    LayoutLine *lines;         /* Array of all typeset lines in chapter */
    size_t line_count;         /* Total number of typeset lines */
    size_t line_cap;           /* Allocated capacity of lines array */
    LayoutPage *pages;         /* Array of paginated screens */
    size_t page_count;         /* Total number of pages in chapter */
    size_t page_cap;           /* Allocated capacity of pages array */
} ChapterLayout;

/* Master book layout encompassing all chapters and global navigation tables.
 *
 * Provides fast O(1) translation between 1-based global page numbers and
 * their corresponding chapter index and local page position. */
typedef struct {
    EpubBook *book;            /* Associated opened EPUB book */
    ChapterLayout *chapters;   /* Array of chapter layouts */
    size_t chapter_count;      /* Number of chapters (matches spine_count) */

    size_t total_pages;        /* Cumulative page count across all chapters */
    int column_width;          /* Text column width in characters */
    int page_height;           /* Available text rows per screen page */
    bool full_justify;         /* True = justified text, false = ragged right */
    int paragraph_style;       /* 0 = classic indent, 1 = spaced paragraphs */

    /* Lookup tables: global_page (1-based) -> (chapter_index, local_page) */
    size_t *page_to_chapter;
    size_t *page_to_local_page;
} BookLayout;

/* ==========================================================================
 * Layout engine API
 * ========================================================================== */

/* Build or recalculate the complete book layout.
 *
 * Reads each spine chapter from the archive, parses its XHTML into semantic
 * blocks, typesets words into justified lines according to `col_width`, and
 * divides lines into pages fitting `page_height`.
 *
 * Returns a newly allocated BookLayout pointer. */
BookLayout *layout_build(EpubBook *book, int col_width, int page_height,
                         bool full_justify, int paragraph_style);

/* Release all memory allocated for a BookLayout, including typeset lines,
 * per-line spacing tables, pages, documents, and page lookup arrays. */
void layout_free(BookLayout *layout);

/* Retrieve the LayoutPage descriptor for a given 1-based global page number.
 * Returns NULL if the page index is out of bounds. */
const LayoutPage *layout_get_page(const BookLayout *layout, size_t global_page);

/* Locate the 1-based global page corresponding to a Table of Contents entry.
 *
 * If the TOC entry specifies an anchor fragment, searches through chapter
 * lines for a matching anchor ID. If unanchored, returns the first page
 * of that spine chapter. */
size_t layout_find_toc_page(const BookLayout *layout, const EpubTocItem *toc);

/* Return the 1-based global page number of the first page of a chapter. */
size_t layout_get_chapter_first_page(const BookLayout *layout, size_t chapter_index);

#endif /* RR_LAYOUT_H */
