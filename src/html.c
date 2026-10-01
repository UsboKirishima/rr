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

#include "html.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/HTMLparser.h>

/* ==========================================================================
 * Parser internal state
 *
 * During recursive DOM traversal, this context structure tracks the active
 * document, current block being accumulated, active style flags, and any
 * HTML anchor ID waiting to attach to the next content block.
 *
 * `font_ratio` is the accumulated `font-size` multiplier of the element being
 * read. Font sizes are relative, so the value is multiplied down the element
 * tree: a 1.25em span inside an unchanged body contributes 1.25, and each word
 * remembers the ratio that was in force where it was found.
 * ========================================================================== */

typedef struct {
    ChapterDocument *doc;    /* Document currently being populated */
    Block *current_block;    /* Block currently receiving words */
    int current_style;       /* Active bitmask of TextStyle flags */
    char *pending_anchor;    /* Anchor ID encountered before text appeared */
    bool in_pre;             /* True when inside <pre> preformatted block */
    float font_ratio;        /* Active font-size multiplier from CSS */
    const CssIndex *css;     /* Compiled stylesheets, or NULL when unstyled */
} ParseContext;

/* ==========================================================================
 * Block and word constructors
 * ========================================================================== */

/* Allocate and append a new semantic block to the chapter document.
 * Grows the blocks array with exponential reallocation.
 *
 * `css_font_ratio` starts at 1.0, the neutral ratio: a block only looks like
 * display type if some rule actually enlarged the type inside it. */
static Block *create_block(ChapterDocument *doc, BlockType type, int heading_level, char *anchor) {
    if (doc->block_count >= doc->block_cap) {
        doc->block_cap = doc->block_cap ? doc->block_cap * 2 : 64;
        doc->blocks = (Block *)xrealloc(doc->blocks, doc->block_cap * sizeof(Block));
    }

    Block *b = &doc->blocks[doc->block_count++];
    memset(b, 0, sizeof(Block));
    b->type = type;
    b->heading_level = heading_level;
    b->anchor_id = anchor;
    b->words = NULL;
    b->word_count = 0;
    b->word_cap = 0;
    b->css_font_ratio = 1.0f;
    return b;
}

/* Append an indivisible word token to a block.
 *
 * Pre-computes the word's visual terminal width using `utf8_strwidth` so that
 * the layout engine never needs to recalculate character widths during
 * re-wrapping or page rendering.
 *
 * The block also records the largest font ratio seen among its words, which is
 * what distinguishes a title set in 1.25em from ordinary body copy. */
static void add_word_to_block(Block *b, const char *text, int style, bool space_after,
                              float font_ratio) {
    if (!text || !*text) return;

    if (b->word_count >= b->word_cap) {
        b->word_cap = b->word_cap ? b->word_cap * 2 : 32;
        b->words = (Word *)xrealloc(b->words, b->word_cap * sizeof(Word));
    }

    Word *w = &b->words[b->word_count++];
    w->text = xstrdup(text);
    w->visual_width = utf8_strwidth(text);
    w->style = style;
    w->space_after = space_after;

    if (font_ratio > b->css_font_ratio) b->css_font_ratio = font_ratio;
}

/* ==========================================================================
 * HTML whitespace collapsing
 *
 * In HTML typography, consecutive whitespace characters (spaces, tabs,
 * newlines, carriage returns) are collapsed into a single space.
 * Non-breaking spaces (U+00A0) are also treated as whitespace delimiters
 * between tokens.
 * ========================================================================== */

/* Test if the byte sequence at `p` begins an HTML whitespace character.
 *
 * Checks ASCII whitespace (' ', '\t', '\n', '\r') as well as the 2-byte
 * UTF-8 encoding of the non-breaking space (U+00A0: 0xC2 0xA0).
 * If matched, stores the byte length of the whitespace token in `out_len`. */
static bool is_html_whitespace(const char *p, size_t *out_len) {
    if (!p || !*p) {
        if (out_len) *out_len = 0;
        return false;
    }
    /* Standard ASCII whitespace */
    if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        if (out_len) *out_len = 1;
        return true;
    }
    /* UTF-8 non-breaking space U+00A0: 0xC2 0xA0 */
    if ((unsigned char)p[0] == 0xC2 && (unsigned char)p[1] == 0xA0) {
        if (out_len) *out_len = 2;
        return true;
    }
    if (out_len) *out_len = 0;
    return false;
}

/* Parse raw text content from a DOM text node into word tokens.
 *
 * Implements HTML whitespace collapsing: leading and trailing spaces set the
 * `space_after` flag on surrounding tokens, and consecutive interior spaces
 * are collapsed into a single word boundary. */
