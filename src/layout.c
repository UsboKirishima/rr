/**
 * @file layout.c
 * @brief Typesetting, full justification, and pagination engine implementation.
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

    line->spaces_after = (int *)xmalloc(count * sizeof(int));
    for (int i = 0; i < count; i++) {
        line->spaces_after[i] = 0;
    }

    /* Compute sum of word widths */
    int words_width = 0;
    for (int i = 0; i < count; i++) {
        words_width += words[i].visual_width;
    }

    int gaps = count - 1;

    /* Full justification logic */
    bool do_justify = full_justify && !is_last_line && !is_heading && !is_centered && (gaps > 0);
    int total_space_needed = col_width - words_width - indent;

    /* Avoid excessive gap stretching if line is too sparse */
    if (do_justify && total_space_needed >= gaps && (total_space_needed - gaps) <= (col_width / 2)) {
        int base_space = total_space_needed / gaps;
        int rem = total_space_needed % gaps;

        /* Alternate remainder distribution on even/odd lines to prevent vertical whitespace rivers */
        bool even_line = (cl->line_count % 2 == 0);
        for (int i = 0; i < gaps; i++) {
            int extra = 0;
            if (even_line) {
                extra = (i < rem) ? 1 : 0;
            } else {
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

static void layout_chapter_blocks(ChapterLayout *cl, ChapterDocument *doc, const EpubBook *book,
                                  int col_width, bool full_justify, int paragraph_style) {
    const char *current_section_title = doc->title;

    /* Check if there is an anchor-less TOC entry for this chapter */
    if (book) {
        for (size_t t = 0; t < book->toc_count; t++) {
            if (book->toc[t].spine_index == (int)cl->chapter_index && !book->toc[t].anchor) {
                if (strcasecmp(book->toc[t].title, "(torna all'indice)") != 0) {
                    current_section_title = book->toc[t].title;
                    break;
                }
            }
        }
    }

    for (size_t b_idx = 0; b_idx < doc->block_count; b_idx++) {
        Block *b = &doc->blocks[b_idx];

        /* Match block anchor against TOC entries */
        if (b->anchor_id && book) {
            for (size_t t = 0; t < book->toc_count; t++) {
                if (book->toc[t].spine_index == (int)cl->chapter_index &&
                    book->toc[t].anchor &&
                    strcmp(book->toc[t].anchor, b->anchor_id) == 0) {
                    current_section_title = book->toc[t].title;
                    break;
                }
            }
        }

        if (b->section_title) {
            current_section_title = b->section_title;
        }

        if (b->type == BLOCK_HR) {
            if (cl->line_count > 0 && !cl->lines[cl->line_count - 1].is_blank) {
                add_blank_line(cl, (int)b_idx, current_section_title);
            }
            add_hr_line(cl, col_width, (int)b_idx, current_section_title);
            add_blank_line(cl, (int)b_idx, current_section_title);
            continue;
        }

        if (b->word_count == 0) continue;

        if (b->type == BLOCK_HEADING) {
            /* Extra vertical space before headings */
            if (cl->line_count > 0 && !cl->lines[cl->line_count - 1].is_blank) {
                add_blank_line(cl, (int)b_idx, current_section_title);
                if (b->heading_level <= 2) {
                    add_blank_line(cl, (int)b_idx, current_section_title);
                }
            }

            bool center_heading = (b->heading_level <= 2);

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
                                     (int)b_idx, current_section_title, (start_w == 0) ? b->anchor_id : NULL);
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
                                 (int)b_idx, current_section_title, (start_w == 0) ? b->anchor_id : NULL);
            }

            /* Blank line after heading */
            add_blank_line(cl, (int)b_idx, current_section_title);
            continue;
        }

        /* Determine indentation and spacing for paragraphs and other blocks */
        int first_line_indent = 0;
        int cont_line_indent = 0;

        if (b->type == BLOCK_PARAGRAPH) {
            if (paragraph_style == 0) {
                /* Traditional book typography: 4 spaces indent, no blank line between paragraphs */
                first_line_indent = 4;
            } else {
                /* Modern spaced paragraphs: blank line between paragraphs, no indent */
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
                                 (int)b_idx, current_section_title, (start_w == 0) ? b->anchor_id : NULL);
                start_w = (int)w;
                cur_w = cont_line_indent + ww;
            } else {
                cur_w += space + ww;
            }
        }

        if ((size_t)start_w < b->word_count) {
            int count = (int)(b->word_count - start_w);
            int indent = (start_w == 0) ? first_line_indent : cont_line_indent;
            add_typeset_line(cl, &b->words[start_w], count, indent, col_width,
                             full_justify, true, false, 0, false,
                             (int)b_idx, current_section_title, (start_w == 0) ? b->anchor_id : NULL);
        }
    }
}

