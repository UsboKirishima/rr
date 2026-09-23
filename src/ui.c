/**
 * @file ui.c
 * @brief Terminal User Interface rendering, color schemes, and interactive dialogs.
 */

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "ui.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <curses.h>
#include <locale.h>

#define PAIR_NORMAL   1
#define PAIR_HEADER   2
#define PAIR_FOOTER   3
#define PAIR_HEADING  4
#define PAIR_BORDER   5
#define PAIR_SEARCH   6
#define PAIR_ACCENT   7
#define PAIR_SELECTED 8

static const char *THEME_NAMES[THEME_COUNT] = {
    "Minimal / Default",
    "Warm Sepia (E-Reader)",
    "Pure Black (OLED)",
    "Forest Green (Vintage)",
    "Nordic Slate"
};

bool ui_init(void) {
    setlocale(LC_ALL, "");
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    /* Enable mouse scrolling & clicks */
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);

    if (has_colors()) {
        start_color();
        use_default_colors();
    }

    ui_set_theme(0);
    return true;
}

void ui_cleanup(void) {
    endwin();
}

void ui_set_theme(int theme_index) {
    if (!has_colors()) return;

    theme_index = (theme_index % THEME_COUNT + THEME_COUNT) % THEME_COUNT;

    if (theme_index == 0) {
        /* Theme 0: Clean Minimalist */
        init_pair(PAIR_NORMAL, -1, -1);
        init_pair(PAIR_HEADER, COLOR_CYAN, -1);
        init_pair(PAIR_FOOTER, COLOR_WHITE, -1);
        init_pair(PAIR_HEADING, COLOR_YELLOW, -1);
        init_pair(PAIR_BORDER, COLOR_BLUE, -1);
        init_pair(PAIR_SEARCH, COLOR_BLACK, COLOR_YELLOW);
        init_pair(PAIR_ACCENT, COLOR_MAGENTA, -1);
        init_pair(PAIR_SELECTED, COLOR_BLACK, COLOR_CYAN);
        bkgd(COLOR_PAIR(PAIR_NORMAL));
    } else if (theme_index == 1) {
        /* Theme 1: Warm Sepia / E-Reader (Amber / Cream on Soft Dark) */
        if (COLORS >= 256) {
            init_pair(PAIR_NORMAL, 230, 234);
            init_pair(PAIR_HEADER, 222, 234);
            init_pair(PAIR_FOOTER, 246, 234);
            init_pair(PAIR_HEADING, 214, 234);
            init_pair(PAIR_BORDER, 240, 234);
            init_pair(PAIR_SEARCH, 0, 221);
            init_pair(PAIR_ACCENT, 215, 234);
            init_pair(PAIR_SELECTED, 234, 222);
            bkgd(COLOR_PAIR(PAIR_NORMAL));
        } else {
            init_pair(PAIR_NORMAL, COLOR_YELLOW, COLOR_BLACK);
            init_pair(PAIR_HEADER, COLOR_WHITE, COLOR_BLACK);
            init_pair(PAIR_FOOTER, COLOR_YELLOW, COLOR_BLACK);
            init_pair(PAIR_HEADING, COLOR_YELLOW, COLOR_BLACK);
            init_pair(PAIR_BORDER, COLOR_WHITE, COLOR_BLACK);
            init_pair(PAIR_SEARCH, COLOR_BLACK, COLOR_YELLOW);
            init_pair(PAIR_ACCENT, COLOR_YELLOW, COLOR_BLACK);
            init_pair(PAIR_SELECTED, COLOR_BLACK, COLOR_YELLOW);
            bkgd(COLOR_PAIR(PAIR_NORMAL));
        }
    } else if (theme_index == 2) {
        /* Theme 2: Pure Black OLED */
        init_pair(PAIR_NORMAL, COLOR_WHITE, COLOR_BLACK);
        init_pair(PAIR_HEADER, COLOR_WHITE, COLOR_BLACK);
        init_pair(PAIR_FOOTER, COLOR_WHITE, COLOR_BLACK);
        init_pair(PAIR_HEADING, COLOR_WHITE, COLOR_BLACK);
        init_pair(PAIR_BORDER, COLOR_WHITE, COLOR_BLACK);
        init_pair(PAIR_SEARCH, COLOR_BLACK, COLOR_WHITE);
        init_pair(PAIR_ACCENT, COLOR_WHITE, COLOR_BLACK);
        init_pair(PAIR_SELECTED, COLOR_BLACK, COLOR_WHITE);
        bkgd(COLOR_PAIR(PAIR_NORMAL));
    } else if (theme_index == 3) {
        /* Theme 3: Retro Forest Green */
        if (COLORS >= 256) {
            init_pair(PAIR_NORMAL, 151, 233);
            init_pair(PAIR_HEADER, 120, 233);
            init_pair(PAIR_FOOTER, 108, 233);
            init_pair(PAIR_HEADING, 157, 233);
            init_pair(PAIR_BORDER, 65, 233);
            init_pair(PAIR_SEARCH, 0, 150);
            init_pair(PAIR_ACCENT, 120, 233);
            init_pair(PAIR_SELECTED, 233, 120);
            bkgd(COLOR_PAIR(PAIR_NORMAL));
        } else {
            init_pair(PAIR_NORMAL, COLOR_GREEN, COLOR_BLACK);
            init_pair(PAIR_HEADER, COLOR_GREEN, COLOR_BLACK);
            init_pair(PAIR_FOOTER, COLOR_GREEN, COLOR_BLACK);
            init_pair(PAIR_HEADING, COLOR_GREEN, COLOR_BLACK);
            init_pair(PAIR_BORDER, COLOR_GREEN, COLOR_BLACK);
            init_pair(PAIR_SEARCH, COLOR_BLACK, COLOR_GREEN);
            init_pair(PAIR_ACCENT, COLOR_GREEN, COLOR_BLACK);
            init_pair(PAIR_SELECTED, COLOR_BLACK, COLOR_GREEN);
            bkgd(COLOR_PAIR(PAIR_NORMAL));
        }
    } else if (theme_index == 4) {
        /* Theme 4: Nordic Slate */
        if (COLORS >= 256) {
            init_pair(PAIR_NORMAL, 253, 235);
            init_pair(PAIR_HEADER, 117, 235);
            init_pair(PAIR_FOOTER, 245, 235);
            init_pair(PAIR_HEADING, 111, 235);
            init_pair(PAIR_BORDER, 67, 235);
            init_pair(PAIR_SEARCH, 0, 117);
            init_pair(PAIR_ACCENT, 117, 235);
            init_pair(PAIR_SELECTED, 235, 117);
            bkgd(COLOR_PAIR(PAIR_NORMAL));
        } else {
            init_pair(PAIR_NORMAL, COLOR_CYAN, COLOR_BLACK);
            init_pair(PAIR_HEADER, COLOR_WHITE, COLOR_BLACK);
            init_pair(PAIR_FOOTER, COLOR_CYAN, COLOR_BLACK);
            init_pair(PAIR_HEADING, COLOR_WHITE, COLOR_BLACK);
            init_pair(PAIR_BORDER, COLOR_CYAN, COLOR_BLACK);
            init_pair(PAIR_SEARCH, COLOR_BLACK, COLOR_CYAN);
            init_pair(PAIR_ACCENT, COLOR_CYAN, COLOR_BLACK);
            init_pair(PAIR_SELECTED, COLOR_BLACK, COLOR_CYAN);
            bkgd(COLOR_PAIR(PAIR_NORMAL));
        }
    }
}