static void parse_text_node(ParseContext *ctx, const char *text) {
    if (!text || !*text) return;

    /* Ensure we have an active block to receive words */
    if (!ctx->current_block) {
        ctx->current_block = create_block(ctx->doc, BLOCK_PARAGRAPH, 0, ctx->pending_anchor);
        ctx->pending_anchor = NULL;
    }

    const char *p = text;
    size_t wlen = 0;

    /* If text begins with whitespace and a preceding word exists in this block,
     * mark that preceding word as followed by a space. */
    if (is_html_whitespace(p, &wlen)) {
        if (ctx->current_block->word_count > 0) {
            ctx->current_block->words[ctx->current_block->word_count - 1].space_after = true;
        }
        while (is_html_whitespace(p, &wlen)) {
            p += wlen;
        }
    }

    char word_buf[4096];
    while (*p) {
        /* Extract contiguous non-whitespace characters into word buffer */
        size_t b_idx = 0;
        while (*p && !is_html_whitespace(p, &wlen)) {
            if (b_idx + 1 < sizeof(word_buf)) {
                word_buf[b_idx++] = *p;
            }
            p++;
        }
        word_buf[b_idx] = '\0';

        /* If we extracted a valid word, check if trailing whitespace follows */
        if (b_idx > 0) {
            bool has_space = false;
            if (is_html_whitespace(p, &wlen)) {
                has_space = true;
                while (is_html_whitespace(p, &wlen)) {
                    p += wlen;
                }
            }
            add_word_to_block(ctx->current_block, word_buf, ctx->current_style, has_space,
                              ctx->font_ratio);
        }
    }
}

/* ==========================================================================
 * DOM helpers
 * ========================================================================== */

/* Report whether an element's subtree contains any visible text.
 *
 * Converted chapter files are littered with empty scaffolding elements:
 *
 *     <div class="mbppagebreak" id="calibre_pb_7"></div>
 *
 * That id names a place in the *print* pagination, not a place in the text. If
 * it were captured like a normal anchor it would attach to the paragraph above
 * it, and the table of contents would appear to open one paragraph early.
 * Elements with no text of their own therefore never anchor a block. */
static bool element_has_text(const xmlNode *node) {
    for (const xmlNode *cur = node; cur; cur = cur->next) {
        if (cur->type == XML_TEXT_NODE) {
            if (!cur->content) continue;
            /* 0xC2 is the lead byte of U+00A0, the non-breaking space, which
             * every converted book sprinkles between words; it is not text. */
            for (const xmlChar *p = cur->content; *p; p++) {
                unsigned char c = (unsigned char)*p;
                if (!isspace(c) && c != 0xC2) return true;
            }
        } else if (cur->type == XML_ELEMENT_NODE) {
            if (element_has_text(cur->children)) return true;
        }
    }
    return false;
}

/* ==========================================================================
 * Recursive DOM traversal
 *
 * Walks the libxml2 DOM tree, managing:
 *   - Element styling (pushing/popping bold, italic, code styles), taken both
 *     from presentational markup (<b>, <em>, ...) and from the book.
 *   - Block boundary creation (<p>, <h1>-<h6>, <blockquote>, <li>, <hr>).
 *   - Accumulation of the typographic evidence (font size, centring, forced
 *     breaks) that html_classify_headings later turns into real headings.
 *   - Anchor ID capture for Table of Contents navigation.
 *   - Filtering of non-content elements (<script>, <style>, <head>, <svg>).
 * ========================================================================== */

