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

/* rr deliberately does not implement a browser: it needs to know only a
 * handful of typographic facts about an element, and it needs to know them
 * without allocating a style tree or walking the cascade for every tag. */

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "css.h"
#include "util.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ==========================================================================
 * Internal representation
 *
 * Every selector that survives parsing is reduced to exactly one key - a tag
 * name, a class name, or an id - and stored in a chained hash bucket for that
 * kind of key. Resolving an element therefore costs one hash lookup per class
 * it carries, plus one for its id and one for its tag, rather than a scan over
 * every rule in the book.
 * ========================================================================== */

/* Number of hash buckets. A power of two so the modulus is a bit mask. */
#define CSS_BUCKETS 1024u

/* Longest selector key that will be indexed. Anything longer is pathological
 * for EPUB markup and is simply skipped rather than truncated, because a
 * truncated key would match the wrong elements. */
#define CSS_KEY_MAX 128

/* Which cascade slot a stored selector belongs to. */
enum {
    CSS_KEY_TAG = 0,   /* p, h1, blockquote ...          */
    CSS_KEY_CLASS = 1, /* .calibre2                       */
    CSS_KEY_ID = 2     /* #chapter1                       */
};

/* Which properties a rule actually mentions.
 *
 * A property a rule does not mention must never overwrite a value inherited
 * from a less specific rule, so presence is tracked separately from value.
 * That distinction matters for `font-weight: normal` inside an otherwise bold
 * class, which rr cannot express as a boolean and therefore ignores; the same
 * limitation is documented on CssStyle in css.h. */
#define CSS_P_BOLD   0x01u
#define CSS_P_ITALIC 0x02u
#define CSS_P_UNDER  0x04u
#define CSS_P_CENTER 0x08u
#define CSS_P_BREAK  0x10u
#define CSS_P_SIZE   0x20u

/* One compiled rule: the value of every recognised property in the rule. */
typedef struct CssRule {
    struct CssRule *next; /* Next rule sharing this bucket, in source order */
    char *key;            /* Owned selector key, without the leading '#'/'.' */
    unsigned char kind;   /* CSS_KEY_TAG, CSS_KEY_CLASS or CSS_KEY_ID */
    unsigned present;     /* CSS_P_* bits that this rule actually specifies */
    CssStyle style;       /* The specified values */
} CssRule;

/* Compiled stylesheets for one book. */
struct CssIndex {
    CssRule *buckets[CSS_BUCKETS];
};

/* ==========================================================================
 * Small lexical helpers
 * ========================================================================== */

/* Report whether `c` may appear in a CSS identifier (tag, class or id name). */
static bool css_is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '\\';
}

/* Case-insensitive FNV-1a over a byte range.
 *
 * Folding case into the hash means selectors are matched case-insensitively.
 * That is technically wrong for standards-mode HTML, but EPUB stylesheets in
 * the wild mix `.Section` and `.section` freely and a terminal reader gains
 * nothing from the distinction. */
static unsigned css_hash(const char *s, size_t len) {
    unsigned h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (unsigned char)tolower((unsigned char)s[i]);
        h *= 16777619u;
    }
    return h;
}

/* Copy the trimmed, lowercased range [s, s+len) into `out`. */
static void css_lower_trim(const char *s, size_t len, char *out, size_t out_size) {
    size_t i = 0;
    while (i < len && isspace((unsigned char)s[i])) i++;
    while (len > i && isspace((unsigned char)s[len - 1])) len--;

    size_t o = 0;
    for (; i < len && o + 1 < out_size; i++) {
        out[o++] = (char)tolower((unsigned char)s[i]);
    }
    out[o] = '\0';
}

/* Case-insensitive substring search, used for `text-decoration: underline` and
 * for pseudo-class detection. `strcasestr` is a GNU extension, so a tiny local
 * implementation keeps the build portable and pedantic-clean. */