const char *ui_get_theme_name(int theme_index) {
    theme_index = (theme_index % THEME_COUNT + THEME_COUNT) % THEME_COUNT;
    return THEME_NAMES[theme_index];
}

void ui_set_status(UIState *ui, const char *fmt, ...) {
    if (!ui) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(ui->status_msg, sizeof(ui->status_msg), fmt, args);
    va_end(args);
    ui->status_ticks = 3;
}

static void draw_box(int y, int x, int h, int w, const char *title) {
    attron(COLOR_PAIR(PAIR_BORDER));

    /* Top border */
    mvaddstr(y, x, "╭");
    for (int i = 1; i < w - 1; i++) addstr("─");
    addstr("╮");

    /* Side borders */
    for (int i = 1; i < h - 1; i++) {
        mvaddstr(y + i, x, "│");
        mvaddstr(y + i, x + w - 1, "│");
    }

    /* Bottom border */
    mvaddstr(y + h - 1, x, "╰");
    for (int i = 1; i < w - 1; i++) addstr("─");
    addstr("╯");

    /* Title */
    if (title && *title) {
        char buf[128];
        snprintf(buf, sizeof(buf), " %s ", title);
        int tw = utf8_strwidth(buf);
        int tx = x + (w - tw) / 2;
        if (tx > x) {
            attron(COLOR_PAIR(PAIR_HEADER) | A_BOLD);
            mvaddstr(y, tx, buf);
            attroff(A_BOLD);
        }
    }

    attroff(COLOR_PAIR(PAIR_BORDER));
}