static void traverse_dom(ParseContext *ctx, xmlNode *node) {
    for (xmlNode *cur = node; cur; cur = cur->next) {
        if (cur->type == XML_TEXT_NODE) {
            if (cur->content) {
                parse_text_node(ctx, (const char *)cur->content);
            }
        } else if (cur->type == XML_ELEMENT_NODE) {
            const char *name = (const char *)cur->name;

            /* Filter out non-content head, script, and vector graphics elements */
            if (strcasecmp(name, "script") == 0 ||
                strcasecmp(name, "style") == 0 ||
                strcasecmp(name, "head") == 0 ||
                strcasecmp(name, "svg") == 0 ||
                strcasecmp(name, "math") == 0) {
                continue;
            }

            /* Identify block elements */
            bool is_p = (strcasecmp(name, "p") == 0);
            bool is_h = (name[0] == 'h' || name[0] == 'H') && (name[1] >= '1' && name[1] <= '6') && name[2] == '\0';
            bool is_blockquote = (strcasecmp(name, "blockquote") == 0);
            bool is_li = (strcasecmp(name, "li") == 0);
            bool is_hr = (strcasecmp(name, "hr") == 0);
            bool is_br = (strcasecmp(name, "br") == 0);
            bool is_pre = (strcasecmp(name, "pre") == 0);

            /* --- Attributes and stylesheet resolution ---------------------
             *
             * The `id` attribute doubles as the NCX anchor target, so it is
             * read once and serves both navigation and id-selector matching. */
            xmlChar *id_attr = xmlGetProp(cur, (const xmlChar *)"id");
            if (!id_attr) {
                id_attr = xmlGetProp(cur, (const xmlChar *)"name");
            }
            xmlChar *class_attr = xmlGetProp(cur, (const xmlChar *)"class");
            xmlChar *style_attr = xmlGetProp(cur, (const xmlChar *)"style");

            /* Resolve this element's typographic style. Even when the book
             * ships no stylesheet at all, an inline `style` attribute is
             * enough to reveal that a paragraph is meant to be centred. */
            CssStyle css_style;
            if (ctx->css) {
                css_resolve(ctx->css, name, (const char *)class_attr,
                            (const char *)id_attr, (const char *)style_attr, &css_style);
            } else {
                memset(&css_style, 0, sizeof(css_style));
                css_style.font_ratio = 1.0f;
            }

            /* Font sizes are relative, so the multiplier is threaded down the
             * element tree and clamped: a runaway stylesheet must not be able
             * to shrink a chapter out of existence or blow it up to nonsense. */
            float prev_ratio = ctx->font_ratio;
            ctx->font_ratio *= css_style.font_ratio;
            if (ctx->font_ratio < 0.2f) ctx->font_ratio = 0.2f;
            if (ctx->font_ratio > 5.0f) ctx->font_ratio = 5.0f;

            int prev_style = ctx->current_style;

            /* --- Anchor capture ------------------------------------------
             *
             * A block element opens a fresh block below that inherits the
             * anchor, so only an anchor nested inside an already open block
             * (e.g. <a name> within a <p>) belongs to that block. Attributing
             * it to both would place a section one block earlier than it
             * appears in the text. */
            if (id_attr) {
                /* `<a name="x"></a>` is the standard way to plant an anchor
                 * with no content, so it counts even though it is empty. Any
                 * other empty element is layout scaffolding and is ignored. */
                bool anchor_marker = (strcasecmp(name, "a") == 0) ||
                                     element_has_text(cur->children);
                if (anchor_marker) {
                    bool opens_block = is_p || is_h || is_blockquote || is_li || is_pre || is_hr;
                    if (!opens_block && ctx->current_block && !ctx->current_block->anchor_id) {
                        ctx->current_block->anchor_id = xstrdup((const char *)id_attr);
                    }
                    if (ctx->pending_anchor) free(ctx->pending_anchor);
                    ctx->pending_anchor = xstrdup((const char *)id_attr);
                }
            }

            /* Horizontal rule: create dedicated divider block */
            if (is_hr) {
                ctx->current_block = create_block(ctx->doc, BLOCK_HR, 0, ctx->pending_anchor);
                ctx->pending_anchor = NULL;
                ctx->current_block = NULL;
                ctx->font_ratio = prev_ratio;
                xmlFree(id_attr);
                xmlFree(class_attr);
                xmlFree(style_attr);
                continue;
            }

            /* Line break (<br>): split into a fresh block */
            if (is_br) {
                if (ctx->current_block && ctx->current_block->word_count > 0) {
                    ctx->current_block = create_block(ctx->doc, BLOCK_PARAGRAPH, 0, ctx->pending_anchor);
                    ctx->pending_anchor = NULL;
                }
                ctx->font_ratio = prev_ratio;
                xmlFree(id_attr);
                xmlFree(class_attr);
                xmlFree(style_attr);
                continue;
            }

            /* Start new semantic block based on tag type */
            if (is_h) {
                int level = name[1] - '0';
                ctx->current_block = create_block(ctx->doc, BLOCK_HEADING, level, ctx->pending_anchor);
                ctx->pending_anchor = NULL;
                ctx->current_style |= STYLE_BOLD | STYLE_HEADING;
            } else if (is_p) {
                if (ctx->current_block && ctx->current_block->word_count > 0) {
                    ctx->current_block = create_block(ctx->doc, BLOCK_PARAGRAPH, 0, ctx->pending_anchor);
                    ctx->pending_anchor = NULL;
                } else if (!ctx->current_block) {
                    ctx->current_block = create_block(ctx->doc, BLOCK_PARAGRAPH, 0, ctx->pending_anchor);
                    ctx->pending_anchor = NULL;
                }
            } else if (is_blockquote) {
                ctx->current_block = create_block(ctx->doc, BLOCK_BLOCKQUOTE, 0, ctx->pending_anchor);
                ctx->pending_anchor = NULL;
                ctx->current_style |= STYLE_ITALIC;
            } else if (is_li) {
                ctx->current_block = create_block(ctx->doc, BLOCK_LIST_ITEM, 0, ctx->pending_anchor);
                ctx->pending_anchor = NULL;
            } else if (is_pre) {
                ctx->current_block = create_block(ctx->doc, BLOCK_PRE, 0, ctx->pending_anchor);
                ctx->pending_anchor = NULL;
                ctx->current_style |= STYLE_CODE;
            }

            /* Fold the stylesheet's verdict into the inline style state. The
             * tag tests below express the same signals directly in markup, so
             * the two sources are simply combined. */
            if (css_style.bold) ctx->current_style |= STYLE_BOLD;
            if (css_style.italic) ctx->current_style |= STYLE_ITALIC;
            if (css_style.underline) ctx->current_style |= STYLE_UNDERLINE;

            /* Apply inline text formatting styles */
            if (strcasecmp(name, "b") == 0 || strcasecmp(name, "strong") == 0) {
                ctx->current_style |= STYLE_BOLD;
            } else if (strcasecmp(name, "i") == 0 || strcasecmp(name, "em") == 0) {
                ctx->current_style |= STYLE_ITALIC;
            } else if (strcasecmp(name, "u") == 0) {
                ctx->current_style |= STYLE_UNDERLINE;
            } else if (strcasecmp(name, "code") == 0 || strcasecmp(name, "tt") == 0) {
                ctx->current_style |= STYLE_CODE;
            }

            /* Record layout evidence on the block this element belongs to. The
             * block exists by now for every element that opens one; an inline
             * element simply annotates the block already being filled. */
            if (ctx->current_block) {
                if (css_style.center) ctx->current_block->css_centered = true;
                if (css_style.page_break_before) ctx->current_block->css_page_break = true;
                if (css_style.font_ratio > ctx->current_block->css_font_ratio) {
                    ctx->current_block->css_font_ratio = css_style.font_ratio;
                }
            }

            /* Recurse into element children */
            traverse_dom(ctx, cur->children);

            /* If exiting a heading block, synthesize a clean title string from words */
            if (is_h && ctx->current_block && ctx->current_block->type == BLOCK_HEADING) {
                size_t total_len = 0;
                for (size_t i = 0; i < ctx->current_block->word_count; i++) {
                    total_len += strlen(ctx->current_block->words[i].text) + 1;
                }
                if (total_len > 0) {
                    char *stitle = (char *)xmalloc(total_len + 1);
                    stitle[0] = '\0';
                    for (size_t i = 0; i < ctx->current_block->word_count; i++) {
                        strcat(stitle, ctx->current_block->words[i].text);
                        if (i + 1 < ctx->current_block->word_count) strcat(stitle, " ");
                    }
                    ctx->current_block->section_title = stitle;
                }
                /* Terminate heading block so following content starts fresh */
                ctx->current_block = NULL;
            }

            /* Restore the style and font-size state of the enclosing element */
            ctx->current_style = prev_style;
            ctx->font_ratio = prev_ratio;

            if (id_attr) xmlFree(id_attr);
            if (class_attr) xmlFree(class_attr);
            if (style_attr) xmlFree(style_attr);
        }
    }
}