static bool css_contains_ci(const char *hay, size_t hlen, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0 || nlen > hlen) return false;
    for (size_t i = 0; i + nlen <= hlen; i++) {
        if (strncasecmp(hay + i, needle, nlen) == 0) return true;
    }
    return false;
}

/* ==========================================================================
 * Rule storage and cascade merging
 * ========================================================================== */

/* Look up the rule for one key, creating an empty one when absent.
 *
 * A selector repeated across stylesheets collapses onto a single rule, whose
 * values are the union of every declaration it ever received. That mirrors how
 * browsers cascade equal-specificity rules with source order deciding the
 * winner, without rr having to store or compare authoring order. */
static CssRule *css_get_rule(CssIndex *idx, const char *key, size_t klen, unsigned char kind) {
    unsigned b = css_hash(key, klen) & (CSS_BUCKETS - 1u);
    CssRule **slot = &idx->buckets[b];
    for (CssRule *r = *slot; r; r = r->next) {
        if (r->kind == kind && strlen(r->key) == klen && strncasecmp(r->key, key, klen) == 0) {
            return r;
        }
    }

    CssRule *r = (CssRule *)xcalloc(1, sizeof(CssRule));
    r->key = (char *)xmalloc(klen + 1);
    memcpy(r->key, key, klen);
    r->key[klen] = '\0';
    r->kind = kind;
    r->style.font_ratio = 1.0f;
    r->next = NULL;

    /* Append at the tail so that lookups walk rules in source order. */
    while (*slot) slot = &(*slot)->next;
    *slot = r;
    return r;
}

/* Fold one declaration set into an accumulator, honouring specificity order.
 *
 * Boolean properties accumulate: once anything in the cascade asks for bold,
 * the element renders bold, and rr has no notion of "explicitly not bold".
 * `font-size` is the exception because it is multiplicative - the most
 * specific declaration that actually specifies a size wins outright. */
static void css_merge(CssStyle *dst, const CssStyle *src, unsigned present) {
    if (src->bold) dst->bold = true;
    if (src->italic) dst->italic = true;
    if (src->underline) dst->underline = true;
    if (src->center) dst->center = true;
    if (src->page_break_before) dst->page_break_before = true;
    if (present & CSS_P_SIZE) dst->font_ratio = src->font_ratio;
}

/* Apply every rule stored under one key to an accumulator. */
static void css_apply_key(const CssIndex *idx, const char *key, size_t klen,
                          unsigned char kind, CssStyle *out) {
    unsigned b = css_hash(key, klen) & (CSS_BUCKETS - 1u);
    for (CssRule *r = idx->buckets[b]; r; r = r->next) {
        if (r->kind == kind && strlen(r->key) == klen && strncasecmp(r->key, key, klen) == 0) {
            css_merge(out, &r->style, r->present);
        }
    }
}

/* ==========================================================================
 * font-size normalization
 *
 * EPUB stylesheets express type sizes in every unit CSS has ever had. rr only
 * needs to compare a heading against body copy, so every size is reduced to a
 * ratio against the document's default type size.
 * ========================================================================== */

/* Default type size assumed for absolute units, in CSS pixels. 16px is the
 * CSS initial value of font-size and the size virtually every EPUB assumes. */
#define CSS_BASE_PX 16.0

/* Convert a `font-size` value to a multiplier of the surrounding type size.
 * Returns false for keywords that carry no usable size information. */