void ui_render(UIState *ui) {
    if (!ui || !ui->layout) return;

    erase();
    getmaxyx(stdscr, ui->term_h, ui->term_w);

    const LayoutPage *page = layout_get_page(ui->layout, ui->current_page);
    if (!page) return;

    const ChapterLayout *cl = &ui->layout->chapters[page->chapter_index];
    int col_w = ui->layout->column_width;
    if (col_w > ui->term_w - 4) col_w = ui->term_w - 4;
    if (col_w < 30) col_w = ui->term_w;

    int margin_x = (ui->term_w - col_w) / 2;
    if (margin_x < 0) margin_x = 0;

    /* 1. Header (Line 0) */
    attron(COLOR_PAIR(PAIR_HEADER));
    int half_w = (col_w - 4) / 2;
    if (half_w < 10) half_w = 10;

    char book_title_buf[128];
    char sec_title_buf[128];
    utf8_truncate(ui->layout->book->title, half_w, book_title_buf, sizeof(book_title_buf));
    utf8_truncate(page->section_title ? page->section_title : "", half_w, sec_title_buf, sizeof(sec_title_buf));

    mvaddstr(0, margin_x, book_title_buf);

    int sec_w = utf8_strwidth(sec_title_buf);
    int sec_x = margin_x + col_w - sec_w;
    if (sec_x > margin_x + utf8_strwidth(book_title_buf) + 2) {
        mvaddstr(0, sec_x, sec_title_buf);
    }
    attroff(COLOR_PAIR(PAIR_HEADER));

    /* 2. Text Body (Lines 2 .. term_h - 3) */
    int cur_y = 2;
    for (size_t i = 0; i < page->line_count && cur_y < ui->term_h - 2; i++) {
        const LayoutLine *line = &cl->lines[page->start_line + i];

        if (line->is_blank) {
            cur_y++;
            continue;
        }

        if (line->is_hr) {
            attron(COLOR_PAIR(PAIR_ACCENT));
            const char *ornament = "─── ✦ ───";
            int ow = utf8_strwidth(ornament);
            int ox = margin_x + (col_w - ow) / 2;
            mvaddstr(cur_y, ox, ornament);
            attroff(COLOR_PAIR(PAIR_ACCENT));
            cur_y++;
            continue;
        }

        int start_x = margin_x + line->indent_spaces;
        if (line->is_centered) {
            int line_w = line->indent_spaces;
            for (int w = 0; w < line->word_count; w++) {
                line_w += line->words[w].visual_width + line->spaces_after[w];
            }
            start_x = margin_x + (col_w - line_w) / 2;
            if (start_x < margin_x) start_x = margin_x;
        }

        move(cur_y, start_x);

        for (int w = 0; w < line->word_count; w++) {
            Word *word = &line->words[w];

            /* Check search match highlight */
            bool is_search_match = false;
            if (ui->search_active && ui->search_query[0] != '\0') {
                if (str_case_contains(word->text, ui->search_query)) {
                    is_search_match = true;
                }
            }

            int attrs = 0;
            int pair = PAIR_NORMAL;

            if (line->is_heading) {
                attrs |= A_BOLD;
                pair = PAIR_HEADING;
            }
            if (word->style & STYLE_BOLD) attrs |= A_BOLD;
            if (word->style & STYLE_ITALIC) attrs |= A_DIM;
            if (word->style & STYLE_UNDERLINE) attrs |= A_UNDERLINE;
            if (word->style & STYLE_CODE) attrs |= A_REVERSE;

            if (is_search_match) {
                attron(COLOR_PAIR(PAIR_SEARCH) | A_BOLD);
                addstr(word->text);
                attroff(COLOR_PAIR(PAIR_SEARCH) | A_BOLD);
            } else {
                attron(COLOR_PAIR(pair) | attrs);
                addstr(word->text);
                attroff(COLOR_PAIR(pair) | attrs);
            }

            /* Print trailing spaces */
            for (int s = 0; s < line->spaces_after[w]; s++) {
                addch(' ');
            }
        }

        cur_y++;
    }

    /* 3. Footer (Line term_h - 1) */
    int footer_y = ui->term_h - 1;
    attron(COLOR_PAIR(PAIR_FOOTER));

    if (ui->status_ticks > 0 && ui->status_msg[0] != '\0') {
        /* Display toast status message centered */
        int sw = utf8_strwidth(ui->status_msg);
        int sx = (ui->term_w - sw) / 2;
        attron(COLOR_PAIR(PAIR_ACCENT) | A_BOLD);
        mvaddstr(footer_y, (sx > 0 ? sx : 0), ui->status_msg);
        attroff(COLOR_PAIR(PAIR_ACCENT) | A_BOLD);
    } else {
        /* Left: Arrow indicators + Page count */
        char left_footer[64];
        bool is_bm = state_has_bookmark(ui->state, ui->current_page);
        snprintf(left_footer, sizeof(left_footer), "%s%s Pag. %zu/%zu %s",
                 is_bm ? "★ " : "",
                 (ui->current_page > 1) ? "←" : " ",
                 ui->current_page,
                 ui->layout->total_pages,
                 (ui->current_page < ui->layout->total_pages) ? "→" : " ");

        mvaddstr(footer_y, margin_x, left_footer);

        /* Center: Reading percentage or search status */
        if (ui->search_active && ui->match_count > 0) {
            char search_info[64];
            snprintf(search_info, sizeof(search_info), "[Match %zu of %zu]",
                     ui->current_match_idx + 1, ui->match_count);
            int mw = utf8_strwidth(search_info);
            mvaddstr(footer_y, (ui->term_w - mw) / 2, search_info);
        } else {
            int pct = (int)((double)ui->current_page * 100.0 / (double)ui->layout->total_pages);
            char pct_str[32];
            snprintf(pct_str, sizeof(pct_str), "[ %d%% ]", pct);
            int pw = utf8_strwidth(pct_str);
            int px = (ui->term_w - pw) / 2;
            if (px > margin_x + utf8_strwidth(left_footer) + 2) {
                mvaddstr(footer_y, px, pct_str);
            }
        }

        /* Right: Live time (matching "2:34am" typography) */
        char time_buf[32];
        get_current_time_str(time_buf, sizeof(time_buf));
        int tw = utf8_strwidth(time_buf);
        int tx = margin_x + col_w - tw;
        if (tx > margin_x) {
            mvaddstr(footer_y, tx, time_buf);
        }
    }

    attroff(COLOR_PAIR(PAIR_FOOTER));
    refresh();
}

