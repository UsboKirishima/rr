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

#ifndef RR_EPUB_H
#define RR_EPUB_H

#include <stddef.h>
#include <stdbool.h>
#include <zip.h>

/* ==========================================================================
 * Data structures
 *
 * An EPUB publication is an Open Container Format (OCF) ZIP archive containing:
 *
 * 1. META-INF/container.xml:
 *    A standard container descriptor locating the primary Open Packaging
 *    Format (OPF) document via a <rootfile> element.
 *
 * 2. The Package OPF document:
 *    An XML file containing:
 *      - <metadata>: Book title, author/creator, language, and identifiers.
 *      - <manifest>: Complete catalogue of resources (XHTML chapters, images,
 *                    fonts, NCX navigation) indexed by unique IDs.
 *      - <spine>: Ordered sequence of <itemref> entries defining the linear
 *                 reading flow of the publication.
 *
 * 3. Table of Contents:
 *    Either an EPUB 2 NCX document (application/x-dtbncx+xml) or an EPUB 3
 *    Navigation Document (<nav epub:type="toc">) providing hierarchical
 *    section titles and anchor links.
 * ========================================================================== */

/* Represents a single file or resource catalogued in the OPF <manifest>.
 *
 * All string fields are heap-allocated and owned by the parent EpubBook. */
typedef struct {
    char *id;          /* Manifest item identifier (e.g. "chapter01") */
    char *href;        /* Relative URI as declared in the OPF file */
    char *media_type;  /* MIME type (e.g. "application/xhtml+xml") */
    char *full_path;   /* Canonical normalized path inside the zip archive */
    char *properties;  /* Optional EPUB 3 properties attribute (e.g. "nav") */
} EpubItem;

/* Represents an entry in the publication's linear reading order (<spine>).
 *
 * Each spine item references an item in the manifest by idref. The linear
 * flag indicates whether the item is part of the primary reading sequence
 * (true) or auxiliary content such as an appendix or copyright page (false). */
typedef struct {
    char *idref;       /* Identifier referencing a manifest item */
    bool linear;       /* True if part of primary sequential reading flow */
    EpubItem *item;    /* Direct pointer to the corresponding manifest item */
} EpubSpineItem;

/* Represents a hierarchical Table of Contents entry.
 *
 * A TOC entry may point to an entire chapter file or to a specific HTML
 * anchor ID within that chapter (e.g. "chapter1.xhtml#section_2"). */
typedef struct {
    char *title;       /* Human-readable chapter or section heading */
    char *href;        /* Raw URI from the TOC source */
    char *full_path;   /* Full path of the target chapter inside zip */
    char *anchor;      /* HTML target anchor ID (without '#'), or NULL */
    int level;         /* Hierarchy nesting depth (0 = top level) */
    int spine_index;   /* Index in the spine items array (-1 if unmapped) */
    int play_order;    /* Sequential display order from NCX, if present */
} EpubTocItem;

/* Master in-memory representation of an opened EPUB book.
 *
 * Holds the underlying zip archive handle, metadata, manifest items,
 * linear spine items, and parsed Table of Contents. */
typedef struct {
    char *filepath;          /* Path to the .epub file on disk */
    char *file_id;           /* Persistent fingerprint for state tracking */
    char *title;             /* Publication title */
    char *author;            /* Primary author or creator */
    char *language;          /* Language code (e.g. "en", "it") */
    char *opf_path;          /* Archive path to the package OPF file */
    char *opf_dir;           /* Archive directory containing the OPF file */

    EpubItem *manifest;      /* Array of all manifest resources */
    size_t manifest_count;   /* Total number of manifest items */

    EpubSpineItem *spine;    /* Array of spine items in linear reading order */
    size_t spine_count;      /* Total number of spine documents */

    EpubTocItem *toc;        /* Hierarchical Table of Contents items */
    size_t toc_count;        /* Total number of TOC entries */

    zip_t *za;               /* Open libzip archive handle */
} EpubBook;

/* ==========================================================================
 * EPUB archive lifecycle
 * ========================================================================== */

/* Open an EPUB file on disk and parse its structural metadata.
 *
 * Performs container verification, OPF parsing, manifest inventory,
 * spine sequencing, and Table of Contents extraction.
 *
 * On success, returns an allocated EpubBook pointer.
 * On failure, returns NULL and sets *out_error (if non-NULL) to an
 * allocated error message that the caller should free. */
EpubBook *epub_open(const char *filepath, char **out_error);

/* Close an open EPUB book and release all associated memory buffers,
 * manifest entries, spine records, TOC items, and the zip archive handle. */
void epub_close(EpubBook *book);

/* ==========================================================================
 * Reading chapter and manifest data
 * ========================================================================== */

/* Read an uncompressed entry from the EPUB archive into memory.
 *
 * Returns a newly allocated null-terminated buffer containing the raw data.
 * If `out_size` is non-NULL, it receives the exact number of bytes read.
 * Returns NULL on error or if the entry is not found. */
char *epub_read_entry(EpubBook *book, const char *entry_path, size_t *out_size);

/* Read an XHTML chapter by its spine index (0 .. spine_count - 1).
 *
 * Resolves the spine item's manifest entry, reads the archive content,
 * and returns a null-terminated buffer ready for HTML parsing. */
char *epub_read_spine_item(EpubBook *book, size_t spine_index, size_t *out_size);

/* ==========================================================================
 * Metadata and navigation queries
 * ========================================================================== */

/* Retrieve the best matching chapter title for a spine index.
 *
 * First searches the Table of Contents for an item referencing this spine
 * document without a sub-anchor. If no exact match is found, falls back to
 * any TOC item matching this spine index, or returns a generic "Chapter" label. */
const char *epub_get_chapter_title_for_spine(EpubBook *book, size_t spine_index);

/* Print book metadata and hierarchical Table of Contents to stdout.
 * Used by the '--info' command-line flag. */
void epub_print_info(const EpubBook *book);

#endif /* RR_EPUB_H */