static bool css_parse_size(const char *value, size_t len, float *out) {
    char buf[64];
    css_lower_trim(value, len, buf, sizeof(buf));
    if (buf[0] == '\0') return false;

    /* Absolute and relative keywords, mapped to the sizes the CSS spec defines
     * relative to the 16px initial value. */
    static const struct { const char *name; float ratio; } keywords[] = {
        { "xx-small", 0.60f }, { "x-small", 0.75f },  { "small", 0.89f },
        { "medium", 1.00f },   { "large", 1.20f },    { "x-large", 1.50f },
        { "xx-large", 2.00f }, { "xxx-large", 3.00f },
        { "larger", 1.20f },   { "smaller", 0.83f },
        { NULL, 0.0f }
    };
    for (int i = 0; keywords[i].name; i++) {
        if (strcmp(buf, keywords[i].name) == 0) {
            *out = keywords[i].ratio;
            return true;
        }
    }

    /* Split the numeric part from the unit. The value is copied into a local
     * buffer first so strtod() never reads past the stylesheet slice. */
    char unit[16];
    size_t o = 0;
    size_t i = 0;
    while (i < len && buf[i] && o + 1 < sizeof(unit) &&
           (isdigit((unsigned char)buf[i]) || buf[i] == '.' || buf[i] == '+' || buf[i] == '-')) {
        unit[o++] = buf[i++];
    }
    if (o == 0) return false;
    while (i < len && buf[i] && o + 1 < sizeof(unit)) {
        unit[o++] = buf[i++];
    }
    unit[o] = '\0';

    char *endp = NULL;
    double v = strtod(unit, &endp);
    if (endp == unit || v <= 0.0) return false;

    const char *u = endp;
    while (*u == ' ') u++;

    double ratio;
    if (*u == '\0') {
        ratio = v;                       /* unitless: treat as a multiplier */
    } else if (strcmp(u, "em") == 0 || strcmp(u, "rem") == 0) {
        ratio = v;
    } else if (strcmp(u, "%") == 0) {
        ratio = v / 100.0;
    } else if (strcmp(u, "px") == 0) {
        ratio = v / CSS_BASE_PX;
    } else if (strcmp(u, "pt") == 0) {
        ratio = v / 12.0;                /* 12pt is 16px */
    } else if (strcmp(u, "pc") == 0) {
        ratio = v;                       /* 1pc == 12pt */
    } else if (strcmp(u, "in") == 0) {
        ratio = v * 96.0 / CSS_BASE_PX;
    } else if (strcmp(u, "cm") == 0) {
        ratio = v * 96.0 / 2.54 / CSS_BASE_PX;
    } else if (strcmp(u, "mm") == 0) {
        ratio = v * 96.0 / 25.4 / CSS_BASE_PX;
    } else if (strcmp(u, "ex") == 0) {
        ratio = v * 0.5;                 /* x-height is about half an em */
    } else if (strcmp(u, "ch") == 0) {
        ratio = v * 0.5;
    } else if (strcmp(u, "vw") == 0 || strcmp(u, "vh") == 0) {
        ratio = v / 100.0;               /* viewport units scale with the page */
    } else {
        return false;
    }

    /* Clamp so that an absurd stylesheet cannot make a heading vanish. */
    if (ratio < 0.2) ratio = 0.2;
    if (ratio > 5.0) ratio = 5.0;
    *out = (float)ratio;
    return true;
}

/* ==========================================================================
 * Declaration block parsing
 *
 * One pass over the text between a rule's braces. Properties rr does not model
 * are recognised by name only to be skipped, which keeps the parser honest
 * about how little it understands.
 * ========================================================================== */

