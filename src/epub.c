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

#include "epub.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/HTMLparser.h>

/* ==========================================================================
 * Forward declarations of internal helpers
 * ========================================================================== */

static bool parse_container_xml(EpubBook *book, char **out_error);
static bool parse_opf_document(EpubBook *book, char **out_error);
static void parse_ncx_navpoint(EpubBook *book, xmlNode *node, int level);
static void parse_ncx_document(EpubBook *book, const char *ncx_full_path);
static void parse_epub3_nav_document(EpubBook *book, const char *nav_full_path);
static void parse_nav_list(EpubBook *book, xmlNode *node, int level);
static void resolve_toc_targets(EpubBook *book);
static void generate_fallback_toc(EpubBook *book);

/* ==========================================================================
 * Archive entry reader
 *
 * Functions in this section interface directly with libzip to extract
 * uncompressed file buffers from the EPUB container.
 * ========================================================================== */

/* Append a new entry to the book's Table of Contents array.
 *
 * Cleans and trims the title, separates any HTML anchor fragment (e.g.
 * "chap.xhtml#sec1" -> path="chap.xhtml", anchor="sec1"), decodes percent-
 * encoded characters in the URL, and resolves the full canonical path inside
 * the zip archive using the book's OPF base directory. */
static void add_toc_item(EpubBook *book, const char *title, const char *href, int level, int play_order) {
    if (!title || !*title) return;

    book->toc = (EpubTocItem *)xrealloc(book->toc, (book->toc_count + 1) * sizeof(EpubTocItem));
    EpubTocItem *item = &book->toc[book->toc_count++];
    memset(item, 0, sizeof(EpubTocItem));

    char *t = xstrdup(title);
    str_trim(t);
    item->title = t;
    item->href = href ? xstrdup(href) : xstrdup("");
    item->level = level;
    item->play_order = play_order;
    item->spine_index = -1;

    /* Extract URL path and anchor fragment */
    char *clean_href = NULL;
    char *anchor = NULL;
    split_url_fragment(item->href, &clean_href, &anchor);

    /* Decode percent-encoded URI entities and build archive path */
    char *decoded_path = url_decode(clean_href);
    item->full_path = path_join(book->opf_dir, decoded_path);
    item->anchor = anchor;

    free(clean_href);
    free(decoded_path);
}

/* Read an arbitrary uncompressed file from the EPUB archive.
 *
 * Allocates a buffer sized to the entry plus one extra byte for a null
 * terminator, making the data directly usable by C string and XML functions.
 * Returns NULL if the file does not exist in the archive or cannot be read. */
char *epub_read_entry(EpubBook *book, const char *entry_path, size_t *out_size) {
    if (!book || !book->za || !entry_path || !*entry_path) return NULL;

    zip_stat_t st;
    zip_stat_init(&st);
    if (zip_stat(book->za, entry_path, 0, &st) != 0) {
        return NULL;
    }

    zip_file_t *zf = zip_fopen(book->za, entry_path, 0);
    if (!zf) return NULL;

    char *buffer = (char *)xmalloc((size_t)st.size + 1);
    zip_int64_t bytes_read = zip_fread(zf, buffer, st.size);
    zip_fclose(zf);

    if (bytes_read < 0) {
        free(buffer);
        return NULL;
    }

    buffer[bytes_read] = '\0';
    if (out_size) {
        *out_size = (size_t)bytes_read;
    }
    return buffer;
}

/* Read the XHTML content of a chapter identified by its spine sequence index.
 * Resolves the spine item's manifest record to its archive path and reads it. */
char *epub_read_spine_item(EpubBook *book, size_t spine_index, size_t *out_size) {
    if (!book || spine_index >= book->spine_count) return NULL;
    EpubSpineItem *si = &book->spine[spine_index];
    if (!si->item || !si->item->full_path) return NULL;
    return epub_read_entry(book, si->item->full_path, out_size);
}

/* ==========================================================================
 * Container XML discovery
 *
 * Every valid EPUB archive contains a file at fixed path:
 *   META-INF/container.xml
 *
 * This file identifies the package rootfile (the .opf file) which serves as
 * the master manifest of the entire book.
 * ========================================================================== */

/* Parse container.xml to locate the book's OPF package document.
 *
 * Reads the XML, locates the <rootfiles> group, and extracts the 'full-path'
 * attribute of the primary <rootfile> element.
 *
 * Saves the canonical OPF path in book->opf_path and its parent directory
 * in book->opf_dir.
 *
 * Returns true on success, or false with an allocated error message. */
