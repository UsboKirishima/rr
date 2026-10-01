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

#include "layout.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ==========================================================================
 * Low-level line constructors
 *
 * Functions in this section append typed lines to a ChapterLayout's line array.
 * Memory for lines is managed with exponential growth reallocation.
 * ========================================================================== */

/* Append a blank vertical spacing line to the chapter layout. */
static void add_blank_line(ChapterLayout *cl, int block_idx, const char *title) {
    if (cl->line_count >= cl->line_cap) {
        cl->line_cap = cl->line_cap ? cl->line_cap * 2 : 128;
        cl->lines = (LayoutLine *)xrealloc(cl->lines, cl->line_cap * sizeof(LayoutLine));
    }
    LayoutLine *line = &cl->lines[cl->line_count++];
    memset(line, 0, sizeof(LayoutLine));
    line->is_blank = true;
    line->block_index = block_idx;
    line->section_title = title;
}

/* Append a centered horizontal divider rule to the chapter layout. */
static void add_hr_line(ChapterLayout *cl, int col_width, int block_idx, const char *title) {
    if (cl->line_count >= cl->line_cap) {
        cl->line_cap = cl->line_cap ? cl->line_cap * 2 : 128;
        cl->lines = (LayoutLine *)xrealloc(cl->lines, cl->line_cap * sizeof(LayoutLine));
    }
    LayoutLine *line = &cl->lines[cl->line_count++];
    memset(line, 0, sizeof(LayoutLine));
    line->is_hr = true;
    line->is_centered = true;
    line->total_width = col_width;
    line->block_index = block_idx;
    line->section_title = title;
}

/* Typeset a slice of words into a single line with optional full justification.
 *
 * Full Justification Mathematics:
 *   When full justification is enabled, the surplus column whitespace
 *   (col_width - sum_of_word_widths - indent) must be distributed across
 *   the (count - 1) inter-word gaps.
 *
 *   Each gap receives a base number of spaces:
 *     base_spaces = total_space_needed / gaps
 *
 *   The remainder (total_space_needed % gaps) represents extra single spaces
 *   that cannot be distributed equally. If we always distributed these extra
 *   spaces from the left, text would develop an uneven, left-heavy density.
 *   Furthermore, when adjacent lines distribute remainders on the same side,
 *   they create vertical white alleys ("rivers").
 *
 *   To counteract this, rr alternates remainder distribution direction:
 *     - Even lines distribute extra spaces from left to right.
 *     - Odd lines distribute extra spaces from right to left.
 *
 *   Paragraph termination lines, single-word lines, and headings are never
 *   stretched; they receive standard single spaces (ragged right). */