/* Read a `prop: value` pair into an accumulator. */
static void css_apply_declaration(const char *prop, size_t plen,
                                  const char *value, size_t vlen,
                                  CssStyle *out, unsigned *present) {
    char p[32];
    css_lower_trim(prop, plen, p, sizeof(p));

    if (strcmp(p, "font-weight") == 0) {
        /* Numeric weights of 600 and above read as bold to a reader. */
        char v[24];
        css_lower_trim(value, vlen, v, sizeof(v));
        if (strcmp(v, "bold") == 0 || strcmp(v, "bolder") == 0 ||
            (v[0] >= '0' && v[0] <= '9' && atoi(v) >= 600)) {
            out->bold = true;
            *present |= CSS_P_BOLD;
        }
    } else if (strcmp(p, "font-style") == 0) {
        char v[24];
        css_lower_trim(value, vlen, v, sizeof(v));
        if (strcmp(v, "italic") == 0 || strcmp(v, "oblique") == 0) {
            out->italic = true;
            *present |= CSS_P_ITALIC;
        }
    } else if (strcmp(p, "text-decoration") == 0 || strcmp(p, "text-decoration-line") == 0) {
        if (css_contains_ci(value, vlen, "underline")) {
            out->underline = true;
            *present |= CSS_P_UNDER;
        }
    } else if (strcmp(p, "text-align") == 0) {
        char v[24];
        css_lower_trim(value, vlen, v, sizeof(v));
        if (strcmp(v, "center") == 0 || strcmp(v, "centre") == 0) {
            out->center = true;
            *present |= CSS_P_CENTER;
        }
    } else if (strcmp(p, "page-break-before") == 0 || strcmp(p, "break-before") == 0) {
        char v[24];
        css_lower_trim(value, vlen, v, sizeof(v));
        if (strcmp(v, "always") == 0 || strcmp(v, "page") == 0 ||
            strcmp(v, "left") == 0 || strcmp(v, "right") == 0 ||
            strcmp(v, "recto") == 0 || strcmp(v, "verso") == 0) {
            out->page_break_before = true;
            *present |= CSS_P_BREAK;
        }
    } else if (strcmp(p, "font-size") == 0) {
        float ratio;
        if (css_parse_size(value, vlen, &ratio)) {
            out->font_ratio = ratio;
            *present |= CSS_P_SIZE;
        }
    }
    /* Every other property - colour, margin, font-family, shorthand `font` -
     * is deliberately ignored: it carries no meaning for terminal typesetting. */
}

/* Parse a run of `prop: value;` declarations, such as an element's inline
 * `style` attribute or a rule's declaration block. */
static void css_parse_declarations(const char *decl, size_t len,
                                   CssStyle *out, unsigned *present) {
    size_t i = 0;
    while (i < len) {
        /* Locate the end of this declaration: the next ';' at nesting depth 0,
         * skipping over quoted strings, parentheses and comments. */
        size_t start = i;
        size_t end = len;
        int depth = 0;
        bool in_quote = false;
        char quote = '\0';

        for (; i < len; i++) {
            char c = decl[i];
            if (in_quote) {
                if (c == '\\') i++;
                else if (c == quote) in_quote = false;
                continue;
            }
            if (c == '"' || c == '\'') { in_quote = true; quote = c; continue; }
            if (c == '/' && i + 1 < len && decl[i + 1] == '*') {
                size_t close = i + 2;
                while (close + 1 < len && !(decl[close] == '*' && decl[close + 1] == '/')) close++;
                i = close + 1;
                continue;
            }
            if (c == '(') { depth++; continue; }
            if (c == ')') { if (depth > 0) depth--; continue; }
            if (c == ';' && depth == 0) { end = i; break; }
        }

        /* Split the declaration at its first top-level colon. */
        size_t colon = len;
        int d2 = 0;
        for (size_t k = start; k < end; k++) {
            if (decl[k] == '(') { d2++; continue; }
            if (decl[k] == ')') { if (d2 > 0) d2--; continue; }
            if (decl[k] == ':' && d2 == 0) { colon = k; break; }
        }
        if (colon < end) {
            css_apply_declaration(decl + start, colon - start,
                                  decl + colon + 1, end - colon - 1, out, present);
        }

        if (i < len) i++; /* step over the ';' */
    }
}

/* ==========================================================================
 * Selector parsing
 * ========================================================================== */

