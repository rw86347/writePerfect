/* WritePerfect — WordPerfect 5.1 compatible word processor
 * Copyright (C) 2026 Rodger Wilson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "view.h"
#include "iofmt.h"

#include <ncurses.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void view_init(View *v)
{
    memset(v, 0, sizeof(*v));
    v->wrap = 80;
    v->wrap_word = 1;
    v->lines_per_page = WP51_LINES_PER_PAGE;
}

void view_free(View *v)
{
    free(v->line_start);
    memset(v, 0, sizeof(*v));
}

static int add_line(View *v, size_t pos)
{
    if ((size_t)v->nlines >= v->line_cap) {
        size_t cap = v->line_cap ? v->line_cap * 2 : 64;
        size_t *p = realloc(v->line_start, cap * sizeof(size_t));
        if (!p) {
            return -1;
        }
        v->line_start = p;
        v->line_cap = cap;
    }
    v->line_start[v->nlines++] = pos;
    return 0;
}

void view_apply_prefs(View *v)
{
    v->wrap = 80;
    v->wrap_word = io_wrap_is_word();
}

void view_relayout(View *v, const Doc *d)
{
    v->nlines = 0;
    add_line(v, 0);

    int col = 0;
    int line = 0;
    int para_indent = 0;
    int center = 0;
    size_t i = 0;
    v->cursor_line = 0;
    v->cursor_col = 0;

    while (i < d->len) {
        if (i == d->cursor) {
            v->cursor_line = line;
            v->cursor_col = col;
        }
        size_t n = doc_unit_len(d, i);
        if (n == 0) {
            break;
        }
        uint8_t b = d->data[i];
        if (b == WP_INDENT) {
            para_indent += WP_INDENT_COLS;
            if (col < para_indent) {
                col = para_indent;
            }
        } else if (b == WP_CENTER) {
            center = 1;
            {
                size_t j = i + n;
                int rem = 0;
                while (j < d->len) {
                    uint8_t c = d->data[j];
                    if (c == WP_HARD_EOL || c == WP_SOFT_EOL || c == WP_HARD_PAGE) {
                        break;
                    }
                    if (doc_is_visible(d, j)) {
                        rem += doc_display_width(d, j);
                    }
                    j += doc_unit_len(d, j);
                }
                int start = (v->wrap > rem) ? (v->wrap - rem) / 2 : 0;
                if (start < para_indent) {
                    start = para_indent;
                }
                col = start;
            }
        }
        int vis = doc_is_visible(d, i);
        if (vis && (b == WP_HARD_EOL || b == WP_SOFT_EOL || b == WP_HARD_PAGE)) {
            col = 0;
            para_indent = 0;
            center = 0;
            line++;
            add_line(v, i + n);
            (void)center;
        } else if (vis) {
            int w = doc_display_width(d, i);
            if (w < 1) {
                w = 1;
            }
            if (col + w > v->wrap && col > para_indent) {
                size_t next = i;
                if (v->wrap_word && v->nlines > 0) {
                    size_t line0 = v->line_start[v->nlines - 1];
                    size_t brk = 0;
                    int found = 0;
                    size_t j = line0;
                    while (j < i) {
                        if (doc_is_visible(d, j) && doc_is_space(d, j)) {
                            brk = j;
                            found = 1;
                        }
                        j += doc_unit_len(d, j);
                    }
                    if (found && brk > line0) {
                        next = brk + doc_unit_len(d, brk);
                    }
                }
                add_line(v, next);
                line++;
                col = para_indent;
                if (next < i) {
                    size_t k = next;
                    while (k < i) {
                        if (doc_is_visible(d, k)) {
                            uint8_t c = d->data[k];
                            if (c != WP_HARD_EOL && c != WP_SOFT_EOL && c != WP_HARD_PAGE) {
                                col += doc_display_width(d, k);
                            }
                        }
                        k += doc_unit_len(d, k);
                    }
                }
                if (d->cursor >= next && d->cursor <= i) {
                    int cc = para_indent;
                    size_t k = next;
                    while (k < d->cursor) {
                        if (doc_is_visible(d, k)) {
                            uint8_t c = d->data[k];
                            if (c != WP_HARD_EOL && c != WP_SOFT_EOL && c != WP_HARD_PAGE) {
                                cc += doc_display_width(d, k);
                            }
                        }
                        k += doc_unit_len(d, k);
                    }
                    v->cursor_line = line;
                    v->cursor_col = cc;
                }
            }
            col += w;
        }
        i += n;
    }
    if (d->cursor >= d->len) {
        v->cursor_line = line;
        v->cursor_col = col;
    }
    if (v->nlines == 0) {
        add_line(v, 0);
    }
    v->cursor_page = v->cursor_line / v->lines_per_page + 1;
}

void view_ensure_cursor_visible(View *v)
{
    int strip = v->hide_strip ? 0 : WP51_STRIP_ROWS;
    int text_rows = v->reveal ? 10 : (WP51_ROWS - 1 - strip);
    if (text_rows < 3) {
        text_rows = 3;
    }
    if (v->cursor_line < v->top_line) {
        v->top_line = v->cursor_line;
    }
    if (v->cursor_line >= v->top_line + text_rows) {
        v->top_line = v->cursor_line - text_rows + 1;
    }
    if (v->top_line < 0) {
        v->top_line = 0;
    }
}

void view_status(View *v, const Doc *d)
{
    int tenths = WP51_LEFT_MARGIN_TENTHS + v->cursor_col;
    int inches = tenths / 10;
    int frac = tenths % 10;
    char posbuf[16];
    if (frac) {
        snprintf(posbuf, sizeof(posbuf), "%d.%d\"", inches, frac);
    } else {
        snprintf(posbuf, sizeof(posbuf), "%d\"", inches);
    }
    const char *name = d->path[0] ? d->path : "";
    snprintf(v->status, sizeof(v->status),
             "%s%sDoc 1 Pg %d Ln %d  Pos %s",
             name,
             name[0] ? "  " : "",
             v->cursor_page,
             (v->cursor_line % v->lines_per_page) + 1,
             posbuf);
}

void view_message(View *v, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(v->message, sizeof(v->message), fmt, ap);
    va_end(ap);
    v->msg_ttl = 2;
}

static void draw_hline(Screen *s, int y, const char *label)
{
    screen_fill_line(s, y, SCR_REVERSE);
    if (label) {
        screen_putn(s, y, 1, label, s->cols - 2, SCR_REVERSE);
    }
}

static void para_state_at(const Doc *d, size_t at, int *indent, int *center, uint8_t *just)
{
    int ind = 0;
    int cen = 0;
    size_t i = 0;
    if (just) {
        *just = doc_just_at(d, at);
    }
    while (i < at && i < d->len) {
        uint8_t b = d->data[i];
        size_t n = doc_unit_len(d, i);
        if (b == WP_HARD_EOL || b == WP_SOFT_EOL || b == WP_HARD_PAGE) {
            ind = 0;
            cen = 0;
        } else if (b == WP_INDENT) {
            ind += WP_INDENT_COLS;
        } else if (b == WP_CENTER) {
            cen = 1;
        }
        i += n ? n : 1;
    }
    *indent = ind;
    *center = cen;
}

static int line_ends_para(const Doc *d, size_t lo, size_t hi)
{
    size_t i = lo;
    while (i < hi && i < d->len) {
        uint8_t b = d->data[i];
        if (b == WP_HARD_EOL || b == WP_SOFT_EOL || b == WP_HARD_PAGE) {
            return 1;
        }
        i += doc_unit_len(d, i);
    }
    return hi >= d->len;
}

static size_t line_last_nonspace(const Doc *d, size_t lo, size_t hi)
{
    size_t i = lo;
    size_t last = lo;
    int seen = 0;
    while (i < hi && i < d->len) {
        uint8_t b = d->data[i];
        size_t n = doc_unit_len(d, i);
        if (b == WP_HARD_EOL || b == WP_SOFT_EOL || b == WP_HARD_PAGE) {
            break;
        }
        if (doc_is_visible(d, i) && !doc_is_space(d, i)) {
            last = i;
            seen = 1;
        }
        i += n ? n : 1;
    }
    return seen ? last : lo;
}

static void line_just_metrics(const Doc *d, size_t lo, size_t hi, int *textw, int *nspaces)
{
    size_t i = lo;
    size_t last_ns = line_last_nonspace(d, lo, hi);
    int w = 0;
    int sp = 0;
    *textw = 0;
    *nspaces = 0;
    if (last_ns == lo && (lo >= d->len || !doc_is_visible(d, lo) || doc_is_space(d, lo))) {
        return;
    }
    while (i <= last_ns && i < hi && i < d->len) {
        uint8_t b = d->data[i];
        size_t n = doc_unit_len(d, i);
        int dw;
        if (b == WP_HARD_EOL || b == WP_SOFT_EOL || b == WP_HARD_PAGE) {
            break;
        }
        if (doc_is_visible(d, i)) {
            dw = doc_display_width(d, i);
            if (dw < 1) {
                dw = 1;
            }
            w += dw;
            if (doc_is_space(d, i)) {
                sp++;
            }
        }
        if (i == last_ns) {
            break;
        }
        i += n ? n : 1;
    }
    *textw = w;
    *nspaces = sp;
}

static int line_start_col_for(const View *v, const Doc *d, size_t lo, size_t hi, int indent,
                              int center, uint8_t just)
{
    int textw = 0;
    int nsp = 0;
    int col = indent;
    if (center || just == WP_JUST_CENTER || just == WP_JUST_RIGHT) {
        line_just_metrics(d, lo, hi, &textw, &nsp);
        if (textw > 0 && textw < v->wrap) {
            if (center || just == WP_JUST_CENTER) {
                col = (v->wrap - textw) / 2;
            } else {
                col = v->wrap - textw;
            }
            if (col < indent) {
                col = indent;
            }
        }
    }
    return col;
}

static int space_extra(int extra, int nspaces, int space_i)
{
    if (extra <= 0 || nspaces <= 0 || space_i < 0 || space_i >= nspaces) {
        return 0;
    }
    return extra / nspaces + (space_i < extra % nspaces ? 1 : 0);
}

static int remaining_visible(const Doc *d, size_t i)
{
    int rem = 0;
    while (i < d->len) {
        uint8_t c = d->data[i];
        if (c == WP_HARD_EOL || c == WP_SOFT_EOL || c == WP_HARD_PAGE) {
            break;
        }
        if (doc_is_visible(d, i)) {
            rem += doc_display_width(d, i);
        }
        i += doc_unit_len(d, i);
    }
    return rem;
}

static void draw_text_window(Screen *s, const View *v, const Doc *d, int y0, int rows)
{
    int line;
    int cols = s->cols;
    for (line = 0; line < rows; line++) {
        int li = v->top_line + line;
        if (li < 0 || li >= v->nlines) {
            continue;
        }
        size_t i = v->line_start[li];
        size_t end = (li + 1 < v->nlines) ? v->line_start[li + 1] : d->len;
        int para_indent = 0;
        int center = 0;
        uint8_t just = WP_JUST_LEFT;
        int textw = 0;
        int nspaces = 0;
        int extra = 0;
        int space_i = 0;
        int do_full = 0;
        size_t last_ns = line_last_nonspace(d, i, end);
        para_state_at(d, i, &para_indent, &center, &just);
        int col = line_start_col_for(v, d, i, end, para_indent, center, just);
        if (!center && just == WP_JUST_FULL && !line_ends_para(d, i, end)) {
            line_just_metrics(d, i, end, &textw, &nspaces);
            extra = v->wrap - textw;
            if (extra > 0 && nspaces > 0) {
                do_full = 1;
            }
        }
        unsigned bits = doc_attrs_at(d, i);
        while (i < end) {
            size_t n = doc_unit_len(d, i);
            if (n == 0) {
                break;
            }
            uint8_t b = d->data[i];
            if (b == WP_INDENT) {
                para_indent += WP_INDENT_COLS;
                if (col < para_indent) {
                    col = para_indent;
                }
            } else if (b == WP_CENTER) {
                int rem = remaining_visible(d, i + n);
                int start = (v->wrap > rem) ? (v->wrap - rem) / 2 : 0;
                if (start < para_indent) {
                    start = para_indent;
                }
                col = start;
            } else if (b == WP_ATTR_ON && n >= 2) {
                unsigned bit = wp_attr_bit(d->data[i + 1]);
                if (wp_attr_is_size(d->data[i + 1])) {
                    bits &= ~ATTR_BITS_SIZE;
                }
                bits |= bit;
            } else if (b == WP_ATTR_OFF && n >= 2) {
                bits &= ~wp_attr_bit(d->data[i + 1]);
            } else if (doc_is_visible(d, i)) {
                unsigned attr = 0;
                uint32_t cp = doc_display_cp(d, i);
                int w = doc_display_width(d, i);
                if (do_full && doc_is_space(d, i) && i > last_ns) {
                    i += n;
                    continue;
                }
                if (w < 1) {
                    w = 1;
                }
                if (col >= cols) {
                    i += n;
                    continue;
                }
                if (bits & ATTR_BIT_BOLD) {
                    attr |= SCR_BOLD;
                }
                if (bits & ATTR_BIT_UNDERLINE) {
                    attr |= SCR_UNDERLINE;
                }
                if (bits & ATTR_BIT_ITALIC) {
                    attr |= SCR_ITALIC;
                }
                if (bits & (ATTR_BIT_FINE | ATTR_BIT_SMALL)) {
                    attr |= SCR_DIM;
                }
                if (b == WP_END_FIELD) {
                    attr |= SCR_REVERSE;
                }
                if (b == WP_HARD_EOL || b == WP_SOFT_EOL || b == WP_HARD_PAGE ||
                    b == WP_TAB || b == '\t') {
                    attr |= SCR_DIM;
                }
                if (doc_sel_active(d)) {
                    size_t lo, hi;
                    doc_sel_bounds(d, &lo, &hi);
                    if (i >= lo && i < hi) {
                        attr |= SCR_REVERSE;
                    }
                }
                if (view_in_spell(v, i)) {
                    attr |= SCR_SPELL;
                }
                screen_put_wide(s, y0 + line, col, cp, w, attr);
                col += w;
                if (do_full && doc_is_space(d, i) && i <= last_ns) {
                    int pad = space_extra(extra, nspaces, space_i);
                    int p;
                    int remain = 0;
                    size_t k = i + n;
                    space_i++;
                    while (k <= last_ns && k < d->len) {
                        if (doc_is_visible(d, k)) {
                            int dw = doc_display_width(d, k);
                            remain += dw < 1 ? 1 : dw;
                        }
                        if (k == last_ns) {
                            break;
                        }
                        k += doc_unit_len(d, k);
                    }
                    if (pad > v->wrap - col - remain) {
                        pad = v->wrap - col - remain;
                    }
                    if (pad < 0) {
                        pad = 0;
                    }
                    for (p = 0; p < pad && col < cols; p++) {
                        screen_put_wide(s, y0 + line, col, ' ', 1, attr);
                        col++;
                    }
                }
            }
            i += n;
        }
    }
}

static const char *code_label(const Doc *d, size_t pos, char *tmp, size_t tmpsz)
{
    return doc_code_label(d, pos, tmp, tmpsz);
}

void view_split(const View *v, int rows, int *text_rows, int *reveal_y, int *reveal_rows)
{
    int strip = v->hide_strip ? 0 : WP51_STRIP_ROWS;
    int chrome = 1 + strip;
    int tr;
    int ry = 0;
    int rr = 0;
    if (v->reveal) {
        rr = rows / 3;
        if (rr < 3) {
            rr = 3;
        }
        if (rr > rows - chrome - 3) {
            rr = rows - chrome - 3;
        }
        if (rr < 2) {
            rr = 2;
        }
        tr = rows - chrome - 1 - rr;
        ry = tr + 1;
    } else {
        tr = rows - chrome;
    }
    if (tr < 2) {
        tr = 2;
    }
    if (text_rows) {
        *text_rows = tr;
    }
    if (reveal_y) {
        *reveal_y = ry;
    }
    if (reveal_rows) {
        *reveal_rows = rr;
    }
}

static size_t reveal_origin(const View *v, const Doc *d)
{
    size_t i = 0;
    if (v->cursor_line >= 0 && v->cursor_line < v->nlines) {
        int back = 40;
        i = v->line_start[v->cursor_line];
        while (i > 0 && back-- > 0) {
            i = doc_prev_unit(d, i);
        }
    }
    return i;
}

static int label_width(const char *lab)
{
    const uint8_t *p = (const uint8_t *)lab;
    size_t left = strlen(lab);
    int w = 0;
    while (left) {
        uint32_t cp;
        size_t u = wp_utf8_decode(p, left, &cp);
        if (!u) {
            break;
        }
        w += wp_cp_width(cp);
        p += u;
        left -= u;
    }
    return w;
}

static void draw_reveal(Screen *s, const View *v, const Doc *d, int y0, int rows,
                        int *cur_x, int *cur_y)
{
    int cols = s->cols;
    size_t i = reveal_origin(v, d);
    int y = y0;
    int x = 0;
    if (cur_x) {
        *cur_x = -1;
    }
    if (cur_y) {
        *cur_y = -1;
    }
    while (i <= d->len && y < y0 + rows) {
        int at_cur = (i == d->cursor);
        const char *lab;
        char tmp[40];
        int w;
        if (i == d->len) {
            lab = "";
            if (!at_cur) {
                break;
            }
        } else {
            lab = code_label(d, i, tmp, sizeof(tmp));
        }
        w = lab[0] ? label_width(lab) : (at_cur ? 1 : 0);
        if (x > 0 && x + w >= cols) {
            y++;
            x = 0;
            if (y >= y0 + rows) {
                break;
            }
        }
        if (at_cur) {
            if (cur_x) {
                *cur_x = x;
            }
            if (cur_y) {
                *cur_y = y;
            }
        }
        unsigned attr = at_cur ? SCR_REVERSE : 0;
        if (lab[0]) {
            screen_putn(s, y, x, lab, cols - x, attr);
            x += w;
        } else if (at_cur) {
            screen_put(s, y, x, ' ', attr);
            x++;
        }
        if (i >= d->len) {
            break;
        }
        i += doc_unit_len(d, i);
    }
}

size_t view_pos_at_reveal(const View *v, const Doc *d, int cols, int rel_row, int col)
{
    size_t i = reveal_origin(v, d);
    int y = 0;
    int x = 0;
    size_t last = i;
    if (rel_row < 0) {
        rel_row = 0;
    }
    if (col < 0) {
        col = 0;
    }
    while (i <= d->len) {
        char tmp[40];
        const char *lab;
        int w;
        int at_end = (i == d->len);
        if (at_end) {
            lab = " ";
        } else {
            lab = code_label(d, i, tmp, sizeof(tmp));
        }
        w = label_width(lab);
        if (w < 1) {
            w = 1;
        }
        if (x > 0 && x + w >= cols) {
            y++;
            x = 0;
        }
        if (y > rel_row) {
            return last;
        }
        if (y == rel_row && col >= x && col < x + w) {
            return i;
        }
        last = i;
        if (at_end) {
            return i;
        }
        x += w;
        i += doc_unit_len(d, i);
    }
    return last;
}

/* Unshifted WP 5.1 template, left-to-right like the LCD F-key row. */
const char *const view_fkey_names[12] = {
    "Cancel", "Search", "Help", "Indent", "List", "Bold",
    "Exit", "Undln", "Merge", "Save", "Reveal", "Center"
};

