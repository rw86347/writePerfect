#include "screen.h"

#include "doc.h"

#include <stdlib.h>
#include <string.h>

void screen_init(Screen *s)
{
    memset(s, 0, sizeof(*s));
}

void screen_free(Screen *s)
{
    free(s->cells);
    memset(s, 0, sizeof(*s));
}

int screen_resize(Screen *s, int rows, int cols)
{
    if (rows < 5) {
        rows = 5;
    }
    if (cols < 20) {
        cols = 20;
    }
    if (s->rows == rows && s->cols == cols && s->cells) {
        return 0;
    }
    Cell *p = calloc((size_t)rows * (size_t)cols, sizeof(Cell));
    if (!p) {
        return -1;
    }
    free(s->cells);
    s->cells = p;
    s->rows = rows;
    s->cols = cols;
    return 0;
}

void screen_clear(Screen *s)
{
    int n = s->rows * s->cols;
    int i;
    for (i = 0; i < n; i++) {
        s->cells[i].cp = ' ';
        s->cells[i].attr = 0;
    }
    s->cy = 0;
    s->cx = 0;
    s->show_cursor = 0;
}

void screen_fill_line(Screen *s, int y, unsigned attr)
{
    int x;
    if (y < 0 || y >= s->rows) {
        return;
    }
    for (x = 0; x < s->cols; x++) {
        s->cells[y * s->cols + x].cp = ' ';
        s->cells[y * s->cols + x].attr = (unsigned char)attr;
    }
}

void screen_put_cp(Screen *s, int y, int x, uint32_t cp, unsigned attr)
{
    if (y < 0 || x < 0 || y >= s->rows || x >= s->cols) {
        return;
    }
    s->cells[y * s->cols + x].cp = cp ? cp : ' ';
    s->cells[y * s->cols + x].attr = (unsigned char)attr;
}

void screen_put(Screen *s, int y, int x, char ch, unsigned attr)
{
    screen_put_cp(s, y, x, (unsigned char)ch, attr);
}

void screen_put_wide(Screen *s, int y, int x, uint32_t cp, int width, unsigned attr)
{
    screen_put_cp(s, y, x, cp, attr);
    if (width > 1 && x + 1 < s->cols) {
        s->cells[y * s->cols + x + 1].cp = 0;
        s->cells[y * s->cols + x + 1].attr = (unsigned char)attr;
    }
}

void screen_puts(Screen *s, int y, int x, const char *str, unsigned attr)
{
    screen_putn(s, y, x, str, s->cols, attr);
}

void screen_putn(Screen *s, int y, int x, const char *str, int n, unsigned attr)
{
    const uint8_t *p;
    size_t left;
    int cols;
    if (!str || n <= 0) {
        return;
    }
    p = (const uint8_t *)str;
    left = strlen(str);
    cols = 0;
    while (left && cols < n && x < s->cols) {
        uint32_t cp;
        size_t u = wp_utf8_decode(p, left, &cp);
        int w;
        if (!u) {
            break;
        }
        w = wp_cp_width(cp);
        if (w < 1) {
            w = 1;
        }
        screen_put_wide(s, y, x, cp, w, attr);
        x += w;
        cols += w;
        p += u;
        left -= u;
    }
}

size_t screen_cell_utf8(const Cell *c, char *out, size_t outsz)
{
    uint8_t tmp[4];
    size_t n;
    if (!c || !out || outsz == 0) {
        return 0;
    }
    if (c->cp == 0) {
        out[0] = 0;
        return 0;
    }
    n = wp_utf8_encode(c->cp, tmp);
    if (n >= outsz) {
        out[0] = 0;
        return 0;
    }
    memcpy(out, tmp, n);
    out[n] = 0;
    return n;
}