static void add_typeset_line(ChapterLayout *cl, Word *words, int count, int indent,
                             int col_width, bool full_justify, bool is_last_line,
                             bool is_heading, int h_level, bool is_centered,
                             int block_idx, const char *title, const char *anchor) {
    if (count <= 0) return;

    if (cl->line_count >= cl->line_cap) {
        cl->line_cap = cl->line_cap ? cl->line_cap * 2 : 128;
        cl->lines = (LayoutLine *)xrealloc(cl->lines, cl->line_cap * sizeof(LayoutLine));
    }

    LayoutLine *line = &cl->lines[cl->line_count++];
    memset(line, 0, sizeof(LayoutLine));

    line->words = words;
    line->word_count = count;
    line->indent_spaces = indent;
    line->is_heading = is_heading;
    line->heading_level = h_level;
    line->is_centered = is_centered;
    line->block_index = block_idx;
    line->section_title = title;
    line->anchor_id = anchor;

    /* Allocate exact spacing array for this line */
    line->spaces_after = (int *)xmalloc(count * sizeof(int));
    for (int i = 0; i < count; i++) {
        line->spaces_after[i] = 0;
    }

    /* Compute cumulative visual width of all words on this line */
    int words_width = 0;
    for (int i = 0; i < count; i++) {
        words_width += words[i].visual_width;
    }

    int gaps = count - 1;

    /* Determine whether to justify this line */
    bool do_justify = full_justify && !is_last_line && !is_heading && !is_centered && (gaps > 0);
    int total_space_needed = col_width - words_width - indent;

    /* Safeguard against extreme space stretching if a line is sparsely filled */
    if (do_justify && total_space_needed >= gaps && (total_space_needed - gaps) <= (col_width / 2)) {
        int base_space = total_space_needed / gaps;
        int rem = total_space_needed % gaps;

        /* Alternating remainder distribution across odd/even lines */
        bool even_line = (cl->line_count % 2 == 0);
        for (int i = 0; i < gaps; i++) {
            int extra = 0;
            if (even_line) {
                /* Distribute extra spaces from left to right */
                extra = (i < rem) ? 1 : 0;
            } else {
                /* Distribute extra spaces from right to left */
                extra = (i >= (gaps - rem)) ? 1 : 0;
            }
            line->spaces_after[i] = base_space + extra;
        }
        line->spaces_after[count - 1] = 0;
        line->total_width = col_width;
    } else {
        /* Natural spacing (ragged right or last line of paragraph) */
        int cur_w = indent + words_width;
        for (int i = 0; i < gaps; i++) {
            line->spaces_after[i] = 1;
            cur_w += 1;
        }
        line->spaces_after[count - 1] = 0;
        line->total_width = cur_w;
    }
}

/* ==========================================================================
 * Title identification helpers
 *
 * Chapter and section titles reach the layout engine from three different
 * sources: the navigation document (toc.ncx), the parsed document title, and
 * the raw text of the first block of a chapter. The helpers below put those
 * sources on equal footing so a title can be recognised, compared, and lifted
 * out of the running text without duplicating it.
 * ========================================================================== */

/* Detect navigation-only titles that must never become a visible title.
 *
 * Scanned and hand-made books frequently seed their table of contents with
 * pure navigation links ("(torna all'indice)", "Indice", "Back to index").
 * Such entries are addressed for navigation but are not chapter titles. */
static bool is_nav_junk_title(const char *title) {
    if (!title || !*title) return true;

    static const char *const junk[] = {
        "(torna all'indice)", "torna all'indice", "torna all’ indice",
        "torna all'indice", "torner", "indice", "indice generale",
        "index", "contents", "table of contents", "sommario",
        "indietro", "back", "<-", "←", NULL
    };

    for (int i = 0; junk[i]; i++) {
        if (strcasecmp(title, junk[i]) == 0) return true;
    }
    return false;
}

/* Normalize a title for comparison.
 *
 * Collapses whitespace runs into single spaces and unifies typographic
 * variants of the same character (three dots versus the ellipsis glyph), so
 * that a title taken from the table of contents matches the copy printed in
 * the body of the text. */
static void normalize_title(const char *in, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!in) return;

    size_t o = 0;
    bool pending_space = false;

    for (size_t i = 0; in[i] != '\0'; i++) {
        if (in[i] == ' ' || in[i] == '\t' || in[i] == '\n' || in[i] == '\r') {
            pending_space = (o > 0);
            continue;
        }
        if (o + 1 >= out_size) break;

        /* Unify the horizontal ellipsis: some sources spell it out as "..." */
        if (in[i] == '.' && in[i + 1] == '.' && in[i + 2] == '.') {
            if (o + 4 > out_size) break;
            if (pending_space) out[o++] = ' ';
            out[o++] = (char)0xE2; /* U+2026 HORIZONTAL ELLIPSIS */
            out[o++] = (char)0x80;
            out[o++] = (char)0xA6;
            i += 2;
            pending_space = false;
            continue;
        }

        if (pending_space) {
            if (o + 2 >= out_size) break;
            out[o++] = ' ';
            pending_space = false;
        }
        out[o++] = in[i];
    }
    out[o] = '\0';
}

/* Case-insensitive comparison of two titles, tolerant of whitespace and
 * typographic differences between their sources. */