/* Ctrl template: classic Ctrl-F8 Font, plus the extras we actually have. */
const char *const view_fkey_names_ctrl[12] = {
    "Cancel", "<-Srch", "Help", "Indent", "List", "Italc",
    "Exit", "Font", "Merge", "Save", "Reveal", "Center"
};

const char *const view_fkey_names_shift[12] = {
    "Cancel", "<-Srch", "Help", "Indent", "List", "Bold",
    "Exit", "Font", "Merge", "Save", "Reveal", "Center"
};

const char *view_fkey_name(int index0, int mod)
{
    if (index0 < 0 || index0 > 11) {
        return "";
    }
    if (mod == FKEY_MOD_CTRL) {
        return view_fkey_names_ctrl[index0];
    }
    if (mod == FKEY_MOD_SHIFT) {
        return view_fkey_names_shift[index0];
    }
    return view_fkey_names[index0];
}

static void draw_fkey_strip(Screen *s, int y)
{
    int i;
    int cols = s->cols;
    int cell = cols / 12;
    if (cell < 5) {
        cell = 5;
    }
    int extra = cols - cell * 12;
    if (extra < 0) {
        extra = 0;
    }
    int x = 0;
    for (i = 0; i < 12 && x < cols; i++) {
        int w = cell + (i < extra ? 1 : 0);
        char num[8];
        snprintf(num, sizeof(num), "F%d", i + 1);
        int k;
        for (k = 0; k < w && x + k < cols; k++) {
            screen_put(s, y, x + k, ' ', SCR_REVERSE);
            if (y + 1 < s->rows) {
                screen_put(s, y + 1, x + k, ' ', SCR_REVERSE);
            }
        }
        screen_putn(s, y, x, view_fkey_names[i], w, SCR_REVERSE);
        if (y + 1 < s->rows) {
            screen_putn(s, y + 1, x, num, w, SCR_REVERSE);
        }
        x += w;
    }
}