size_t ui_show_toc_modal(UIState *ui) {
    if (!ui || !ui->layout || !ui->layout->book) return 0;
    const EpubBook *book = ui->layout->book;
    if (book->toc_count == 0) return 0;

    int box_w = ui->term_w - 6;
    if (box_w > 76) box_w = 76;
    if (box_w < 36) box_w = ui->term_w - 2;

    int box_h = ui->term_h - 4;
    if (box_h > 24) box_h = 24;
    if (box_h < 10) box_h = ui->term_h - 2;

    int box_y = (ui->term_h - box_h) / 2;
    int box_x = (ui->term_w - box_w) / 2;

    /* Find currently active TOC entry */
    size_t selected = 0;
    for (size_t i = 0; i < book->toc_count; i++) {
        size_t p = layout_find_toc_page(ui->layout, &book->toc[i]);
        if (p <= ui->current_page) {
            selected = i;
        } else {
            break;
        }
    }

    size_t scroll_offset = 0;
    int visible_items = box_h - 4;
    if (visible_items < 1) visible_items = 1;

    while (true) {
        if (selected < scroll_offset) {
            scroll_offset = selected;
        } else if (selected >= scroll_offset + (size_t)visible_items) {
            scroll_offset = selected - (size_t)visible_items + 1;
        }

        /* Redraw base reader screen behind modal */
        ui_render(ui);

        /* Draw modal box */
        draw_box(box_y, box_x, box_h, box_w, "Table of Contents");

        /* Render items */
        for (int i = 0; i < visible_items; i++) {
            size_t idx = scroll_offset + (size_t)i;
            int row_y = box_y + 2 + i;
            if (idx >= book->toc_count) break;

            const EpubTocItem *item = &book->toc[idx];
            size_t item_page = layout_find_toc_page(ui->layout, item);

            char page_str[32];
            snprintf(page_str, sizeof(page_str), "Pag. %3zu", item_page);
            int pw = utf8_strwidth(page_str);

            int avail_w = box_w - 6 - pw - (item->level * 2);
            if (avail_w < 10) avail_w = 10;

            char title_trunc[128];
            utf8_truncate(item->title, avail_w, title_trunc, sizeof(title_trunc));

            bool is_sel = (idx == selected);
            if (is_sel) {
                attron(COLOR_PAIR(PAIR_SELECTED) | A_BOLD);
            } else {
                attron(COLOR_PAIR(PAIR_NORMAL));
            }

            /* Clear row inside box */
            mvprintw(row_y, box_x + 1, "%*s", box_w - 2, "");

            int text_x = box_x + 3 + (item->level * 2);
            mvprintw(row_y, text_x, "%s %s", is_sel ? "▶" : " ", title_trunc);

            /* Fill with subtle dots towards page number */
            int dots_start = text_x + 2 + utf8_strwidth(title_trunc) + 1;
            int dots_end = box_x + box_w - 3 - pw;
            for (int dx = dots_start; dx < dots_end; dx++) {
                mvaddch(row_y, dx, '.');
            }

            mvaddstr(row_y, box_x + box_w - 2 - pw, page_str);

            if (is_sel) {
                attroff(COLOR_PAIR(PAIR_SELECTED) | A_BOLD);
            } else {
                attroff(COLOR_PAIR(PAIR_NORMAL));
            }
        }

        /* Footer hint */
        attron(COLOR_PAIR(PAIR_FOOTER));
        const char *hint = "[↑/↓] Navigate  •  [Enter] Select  •  [Esc/q] Close";
        int hw = utf8_strwidth(hint);
        if (hw < box_w - 4) {
            mvaddstr(box_y + box_h - 1, box_x + (box_w - hw) / 2, hint);
        }
        attroff(COLOR_PAIR(PAIR_FOOTER));

        refresh();

        int ch = getch();
        if (ch == 27 || ch == 'q' || ch == 'Q' || ch == 't' || ch == '\t') {
            return 0; /* Cancelled */
        } else if (ch == KEY_UP || ch == 'k') {
            if (selected > 0) selected--;
        } else if (ch == KEY_DOWN || ch == 'j') {
            if (selected + 1 < book->toc_count) selected++;
        } else if (ch == KEY_PPAGE) {
            if (selected > (size_t)visible_items) selected -= (size_t)visible_items;
            else selected = 0;
        } else if (ch == KEY_NPAGE) {
            selected += (size_t)visible_items;
            if (selected >= book->toc_count) selected = book->toc_count - 1;
        } else if (ch == KEY_HOME) {
            selected = 0;
        } else if (ch == KEY_END) {
            selected = book->toc_count - 1;
        } else if (ch == '\n' || ch == KEY_ENTER || ch == ' ') {
            return layout_find_toc_page(ui->layout, &book->toc[selected]);
        }
    }
}