static bool titles_equal(const char *a, const char *b) {
    char na[256], nb[256];
    normalize_title(a, na, sizeof(na));
    normalize_title(b, nb, sizeof(nb));
    return na[0] != '\0' && strcmp(na, nb) == 0;
}

/* Decide whether a block only restates the title of the section it opens.
 *
 * Headings always do. Short paragraphs do when they read like a title, that is
 * when the section title is contained in them ("VII - Cambio treno (torna
 * all'indice)" announces "Cambio treno") or they are contained in it. Longer
 * paragraphs are genuine prose and are always kept. */
static bool block_repeats_title(const Block *b, const char *title) {
    if (!b || !title || !*title) return false;
    if (b->type == BLOCK_HEADING) return true;
    if (b->type != BLOCK_PARAGRAPH) return false;
    if (b->word_count == 0 || b->word_count > 12) return false;

    /* Join the block's words into one plain-text line, as the reader sees it */
    char text[512];
    size_t o = 0;
    text[0] = '\0';
    for (size_t i = 0; i < b->word_count; i++) {
        size_t wl = strlen(b->words[i].text);
        size_t sep = (i > 0) ? 1 : 0;
        if (o + sep + wl + 1 >= sizeof(text)) break;
        if (sep) text[o++] = ' ';
        memcpy(text + o, b->words[i].text, wl);
        o += wl;
        text[o] = '\0';
    }
    if (o < 2) return false;

    char nt[256], nb[512];
    normalize_title(title, nt, sizeof(nt));
    normalize_title(text, nb, sizeof(nb));
    if (nt[0] == '\0' || nb[0] == '\0') return false;

    if (str_case_contains(nb, nt)) return true;
    return str_case_contains(nt, nb);
}

/* Report whether a table of contents title is actually located in this spine
 * item.
 *
 * Spine metadata is not always faithful: a single XHTML file can hold several
 * chapters, and the navigation may list a title against an item whose text
 * never carries its anchor. An anchored title counts only when the document
 * really contains that anchor, so a chapter is not named after a neighbour's
 * section. Entries without an anchor describe the item as a whole and are
 * always accepted. */
static bool titled_by_this_item(const EpubBook *book, const ChapterDocument *doc,
                                const char *toc_title) {
    if (!book || !toc_title) return true;
    for (size_t t = 0; t < book->toc_count; t++) {
        if (book->toc[t].title != toc_title) continue;
        if (!book->toc[t].anchor) return true;
        if (!doc) return false;
        for (size_t b = 0; b < doc->block_count; b++) {
            if (doc->blocks[b].anchor_id &&
                strcmp(doc->blocks[b].anchor_id, book->toc[t].anchor) == 0) {
                return true;
            }
        }
        return false;
    }
    return true;
}

/* Resolve the title carried by a chapter's leading title page.
 *
 * Preference order: a table of contents entry naming the whole spine item,
 * then any other entry of that spine item, then the document's own title, and
 * finally the book title, so that even cover-only or untitled spine items get a
 * sensible page instead of a bare placeholder. */
static const char *resolve_chapter_title(const EpubBook *book, const ChapterDocument *doc,
                                         size_t chapter_index) {
    if (book) {
        /* Pass 0 prefers entries without an anchor (whole-chapter titles),
         * pass 1 accepts any entry belonging to the spine item. Later entries
         * are skipped so a spine item holding several chapters is named after
         * the first one it actually contains. */
        for (int pass = 0; pass < 2; pass++) {
            for (size_t t = 0; t < book->toc_count; t++) {
                if (book->toc[t].spine_index != (int)chapter_index) continue;
                if (pass == 0 && book->toc[t].anchor) continue;
                if (is_nav_junk_title(book->toc[t].title)) continue;
                if (!titled_by_this_item(book, doc, book->toc[t].title)) continue;
                return book->toc[t].title;
            }
        }
    }

    if (doc && doc->title && *doc->title &&
        strcmp(doc->title, "Chapter") != 0 && !is_nav_junk_title(doc->title)) {
        return doc->title;
    }

    if (book && book->title && *book->title) return book->title;
    return (doc && doc->title) ? doc->title : "Chapter";
}

