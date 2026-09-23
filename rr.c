/**
 * @file rr.c
 * @brief Lightweight aesthetic terminal EPUB reader entry point.
 */

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "src/epub.h"
#include "src/html.h"
#include "src/layout.h"
#include "src/state.h"
#include "src/ui.h"
#include "src/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curses.h>

#define RR_VERSION "1.0.0"

static void print_usage(const char *prog_name) {
    printf("rr - Lightweight aesthetic terminal EPUB reader (v%s)\n\n", RR_VERSION);
    printf("Usage:\n");
    printf("  %s <book.epub>              Open and read an EPUB book\n", prog_name);
    printf("  %s <book.epub> -p <page>    Open directly at a specific page\n", prog_name);
    printf("  %s <book.epub> -c <chap>    Open directly at a specific chapter\n", prog_name);
    printf("  %s --info <book.epub>       Print book metadata and Table of Contents\n", prog_name);
    printf("  %s -h, --help               Show this help message and exit\n", prog_name);
    printf("  %s -v, --version            Show version information\n\n", prog_name);
    printf("Controls:\n");
    printf("  → / Space / Enter / l       Next page\n");
    printf("  ← / Backspace / h / k       Previous page\n");
    printf("  [ / ]                       Previous / Next chapter\n");
    printf("  t / Tab                     Table of Contents\n");
    printf("  /                           Search text across book\n");
    printf("  n / N                       Next / Previous search match\n");
    printf("  b / B                       Bookmark current page / View bookmarks\n");
    printf("  g                           Jump to page number\n");
    printf("  w                           Cycle reading column width\n");
    printf("  F / J                       Toggle full justification\n");
    printf("  p                           Toggle paragraph indent / spacing\n");
    printf("  c                           Cycle color theme\n");
    printf("  ?                           Help overlay\n");
    printf("  q / Esc                     Quit reader\n");
}

