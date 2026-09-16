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