/* ==========================================================================
 * Block typesetting and word wrapping
 *
 * Iterates through semantic blocks, applies paragraph formatting rules
 * (classic first-line indent vs modern spaced lines), wraps words at column
 * boundaries, and attaches TOC section titles.
 * ========================================================================== */

/* Mark the first line emitted for a newly opened section.
 *
 * Pagination reads this flag to close the running page and insert the
 * section's title page before the line is placed. `since` is the line count
 * observed before the block started emitting its content. */
static void flush_section_start(ChapterLayout *cl, bool *pending, size_t since) {
    if (*pending && since < cl->line_count) {
        cl->lines[since].is_section_start = true;
    }
    *pending = false;
}

static void layout_chapter_blocks(ChapterLayout *cl, const ChapterDocument *doc, const EpubBook *book,
                                  int col_width, bool full_justify, int paragraph_style) {
    const char *current_section_title = cl->chapter_title;

    /* Set when a titled section has begun: the next content line emitted is
     * flagged so that pagination can give that section a title page. */
    bool pending_section_start = false;

    /* Anchor of a title block moved onto a title page, kept alive so table of
     * contents jumps can still resolve it. */
    const char *pending_anchor = NULL;

    for (size_t b_idx = 0; b_idx < doc->block_count; b_idx++) {
        const Block *b = &doc->blocks[b_idx];

        /* Locate the table of contents entry anchored to this block, if any */
        const char *toc_title = NULL;
        if (b->anchor_id && book) {
            for (size_t t = 0; t < book->toc_count; t++) {
                if (book->toc[t].spine_index == (int)cl->chapter_index &&
                    book->toc[t].anchor &&
                    strcmp(book->toc[t].anchor, b->anchor_id) == 0) {
                    toc_title = book->toc[t].title;
                    break;
                }
            }
        }

        /* An anchored table of contents entry opens a titled section, unless it
         * is a pure navigation link rather than a title. */
        bool opens_section = toc_title && !is_nav_junk_title(toc_title);
        if (opens_section) {
            current_section_title = toc_title;
        } else if (b->section_title) {
            current_section_title = b->section_title;
        }

        /* --- Case 1: Thematic Break (<hr>) --- */
        if (b->type == BLOCK_HR) {
            if (cl->line_count > 0 && !cl->lines[cl->line_count - 1].is_blank) {
                add_blank_line(cl, (int)b_idx, current_section_title);
            }
            size_t hr_line = cl->line_count;
            add_hr_line(cl, col_width, (int)b_idx, current_section_title);
            flush_section_start(cl, &pending_section_start, hr_line);
            add_blank_line(cl, (int)b_idx, current_section_title);
            continue;
        }

        if (b->word_count == 0) continue;

        /* A new section begins here: pagination will close the running page and
         * give the section a title page before its first line of text.
         *
         * A heading recovered from CSS opens a section even without a table of
         * contents entry behind it. That is what lets a book with no usable
         * navigation still present its chapters one screen at a time. */
        if (opens_section || (b->type == BLOCK_HEADING && b->section_title)) {
            pending_section_start = true;
        }

        /* --- Case 2: Title blocks lifted out of the running text ---
         *
         * A title is never printed inline: the section it introduces is about
         * to be given a title page, and a chapter's leading title already has
         * one. Only blocks that genuinely restate the title are dropped, so
         * real prose is never lost. */
        bool is_title_block = (opens_section || toc_title || cl->line_count == 0) &&
                              (b->type == BLOCK_HEADING || b->type == BLOCK_PARAGRAPH) &&
                              block_repeats_title(b, current_section_title);
        if (is_title_block) {
            pending_anchor = b->anchor_id;
            continue;
        }

        /* Anchor of the first line of this block. The anchor of a title block
         * just lifted onto a title page takes precedence, so that table of
         * contents jumps still resolve to the section's opening page. */
        const char *first_anchor = pending_anchor ? pending_anchor : b->anchor_id;
        pending_anchor = NULL;

        /* --- Case 3: Headings (<h1> - <h6>) --- */
        if (b->type == BLOCK_HEADING) {
            /* Generous vertical whitespace preceding headings */
            if (cl->line_count > 0 && !cl->lines[cl->line_count - 1].is_blank) {
                add_blank_line(cl, (int)b_idx, current_section_title);
                if (b->heading_level <= 2) {
                    add_blank_line(cl, (int)b_idx, current_section_title);
                }
            }

            bool center_heading = (b->heading_level <= 2);
            size_t heading_line = cl->line_count;

            /* Wrap heading words */
            int start_w = 0;
            int cur_w = 0;
            for (size_t w = 0; w < b->word_count; w++) {
                int ww = b->words[w].visual_width;
                int space = (w > (size_t)start_w) ? 1 : 0;
                if (cur_w + space + ww > col_width && w > (size_t)start_w) {
                    int count = (int)(w - start_w);
                    add_typeset_line(cl, &b->words[start_w], count, 0, col_width,
                                     false, true, true, b->heading_level, center_heading,
                                     (int)b_idx, current_section_title, (start_w == 0) ? first_anchor : NULL);
                    start_w = (int)w;
                    cur_w = ww;
                } else {
                    cur_w += space + ww;
                }
            }
            if ((size_t)start_w < b->word_count) {
                int count = (int)(b->word_count - start_w);
                add_typeset_line(cl, &b->words[start_w], count, 0, col_width,
                                 false, true, true, b->heading_level, center_heading,
                                 (int)b_idx, current_section_title, (start_w == 0) ? first_anchor : NULL);
            }

            flush_section_start(cl, &pending_section_start, heading_line);

            /* Trailing blank line after heading */
            add_blank_line(cl, (int)b_idx, current_section_title);
            continue;
        }

        /* --- Case 4: Paragraphs, Blockquotes, and Lists --- */
        int first_line_indent = 0;
        int cont_line_indent = 0;

        if (b->type == BLOCK_PARAGRAPH) {
            if (paragraph_style == 0) {
                /* Traditional paperback typography: 4-space first-line indent, no blank lines */
                first_line_indent = 4;
            } else {
                /* Modern digital typography: blank line separator, zero indent */
                if (cl->line_count > 0 && !cl->lines[cl->line_count - 1].is_blank) {
                    add_blank_line(cl, (int)b_idx, current_section_title);
                }
            }
        } else if (b->type == BLOCK_BLOCKQUOTE) {
            first_line_indent = 4;
            cont_line_indent = 4;
            if (cl->line_count > 0 && !cl->lines[cl->line_count - 1].is_blank) {
                add_blank_line(cl, (int)b_idx, current_section_title);
            }
        } else if (b->type == BLOCK_LIST_ITEM) {
            first_line_indent = 2;
            cont_line_indent = 4;
        }

        size_t body_line = cl->line_count;

        /* Wrap block words into typeset lines */
        int start_w = 0;
        int cur_w = first_line_indent;

        for (size_t w = 0; w < b->word_count; w++) {
            int ww = b->words[w].visual_width;
            int space = (w > (size_t)start_w) ? 1 : 0;

            if (cur_w + space + ww > col_width && w > (size_t)start_w) {
                int count = (int)(w - start_w);
                int indent = (start_w == 0) ? first_line_indent : cont_line_indent;
                add_typeset_line(cl, &b->words[start_w], count, indent, col_width,
                                 full_justify, false, false, 0, false,
                                 (int)b_idx, current_section_title, (start_w == 0) ? first_anchor : NULL);
                start_w = (int)w;
                cur_w = cont_line_indent + ww;
            } else {
                cur_w += space + ww;
            }
        }

        /* Final line of paragraph (ragged right) */
        if ((size_t)start_w < b->word_count) {
            int count = (int)(b->word_count - start_w);
            int indent = (start_w == 0) ? first_line_indent : cont_line_indent;
            add_typeset_line(cl, &b->words[start_w], count, indent, col_width,
                             full_justify, true, false, 0, false,
                             (int)b_idx, current_section_title, (start_w == 0) ? first_anchor : NULL);
        }

        flush_section_start(cl, &pending_section_start, body_line);
    }
}

