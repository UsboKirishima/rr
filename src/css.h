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

#ifndef RR_CSS_H
#define RR_CSS_H

#include <stddef.h>
#include <stdbool.h>

/* ==========================================================================
 * Minimal CSS introspection
 *
 * Real EPUB files very rarely mark up their headings with <h1>..<h6>. The
 * overwhelming majority of books produced by conversion pipelines (Calibre,
 * Word, InDesign, Google Books, Internet Archive) express the same intent
 * purely through stylesheets:
 *
 *     .calibre1 { text-indent: 1.5em }              <- body paragraph
 *     .calibre2 { font-size: 1.25em }               <- chapter title
 *     .bold     { font-weight: bold }
 *     .MsoNormal{ text-align: center }              <- display heading
 *
 * A reader that ignores stylesheets therefore sees a wall of undifferentiated
 * paragraphs and cannot tell a chapter title from running prose. This module
 * implements just enough CSS to recover those signals.
 *
 * Design constraints (rr is a performance and size sensitive program):
 *
 *   - No dependency is added. The parser is a single forward scan over the
 *     stylesheet bytes with no backtracking and no intermediate DOM.
 *   - Only the handful of properties that carry typographic meaning are
 *     retained; everything else (colors, borders, floats, animations, media
 *     queries, font-face) is skipped without being stored.
 *   - Rules are indexed by a chained hash table, so resolving the style of an
 *     element costs O(number of classes on that element) instead of a linear
 *     scan over every rule in the book.
 * ========================================================================== */

/* The subset of typographic facts rr cares about, resolved for one element. */
typedef struct {
    bool bold;              /* font-weight is bold or bolder */
    bool italic;            /* font-style is italic or oblique */
    bool underline;         /* text-decoration contains underline */
    bool center;            /* text-align is center */
    bool page_break_before; /* the element starts a new page (chapter mark) */
    float font_ratio;       /* font-size multiplier relative to the parent.
                             * 1.0 means "inherits the surrounding size" and is
                             * the neutral value that composes multiplicatively
                             * down the element tree. */
} CssStyle;

/* Opaque handle holding the compiled rules of every stylesheet in a book. */
typedef struct CssIndex CssIndex;

/* Allocate an empty style index. Aborts on allocation failure. */
CssIndex *css_index_new(void);

/* Release an index and every rule string it owns. */
void css_index_free(CssIndex *idx);

/* Compile one stylesheet's source into `idx`.
 *
 * Later stylesheets and later rules override earlier ones for the same
 * selector, mirroring normal browser cascade behaviour. Unparsable input is
 * skipped silently: a broken stylesheet must never prevent a book from
 * opening. */
void css_index_load(CssIndex *idx, const char *data, size_t len);

/* Resolve the effective style of a single element.
 *
 * Rules are applied in increasing order of specificity, exactly as a browser
 * would: element selector, then id, then each class in document order, then
 * the element's own `style` attribute. Any argument may be NULL, in which case
 * that level contributes nothing.
 *
 * `out` is fully overwritten; the caller does not need to initialise it. */
void css_resolve(const CssIndex *idx, const char *tag, const char *class_attr,
                 const char *id_attr, const char *inline_style, CssStyle *out);

#endif /* RR_CSS_H */