static bool parse_container_xml(EpubBook *book, char **out_error) {
    size_t size = 0;
    char *data = epub_read_entry(book, "META-INF/container.xml", &size);
    if (!data) {
        if (out_error) *out_error = xstrdup("Missing or corrupted META-INF/container.xml");
        return false;
    }

    xmlDocPtr doc = xmlReadMemory(data, (int)size, "container.xml", NULL,
                                  XML_PARSE_RECOVER | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    free(data);
    if (!doc) {
        if (out_error) *out_error = xstrdup("Failed to parse META-INF/container.xml");
        return false;
    }

    xmlNode *root = xmlDocGetRootElement(doc);
    if (!root) {
        xmlFreeDoc(doc);
        if (out_error) *out_error = xstrdup("Empty container.xml");
        return false;
    }

    char *full_path = NULL;

    /* Search for <rootfiles> -> <rootfile full-path="..."> */
    for (xmlNode *cur = root->children; cur; cur = cur->next) {
        if (cur->type == XML_ELEMENT_NODE && strcasecmp((const char *)cur->name, "rootfiles") == 0) {
            for (xmlNode *rf = cur->children; rf; rf = rf->next) {
                if (rf->type == XML_ELEMENT_NODE && strcasecmp((const char *)rf->name, "rootfile") == 0) {
                    xmlChar *prop = xmlGetProp(rf, (const xmlChar *)"full-path");
                    if (prop) {
                        full_path = xstrdup((const char *)prop);
                        xmlFree(prop);
                        break;
                    }
                }
            }
        }
        if (full_path) break;
    }

    xmlFreeDoc(doc);

    if (!full_path) {
        if (out_error) *out_error = xstrdup("No rootfile element found in container.xml");
        return false;
    }

    path_normalize(full_path);
    book->opf_path = full_path;
    book->opf_dir = path_dirname(full_path);
    return true;
}

/* ==========================================================================
 * Stylesheet compilation
 *
 * A book decides what is a heading almost entirely in CSS, so rr compiles
 * every stylesheet the manifest declares before it reads a single chapter.
 * Compiling is cheap: the rules are indexed by class, id and tag name, so
 * resolving one element later costs a handful of hash lookups rather than a
 * scan of the sheet.
 *
 * Stylesheets are loaded in manifest order, which is the author's declared
 * preference order and matches how a browser would apply them.
 * ========================================================================== */

/* Compile every `text/css` resource listed in the manifest into `book->css`.
 *
 * A stylesheet that fails to read is skipped rather than treated as fatal: a
 * broken asset must never stop a book from opening. */
static void load_stylesheets(EpubBook *book) {
    book->css = css_index_new();
    if (!book->css) return;

    for (size_t i = 0; i < book->manifest_count; i++) {
        EpubItem *item = &book->manifest[i];
        if (!item->media_type || strcmp(item->media_type, "text/css") != 0) continue;
        if (!item->full_path) continue;

        size_t size = 0;
        char *data = epub_read_entry(book, item->full_path, &size);
        if (!data) continue;

        css_index_load(book->css, data, size);
        free(data);
    }
}

/* ==========================================================================
 * OPF package document parser
 *
 * The OPF file defines the structure of the publication:
 *   1. <metadata>: Title, author, language.
 *   2. <manifest>: Full list of constituent resources (HTML, styles, fonts).
 *   3. <spine>: Ordered reading sequence of items.
 * ========================================================================== */

/* Parse the OPF package document into metadata, manifest, and spine structures.
 * Also initiates discovery and parsing of the Table of Contents. */
static bool parse_opf_document(EpubBook *book, char **out_error) {
    size_t size = 0;
    char *data = epub_read_entry(book, book->opf_path, &size);
    if (!data) {
        if (out_error) *out_error = xstrdup("Cannot open OPF package file");
        return false;
    }

    xmlDocPtr doc = xmlReadMemory(data, (int)size, book->opf_path, NULL,
                                  XML_PARSE_RECOVER | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    free(data);
    if (!doc) {
        if (out_error) *out_error = xstrdup("Failed to parse OPF package file");
        return false;
    }

    xmlNode *root = xmlDocGetRootElement(doc);
    if (!root) {
        xmlFreeDoc(doc);
        if (out_error) *out_error = xstrdup("Empty OPF package element");
        return false;
    }

    char *toc_ncx_id = NULL;

    /* Iterate through primary OPF child sections */
    for (xmlNode *sec = root->children; sec; sec = sec->next) {
        if (sec->type != XML_ELEMENT_NODE) continue;

        /* --- Section 1: Publication Metadata --- */
        if (strcasecmp((const char *)sec->name, "metadata") == 0) {
            for (xmlNode *meta = sec->children; meta; meta = meta->next) {
                if (meta->type != XML_ELEMENT_NODE) continue;
                const char *tag = (const char *)meta->name;

                if (strcasecmp(tag, "title") == 0 && !book->title) {
                    xmlChar *content = xmlNodeGetContent(meta);
                    if (content) {
                        book->title = xstrdup((const char *)content);
                        str_trim(book->title);
                        xmlFree(content);
                    }
                } else if (strcasecmp(tag, "creator") == 0 && !book->author) {
                    xmlChar *content = xmlNodeGetContent(meta);
                    if (content) {
                        book->author = xstrdup((const char *)content);
                        str_trim(book->author);
                        xmlFree(content);
                    }
                } else if (strcasecmp(tag, "language") == 0 && !book->language) {
                    xmlChar *content = xmlNodeGetContent(meta);
                    if (content) {
                        book->language = xstrdup((const char *)content);
                        str_trim(book->language);
                        xmlFree(content);
                    }
                }
            }
        }
        /* --- Section 2: Resource Manifest --- */
        else if (strcasecmp((const char *)sec->name, "manifest") == 0) {
            for (xmlNode *item = sec->children; item; item = item->next) {
                if (item->type != XML_ELEMENT_NODE || strcasecmp((const char *)item->name, "item") != 0) {
                    continue;
                }

                xmlChar *id = xmlGetProp(item, (const xmlChar *)"id");
                xmlChar *href = xmlGetProp(item, (const xmlChar *)"href");
                xmlChar *media = xmlGetProp(item, (const xmlChar *)"media-type");
                xmlChar *props = xmlGetProp(item, (const xmlChar *)"properties");

                if (id && href) {
                    book->manifest = (EpubItem *)xrealloc(book->manifest, (book->manifest_count + 1) * sizeof(EpubItem));
                    EpubItem *ei = &book->manifest[book->manifest_count++];
                    memset(ei, 0, sizeof(EpubItem));

                    ei->id = xstrdup((const char *)id);
                    ei->href = xstrdup((const char *)href);
                    ei->media_type = media ? xstrdup((const char *)media) : xstrdup("");
                    ei->properties = props ? xstrdup((const char *)props) : NULL;

                    /* Resolve path relative to OPF base directory */
                    char *decoded_href = url_decode(ei->href);
                    ei->full_path = path_join(book->opf_dir, decoded_href);
                    free(decoded_href);
                }

                if (id) xmlFree(id);
                if (href) xmlFree(href);
                if (media) xmlFree(media);
                if (props) xmlFree(props);
            }
        }
        /* --- Section 3: Reading Spine --- */
        else if (strcasecmp((const char *)sec->name, "spine") == 0) {
            /* The 'toc' attribute on <spine> references the NCX manifest ID */
            xmlChar *toc_prop = xmlGetProp(sec, (const xmlChar *)"toc");
            if (toc_prop) {
                toc_ncx_id = xstrdup((const char *)toc_prop);
                xmlFree(toc_prop);
            }

            for (xmlNode *ref = sec->children; ref; ref = ref->next) {
                if (ref->type != XML_ELEMENT_NODE || strcasecmp((const char *)ref->name, "itemref") != 0) {
                    continue;
                }

                xmlChar *idref = xmlGetProp(ref, (const xmlChar *)"idref");
                xmlChar *linear = xmlGetProp(ref, (const xmlChar *)"linear");

                if (idref) {
                    /* Match the idref attribute against our manifest inventory */
                    EpubItem *matched = NULL;
                    for (size_t i = 0; i < book->manifest_count; i++) {
                        if (strcmp(book->manifest[i].id, (const char *)idref) == 0) {
                            matched = &book->manifest[i];
                            break;
                        }
                    }

                    if (matched) {
                        book->spine = (EpubSpineItem *)xrealloc(book->spine, (book->spine_count + 1) * sizeof(EpubSpineItem));
                        EpubSpineItem *si = &book->spine[book->spine_count++];
                        si->idref = xstrdup((const char *)idref);
                        si->linear = (!linear || strcasecmp((const char *)linear, "no") != 0);
                        si->item = matched;
                    }
                    xmlFree(idref);
                }
                if (linear) xmlFree(linear);
            }
        }
    }

    xmlFreeDoc(doc);

    /* ======================================================================
     * Discover Table of Contents
     *
     * We try navigation extraction in order of preference:
     *   1. EPUB 2 NCX document (referenced by spine 'toc' or MIME type).
     *   2. EPUB 3 Navigation Document (<item properties="nav">).
     *   3. Synthetic fallback TOC based on sequential spine chapters.
     * ====================================================================== */

    /* 1. Try EPUB 2 NCX document */
    const char *ncx_full_path = NULL;
    if (toc_ncx_id) {
        for (size_t i = 0; i < book->manifest_count; i++) {
            if (strcmp(book->manifest[i].id, toc_ncx_id) == 0) {
                ncx_full_path = book->manifest[i].full_path;
                break;
            }
        }
    }
    if (!ncx_full_path) {
        for (size_t i = 0; i < book->manifest_count; i++) {
            if (strcmp(book->manifest[i].media_type, "application/x-dtbncx+xml") == 0) {
                ncx_full_path = book->manifest[i].full_path;
                break;
            }
        }
    }

    if (ncx_full_path) {
        parse_ncx_document(book, ncx_full_path);
    }

    /* 2. If NCX yielded no items, try EPUB 3 Navigation Document */
    if (book->toc_count == 0) {
        const char *nav_full_path = NULL;
        for (size_t i = 0; i < book->manifest_count; i++) {
            if (book->manifest[i].properties && strstr(book->manifest[i].properties, "nav")) {
                nav_full_path = book->manifest[i].full_path;
                break;
            }
        }
        if (nav_full_path) {
            parse_epub3_nav_document(book, nav_full_path);
        }
    }

    free(toc_ncx_id);

    /* 3. If still empty, synthesize a numbered chapter fallback */
    if (book->toc_count == 0) {
        generate_fallback_toc(book);
    } else {
        resolve_toc_targets(book);
    }

    /* Apply default metadata fallbacks if publication omits them */
    if (!book->title) book->title = xstrdup("Untitled");
    if (!book->author) book->author = xstrdup("Unknown Author");
    if (!book->language) book->language = xstrdup("en");

    /* Compile the book's stylesheets now, while the manifest is in hand.
     * Chapter parsing depends on them, and it happens later and repeatedly. */
    load_stylesheets(book);

    return true;
}

/* ==========================================================================
 * EPUB 2 NCX navigation parser
 *
 * NCX documents organize chapters into hierarchical <navPoint> trees.
 * Each navPoint contains a <navLabel><text> title and a <content src="..."> URI.
 * ========================================================================== */

/* Recursively parse <navPoint> elements and nested sub-chapters. */
static void parse_ncx_navpoint(EpubBook *book, xmlNode *node, int level) {
    for (xmlNode *cur = node; cur; cur = cur->next) {
        if (cur->type != XML_ELEMENT_NODE) continue;
        if (strcasecmp((const char *)cur->name, "navPoint") == 0) {
            xmlChar *order_attr = xmlGetProp(cur, (const xmlChar *)"playOrder");
            int play_order = order_attr ? atoi((const char *)order_attr) : 0;
            if (order_attr) xmlFree(order_attr);

            char *label_text = NULL;
            char *src_text = NULL;

            /* Extract label and content src */
            for (xmlNode *child = cur->children; child; child = child->next) {
                if (child->type != XML_ELEMENT_NODE) continue;
                if (strcasecmp((const char *)child->name, "navLabel") == 0) {
                    for (xmlNode *lbl = child->children; lbl; lbl = lbl->next) {
                        if (lbl->type == XML_ELEMENT_NODE && strcasecmp((const char *)lbl->name, "text") == 0) {
                            xmlChar *c = xmlNodeGetContent(lbl);
                            if (c) {
                                label_text = xstrdup((const char *)c);
                                xmlFree(c);
                            }
                        }
                    }
                } else if (strcasecmp((const char *)child->name, "content") == 0) {
                    xmlChar *src = xmlGetProp(child, (const xmlChar *)"src");
                    if (src) {
                        src_text = xstrdup((const char *)src);
                        xmlFree(src);
                    }
                }
            }

            if (label_text && src_text) {
                add_toc_item(book, label_text, src_text, level, play_order);
            }

            free(label_text);
            free(src_text);

            /* Recurse into nested sub-chapters */
            for (xmlNode *sub = cur->children; sub; sub = sub->next) {
                if (sub->type == XML_ELEMENT_NODE && strcasecmp((const char *)sub->name, "navPoint") == 0) {
                    parse_ncx_navpoint(book, sub, level + 1);
                }
            }
        }
    }
}

/* Parse an EPUB 2 NCX file located at `ncx_full_path`. */
static void parse_ncx_document(EpubBook *book, const char *ncx_full_path) {
    size_t size = 0;
    char *data = epub_read_entry(book, ncx_full_path, &size);
    if (!data) return;

    xmlDocPtr doc = xmlReadMemory(data, (int)size, ncx_full_path, NULL,
                                  XML_PARSE_RECOVER | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    free(data);
    if (!doc) return;

    xmlNode *root = xmlDocGetRootElement(doc);
    if (root) {
        for (xmlNode *cur = root->children; cur; cur = cur->next) {
            if (cur->type == XML_ELEMENT_NODE && strcasecmp((const char *)cur->name, "navMap") == 0) {
                parse_ncx_navpoint(book, cur->children, 0);
            }
        }
    }
    xmlFreeDoc(doc);
}

/* ==========================================================================
 * EPUB 3 Navigation Document parser
 *
 * EPUB 3 defines navigation using a standard XHTML document containing a
 * <nav epub:type="toc"> section structured as nested <ol> or <ul> lists.
 * ========================================================================== */

/* Recursively parse ordered and unordered list navigation items. */
static void parse_nav_list(EpubBook *book, xmlNode *node, int level) {
    for (xmlNode *cur = node; cur; cur = cur->next) {
        if (cur->type != XML_ELEMENT_NODE) continue;
        if (strcasecmp((const char *)cur->name, "li") == 0) {
            for (xmlNode *child = cur->children; child; child = child->next) {
                if (child->type == XML_ELEMENT_NODE) {
                    if (strcasecmp((const char *)child->name, "a") == 0) {
                        xmlChar *href = xmlGetProp(child, (const xmlChar *)"href");
                        xmlChar *text = xmlNodeGetContent(child);
                        if (href && text) {
                            add_toc_item(book, (const char *)text, (const char *)href, level, 0);
                        }
                        if (href) xmlFree(href);
                        if (text) xmlFree(text);
                    } else if (strcasecmp((const char *)child->name, "ol") == 0 ||
                               strcasecmp((const char *)child->name, "ul") == 0) {
                        parse_nav_list(book, child->children, level + 1);
                    }
                }
            }
        }
    }
}

/* Parse an EPUB 3 Navigation Document at `nav_full_path`. */
static void parse_epub3_nav_document(EpubBook *book, const char *nav_full_path) {
    size_t size = 0;
    char *data = epub_read_entry(book, nav_full_path, &size);
    if (!data) return;

    htmlDocPtr doc = htmlReadMemory(data, (int)size, nav_full_path, "UTF-8",
                                    HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);
    free(data);
    if (!doc) return;

    xmlNode *root = xmlDocGetRootElement(doc);
    if (root) {
        xmlNode *nav_node = NULL;

        /* Traverse tree searching for the <nav> element */
        for (xmlNode *cur = root; cur; cur = cur->next) {
            if (cur->type == XML_ELEMENT_NODE && strcasecmp((const char *)cur->name, "nav") == 0) {
                nav_node = cur;
                break;
            }
            if (cur->children) {
                xmlNode *stack[64];
                int s_idx = 0;
                stack[s_idx++] = cur->children;
                while (s_idx > 0) {
                    xmlNode *n = stack[--s_idx];
                    while (n) {
                        if (n->type == XML_ELEMENT_NODE && strcasecmp((const char *)n->name, "nav") == 0) {
                            nav_node = n;
                            break;
                        }
                        if (n->children && s_idx < 63) {
                            stack[s_idx++] = n->children;
                        }
                        n = n->next;
                    }
                    if (nav_node) break;
                }
            }
            if (nav_node) break;
        }

        /* Parse list links inside <nav> */
        if (nav_node) {
            for (xmlNode *child = nav_node->children; child; child = child->next) {
                if (child->type == XML_ELEMENT_NODE &&
                    (strcasecmp((const char *)child->name, "ol") == 0 ||
                     strcasecmp((const char *)child->name, "ul") == 0)) {
                    parse_nav_list(book, child->children, 0);
                }
            }
        }
    }
    xmlFreeDoc(doc);
}

/* ==========================================================================
 * TOC target resolution & fallback
 * ========================================================================== */

/* Correlate TOC entry file paths with items in the linear spine.
 * This mapping allows instant jump to the spine chapter when selecting a TOC item. */
static void resolve_toc_targets(EpubBook *book) {
    for (size_t i = 0; i < book->toc_count; i++) {
        EpubTocItem *toc = &book->toc[i];
        for (size_t j = 0; j < book->spine_count; j++) {
            if (book->spine[j].item && book->spine[j].item->full_path) {
                if (strcmp(book->spine[j].item->full_path, toc->full_path) == 0) {
                    toc->spine_index = (int)j;
                    break;
                }
            }
        }
    }
}

/* Generate a synthetic Table of Contents when a book lacks navigation documents.
 * Creates one entry per spine chapter ("Chapter 1", "Chapter 2", etc.). */
static void generate_fallback_toc(EpubBook *book) {
    for (size_t i = 0; i < book->spine_count; i++) {
        char title[64];
        snprintf(title, sizeof(title), "Chapter %zu", i + 1);
        add_toc_item(book, title, book->spine[i].item ? book->spine[i].item->href : "", 0, (int)i);
        book->toc[book->toc_count - 1].spine_index = (int)i;
    }
}

/* Lookup the best human-readable chapter title for a spine index. */
const char *epub_get_chapter_title_for_spine(EpubBook *book, size_t spine_index) {
    if (!book) return "Chapter";

    /* Prefer a TOC entry without an anchor (i.e. representing the whole chapter) */
    for (size_t i = 0; i < book->toc_count; i++) {
        if (book->toc[i].spine_index == (int)spine_index && !book->toc[i].anchor) {
            return book->toc[i].title;
        }
    }
    /* Fall back to any TOC entry located within that chapter */
    for (size_t i = 0; i < book->toc_count; i++) {
        if (book->toc[i].spine_index == (int)spine_index) {
            return book->toc[i].title;
        }
    }
    return "Chapter";
}

/* ==========================================================================
 * Reading cache construction
 *
 * Everything expensive about opening a book happens exactly once, here:
 * stylesheets are compiled, each spine document is parsed into semantic
 * blocks, conversion debris is removed, and a table of contents is derived
 * from the headings when the book does not supply a usable one.
 *
 * The reader rebuilds its whole layout on every resize, margin change or
 * typography toggle. Typesetting is cheap; re-parsing an entire novel from
 * XHTML is not. Caching the parsed documents is therefore the difference
 * between a resize that is instant and one that visibly stalls.
 * ========================================================================== */

/* Report how many spine documents the book's own navigation actually names.
 *
 * A navigation document that mentions one document out of eleven is not a
 * table of contents; it is a stray bookmark. The count decides whether the
 * detected headings are allowed to stand in for it. */
static size_t count_toc_spine_coverage(const EpubBook *book) {
    bool *covered = (bool *)xcalloc(book->spine_count ? book->spine_count : 1, sizeof(bool));
    for (size_t i = 0; i < book->toc_count; i++) {
        int s = book->toc[i].spine_index;
        if (s >= 0 && (size_t)s < book->spine_count) covered[s] = true;
    }

    size_t n = 0;
    for (size_t i = 0; i < book->spine_count; i++) {
        if (covered[i]) n++;
    }
    free(covered);
    return n;
}

/* Replace the book's table of contents with one built from detected headings.
 *
 * Some books - typically those produced by scanning and re-converting a printed
 * edition - ship an NCX containing a single entry, or none at all, while the
 * text itself carries a clear hierarchy of titles. Since those titles have
 * already been recovered from the stylesheets, the honest table of contents is
 * the one that lists them.
 *
 * The book's existing navigation is not thrown away but for the one entry that
 * makes it useless: entries that name a spine document the detected headings
 * do not cover are kept, so a partial navigation still contributes what it
 * knows. */
static void synthesize_toc_from_headings(EpubBook *book) {
    EpubTocItem *synthesized = NULL;
    size_t synth_count = 0;
    int play_order = 0;

    for (size_t s = 0; s < book->spine_count; s++) {
        const ChapterDocument *doc = book->docs ? book->docs[s] : NULL;
        if (!doc) continue;

        for (size_t b = 0; b < doc->block_count; b++) {
            const Block *blk = &doc->blocks[b];
            if (blk->type != BLOCK_HEADING) continue;
            if (!blk->section_title || !*blk->section_title) continue;

            const char *path = book->spine[s].item ? book->spine[s].item->full_path : NULL;
            synthesized = (EpubTocItem *)xrealloc(synthesized,
                                                  (synth_count + 1) * sizeof(EpubTocItem));
            EpubTocItem *it = &synthesized[synth_count++];
            memset(it, 0, sizeof(EpubTocItem));
            it->title = xstrdup(blk->section_title);
            str_trim(it->title);
            it->href = xstrdup(path ? path : "");
            it->full_path = xstrdup(path ? path : "");
            it->anchor = blk->anchor_id ? xstrdup(blk->anchor_id) : NULL;
            it->level = (blk->heading_level > 0 ? blk->heading_level : 2) - 1;
            it->spine_index = (int)s;
            it->play_order = play_order++;
        }
    }

    if (synth_count == 0) {
        free(synthesized);
        return;
    }

    /* Splice the existing navigation into the synthesized list, keeping only
     * entries that are not already represented by a detected heading. */
    EpubTocItem *merged = (EpubTocItem *)xcalloc(synth_count + book->toc_count, sizeof(EpubTocItem));
    size_t merged_count = 0;

    for (size_t i = 0; i < synth_count; i++) {
        merged[merged_count++] = synthesized[i];
    }

    for (size_t i = 0; i < book->toc_count; i++) {
        const EpubTocItem *old = &book->toc[i];
        bool duplicate = false;
        for (size_t j = 0; j < synth_count; j++) {
            if (merged[j].spine_index == old->spine_index &&
                strcmp(merged[j].title, old->title) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;

        EpubTocItem copy = *old;
        copy.title = xstrdup(old->title);
        copy.href = xstrdup(old->href ? old->href : "");
        copy.full_path = xstrdup(old->full_path ? old->full_path : "");
        copy.anchor = old->anchor ? xstrdup(old->anchor) : NULL;
        merged[merged_count++] = copy;
    }

    free(synthesized);
    if (book->toc) {
        for (size_t i = 0; i < book->toc_count; i++) {
            free(book->toc[i].title);
            free(book->toc[i].href);
            free(book->toc[i].full_path);
            free(book->toc[i].anchor);
        }
        free(book->toc);
    }

    book->toc = merged;
    book->toc_count = merged_count;
    resolve_toc_targets(book);
}

/* Build the reading cache: parse every spine document and repair navigation. */
bool epub_load_documents(EpubBook *book) {
    if (!book) return false;
    if (book->docs_loaded) return true;
    if (book->spine_count == 0) return false;

    if (!book->css) book->css = css_index_new();

    book->docs = (ChapterDocument **)xcalloc(book->spine_count, sizeof(ChapterDocument *));

    for (size_t s = 0; s < book->spine_count; s++) {
        const char *href = book->spine[s].item ? book->spine[s].item->href : NULL;
        const char *def_title = epub_get_chapter_title_for_spine(book, s);

        size_t size = 0;
        char *data = epub_read_spine_item(book, s, &size);
        if (data && size > 0) {
            book->docs[s] = html_parse_chapter(data, size, s, href, def_title, book->css);
            free(data);
        }

        if (!book->docs[s]) {
            /* The chapter could not be parsed at all. An empty document keeps
             * the spine and the layout arrays aligned. */
            book->docs[s] = (ChapterDocument *)xcalloc(1, sizeof(ChapterDocument));
            book->docs[s]->spine_index = s;
            book->docs[s]->href = xstrdup(href ? href : "");
            book->docs[s]->title = xstrdup(def_title);
        }
    }

    /* Printed page numbers and running heads can only be recognised by looking
     * at the whole book at once, so pruning happens only now that every
     * document has been read. */
    html_prune_document_noise(book->docs, book->spine_count);

    /* Headings are classified only after the pruning above, never before. A
     * converted book writes its running head in exactly the same markup as its
     * chapter titles - a bold span in the body face - so the two can only be
     * told apart by the repetition that pruning measures. Promoting a running
     * head would additionally give it a synthetic anchor, and an anchored
     * block is one that pruning deliberately spares. */
    for (size_t s = 0; s < book->spine_count; s++) {
        html_classify_headings(book->docs[s]);
    }

    /* Titles are chosen last of all, so that a running head never becomes the
     * name of the chapter it was printed at the top of. */
    for (size_t s = 0; s < book->spine_count; s++) {
        html_finalize_title(book->docs[s], epub_get_chapter_title_for_spine(book, s));
    }

    /* A navigation document that names at most one spine item out of many is
     * not describing this book. Rebuild it from the headings that were
     * detected. */
    size_t coverage = count_toc_spine_coverage(book);
    if (book->spine_count >= 2 && coverage * 2 <= book->spine_count) {
        synthesize_toc_from_headings(book);
    }

    book->docs_loaded = 1;
    return true;
}

/* Borrow a spine document from the reading cache, loading it if necessary. */
const ChapterDocument *epub_get_document(EpubBook *book, size_t spine_index) {
    if (!book || spine_index >= book->spine_count) return NULL;
    if (!book->docs_loaded) epub_load_documents(book);
    return book->docs ? book->docs[spine_index] : NULL;
}

/* ==========================================================================
 * High-level public API
 * ========================================================================== */

/* Open an EPUB publication on disk.
 *
 * Verifies container integrity, parses package metadata, indexes manifest
 * assets, builds the linear reading spine, and extracts the Table of Contents.
 *
 * Returns an allocated EpubBook structure or NULL on error. */
EpubBook *epub_open(const char *filepath, char **out_error) {
    if (!filepath || !*filepath) {
        if (out_error) *out_error = xstrdup("No EPUB file specified");
        return NULL;
    }

    int zip_err = 0;
    zip_t *za = zip_open(filepath, ZIP_RDONLY, &zip_err);
    if (!za) {
        if (out_error) {
            zip_error_t zerr;
            zip_error_init_with_code(&zerr, zip_err);
            char buf[256];
            snprintf(buf, sizeof(buf), "Failed to open '%s': %s", filepath, zip_error_strerror(&zerr));
            zip_error_fini(&zerr);
            *out_error = xstrdup(buf);
        }
        return NULL;
    }

    EpubBook *book = (EpubBook *)xcalloc(1, sizeof(EpubBook));
    book->filepath = xstrdup(filepath);
    book->file_id = get_file_identifier(filepath);
    book->za = za;

    if (!parse_container_xml(book, out_error)) {
        epub_close(book);
        return NULL;
    }

    if (!parse_opf_document(book, out_error)) {
        epub_close(book);
        return NULL;
    }

    if (book->spine_count == 0) {
        if (out_error) *out_error = xstrdup("EPUB has no readable spine chapters");
        epub_close(book);
        return NULL;
    }

    return book;
}

/* Release all memory and close archive handles for an opened publication. */
void epub_close(EpubBook *book) {
    if (!book) return;

    free(book->filepath);
    free(book->file_id);
    free(book->title);
    free(book->author);
    free(book->language);
    free(book->opf_path);
    free(book->opf_dir);

    if (book->manifest) {
        for (size_t i = 0; i < book->manifest_count; i++) {
            free(book->manifest[i].id);
            free(book->manifest[i].href);
            free(book->manifest[i].media_type);
            free(book->manifest[i].full_path);
            free(book->manifest[i].properties);
        }
        free(book->manifest);
    }

    if (book->spine) {
        for (size_t i = 0; i < book->spine_count; i++) {
            free(book->spine[i].idref);
        }
        free(book->spine);
    }

    if (book->toc) {
        for (size_t i = 0; i < book->toc_count; i++) {
            free(book->toc[i].title);
            free(book->toc[i].href);
            free(book->toc[i].full_path);
            free(book->toc[i].anchor);
        }
        free(book->toc);
    }

    /* The parsed documents and the compiled rules are the last things to go:
     * the layout holds pointers into them, so nothing that references the book
     * may be freed before they are. */
    if (book->docs) {
        for (size_t i = 0; i < book->spine_count; i++) {
            chapter_document_free(book->docs[i]);
        }
        free(book->docs);
    }

    css_index_free(book->css);

    if (book->za) {
        zip_close(book->za);
    }

    free(book);
}

/* Print publication metadata and Table of Contents hierarchy to stdout. */
void epub_print_info(const EpubBook *book) {
    if (!book) return;
    printf("Title:     %s\n", book->title ? book->title : "Unknown");
    printf("Author:    %s\n", book->author ? book->author : "Unknown");
    printf("Language:  %s\n", book->language ? book->language : "Unknown");
    printf("Chapters:  %zu spine documents\n", book->spine_count);
    printf("TOC Items: %zu entries\n\n", book->toc_count);

    printf("Table of Contents:\n");
    for (size_t i = 0; i < book->toc_count; i++) {
        for (int l = 0; l < book->toc[i].level; l++) {
            printf("  ");
        }
        printf("• %s", book->toc[i].title);
        if (book->toc[i].spine_index >= 0) {
            printf(" (Spine item %d)", book->toc[i].spine_index + 1);
        }
        printf("\n");
    }
}