/* ==========================================================================
 * Screen pagination and orphan control
 *
 * Divides chapter lines into fixed-height screen pages.
 * Enforces typographic aesthetics:
 *   - Opens every chapter on a dedicated title page carrying only the chapter
 *     title, so the text begins on a clean screen.
 *   - Closes the running page and inserts a title page whenever a titled
 *     section begins, keeping titles out of the middle of the prose.
 *   - Strips top-of-page blank lines so text starts cleanly under the header.
 *   - Strips bottom-of-page blank lines.
 *   - Prevents orphan headings: if a heading falls on the very last line of a
 *     page without following body text, it is moved to the top of the next page.
 * ========================================================================== */

/* Append an empty page descriptor to a chapter, growing the page array. */
static LayoutPage *push_page(ChapterLayout *cl) {
    if (cl->page_count >= cl->page_cap) {
        cl->page_cap = cl->page_cap ? cl->page_cap * 2 : 32;
        cl->pages = (LayoutPage *)xrealloc(cl->pages, cl->page_cap * sizeof(LayoutPage));
    }
    LayoutPage *page = &cl->pages[cl->page_count++];
    memset(page, 0, sizeof(LayoutPage));
    page->chapter_index = cl->chapter_index;
    page->page_in_chapter = cl->page_count;
    return page;
}