/* ==========================================================================
 * High-level HTML parser entry point
 * ========================================================================== */

/* Parse raw XHTML chapter data into a structured ChapterDocument. */
ChapterDocument *html_parse_chapter(const char *xhtml_data, size_t data_len,
                                    size_t spine_index, const char *href,
                                    const char *default_title, const CssIndex *css) {
    if (!xhtml_data || data_len == 0) return NULL;

    htmlDocPtr doc = htmlReadMemory(xhtml_data, (int)data_len, href ? href : "chapter.xhtml",
                                    "UTF-8", HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);
    if (!doc) return NULL;

    xmlNode *root = xmlDocGetRootElement(doc);
    if (!root) {
        xmlFreeDoc(doc);
        return NULL;
    }

    ChapterDocument *ch = (ChapterDocument *)xcalloc(1, sizeof(ChapterDocument));
    ch->spine_index = spine_index;
    ch->href = href ? xstrdup(href) : xstrdup("");

    ParseContext ctx;
    memset(&ctx, 0, sizeof(ParseContext));
    ctx.doc = ch;
    ctx.css = css;
    ctx.font_ratio = 1.0f;

    traverse_dom(&ctx, root);

    if (ctx.pending_anchor) {
        free(ctx.pending_anchor);
    }

    xmlFreeDoc(doc);

    /* Discard completely empty blocks (except BLOCK_HR which carries no words) */
    size_t valid_count = 0;
    for (size_t i = 0; i < ch->block_count; i++) {
        if (ch->blocks[i].type == BLOCK_HR || ch->blocks[i].word_count > 0) {
            ch->blocks[valid_count++] = ch->blocks[i];
        } else {
            free(ch->blocks[i].anchor_id);
            free(ch->blocks[i].section_title);
        }
    }
    ch->block_count = valid_count;

    /* A provisional title, so that a document is never left without one.
     * html_finalize_title replaces it once the whole book has been pruned and
     * its headings recovered. */
    free(ch->title);
    ch->title = xstrdup(default_title ? default_title : "Chapter");

    return ch;
}

/* Free all resources associated with a ChapterDocument. */
void chapter_document_free(ChapterDocument *doc) {
    if (!doc) return;

    free(doc->href);
    free(doc->title);

    if (doc->blocks) {
        for (size_t i = 0; i < doc->block_count; i++) {
            Block *b = &doc->blocks[i];
            free(b->anchor_id);
            free(b->section_title);
            if (b->words) {
                for (size_t j = 0; j < b->word_count; j++) {
                    free(b->words[j].text);
                }
                free(b->words);
            }
        }
        free(doc->blocks);
    }

    free(doc);
}