static void paginate_chapter(ChapterLayout *cl, int page_height) {
    if (page_height <= 0) page_height = 20;

    size_t line_idx = 0;
    while (line_idx < cl->line_count) {
        /* Skip leading blank lines at the top of a page */
        while (line_idx < cl->line_count && cl->lines[line_idx].is_blank) {
            line_idx++;
        }
        if (line_idx >= cl->line_count) break;

        size_t start = line_idx;
        size_t count = 0;

        while (line_idx < cl->line_count && count < (size_t)page_height) {
            count++;
            line_idx++;
        }

        /* Prevent orphan heading stranded at the very bottom of a page */
        if (count > 1 && line_idx < cl->line_count) {
            size_t last_idx = start + count - 1;
            if (cl->lines[last_idx].is_heading) {
                count--;
                line_idx--;
            }
        }

        /* Skip trailing blank lines on page */
        while (count > 0 && cl->lines[start + count - 1].is_blank) {
            count--;
        }

        if (count == 0) continue;

        if (cl->page_count >= cl->page_cap) {
            cl->page_cap = cl->page_cap ? cl->page_cap * 2 : 32;
            cl->pages = (LayoutPage *)xrealloc(cl->pages, cl->page_cap * sizeof(LayoutPage));
        }

        LayoutPage *page = &cl->pages[cl->page_count++];
        memset(page, 0, sizeof(LayoutPage));
        page->start_line = start;
        page->line_count = count;
        page->chapter_index = cl->chapter_index;
        page->page_in_chapter = cl->page_count;

        /* Determine active section title for page header */
        const char *sec_title = cl->lines[start].section_title;
        if (!sec_title && cl->doc) sec_title = cl->doc->title;
        page->section_title = sec_title;
    }

    /* Ensure at least 1 page even if chapter is empty */
    if (cl->page_count == 0) {
        cl->pages = (LayoutPage *)xrealloc(cl->pages, sizeof(LayoutPage));
        LayoutPage *page = &cl->pages[0];
        memset(page, 0, sizeof(LayoutPage));
        page->start_line = 0;
        page->line_count = 0;
        page->chapter_index = cl->chapter_index;
        page->page_in_chapter = 1;
        page->section_title = cl->doc ? cl->doc->title : "Chapter";
        cl->page_count = 1;
    }
}

BookLayout *layout_build(EpubBook *book, int col_width, int page_height,
                         bool full_justify, int paragraph_style) {
    if (!book) return NULL;

    BookLayout *bl = (BookLayout *)xcalloc(1, sizeof(BookLayout));
    bl->book = book;
    bl->column_width = col_width;
    bl->page_height = page_height;
    bl->full_justify = full_justify;
    bl->paragraph_style = paragraph_style;

    bl->chapter_count = book->spine_count;
    bl->chapters = (ChapterLayout *)xcalloc(bl->chapter_count, sizeof(ChapterLayout));

    size_t total_pages = 0;

    for (size_t c = 0; c < book->spine_count; c++) {
        ChapterLayout *cl = &bl->chapters[c];
        cl->chapter_index = c;

        size_t sz = 0;
        char *data = epub_read_spine_item(book, c, &sz);
        const char *def_title = epub_get_chapter_title_for_spine(book, c);

        if (data && sz > 0) {
            cl->doc = html_parse_chapter(data, sz, c, book->spine[c].item->href, def_title);
            free(data);
        } else {
            cl->doc = (ChapterDocument *)xcalloc(1, sizeof(ChapterDocument));
            cl->doc->spine_index = c;
            cl->doc->title = xstrdup(def_title);
        }

        layout_chapter_blocks(cl, cl->doc, book, col_width, full_justify, paragraph_style);
        paginate_chapter(cl, page_height);

        total_pages += cl->page_count;
    }

    bl->total_pages = total_pages;
    if (bl->total_pages == 0) bl->total_pages = 1;

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
            if (cl->doc) {
                chapter_document_free(cl->doc);
            }
        }
        free(layout->chapters);
    }

    free(layout->page_to_chapter);
    free(layout->page_to_local_page);
    free(layout);
}

const LayoutPage *layout_get_page(const BookLayout *layout, size_t global_page) {
    if (!layout || global_page == 0 || global_page > layout->total_pages) return NULL;
    size_t c = layout->page_to_chapter[global_page];
    size_t p = layout->page_to_local_page[global_page] - 1;
    if (c >= layout->chapter_count || p >= layout->chapters[c].page_count) return NULL;
    return &layout->chapters[c].pages[p];
}

size_t layout_find_toc_page(const BookLayout *layout, const EpubTocItem *toc) {
    if (!layout || !toc) return 1;

    int spine_idx = toc->spine_index;
    if (spine_idx < 0 || (size_t)spine_idx >= layout->chapter_count) {
        return 1;
    }

    const ChapterLayout *cl = &layout->chapters[spine_idx];
    if (cl->page_count == 0) return 1;

    /* If anchor specified, search lines for matching anchor */
    if (toc->anchor && *toc->anchor) {
        for (size_t l = 0; l < cl->line_count; l++) {
            if (cl->lines[l].anchor_id && strcmp(cl->lines[l].anchor_id, toc->anchor) == 0) {
                /* Find which page contains this line */
                for (size_t p = 0; p < cl->page_count; p++) {
                    if (l >= cl->pages[p].start_line &&
                        l < (cl->pages[p].start_line + cl->pages[p].line_count)) {
                        return cl->pages[p].global_page;
                    }
                }
            }
        }
    }

    /* Fallback: first page of that chapter */
    return cl->pages[0].global_page;
}

size_t layout_get_chapter_first_page(const BookLayout *layout, size_t chapter_index) {
    if (!layout || chapter_index >= layout->chapter_count) return 1;
    const ChapterLayout *cl = &layout->chapters[chapter_index];
    if (cl->page_count == 0) return 1;
    return cl->pages[0].global_page;
}