/* Insert a standalone title page carrying `title` and nothing else.
 *
 * Titles that only navigate (back-to-index links) are ignored, and a title page
 * that would directly repeat the previous one is merged into it, so a spine
 * chapter opening on the very section it is named after yields a single page. */
static void add_title_page(ChapterLayout *cl, const char *title, const char *anchor) {
    if (is_nav_junk_title(title)) return;

    if (cl->page_count > 0) {
        LayoutPage *last = &cl->pages[cl->page_count - 1];
        if (last->is_title_page && titles_equal(last->section_title, title)) {
            /* Keep the merged page reachable for anchor navigation */
            if (!last->anchor_id && anchor) last->anchor_id = anchor;
            return;
        }
    }

    LayoutPage *page = push_page(cl);
    page->is_title_page = true;
    page->section_title = title;
    page->anchor_id = anchor;
}

/* Fold an anchor into the chapter's own title page, and report success.
 *
 * Books whose spine items are cut at slightly arbitrary points place the anchor
 * of a chapter's first section a few paragraphs into the item, so the section
 * announces the very title the chapter page already shows. Rather than repeat
 * it mid-chapter, the anchor moves to the chapter's title page and table of
 * contents jumps land there instead. */
static bool adopt_anchor_by_chapter_title_page(ChapterLayout *cl, const char *anchor) {
    if (cl->page_count == 0 || !cl->pages[0].is_title_page) return false;
    if (anchor && !cl->pages[0].anchor_id) cl->pages[0].anchor_id = anchor;
    return true;
}

/* Test whether any typeset text follows line `from` inside the chapter. */
static bool has_body_text(const ChapterLayout *cl, size_t from) {
    for (size_t i = from; i < cl->line_count; i++) {
        if (!cl->lines[i].is_blank) return true;
    }
    return false;
}