size_t ui_show_bookmarks_modal(UIState *ui) {
    if (!ui || !ui->state) return 0;
    BookState *st = ui->state;

    int box_w = 64;
    if (box_w > ui->term_w - 4) box_w = ui->term_w - 4;
    int box_h = 16;
    if (box_h > ui->term_h - 4) box_h = ui->term_h - 4;

    int box_y = (ui->term_h - box_h) / 2;
    int box_x = (ui->term_w - box_w) / 2;

    size_t selected = 0;

    while (true) {
        ui_render(ui);
        draw_box(box_y, box_x, box_h, box_w, "Bookmarks");

        if (st->bookmark_count == 0) {
            attron(COLOR_PAIR(PAIR_FOOTER));
            const char *empty_msg = "No bookmarks yet. Press 'b' while reading to add one!";
            mvaddstr(box_y + box_h / 2, box_x + (box_w - utf8_strwidth(empty_msg)) / 2, empty_msg);
            attroff(COLOR_PAIR(PAIR_FOOTER));
        } else {
            int visible_items = box_h - 4;
            for (int i = 0; i < visible_items && (size_t)i < st->bookmark_count; i++) {
                int row_y = box_y + 2 + i;
                size_t p = st->bookmarks[i];
                const LayoutPage *lp = layout_get_page(ui->layout, p);

                bool is_sel = ((size_t)i == selected);
                if (is_sel) attron(COLOR_PAIR(PAIR_SELECTED) | A_BOLD);

                mvprintw(row_y, box_x + 1, "%*s", box_w - 2, "");
                mvprintw(row_y, box_x + 3, "%s ★ Pag. %3zu  -  %s",
                         is_sel ? "▶" : " ",
                         p,
                         (lp && lp->section_title) ? lp->section_title : "Chapter");

                if (is_sel) attroff(COLOR_PAIR(PAIR_SELECTED) | A_BOLD);
            }
        }

        attron(COLOR_PAIR(PAIR_FOOTER));
        const char *hint = "[Enter] Jump  •  [d] Delete  •  [Esc] Close";
        int hw = utf8_strwidth(hint);
        mvaddstr(box_y + box_h - 1, box_x + (box_w - hw) / 2, hint);
        attroff(COLOR_PAIR(PAIR_FOOTER));

        refresh();

        int ch = getch();
        if (ch == 27 || ch == 'q' || ch == 'Q' || ch == 'B') {
            return 0;
        } else if (ch == KEY_UP || ch == 'k') {
            if (selected > 0) selected--;
        } else if (ch == KEY_DOWN || ch == 'j') {
            if (st->bookmark_count > 0 && selected + 1 < st->bookmark_count) selected++;
        } else if (ch == 'd' || ch == 'D' || ch == KEY_DC) {
            if (st->bookmark_count > 0) {
                state_toggle_bookmark(st, st->bookmarks[selected]);
                if (selected >= st->bookmark_count && selected > 0) {
                    selected--;
                }
            }
        } else if ((ch == '\n' || ch == KEY_ENTER || ch == ' ') && st->bookmark_count > 0) {
            return st->bookmarks[selected];
        }
    }
}