/* ==========================================================================
 * Heading recovery from stylesheet evidence
 *
 * A book converted by Calibre, Word or InDesign writes its chapter titles as
 *
 *     <p class="calibre1"><span class="calibre2"><span class="bold">7. Title
 *
 * where `.calibre2 { font-size: 1.25em }`. There is no heading tag anywhere in
 * the file, so a reader that trusts markup alone cannot tell a title from the
 * running head printed above it - the two differ only in the CSS.
 *
 * The tests below reconstruct that distinction. They are deliberately
 * conservative: promoting a paragraph to a heading costs the reader a title
 * page, so every candidate must satisfy at least two independent signals.
 * ========================================================================== */

/* A paragraph longer than this is prose, whatever its styling says. */
#define HEADING_MAX_WORDS 20

/* Smallest font-size ratio that reads as display type rather than body copy.
 * Calibre writes chapter titles at 1.25em, Word conversions at 1.125em, so
 * the threshold sits below both. */
#define HEADING_MIN_RATIO 1.10f

/* Ratio at which a heading is treated as a top-level chapter title: large
 * enough that the typesetter centres it. */
#define HEADING_TITLE_RATIO 1.30f

/* Test whether a word is made only of decimal digits, allowing the stray
 * punctuation that conversion leaves around printed page numbers. */
static bool word_is_digits(const char *s) {
    if (!s || !*s) return false;
    size_t digits = 0;
    for (const char *p = s; *p; p++) {
        if (*p >= '0' && *p <= '9') { digits++; continue; }
        if (*p == '.' || *p == ',' || *p == '-') continue;
        return false;
    }
    return digits > 0 && digits <= 4;
}

/* Report whether every word of a block carries bold emphasis.
 *
 * Titles are set bold from end to end; prose that merely contains a bolded
 * phrase is not a heading, so the test is deliberately all-or-nothing. */
static bool block_is_fully_bold(const Block *b) {
    if (b->word_count == 0) return false;
    for (size_t i = 0; i < b->word_count; i++) {
        if (!(b->words[i].style & STYLE_BOLD)) return false;
    }
    return true;
}

/* Report whether a block's text is a bare page number. */
static bool block_is_page_number(const Block *b) {
    if (b->word_count == 0) return false;
    for (size_t i = 0; i < b->word_count; i++) {
        if (!word_is_digits(b->words[i].text)) return false;
    }
    return true;
}

/* Join a block's words into a single plain-text line, as the reader sees it.
 * Returns a heap string the caller must free. */
static char *block_plain_text(const Block *b) {
    size_t total = 0;
    for (size_t i = 0; i < b->word_count; i++) total += strlen(b->words[i].text) + 1;

    char *out = (char *)xmalloc(total + 1);
    out[0] = '\0';
    size_t o = 0;
    for (size_t i = 0; i < b->word_count; i++) {
        size_t wl = strlen(b->words[i].text);
        if (i > 0 && o + 1 < total + 1) out[o++] = ' ';
        memcpy(out + o, b->words[i].text, wl);
        o += wl;
    }
    out[o] = '\0';
    return out;
}

/* Promote stylesheet-styled display paragraphs to real headings.
 *
 * This pass must run *after* html_prune_document_noise, never before. A title
 * and a running head are written in exactly the same markup by these
 * converters:
 *
 *     <p class="calibre1"><span class="bold">3. Il fumo</span></p>
 *     <p class="calibre1"><span class="bold">La coscienza di Zeno - Svevo</span></p>
 *
 * Bold is the only signal either of them carries, so a rule that accepted bold
 * on its own would promote 219 running heads and, worse, give each one a
 * synthetic anchor - which is precisely the marker that makes
 * html_prune_document_noise leave a block alone. Classifying first would make
 * the noise untouchable. Pruning first means that by the time this pass runs,
 * the book title has already been deleted everywhere it recurred, and a
 * surviving short bold paragraph really is a title.
 *
 * Conversely, a bold paragraph that recurs but not quite enough to be
 * condemned survives pruning and may be promoted here. That is the one false
 * positive this ordering accepts, and it is bounded: the paragraph must be
 * fully bold and no longer than HEADING_MAX_WORDS. */