static void paginate_chapter(ChapterLayout *cl, int page_height) {
    if (page_height <= 0) page_height = 20;

    /* Each chapter opens on a page of its own bearing only its title */
    add_title_page(cl, cl->chapter_title, NULL);

    size_t line_idx = 0;
    while (line_idx < cl->line_count) {
        /* Discard leading blank lines at the top of a page */
        while (line_idx < cl->line_count && cl->lines[line_idx].is_blank) {
            line_idx++;
        }
        if (line_idx >= cl->line_count) break;

        /* A titled section never shares a screen with the text preceding it:
         * close the page here and give the section its own title page. */
        if (cl->lines[line_idx].is_section_start) {
            const LayoutLine *head = &cl->lines[line_idx];
            if (has_body_text(cl, line_idx + 1)) {
                /* A section repeating its own chapter's title needs no second
                 * page; the chapter title page stands in for it. */
                if (titles_equal(head->section_title, cl->chapter_title) &&
                    adopt_anchor_by_chapter_title_page(cl, head->anchor_id)) {
                    /* already announced at the chapter's title page */
                } else {
                    add_title_page(cl, head->section_title, head->anchor_id);
                }
            }
        }

        size_t start = line_idx;
        size_t count = 0;

        while (line_idx < cl->line_count && count < (size_t)page_height) {
            /* Never place text that follows a section opening on this screen */
            if (count > 0 && cl->lines[line_idx].is_section_start) break;
            count++;
            line_idx++;
        }

        /* Prevent orphan headings stranded at the bottom of a page */
        if (count > 1 && line_idx < cl->line_count) {
            size_t last_idx = start + count - 1;
            if (cl->lines[last_idx].is_heading) {
                count--;
                line_idx--;
            }
        }

        /* Strip trailing blank lines at bottom of page */
        while (count > 0 && cl->lines[start + count - 1].is_blank) {
            count--;
        }

        if (count == 0) continue;

        LayoutPage *page = push_page(cl);
        page->start_line = start;
        page->line_count = count;

        /* Section title for running header */
        const char *sec_title = cl->lines[start].section_title;
        if (!sec_title && cl->doc) sec_title = cl->doc->title;
        page->section_title = sec_title;
    }

    /* Guarantee at least 1 page even if chapter is blank */
    if (cl->page_count == 0) {
        LayoutPage *page = push_page(cl);
        page->section_title = cl->doc ? cl->doc->title : "Chapter";
    }
}

/* ==========================================================================
 * High-level layout construction and lookup
 * ========================================================================== */

/* Build the complete layout for an opened EPUB book.
 *
 * Loads each chapter's XHTML from the archive, parses semantic blocks,
 * typesets lines, paginates screens, and constructs the global page lookup tables. */
BookLayout *layout_build(EpubBook *book, int col_width, int page_height,
                         bool full_justify, int paragraph_style) {
    if (!book) return NULL;

    /* Ensure the reading cache is warm. Typesetting then reuses the parsed
     * documents rather than reading and parsing the book again. */
    epub_load_documents(book);

    BookLayout *bl = (BookLayout *)xcalloc(1, sizeof(BookLayout));
    bl->book = book;
    bl->column_width = col_width;
    bl->page_height = page_height;
    bl->full_justify = full_justify;
    bl->paragraph_style = paragraph_style;

    bl->chapter_count = book->spine_count;
    bl->chapters = (ChapterLayout *)xcalloc(bl->chapter_count, sizeof(ChapterLayout));

    size_t total_pages = 0;

    /* Format each spine chapter in reading order */
    for (size_t c = 0; c < book->spine_count; c++) {
        ChapterLayout *cl = &bl->chapters[c];
        cl->chapter_index = c;

        /* Parse the chapter once, when the book is opened, and reuse the
         * result. This layout rebuild runs on every resize and every
         * typography toggle, so re-reading and re-parsing the whole novel here
         * would make those interactions visibly slow. */
        const ChapterDocument *doc = epub_get_document(book, c);
        cl->doc = (ChapterDocument *)doc;

        cl->chapter_title = resolve_chapter_title(book, cl->doc, c);

        layout_chapter_blocks(cl, cl->doc, book, col_width, full_justify, paragraph_style);
        paginate_chapter(cl, page_height);

        total_pages += cl->page_count;
    }

    bl->total_pages = total_pages;
    if (bl->total_pages == 0) bl->total_pages = 1;

    /* Build fast O(1) global-to-local page translation lookup tables */
    bl->page_to_chapter = (size_t *)xmalloc((bl->total_pages + 1) * sizeof(size_t));
    bl->page_to_local_page = (size_t *)xmalloc((bl->total_pages + 1) * sizeof(size_t));

    size_t g_page = 1;
    for (size_t c = 0; c < bl->chapter_count; c++) {
        ChapterLayout *cl = &bl->chapters[c];
        for (size_t p = 0; p < cl->page_count; p++) {
            cl->pages[p].global_page = g_page;
            bl->page_to_chapter[g_page] = c;
            bl->page_to_local_page[g_page] = p + 1;
            g_page++;
        }
    }

    return bl;
}