void view_render_text(const View *v, const Doc *d, Screen *s)
{
    screen_clear(s);
    draw_text_window(s, v, d, 0, s->rows);
    s->show_cursor = 0;
}

int view_line_start_col(const View *v, const Doc *d, int li)
{
    size_t i;
    size_t end;
    int para_indent = 0;
    int center = 0;
    uint8_t just = WP_JUST_LEFT;
    int col;
    if (!v || !d || li < 0 || li >= v->nlines) {
        return 0;
    }
    i = v->line_start[li];
    end = (li + 1 < v->nlines) ? v->line_start[li + 1] : d->len;
    para_state_at(d, i, &para_indent, &center, &just);
    col = line_start_col_for(v, d, i, end, para_indent, center, just);
    while (i < end) {
        size_t n = doc_unit_len(d, i);
        uint8_t b;
        if (!n) {
            break;
        }
        b = d->data[i];
        if (b == WP_INDENT) {
            para_indent += WP_INDENT_COLS;
            if (col < para_indent) {
                col = para_indent;
            }
        } else if (b == WP_CENTER) {
            int rem = remaining_visible(d, i + n);
            int start = (v->wrap > rem) ? (v->wrap - rem) / 2 : 0;
            if (start < para_indent) {
                start = para_indent;
            }
            col = start;
        } else if (doc_is_visible(d, i)) {
            if (b != WP_HARD_EOL && b != WP_SOFT_EOL && b != WP_HARD_PAGE) {
                return col;
            }
            break;
        }
        i += n;
    }
    return col;
}