static void rebuild_ui_layout(UIState *ui, size_t preferred_page) {
    int col_w = ui->state->column_width;
    if (col_w <= 0) {
        col_w = ui->term_w - 6;
        if (col_w > 66) col_w = 66;
        if (col_w < 30) col_w = ui->term_w - 2;
    }
    if (col_w > ui->term_w - 2) col_w = ui->term_w - 2;

    int page_h = ui->term_h - 4;
    if (page_h < 4) page_h = 4;

    double ratio = 0.0;
    if (ui->layout && ui->layout->total_pages > 0) {
        ratio = (double)preferred_page / (double)ui->layout->total_pages;
    }

    EpubBook *book = ui->layout ? ui->layout->book : NULL;
    if (ui->layout) {
        layout_free(ui->layout);
    }

    ui->layout = layout_build(book, col_w, page_h, ui->state->full_justify, ui->state->paragraph_style);

    if (preferred_page > 0 && preferred_page <= ui->layout->total_pages) {
        ui->current_page = preferred_page;
    } else if (ratio > 0.0) {
        size_t p = (size_t)(ratio * (double)ui->layout->total_pages + 0.5);
        if (p < 1) p = 1;
        if (p > ui->layout->total_pages) p = ui->layout->total_pages;
        ui->current_page = p;
    } else {
        ui->current_page = 1;
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "ERROR: Please include the EPUB file\n");
        fprintf(stderr, "Run '%s --help' for usage.\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *epub_path = NULL;
    size_t start_page = 0;
    int start_chapter = -1;
    bool info_only = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("rr %s\n", RR_VERSION);
            return EXIT_SUCCESS;
        } else if (strcmp(argv[i], "--info") == 0) {
            info_only = true;
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--page") == 0) {
            if (i + 1 < argc) {
                start_page = (size_t)atol(argv[++i]);
            }
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--chapter") == 0) {
            if (i + 1 < argc) {
                start_chapter = atoi(argv[++i]);
            }
        } else if (argv[i][0] != '-') {
            epub_path = argv[i];
        }
    }

    if (!epub_path) {
        fprintf(stderr, "ERROR: Please include the EPUB file\n");
        return EXIT_FAILURE;
    }

    char *err_msg = NULL;
    EpubBook *book = epub_open(epub_path, &err_msg);
    if (!book) {
        fprintf(stderr, "ERROR: %s\n", err_msg ? err_msg : "Invalid or unsupported EPUB file.");
        free(err_msg);
        return EXIT_FAILURE;
    }

    if (info_only) {
        epub_print_info(book);
        epub_close(book);
        return EXIT_SUCCESS;
    }

    /* Load saved session state */
    BookState *state = state_load(book->file_id);

    /* Initialize Curses UI */
    if (!ui_init()) {
        fprintf(stderr, "ERROR: Failed to initialize terminal interface.\n");
        state_free(state);
        epub_close(book);
        return EXIT_FAILURE;
    }

    UIState ui;
    memset(&ui, 0, sizeof(UIState));
    ui.state = state;
    getmaxyx(stdscr, ui.term_h, ui.term_w);

    /* Build initial layout */
    ui.layout = (BookLayout *)xcalloc(1, sizeof(BookLayout));
    ui.layout->book = book;

    size_t initial_page = state->last_global_page;
    if (start_page > 0) {
        initial_page = start_page;
    }

    rebuild_ui_layout(&ui, initial_page);

    if (start_chapter > 0 && (size_t)start_chapter <= book->spine_count) {
        ui.current_page = layout_get_chapter_first_page(ui.layout, (size_t)(start_chapter - 1));
    }

    ui_set_theme(state->theme);

    /* Interactive Event Loop */
    bool running = true;
    while (running) {
        ui_render(&ui);

        int ch = getch();

        if (ui.status_ticks > 0) {
            ui.status_ticks--;
            if (ui.status_ticks == 0) {
                ui.status_msg[0] = '\0';
            }
        }

        switch (ch) {
            case 'q':
            case 'Q':
                running = false;
                break;

            /* Next page */
            case KEY_RIGHT:
            case KEY_DOWN:
            case ' ':
            case '\n':
            case KEY_ENTER:
            case 'l':
            case 'j':
            case KEY_NPAGE:
                if (ui.current_page < ui.layout->total_pages) {
                    ui.current_page++;
                } else {
                    ui_set_status(&ui, "End of book");
                }
                break;

            /* Previous page */
            case KEY_LEFT:
            case KEY_UP:
            case KEY_BACKSPACE:
            case 127:
            case '8':
            case 'h':
            case 'k':
            case KEY_PPAGE:
                if (ui.current_page > 1) {
                    ui.current_page--;
                } else {
                    ui_set_status(&ui, "Beginning of book");
                }
                break;

            /* Next chapter */
            case ']': {
                const LayoutPage *cur_lp = layout_get_page(ui.layout, ui.current_page);
                if (cur_lp && cur_lp->chapter_index + 1 < ui.layout->chapter_count) {
                    ui.current_page = layout_get_chapter_first_page(ui.layout, cur_lp->chapter_index + 1);
                    ui_set_status(&ui, "Next chapter");
                }
                break;
            }

            /* Previous chapter */
            case '[': {
                const LayoutPage *cur_lp = layout_get_page(ui.layout, ui.current_page);
                if (cur_lp) {
                    size_t first_p = layout_get_chapter_first_page(ui.layout, cur_lp->chapter_index);
                    if (ui.current_page > first_p) {
                        ui.current_page = first_p;
                    } else if (cur_lp->chapter_index > 0) {
                        ui.current_page = layout_get_chapter_first_page(ui.layout, cur_lp->chapter_index - 1);
                        ui_set_status(&ui, "Previous chapter");
                    }
                }
                break;
            }

            /* Home / End */
            case KEY_HOME:
                ui.current_page = 1;
                break;

            case KEY_END:
            case 'G':
                ui.current_page = ui.layout->total_pages;
                break;

            /* Table of Contents */
            case 't':
            case '\t': {
                size_t target = ui_show_toc_modal(&ui);
                if (target > 0) {
                    ui.current_page = target;
                }
                break;
            }

            /* Search */
            case '/':
                ui_prompt_search(&ui);
                break;

            case 'n':
                if (ui.search_active) {
                    ui_search_next(&ui);
                } else {
                    ui_set_status(&ui, "No active search. Press '/' to search.");
                }
                break;

            case 'N':
                if (ui.search_active) {
                    ui_search_prev(&ui);
                } else {
                    ui_set_status(&ui, "No active search. Press '/' to search.");
                }
                break;

            case 27: /* Escape */
                if (ui.search_active) {
                    ui_search_clear(&ui);
                }
                break;

            /* Bookmarks */
            case 'b': {
                bool added = state_toggle_bookmark(ui.state, ui.current_page);
                ui_set_status(&ui, added ? "✓ Page %zu bookmarked" : "✗ Removed bookmark on page %zu", ui.current_page);
                break;
            }

            case 'B': {
                size_t bm_target = ui_show_bookmarks_modal(&ui);
                if (bm_target > 0) {
                    ui.current_page = bm_target;
                }
                break;
            }

            /* Jump to page */
            case 'g': {
                size_t goto_p = ui_prompt_goto_page(&ui);
                if (goto_p > 0) {
                    ui.current_page = goto_p;
                }
                break;
            }

            /* Cycle column width / margins */
            case 'w':
            case 'W': {
                int widths[] = {0, 50, 60, 66, 76, 86};
                int num_w = sizeof(widths) / sizeof(widths[0]);
                int cur_idx = 0;
                for (int i = 0; i < num_w; i++) {
                    if (widths[i] == ui.state->column_width) {
                        cur_idx = i;
                        break;
                    }
                }
                cur_idx = (cur_idx + 1) % num_w;
                ui.state->column_width = widths[cur_idx];
                rebuild_ui_layout(&ui, ui.current_page);
                if (ui.state->column_width == 0) {
                    ui_set_status(&ui, "Column width: Auto (%d cols)", ui.layout->column_width);
                } else {
                    ui_set_status(&ui, "Column width: %d cols", ui.layout->column_width);
                }
                break;
            }

            /* Toggle full justification */
            case 'F':
            case 'J': {
                ui.state->full_justify = !ui.state->full_justify;
                rebuild_ui_layout(&ui, ui.current_page);
                ui_set_status(&ui, "Justification: %s", ui.state->full_justify ? "Full (Book)" : "Left-aligned");
                break;
            }

            /* Toggle paragraph styling (indent vs blank line) */
            case 'p':
            case 'P': {
                ui.state->paragraph_style = (ui.state->paragraph_style == 0) ? 1 : 0;
                rebuild_ui_layout(&ui, ui.current_page);
                ui_set_status(&ui, "Paragraph style: %s",
                              (ui.state->paragraph_style == 0) ? "Classic indent" : "Spaced lines");
                break;
            }

            /* Cycle color theme */
            case 'c':
            case 'C': {
                ui.state->theme = (ui.state->theme + 1) % THEME_COUNT;
                ui_set_theme(ui.state->theme);
                ui_set_status(&ui, "Theme: %s", ui_get_theme_name(ui.state->theme));
                break;
            }

            /* Help overlay */
            case '?':
            case KEY_F(1):
                ui_show_help_modal(&ui);
                break;

            /* Mouse events */
            case KEY_MOUSE: {
                MEVENT event;
                if (getmouse(&event) == OK) {
                    if (event.bstate & BUTTON4_PRESSED) {
                        /* Scroll up -> Previous page */
                        if (ui.current_page > 1) ui.current_page--;
                    } else if (event.bstate & BUTTON5_PRESSED) {
                        /* Scroll down -> Next page */
                        if (ui.current_page < ui.layout->total_pages) ui.current_page++;
                    } else if (event.bstate & BUTTON1_CLICKED) {
                        /* Click left half of terminal -> Prev, right half -> Next */
                        if (event.x < ui.term_w / 2) {
                            if (ui.current_page > 1) ui.current_page--;
                        } else {
                            if (ui.current_page < ui.layout->total_pages) ui.current_page++;
                        }
                    }
                }
                break;
            }

            /* Window resized */
            case KEY_RESIZE:
                getmaxyx(stdscr, ui.term_h, ui.term_w);
                rebuild_ui_layout(&ui, ui.current_page);
                break;

            default:
                break;
        }
    }

    /* Save reading position and preferences */
    ui.state->last_global_page = ui.current_page;
    state_save(ui.state);

    /* Teardown and clean up memory */
    ui_cleanup();
    ui_search_clear(&ui);
    state_free(ui.state);
    layout_free(ui.layout);
    epub_close(book);

    return EXIT_SUCCESS;
}