/* Reduce one selector to the single key rr can index it by.
 *
 * Real selector matching - descendant combinators, attribute tests, pseudo
 * classes - needs a selector engine that rr has no use for. A selector is
 * therefore indexed by exactly one simple selector: the tag name, the class or
 * the id that makes up the last compound of the chain, since that is the part
 * that actually discriminates.
 *
 * Anything that cannot be reduced to a single simple selector without changing
 * its meaning is skipped rather than approximated:
 *
 *   `p .bold`      reduced to `.bold`, the compound that carries the style
 *   `p.calibre2`   rejected - both parts are required, indexing one would
 *                  match elements the author never styled
 *   `a:hover`      rejected - `hover` is a state, not an element name
 *   `*[lang]`      rejected - attribute tests are not modelled
 *   `*`            rejected - the universal selector styles nothing observable
 *
 * A missed rule costs a lost typographic hint. A wrongly matched rule costs
 * bold or centring on a paragraph the author styled as body text, which is
 * far more visible in a terminal. erring towards rejection is therefore the
 * right trade for a reader. */
static bool css_selector_key(const char *sel, size_t len, unsigned char *out_kind,
                             char *out_key, size_t *out_len) {
    /* Trim the selector range first. The caller's `{` is always preceded by
     * whitespace, and trimming here rather than after the scan is what keeps a
     * trailing space from pointing the start of the final compound past the
     * end of the selector. */
    while (len > 0 && isspace((unsigned char)sel[len - 1])) len--;
    while (len > 0 && isspace((unsigned char)sel[0])) { sel++; len--; }
    if (len == 0) return false;

    /* Reduce a compound selector to its last simple selector. Descendant,
     * child and sibling combinators all separate compounds, so the compound
     * after the final one is the only part that discriminates. */
    size_t start = 0;
    for (size_t i = 0; i < len; i++) {
        char c = sel[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
            c == '>' || c == '+' || c == '~' || c == ',') {
            start = i + 1;
        } else if (c == ':' || c == '[' || c == '(') {
            /* A pseudo class, attribute test or functional notation may appear
             * anywhere in the final compound and changes what the selector
             * means (`a:hover` must not be read as an element named `hover`),
             * so such a selector is skipped outright. */
            return false;
        }
    }
    while (start < len && isspace((unsigned char)sel[start])) start++;
    if (start >= len) return false;

    unsigned char kind;
    size_t key_off;

    if (sel[start] == '#') {
        kind = CSS_KEY_ID;
        key_off = start + 1;
    } else if (sel[start] == '.') {
        kind = CSS_KEY_CLASS;
        key_off = start + 1;
    } else if (sel[start] == '*') {
        return false; /* universal selector: styles nothing rr can act on */
    } else {
        kind = CSS_KEY_TAG;
        key_off = start;
    }

    /* Read the identifier, then require that it ends the compound. Anything
     * still trailing it - a second class (`.a.b`), a second tag (`p#a`) or an
     * attribute test (`[rel]`) - means the selector needs more than one key to
     * describe, and indexing only part of it would match elements the author
     * never styled. */
    size_t key_len = 0;
    while (key_off + key_len < len && css_is_ident_char(sel[key_off + key_len])) key_len++;
    if (key_len == 0 || key_len >= CSS_KEY_MAX) return false;
    if (key_off + key_len != len) return false;

    *out_kind = kind;
    *out_len = key_len;
    memcpy(out_key, sel + key_off, key_len);
    out_key[key_len] = '\0';
    return true;
}

/* ==========================================================================
 * Stylesheet scanning
 *
 * A single forward pass with no backtracking. Braces, quotes and comments are
 * skipped in place; anything that is not a style rule is stepped over.
 * ========================================================================== */

/* Return the index just past the '}' matching the '{' at `i`.
 *
 * The opening brace must itself be part of the range being scanned. Starting
 * one byte later leaves the depth counter at zero, the rule's own closing brace
 * drives it to -1 without matching, and the scan then consumes the remainder of
 * the stylesheet as if it were one declaration block. */