void html_classify_headings(ChapterDocument *doc) {
    if (!doc) return;

    for (size_t i = 0; i < doc->block_count; i++) {
        Block *b = &doc->blocks[i];

        /* Only plain paragraphs are candidates: markup that already says
         * <h1> has been handled, and list items and quotes are never titles. */
        if (b->type != BLOCK_PARAGRAPH || b->css_heading) continue;
        if (b->word_count == 0 || b->word_count > HEADING_MAX_WORDS) continue;
        if (block_is_page_number(b)) continue;

        bool large = (b->css_font_ratio >= HEADING_MIN_RATIO);
        bool bold = block_is_fully_bold(b);

        /* Three ways to earn promotion, in decreasing order of confidence:
         *
         *   bold + large     Calibre:  .calibre2 { font-size: 1.25em } inside a
         *                     <span class="bold">
         *   bold + centred   Word:     .MsoNormal { text-align: center }
         *   large + centred  display type, no bold required
         *
         * and, once running heads are gone, one more:
         *
         *   bold alone       Svevo's own title page, and the last two chapter
         *                     titles of Zeno, are <span class="bold"> in the
         *                     body face with nothing else set
         *
         * A centred italic paragraph of body text - very common in front
         * matter - satisfies none of them, and neither does an index line that
         * merely borrows the body class. Bold prose in the body face that is
         * neither large nor centred still has to clear the running-head test
         * above before it is believed. */
        /* Which simplifies to `bold || (large && centered)`: once the running
         * heads have been deleted, bold on its own is decisive. */
        if (!bold && !(large && b->css_centered)) continue;

        /* Assign a heading level from the evidence. A heading that the author
         * also forced onto a new page is a chapter; a merely large one is a
         * section. */
        if (b->css_page_break || b->css_font_ratio >= HEADING_TITLE_RATIO) {
            b->heading_level = 1;
        } else if (large || b->css_centered) {
            b->heading_level = 2;
        } else {
            /* Bold alone, in the body face. Zeno numbers its chapters in one
             * flat sequence and gives every one of them this treatment, so
             * these are siblings of the larger chapter titles rather than
             * subsections beneath them: level 2 keeps the table of contents
             * level with itself. */
            b->heading_level = 2;
        }

        b->type = BLOCK_HEADING;
        b->css_heading = true;

        /* The text of the heading becomes its own title, used for the title
         * page, the running header, and the synthesized table of contents. */
        free(b->section_title);
        b->section_title = block_plain_text(b);

        /* Promoted titles must also read as headings in the typesetter, which
         * keys off the word style flags rather than the block type alone. */
        for (size_t w = 0; w < b->word_count; w++) {
            b->words[w].style |= STYLE_BOLD | STYLE_HEADING;
        }

        /* Give the heading an anchor so that table of contents entries built
         * from it can be navigated to. Books whose headings carry a real id
         * keep it, and keep matching the anchors their NCX already declares. */
        if (!b->anchor_id) {
            char anchor[48];
            snprintf(anchor, sizeof(anchor), "rr-h-%zu-%zu", doc->spine_index, i);
            b->anchor_id = xstrdup(anchor);
        }
    }
}

/* ==========================================================================
 * Conversion debris: printed page numbers and running heads
 *
 * A book scanned from print and converted to EPUB carries two artefacts that
 * are not part of the text:
 *
 *   running head   the book title, reprinted in the margin of every page
 *   folio          the printed page number, on its own line or glued to the
 *                  end of the paragraph that finished the page
 *
 * Neither can be identified from a single chapter: "15" is a page number in
 * one book and a date in another, and a running head is indistinguishable
 * from a title until it is seen again. Both decisions therefore need the
 * whole book as evidence, which is why this pass takes every document at once.
 * ========================================================================== */

/* How often a short paragraph must recur before it is a running head. Verse
 * and legal texts legitimately repeat a line; four pages of the same short
 * line is not a reading experience, it is a margin. */
#define RUNNING_HEAD_MIN_REPEATS 4

/* Shortest recurring text considered at all. Below this a repeated fragment
 * is far more likely to be a short interjection than a running head. */
#define RUNNING_HEAD_MIN_CHARS 8

/* Longest paragraph considered as a running-head candidate. Real heads are
 * short; body paragraphs are not. */
#define RUNNING_HEAD_MAX_WORDS 16

/* A book must show at least this many distinct folios before rr believes any
 * paragraph number is a page number rather than content. */
#define FOLIO_MIN_EVIDENCE 4

/* Shortest paragraph whose trailing word could plausibly be a glued folio.
 * A one- or two-word block that ends in a number is far more likely to be
 * genuinely numeric - a date, a quantity, a numbered list - than a printed
 * page number that happened to fall mid-sentence. */
#define FOLIO_MIN_PARAGRAPH_WORDS 8

/* An open-addressing map from a normalized string to how often it occurs.
 *
 * A book can contain tens of thousands of distinct short paragraphs, so the
 * table grows rather than assuming a fixed size; but it holds one key per
 * *distinct* short paragraph, which in practice is a few hundred. */
typedef struct {
    char **keys;
    size_t *counts;
    size_t cap;    /* Always a power of two. */
    size_t used;
} TextTally;

/* Initialize an empty tally. */
static void tally_init(TextTally *t) {
    t->cap = 256;
    t->used = 0;
    t->keys = (char **)xcalloc(t->cap, sizeof(char *));
    t->counts = (size_t *)xcalloc(t->cap, sizeof(size_t));
}