void view_render(const View *v, const Doc *d, Screen *s)
{
    int rows = s->rows;
    int cols = s->cols;
    int strip = v->hide_strip ? 0 : WP51_STRIP_ROWS;
    int text_rows = 0;
    int reveal_y = 0;
    int reveal_rows = 0;
    int rcx = -1;
    int rcy = -1;

    screen_clear(s);
    view_split(v, rows, &text_rows, &reveal_y, &reveal_rows);

    draw_text_window(s, v, d, 0, text_rows);
    if (v->reveal) {
        draw_hline(s, text_rows, "Reveal Codes");
        draw_reveal(s, v, d, reveal_y, reveal_rows, &rcx, &rcy);
    }

    if (strip) {
        draw_fkey_strip(s, rows - 1 - strip);
    }

    screen_fill_line(s, rows - 1, SCR_REVERSE);
    if (v->message[0] && v->msg_ttl) {
        screen_putn(s, rows - 1, 0, v->message, cols, SCR_REVERSE);
    } else {
        screen_putn(s, rows - 1, 0, v->status, cols, SCR_REVERSE);
    }

    if (v->reveal && rcy >= 0 && rcx >= 0) {
        s->cy = rcy;
        s->cx = rcx;
        s->show_cursor = 1;
    } else {
        int cy = v->cursor_line - v->top_line;
        int cx = v->cursor_col;
        if (cy >= 0 && cy < text_rows && cx >= 0 && cx < cols) {
            s->cy = cy;
            s->cx = cx;
            s->show_cursor = 1;
        } else {
            s->cy = rows - 1;
            s->cx = cols - 1;
            s->show_cursor = 0;
        }
    }
}