static size_t css_block_end(const char *s, size_t i, size_t end) {
    int depth = 0;
    bool in_quote = false;
    char quote = '\0';

    for (; i < end; i++) {
        char c = s[i];
        if (in_quote) {
            if (c == '\\') i++;
            else if (c == quote) in_quote = false;
            continue;
        }
        if (c == '"' || c == '\'') { in_quote = true; quote = c; continue; }
        if (c == '/' && i + 1 < end && s[i + 1] == '*') {
            size_t close = i + 2;
            while (close + 1 < end && !(s[close] == '*' && s[close + 1] == '/')) close++;
            i = close + 1;
            continue;
        }
        if (c == '{') depth++;
        else if (c == '}') {
            if (--depth == 0) return i + 1;
        }
    }
    return end;
}

/* Index every style rule in the byte range [i, end). */
static void css_scan_rules(CssIndex *idx, const char *s, size_t i, size_t end) {
    char key[CSS_KEY_MAX];

    while (i < end) {
        char c = s[i];

        /* Skip whitespace and comments between rules. */
        if (isspace((unsigned char)c)) { i++; continue; }
        if (c == '/' && i + 1 < end && s[i + 1] == '*') {
            size_t close = i + 2;
            while (close + 1 < end && !(s[close] == '*' && s[close + 1] == '/')) close++;
            i = (close + 1 < end) ? close + 2 : end;
            continue;
        }

        if (c == '}') return; /* end of an enclosing @media block */

        /* --- At-rules ---
         *
         * `@media` and `@supports` wrap ordinary style rules, so their contents
         * are scanned recursively: EPUB print styles hide chapter titles inside
         * them often enough that ignoring them would lose the very headings rr
         * is looking for. Every other at-rule (`@font-face`, `@keyframes`,
         * `@import`, ...) is stepped over. */
        if (c == '@') {
            size_t name = i + 1;
            size_t j = name;
            while (j < end && css_is_ident_char(s[j])) j++;
            char at_name[16];
            css_lower_trim(s + name, j - name, at_name, sizeof(at_name));

            /* Find the at-rule's terminator, or its block if it has one. */
            size_t brace = end;
            size_t semi = end;
            bool in_quote = false;
            char quote = '\0';
            for (size_t k = j; k < end; k++) {
                char d = s[k];
                if (in_quote) {
                    if (d == '\\') k++;
                    else if (d == quote) in_quote = false;
                    continue;
                }
                if (d == '"' || d == '\'') { in_quote = true; quote = d; continue; }
                if (d == '{') { brace = k; break; }
                if (d == ';') { semi = k; break; }
            }

            if (brace < end) {
                size_t after = css_block_end(s, brace, end);
                if (strcmp(at_name, "media") == 0 || strcmp(at_name, "supports") == 0) {
                    css_scan_rules(idx, s, brace + 1, after - 1);
                }
                i = after;
            } else {
                i = (semi < end) ? semi + 1 : end;
            }
            continue;
        }

        /* --- Style rule: read the selector list up to '{' --- */
        size_t sel_start = i;
        size_t sel_end = end;
        bool in_quote = false;
        char quote = '\0';
        for (size_t k = i; k < end; k++) {
            char d = s[k];
            if (in_quote) {
                if (d == '\\') k++;
                else if (d == quote) in_quote = false;
                continue;
            }
            if (d == '"' || d == '\'') { in_quote = true; quote = d; continue; }
            if (d == '/' && k + 1 < end && s[k + 1] == '*') {
                size_t close = k + 2;
                while (close + 1 < end && !(s[close] == '*' && s[close + 1] == '/')) close++;
                k = close + 1;
                continue;
            }
            if (d == '{' || d == '}') { sel_end = k; break; }
        }
        if (sel_end >= end) break; /* truncated stylesheet: stop cleanly */
        if (s[sel_end] != '{') { i = sel_end + 1; continue; }

        size_t decl_start = sel_end + 1;
        size_t decl_end = css_block_end(s, sel_end, end);

        /* Parse the declaration block once, then apply it to every selector. */
        CssStyle decl_style;
        memset(&decl_style, 0, sizeof(decl_style));
        decl_style.font_ratio = 1.0f;
        unsigned decl_present = 0;
        css_parse_declarations(s + decl_start, decl_end - 1 - decl_start,
                                &decl_style, &decl_present);

        if (decl_present) {
            /* Split the selector list on top-level commas. */
            size_t sel = sel_start;
            while (sel < sel_end) {
                /* `sel_end` is the default: a selector with no comma in it is
                 * the whole run, not an empty piece. */
                size_t piece_end = sel_end;
                int depth = 0;
                for (size_t k = sel; k < sel_end; k++) {
                    char d = s[k];
                    if (d == '[' || d == '(') { depth++; continue; }
                    if (d == ']' || d == ')') { if (depth > 0) depth--; continue; }
                    if (d == ',' && depth == 0) { piece_end = k; break; }
                }

                unsigned char kind;
                size_t klen;
                if (css_selector_key(s + sel, piece_end - sel, &kind, key, &klen)) {
                    CssRule *r = css_get_rule(idx, key, klen, kind);
                    css_merge(&r->style, &decl_style, decl_present);
                    r->present |= decl_present;
                }

                sel = piece_end + 1;
            }
        }

        i = decl_end;
    }
}