/* Release every key held by a tally. */
static void tally_free(TextTally *t) {
    if (!t->keys) return;
    for (size_t i = 0; i < t->cap; i++) free(t->keys[i]);
    free(t->keys);
    free(t->counts);
    t->keys = NULL;
    t->counts = NULL;
    t->cap = 0;
    t->used = 0;
}

/* Hash a normalized string into a bucket index. */
static size_t tally_hash(const char *s) {
    size_t h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        h ^= *p;
        h *= 16777619u;
    }
    return h;
}

/* Grow the tally, rehashing every key into the larger table. */
static void tally_grow(TextTally *t) {
    size_t old_cap = t->cap;
    char **old_keys = t->keys;
    size_t *old_counts = t->counts;

    t->cap = old_cap * 2;
    t->keys = (char **)xcalloc(t->cap, sizeof(char *));
    t->counts = (size_t *)xcalloc(t->cap, sizeof(size_t));

    for (size_t i = 0; i < old_cap; i++) {
        if (!old_keys[i]) continue;
        size_t j = tally_hash(old_keys[i]) & (t->cap - 1);
        while (t->keys[j]) j = (j + 1) & (t->cap - 1);
        t->keys[j] = old_keys[i];
        t->counts[j] = old_counts[i];
    }
    free(old_keys);
    free(old_counts);
}

/* Count one occurrence of `key`, which the caller has already normalized. */
static void tally_add(TextTally *t, const char *key) {
    /* Keep the load factor under one half so probe sequences stay short. */
    if ((t->used + 1) * 2 >= t->cap) tally_grow(t);

    size_t j = tally_hash(key) & (t->cap - 1);
    while (t->keys[j]) {
        if (strcmp(t->keys[j], key) == 0) {
            t->counts[j]++;
            return;
        }
        j = (j + 1) & (t->cap - 1);
    }
    t->keys[j] = xstrdup(key);
    t->counts[j] = 1;
    t->used++;
}

/* Look up how often a normalized key was seen; zero when never seen. */
static size_t tally_count(const TextTally *t, const char *key) {
    if (!t->keys) return 0;
    size_t j = tally_hash(key) & (t->cap - 1);
    while (t->keys[j]) {
        if (strcmp(t->keys[j], key) == 0) return t->counts[j];
        j = (j + 1) & (t->cap - 1);
    }
    return 0;
}

/* Normalize a block's text for comparison: lower case, punctuation dropped,
 * whitespace collapsed.
 *
 * Punctuation is discarded because a running head is re-typeset on every page
 * and the conversion is rarely byte-identical between occurrences. */
static char *normalized_text(const Block *b) {
    char *raw = block_plain_text(b);
    size_t o = 0;
    bool space = false;
    for (const char *p = raw; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c >= 0x80) continue;                 /* keep ASCII only */
        if (isspace(c)) { space = (o > 0); continue; }
        if (ispunct(c)) continue;                 /* drop punctuation */
        if (space && o > 0) raw[o++] = ' ';
        space = false;
        raw[o++] = (char)tolower(c);
    }
    raw[o] = '\0';
    return raw;
}

/* Release the memory owned by one block's words, anchor and title. */
static void release_block(Block *b) {
    free(b->anchor_id);
    free(b->section_title);
    if (b->words) {
        for (size_t j = 0; j < b->word_count; j++) free(b->words[j].text);
        free(b->words);
    }
}

/* Remove every block flagged for deletion and compact the array in place. */
static void compact_blocks(ChapterDocument *doc, const bool *drop) {
    size_t keep = 0;
    for (size_t i = 0; i < doc->block_count; i++) {
        if (drop[i]) {
            release_block(&doc->blocks[i]);
        } else {
            if (keep != i) doc->blocks[keep] = doc->blocks[i];
            keep++;
        }
    }
    doc->block_count = keep;
}

/* Copy the leading run of decimal digits out of `text` into `out`, which must
 * have room for at least two bytes. Returns the number of digits copied. */
static size_t copy_digits(const char *text, char *out) {
    size_t n = 0;
    while (text[n] >= '0' && text[n] <= '9') {
        out[n] = text[n];
        n++;
    }
    out[n] = '\0';
    return n;
}

