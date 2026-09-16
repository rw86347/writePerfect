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

#ifndef DOC_H
#define DOC_H

#include "font.h"
#include "wp51.h"

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
    size_t cursor;
    size_t mark; /* SIZE_MAX = no selection */
    int dirty;
    char path[1024];
} Doc;

void doc_init(Doc *d);
void doc_free(Doc *d);
void doc_clear(Doc *d);
int doc_reserve(Doc *d, size_t need);

/* Length of the function or character at pos. */
size_t doc_unit_len(const Doc *d, size_t pos);

int doc_is_visible(const Doc *d, size_t pos);
int doc_is_space(const Doc *d, size_t pos);
unsigned doc_attrs_at(const Doc *d, size_t pos);
void doc_font_at(const Doc *d, size_t pos, DocFont *out);
uint8_t doc_just_at(const Doc *d, size_t pos);
const char *doc_code_label(const Doc *d, size_t pos, char *tmp, size_t tmpsz);
const char *wp_just_name(uint8_t mode);

int doc_insert(Doc *d, size_t pos, const uint8_t *bytes, size_t n);
int doc_insert_char(Doc *d, int ch);
int doc_insert_utf8(Doc *d, const char *s);
int doc_insert_hard_return(Doc *d);

size_t wp_utf8_decode(const uint8_t *p, size_t n, uint32_t *cp);
size_t wp_utf8_encode(uint32_t cp, uint8_t *out);
int wp_cp_width(uint32_t cp);
uint32_t doc_display_cp(const Doc *d, size_t pos);
int doc_display_width(const Doc *d, size_t pos);
int doc_toggle_attr(Doc *d, uint8_t attr);
int doc_insert_font(Doc *d, uint8_t family, uint8_t size_pt);
int doc_normal_attr(Doc *d);
int doc_normal_size(Doc *d);
int doc_strip_codes(Doc *d);
int doc_insert_just(Doc *d, uint8_t mode);
int doc_insert_code(Doc *d, uint8_t code);
/* Forward search if backward==0. Starts after cursor; wraps once. 0=found. */
int doc_search(Doc *d, const char *needle, int backward);
size_t doc_prev_unit(const Doc *d, size_t pos);
int doc_backspace(Doc *d, int codes);
int doc_delete_forward(Doc *d, int codes);

void doc_move_left(Doc *d, int codes);
void doc_move_right(Doc *d, int codes);
void doc_move_home(Doc *d);
void doc_move_end(Doc *d);
void doc_nav_left(Doc *d, int extend, int codes);
void doc_nav_right(Doc *d, int extend, int codes);
void doc_nav_home(Doc *d, int extend);
void doc_nav_end(Doc *d, int extend);

int doc_sel_active(const Doc *d);
void doc_sel_clear(Doc *d);
void doc_sel_all(Doc *d);
void doc_sel_bounds(const Doc *d, size_t *lo, size_t *hi);
int doc_sel_delete(Doc *d);
/* Visible text of [lo, hi). Caller frees. */
char *doc_range_utf8(const Doc *d, size_t lo, size_t hi, size_t *out_n);

/* Map cursor onto the next visible unit (or EOF). */
void doc_snap_cursor(Doc *d);

#endif