static int curses_attr(unsigned attr)
{
    int a = 0;
    if (attr & SCR_BOLD) {
        a |= A_BOLD;
    }
    if (attr & SCR_UNDERLINE) {
        a |= A_UNDERLINE;
    }
    if (attr & SCR_REVERSE) {
        a |= A_REVERSE;
    }
    if (attr & SCR_DIM) {
        a |= A_DIM;
    }
#ifdef A_ITALIC
    if (attr & SCR_ITALIC) {
        a |= A_ITALIC;
    }
#endif
    return a;
}

void view_draw(const View *v, const Doc *d, int rows, int cols)
{
    Screen s;
    int y, x;

    screen_init(&s);
    if (screen_resize(&s, rows, cols) != 0) {
        return;
    }
    view_render(v, d, &s);
    erase();
    for (y = 0; y < s.rows; y++) {
        for (x = 0; x < s.cols; x++) {
            Cell c = s.cells[y * s.cols + x];
            int a = curses_attr(c.attr);
            char u8[8];
            if (c.cp == 0) {
                continue;
            }
            if (a) {
                attron(a);
            }
            if (screen_cell_utf8(&c, u8, sizeof(u8)) && (unsigned char)u8[0] >= 128) {
                mvaddstr(y, x, u8);
            } else {
                mvaddch(y, x, (chtype)(c.cp < 128 ? c.cp : ' '));
            }
            if (a) {
                attroff(a);
            }
        }
    }
    if (s.show_cursor) {
        move(s.cy, s.cx);
    } else {
        move(s.rows - 1, s.cols - 1);
    }
    screen_free(&s);
}

