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
 * ========================================================================== */

typedef struct {
    ChapterDocument *doc;    /* Document currently being populated */
    Block *current_block;    /* Block currently receiving words */
    int current_style;       /* Active bitmask of TextStyle flags */
    char *pending_anchor;    /* Anchor ID encountered before text appeared */
    bool in_pre;             /* True when inside <pre> preformatted block */
} ParseContext;

/* ==========================================================================
 * Block and word constructors
 * ========================================================================== */

/* Allocate and append a new semantic block to the chapter document.
 * Grows the blocks array with exponential reallocation. */
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
    return b;
}

/* Append an indivisible word token to a block.
 *
 * Pre-computes the word's visual terminal width using `utf8_strwidth` so that
 * the layout engine never needs to recalculate character widths during
 * re-wrapping or page rendering. */
static void add_word_to_block(Block *b, const char *text, int style, bool space_after) {
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
            add_word_to_block(ctx->current_block, word_buf, ctx->current_style, has_space);
        }
    }
}

/* ==========================================================================
 * Recursive DOM traversal
 *
 * Walks the libxml2 DOM tree, managing:
 *   - Element styling (pushing/popping bold, italic, code styles).
 *   - Block boundary creation (<p>, <h1>-<h6>, <blockquote>, <li>, <hr>).
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

            /* Check for anchor target attribute (id or name) */
            xmlChar *id_attr = xmlGetProp(cur, (const xmlChar *)"id");
            if (!id_attr) {
                id_attr = xmlGetProp(cur, (const xmlChar *)"name");
            }
            if (id_attr) {
                if (ctx->current_block && !ctx->current_block->anchor_id) {
                    ctx->current_block->anchor_id = xstrdup((const char *)id_attr);
                }
                if (ctx->pending_anchor) free(ctx->pending_anchor);
                ctx->pending_anchor = xstrdup((const char *)id_attr);
                xmlFree(id_attr);
            }

            /* Identify block elements */
            bool is_p = (strcasecmp(name, "p") == 0);
            bool is_h = (name[0] == 'h' || name[0] == 'H') && (name[1] >= '1' && name[1] <= '6') && name[2] == '\0';
            bool is_blockquote = (strcasecmp(name, "blockquote") == 0);
            bool is_li = (strcasecmp(name, "li") == 0);
            bool is_hr = (strcasecmp(name, "hr") == 0);
            bool is_br = (strcasecmp(name, "br") == 0);
            bool is_pre = (strcasecmp(name, "pre") == 0);

            /* Horizontal rule: create dedicated divider block */
            if (is_hr) {
                ctx->current_block = create_block(ctx->doc, BLOCK_HR, 0, ctx->pending_anchor);
                ctx->pending_anchor = NULL;
                ctx->current_block = NULL;
                continue;
            }

            /* Line break (<br>): split into a fresh block */
            if (is_br) {
                if (ctx->current_block && ctx->current_block->word_count > 0) {
                    ctx->current_block = create_block(ctx->doc, BLOCK_PARAGRAPH, 0, ctx->pending_anchor);
                    ctx->pending_anchor = NULL;
                }
                continue;
            }

            int prev_style = ctx->current_style;

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
                    if (!ctx->doc->title) {
                        ctx->doc->title = xstrdup(stitle);
                    }
                }
                /* Terminate heading block so following content starts fresh */
                ctx->current_block = NULL;
            }

            /* Restore previous inline style state */
            ctx->current_style = prev_style;
        }
    }
}

/* ==========================================================================
 * High-level HTML parser entry point
 * ========================================================================== */

/* Parse raw XHTML chapter data into a structured ChapterDocument. */
ChapterDocument *html_parse_chapter(const char *xhtml_data, size_t data_len,
                                    size_t spine_index, const char *href, const char *default_title) {
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

    /* Ensure document has a display title */
    if (!ch->title) {
        ch->title = default_title ? xstrdup(default_title) : xstrdup("Chapter");
    }

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
