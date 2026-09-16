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

#ifndef SCREEN_H
#define SCREEN_H

#include "wp51.h"

#define SCR_BOLD 1u
#define SCR_UNDERLINE 2u
#define SCR_REVERSE 4u
#define SCR_DIM 8u
#define SCR_SPELL 16u
#define SCR_ITALIC 32u

typedef struct {
    uint32_t cp; /* Unicode scalar; 0 = continuation of a wide glyph */
    unsigned char attr;
} Cell;

typedef struct {
    int rows;
    int cols;
    Cell *cells;
    int cy;
    int cx;
    int show_cursor;
} Screen;

void screen_init(Screen *s);
void screen_free(Screen *s);
int screen_resize(Screen *s, int rows, int cols);
void screen_clear(Screen *s);
void screen_fill_line(Screen *s, int y, unsigned attr);
void screen_put(Screen *s, int y, int x, char ch, unsigned attr);
void screen_put_cp(Screen *s, int y, int x, uint32_t cp, unsigned attr);
void screen_put_wide(Screen *s, int y, int x, uint32_t cp, int width, unsigned attr);
void screen_puts(Screen *s, int y, int x, const char *str, unsigned attr);
void screen_putn(Screen *s, int y, int x, const char *str, int n, unsigned attr);
size_t screen_cell_utf8(const Cell *c, char *out, size_t outsz);

#endif