void ui_show_help_modal(UIState *ui) {
    if (!ui) return;

    int box_w = 68;
    if (box_w > ui->term_w - 2) box_w = ui->term_w - 2;
    int box_h = 22;
    if (box_h > ui->term_h - 2) box_h = ui->term_h - 2;

    int box_y = (ui->term_h - box_h) / 2;
    int box_x = (ui->term_w - box_w) / 2;

    ui_render(ui);
    draw_box(box_y, box_x, box_h, box_w, "Keyboard Controls");

    const char *help_lines[] = {
        "NAVIGATION",
        "  → / ↓ / Space / Enter / l   Next page",
        "  ← / ↑ / Backspace / h / k   Previous page",
        "  [ / ]                       Previous / Next chapter",
        "  Home / End                  First / Last page of book",
        "  g                           Jump to page number",
        "",
        "TABLE OF CONTENTS & SEARCH",
        "  t / Tab                     Open Table of Contents",
        "  /                           Search text across book",
        "  n / N                       Next / Previous search match",
        "  b / B                       Toggle bookmark / Manage bookmarks",
        "",
        "DISPLAY & TYPOGRAPHY",
        "  w                           Cycle column width (margins)",
        "  j                           Toggle full justification",
        "  p                           Toggle paragraph indent / spacing",
        "  c                           Cycle color theme",
        "",
        "  ? / F1                      Show this help dialog",
        "  q / Esc                     Quit reader",
    };

    size_t count = sizeof(help_lines) / sizeof(help_lines[0]);
    for (size_t i = 0; i < count && (int)i + 2 < box_h - 1; i++) {
        int y = box_y + 2 + (int)i;
        bool is_cat = (help_lines[i][0] != ' ' && help_lines[i][0] != '\0');
        if (is_cat) {
            attron(COLOR_PAIR(PAIR_HEADER) | A_BOLD);
        } else {
            attron(COLOR_PAIR(PAIR_NORMAL));
        }
        mvaddstr(y, box_x + 4, help_lines[i]);
        if (is_cat) attroff(COLOR_PAIR(PAIR_HEADER) | A_BOLD);
    }

    attron(COLOR_PAIR(PAIR_FOOTER));
    const char *close_hint = "Press any key to return to reading";
    mvaddstr(box_y + box_h - 1, box_x + (box_w - utf8_strwidth(close_hint)) / 2, close_hint);
    attroff(COLOR_PAIR(PAIR_FOOTER));

    refresh();
    getch();
}