/* Strip folios and running heads from a whole book of parsed documents. */
void html_prune_document_noise(ChapterDocument **docs, size_t doc_count) {
    if (!docs || doc_count == 0) return;

    size_t total_blocks = 0;
    for (size_t d = 0; d < doc_count; d++) {
        if (docs[d]) total_blocks += docs[d]->block_count;
    }
    if (total_blocks == 0) return;

    /* --- Pass 1: tally every short paragraph in the book ------------------
     *
     * Anchored blocks are excluded outright. An id means something in the
     * book's navigation, so removing the block would break a table of contents
     * link in order to remove a line of print. */
    TextTally tally;
    tally_init(&tally);

    for (size_t d = 0; d < doc_count; d++) {
        ChapterDocument *doc = docs[d];
        if (!doc) continue;
        for (size_t i = 0; i < doc->block_count; i++) {
            Block *b = &doc->blocks[i];
            if (b->anchor_id || b->type != BLOCK_PARAGRAPH) continue;
            if (b->word_count == 0 || b->word_count > RUNNING_HEAD_MAX_WORDS) continue;
            if (block_is_page_number(b)) continue;

            char *norm = normalized_text(b);
            if (strlen(norm) >= RUNNING_HEAD_MIN_CHARS) tally_add(&tally, norm);
            free(norm);
        }
    }

    /* --- Pass 2: gather the numbers that could be printed page numbers -----
     *
     * A paragraph made only of digits is a candidate outright. A trailing
     * number glued to the end of a long paragraph is a candidate too, but only
     * once the book has proved it prints folios at all: in a book without them
     * the last word of a paragraph is far more likely to be part of the prose. */
    TextTally folio_candidates;
    tally_init(&folio_candidates);

    for (size_t d = 0; d < doc_count; d++) {
        ChapterDocument *doc = docs[d];
        if (!doc) continue;
        for (size_t i = 0; i < doc->block_count; i++) {
            Block *b = &doc->blocks[i];
            if (b->anchor_id || b->type != BLOCK_PARAGRAPH || b->word_count == 0) continue;

            const char *candidate = NULL;
            if (block_is_page_number(b)) {
                candidate = b->words[0].text;
            } else if (b->word_count >= FOLIO_MIN_PARAGRAPH_WORDS &&
                       word_is_digits(b->words[b->word_count - 1].text)) {
                candidate = b->words[b->word_count - 1].text;
            }
            if (candidate) {
                char key[8];
                if (copy_digits(candidate, key) > 0) tally_add(&folio_candidates, key);
            }
        }
    }

    /* Only strip numbers when the book shows enough distinct folios that they
     * cannot be an accident of the prose. */
    bool strip_folios = (folio_candidates.used >= FOLIO_MIN_EVIDENCE);

    /* --- Pass 3: delete what the evidence condemns ------------------------ */
    for (size_t d = 0; d < doc_count; d++) {
        ChapterDocument *doc = docs[d];
        if (!doc || doc->block_count == 0) continue;

        bool *drop = (bool *)xcalloc(doc->block_count, sizeof(bool));
        bool *trim_last = (bool *)xcalloc(doc->block_count, sizeof(bool));

        for (size_t i = 0; i < doc->block_count; i++) {
            Block *b = &doc->blocks[i];
            if (b->anchor_id || b->type != BLOCK_PARAGRAPH || b->word_count == 0) continue;

            /* Printed page number standing on a line of its own. */
            if (block_is_page_number(b)) {
                if (strip_folios) drop[i] = true;
                continue;
            }

            /* Printed page number glued to the end of the paragraph that
             * happened to finish the printed page. */
            if (strip_folios && b->word_count >= FOLIO_MIN_PARAGRAPH_WORDS) {
                Word *last = &b->words[b->word_count - 1];
                char key[8];
                if (word_is_digits(last->text) && copy_digits(last->text, key) > 0) {
                    trim_last[i] = true;
                }
            }

            /* Running head: a short paragraph that recurs across the book. */
            if (b->word_count <= RUNNING_HEAD_MAX_WORDS) {
                char *norm = normalized_text(b);
                if (strlen(norm) >= RUNNING_HEAD_MIN_CHARS &&
                    tally_count(&tally, norm) >= RUNNING_HEAD_MIN_REPEATS) {
                    drop[i] = true;
                }
                free(norm);
            }
        }

        /* Detach the trailing folios, then drop whole blocks, so that a
         * paragraph survives with its prose intact. */
        for (size_t i = 0; i < doc->block_count; i++) {
            if (!trim_last[i] || drop[i]) continue;
            Block *b = &doc->blocks[i];
            free(b->words[b->word_count - 1].text);
            b->word_count--;
        }

        compact_blocks(doc, drop);
        free(trim_last);
        free(drop);
    }

    tally_free(&tally);
    tally_free(&folio_candidates);
}

/* Choose the chapter's displayed title once noise has been pruned.
 *
 * The first surviving heading names the chapter, because that is the title the
 * book itself prints. Spine items without any heading - a cover page, a
 * copyright notice - fall back to the navigation entry that pointed at them,
 * and then to a generic label. */
void html_finalize_title(ChapterDocument *doc, const char *default_title) {
    if (!doc) return;

    for (size_t i = 0; i < doc->block_count; i++) {
        Block *b = &doc->blocks[i];
        if (b->type != BLOCK_HEADING || !b->section_title || !*b->section_title) continue;
        free(doc->title);
        doc->title = xstrdup(b->section_title);
        return;
    }

    if (!doc->title) {
        doc->title = default_title ? xstrdup(default_title) : xstrdup("Chapter");
    }
}