/* Free all memory associated with a BookLayout. */
void layout_free(BookLayout *layout) {
    if (!layout) return;

    if (layout->chapters) {
        for (size_t c = 0; c < layout->chapter_count; c++) {
            ChapterLayout *cl = &layout->chapters[c];
            if (cl->lines) {
                for (size_t l = 0; l < cl->line_count; l++) {
                    free(cl->lines[l].spaces_after);
                }
                free(cl->lines);
            }
            free(cl->pages);
            /* `cl->doc` belongs to the book's reading cache and is released by
             * epub_close, not here. The lines above point into it. */
        }
        free(layout->chapters);
    }

    free(layout->page_to_chapter);
    free(layout->page_to_local_page);
    free(layout);
}

/* Resolve a 1-based global page index to its LayoutPage descriptor. */
const LayoutPage *layout_get_page(const BookLayout *layout, size_t global_page) {
    if (!layout || global_page == 0 || global_page > layout->total_pages) return NULL;
    size_t c = layout->page_to_chapter[global_page];
    size_t p = layout->page_to_local_page[global_page] - 1;
    if (c >= layout->chapter_count || p >= layout->chapters[c].page_count) return NULL;
    return &layout->chapters[c].pages[p];
}

/* Find the 1-based global page corresponding to a TOC entry.
 * Resolves HTML anchor IDs to the exact page containing that line. */
size_t layout_find_toc_page(const BookLayout *layout, const EpubTocItem *toc) {
    if (!layout || !toc) return 1;

    int spine_idx = toc->spine_index;
    if (spine_idx < 0 || (size_t)spine_idx >= layout->chapter_count) {
        return 1;
    }

    const ChapterLayout *cl = &layout->chapters[spine_idx];
    if (cl->page_count == 0) return 1;

    /* If anchor specified, search chapter pages for the matching anchor ID */
    if (toc->anchor && *toc->anchor) {
        /* Title pages own the anchor of the section they introduce, so a table
         * of contents jump lands on the chapter's opening page. */
        for (size_t p = 0; p < cl->page_count; p++) {
            if (cl->pages[p].anchor_id && strcmp(cl->pages[p].anchor_id, toc->anchor) == 0) {
                return cl->pages[p].global_page;
            }
        }

        /* Otherwise find the typeset line carrying the anchor */
        for (size_t l = 0; l < cl->line_count; l++) {
            if (cl->lines[l].anchor_id && strcmp(cl->lines[l].anchor_id, toc->anchor) == 0) {
                /* Identify which page contains this line */
                for (size_t p = 0; p < cl->page_count; p++) {
                    if (l >= cl->pages[p].start_line &&
                        l < (cl->pages[p].start_line + cl->pages[p].line_count)) {
                        return cl->pages[p].global_page;
                    }
                }
            }
        }
    }

    /* Fallback: first page of the chapter */
    return cl->pages[0].global_page;
}

/* Return the 1-based global page number of the first page of a chapter. */
size_t layout_get_chapter_first_page(const BookLayout *layout, size_t chapter_index) {
    if (!layout || chapter_index >= layout->chapter_count) return 1;
    const ChapterLayout *cl = &layout->chapters[chapter_index];
    if (cl->page_count == 0) return 1;
    return cl->pages[0].global_page;
}