/* ==========================================================================
 * Public interface
 * ========================================================================== */

/* Allocate an empty style index. */
CssIndex *css_index_new(void) {
    return (CssIndex *)xcalloc(1, sizeof(CssIndex));
}

/* Release an index and every rule string it owns. */
void css_index_free(CssIndex *idx) {
    if (!idx) return;

    for (unsigned b = 0; b < CSS_BUCKETS; b++) {
        CssRule *r = idx->buckets[b];
        while (r) {
            CssRule *next = r->next;
            free(r->key);
            free(r);
            r = next;
        }
    }
    free(idx);
}

/* Compile one stylesheet's source into `idx`. */
void css_index_load(CssIndex *idx, const char *data, size_t len) {
    if (!idx || !data || len == 0) return;

    /* A stylesheet may begin with a UTF-8 byte order mark; skip it so the
     * first real rule is still recognised. */
    if (len >= 3 && (unsigned char)data[0] == 0xEF &&
        (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) {
        data += 3;
        len -= 3;
    }

    css_scan_rules(idx, data, 0, len);
}

/* Resolve the effective style of one element. */
void css_resolve(const CssIndex *idx, const char *tag, const char *class_attr,
                 const char *id_attr, const char *inline_style, CssStyle *out) {
    if (!out) return;

    out->bold = false;
    out->italic = false;
    out->underline = false;
    out->center = false;
    out->page_break_before = false;
    out->font_ratio = 1.0f;

    if (idx) {
        /* Cascade order, weakest first: element selector, then id, then each
         * class in document order. */
        if (tag && *tag) {
            css_apply_key(idx, tag, strlen(tag), CSS_KEY_TAG, out);
        }
        if (id_attr && *id_attr) {
            css_apply_key(idx, id_attr, strlen(id_attr), CSS_KEY_ID, out);
        }
        if (class_attr && *class_attr) {
            size_t len = strlen(class_attr);
            size_t i = 0;
            while (i < len) {
                while (i < len && isspace((unsigned char)class_attr[i])) i++;
                size_t start = i;
                while (i < len && !isspace((unsigned char)class_attr[i])) i++;
                if (i > start) {
                    css_apply_key(idx, class_attr + start, i - start, CSS_KEY_CLASS, out);
                }
            }
        }
    }

    /* The inline `style` attribute outranks every stylesheet rule. */
    if (inline_style && *inline_style) {
        CssStyle inline_style_value;
        memset(&inline_style_value, 0, sizeof(inline_style_value));
        inline_style_value.font_ratio = 1.0f;
        unsigned present = 0;
        css_parse_declarations(inline_style, strlen(inline_style), &inline_style_value, &present);
        css_merge(out, &inline_style_value, present);
    }
}