void view_move_vert(View *v, Doc *d, int delta)
{
    int target = v->cursor_line + delta;
    if (target < 0) {
        target = 0;
    }
    if (target >= v->nlines) {
        target = v->nlines - 1;
    }
    if (target < 0) {
        d->cursor = 0;
        return;
    }
    int want_col = v->cursor_col;
    size_t i = v->line_start[target];
    size_t end = (target + 1 < v->nlines) ? v->line_start[target + 1] : d->len;
    int col = 0;
    size_t last = i;
    while (i < end) {
        if (col >= want_col) {
            d->cursor = i;
            return;
        }
        size_t n = doc_unit_len(d, i);
        if (doc_is_visible(d, i)) {
            uint8_t b = d->data[i];
            if (b == WP_HARD_EOL || b == WP_SOFT_EOL || b == WP_HARD_PAGE) {
                d->cursor = i;
                return;
            }
            last = i;
            col += doc_display_width(d, i);
        }
        i += n;
    }
    d->cursor = (end > v->line_start[target]) ? last : end;
    if (i >= end && want_col >= col) {
        d->cursor = end;
        /* If line ends with HRt, stay before it when we walked onto it. */
        if (end > 0) {
            size_t p = 0, t = 0;
            while (t < end) {
                p = t;
                t += doc_unit_len(d, t);
            }
            if (doc_is_visible(d, p) &&
                (d->data[p] == WP_HARD_EOL || d->data[p] == WP_HARD_PAGE || d->data[p] == WP_SOFT_EOL)) {
                d->cursor = p;
            }
        }
    }
}

