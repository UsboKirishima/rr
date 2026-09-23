/**
 * @file epub.h
 * @brief EPUB 2/3 container, metadata, spine, and TOC parser.
 */

#ifndef RR_EPUB_H
#define RR_EPUB_H

#include <stddef.h>
#include <stdbool.h>
#include <zip.h>

/**
 * @brief Represents a single file or resource listed in the EPUB manifest.
 */
typedef struct {
    char *id;          /**< Manifest identifier */
    char *href;        /**< Relative href as declared in OPF */
    char *media_type;  /**< MIME media type */
    char *full_path;   /**< Full normalized path inside the zip archive */
    char *properties;  /**< Optional item properties (e.g. "nav") */
} EpubItem;

/**
 * @brief Represents an item reference in the linear spine reading sequence.
 */
typedef struct {
    char *idref;       /**< ID referencing manifest item */
    bool linear;       /**< True if part of primary reading flow */
    EpubItem *item;    /**< Direct pointer to manifest item */
} EpubSpineItem;

/**
 * @brief Represents a Table of Contents entry.
 */
typedef struct {
    char *title;       /**< Display title of chapter/section */
    char *href;        /**< Relative href as declared in TOC */
    char *full_path;   /**< Full path inside zip archive */
    char *anchor;      /**< HTML anchor fragment (without #), or NULL */
    int level;         /**< Hierarchy nesting level (0 = top-level) */
    int spine_index;   /**< Index into spine items (-1 if unresolved) */
    int play_order;    /**< Play order from NCX, if available */
} EpubTocItem;

/**
 * @brief Represents a parsed EPUB book.
 */
typedef struct {
    char *filepath;          /**< Path to the epub file */
    char *file_id;           /**< Stable identifier for progress tracking */
    char *title;             /**< Book title */
    char *author;            /**< Book author/creator */
    char *language;          /**< Book language code */
    char *opf_path;          /**< Path to OPF file inside zip archive */
    char *opf_dir;           /**< Directory containing the OPF file */

    EpubItem *manifest;      /**< Manifest items array */
    size_t manifest_count;   /**< Manifest items count */

    EpubSpineItem *spine;    /**< Spine items array in linear reading order */
    size_t spine_count;      /**< Spine items count */

    EpubTocItem *toc;        /**< Table of Contents items array */
    size_t toc_count;        /**< Table of Contents items count */

    zip_t *za;               /**< Open zip archive handle */
} EpubBook;

/**
 * @brief Opens and parses an EPUB archive.
 * @param filepath Path to the .epub file on disk.
 * @param out_error If non-NULL and an error occurs, receives a malloc'd error description.
 * @return Pointer to allocated EpubBook on success, NULL on failure.
 */
EpubBook *epub_open(const char *filepath, char **out_error);

/**
 * @brief Closes an EPUB book and frees all associated memory.
 */
void epub_close(EpubBook *book);

/**
 * @brief Reads an entry from the EPUB archive into memory.
 * @param book Pointer to EpubBook.
 * @param entry_path Path inside the zip archive.
 * @param out_size Optional pointer to receive byte size of read data.
 * @return Null-terminated malloc'd buffer on success, NULL on error.
 */
char *epub_read_entry(EpubBook *book, const char *entry_path, size_t *out_size);

/**
 * @brief Reads a chapter/spine item by its spine index.
 * @param book Pointer to EpubBook.
 * @param spine_index 0-based index in the spine.
 * @param out_size Optional pointer to receive byte size.
 * @return Null-terminated malloc'd buffer containing XHTML content, or NULL on error.
 */
char *epub_read_spine_item(EpubBook *book, size_t spine_index, size_t *out_size);

/**
 * @brief Retrieves the best matching chapter title for a given spine index.
 */
const char *epub_get_chapter_title_for_spine(EpubBook *book, size_t spine_index);

/**
 * @brief Prints book metadata and table of contents to stdout (for --info).
 */
void epub_print_info(const EpubBook *book);

#endif /* RR_EPUB_H */