size_t ui_prompt_goto_page(UIState *ui) {
    if (!ui || !ui->layout) return 0;

    int footer_y = ui->term_h - 1;
    move(footer_y, 0);
    clrtoeol();

    char prompt[64];
    snprintf(prompt, sizeof(prompt), "Go to page (1 - %zu): ", ui->layout->total_pages);
    attron(COLOR_PAIR(PAIR_HEADER) | A_BOLD);
    mvaddstr(footer_y, 2, prompt);
    attroff(COLOR_PAIR(PAIR_HEADER) | A_BOLD);

    echo();
    curs_set(1);

    char input[32];
    getnstr(input, sizeof(input) - 1);

    noecho();
    curs_set(0);

    str_trim(input);
    if (!input[0]) return 0;

    long p = atol(input);
    if (p < 1) p = 1;
    if ((size_t)p > ui->layout->total_pages) p = (long)ui->layout->total_pages;

    return (size_t)p;
}

void ui_prompt_search(UIState *ui) {
    if (!ui || !ui->layout) return;

    int footer_y = ui->term_h - 1;
    move(footer_y, 0);
    clrtoeol();

    attron(COLOR_PAIR(PAIR_HEADER) | A_BOLD);
    mvaddstr(footer_y, 2, "/ ");
    attroff(COLOR_PAIR(PAIR_HEADER) | A_BOLD);

    echo();
    curs_set(1);

    char query[128];
    getnstr(query, sizeof(query) - 1);

    noecho();
    curs_set(0);

    str_trim(query);
    if (!query[0]) return;

    snprintf(ui->search_query, sizeof(ui->search_query), "%s", query);

    /* Free previous matches */
    free(ui->matches);
    ui->matches = NULL;
    ui->match_count = 0;
    ui->current_match_idx = 0;

    /* Scan entire book layout for matches */
    size_t cap = 0;
    for (size_t g_page = 1; g_page <= ui->layout->total_pages; g_page++) {
        const LayoutPage *lp = layout_get_page(ui->layout, g_page);
        if (!lp) continue;
        const ChapterLayout *cl = &ui->layout->chapters[lp->chapter_index];

        for (size_t li = 0; li < lp->line_count; li++) {
            size_t l_idx = lp->start_line + li;
            const LayoutLine *line = &cl->lines[l_idx];

            for (int wi = 0; wi < line->word_count; wi++) {
                if (str_case_contains(line->words[wi].text, ui->search_query)) {
                    if (ui->match_count >= cap) {
                        cap = cap ? cap * 2 : 64;
                        ui->matches = (SearchMatch *)xrealloc(ui->matches, cap * sizeof(SearchMatch));
                    }
                    SearchMatch *m = &ui->matches[ui->match_count++];
                    m->global_page = g_page;
                    m->chapter_index = lp->chapter_index;
                    m->line_index = l_idx;
                    m->word_index = wi;
                }
            }
        }
    }

    if (ui->match_count > 0) {
        ui->search_active = true;
        /* Find first match on or after current page */
        size_t best = 0;
        for (size_t i = 0; i < ui->match_count; i++) {
            if (ui->matches[i].global_page >= ui->current_page) {
                best = i;
                break;
            }
        }
        ui->current_match_idx = best;
        ui->current_page = ui->matches[best].global_page;
        ui_set_status(ui, "Match 1 of %zu (n: next, N: prev, Esc: clear)", ui->match_count);
    } else {
        ui->search_active = false;
        ui_set_status(ui, "Pattern not found: \"%s\"", ui->search_query);
    }
}

void ui_search_next(UIState *ui) {
    if (!ui || !ui->search_active || ui->match_count == 0) return;
    ui->current_match_idx = (ui->current_match_idx + 1) % ui->match_count;
    ui->current_page = ui->matches[ui->current_match_idx].global_page;
}

void ui_search_prev(UIState *ui) {
    if (!ui || !ui->search_active || ui->match_count == 0) return;
    if (ui->current_match_idx == 0) {
        ui->current_match_idx = ui->match_count - 1;
    } else {
        ui->current_match_idx--;
    }
    ui->current_page = ui->matches[ui->current_match_idx].global_page;
}

void ui_search_clear(UIState *ui) {
    if (!ui) return;
    ui->search_active = false;
    free(ui->matches);
    ui->matches = NULL;
    ui->match_count = 0;
    ui->search_query[0] = '\0';
    ui_set_status(ui, "Search cleared");
}