void view_nav_vert(View *v, Doc *d, int delta, int extend)
{
    if (extend) {
        if (d->mark == SIZE_MAX) {
            d->mark = d->cursor;
        }
    } else {
        d->mark = SIZE_MAX;
    }
    view_relayout(v, d);
    view_move_vert(v, d, delta);
}

int view_in_spell(const View *v, size_t pos)
{
    int k;
    for (k = 0; k < v->spell_n; k++) {
        if (pos >= v->spell_lo[k] && pos < v->spell_hi[k]) {
            return 1;
        }
    }
    return 0;
}

size_t view_pos_at(const View *v, const Doc *d, int line, int want_col)
{
    size_t i, end, last;
    int col = 0;
    if (v->nlines <= 0) {
        return 0;
    }
    if (line < 0) {
        line = 0;
    }
    if (line >= v->nlines) {
        line = v->nlines - 1;
    }
    i = v->line_start[line];
    end = (line + 1 < v->nlines) ? v->line_start[line + 1] : d->len;
    last = i;
    if (want_col < 0) {
        want_col = 0;
    }
    {
        int para_indent = 0;
        int center = 0;
        uint8_t just = WP_JUST_LEFT;
        int textw = 0;
        int nspaces = 0;
        int extra = 0;
        int do_full = 0;
        int space_i = 0;
        size_t last_ns = line_last_nonspace(d, i, end);
        para_state_at(d, i, &para_indent, &center, &just);
        col = line_start_col_for(v, d, i, end, para_indent, center, just);
        if (!center && just == WP_JUST_FULL && !line_ends_para(d, i, end)) {
            line_just_metrics(d, i, end, &textw, &nspaces);
            extra = v->wrap - textw;
            if (extra > 0 && nspaces > 0) {
                do_full = 1;
            }
        }
        while (i < end) {
            size_t n;
            if (col >= want_col) {
                return i;
            }
            n = doc_unit_len(d, i);
            if (doc_is_visible(d, i)) {
                uint8_t b = d->data[i];
                if (b == WP_HARD_EOL || b == WP_SOFT_EOL || b == WP_HARD_PAGE) {
                    return i;
                }
                if (do_full && doc_is_space(d, i) && i > last_ns) {
                    i += n ? n : 1;
                    continue;
                }
                last = i;
                col += doc_display_width(d, i);
                if (do_full && doc_is_space(d, i) && space_i < nspaces) {
                    col += space_extra(extra, nspaces, space_i);
                    space_i++;
                }
            }
            i += n ? n : 1;
        }
    }
    return (end > v->line_start[line]) ? last : end;
}
