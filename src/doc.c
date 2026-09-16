#include "doc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void doc_init(Doc *d)
{
    memset(d, 0, sizeof(*d));
    d->mark = SIZE_MAX;
}

void doc_free(Doc *d)
{
    free(d->data);
    memset(d, 0, sizeof(*d));
}

void doc_clear(Doc *d)
{
    d->len = 0;
    d->cursor = 0;
    d->mark = SIZE_MAX;
    d->dirty = 0;
    d->path[0] = '\0';
}

int doc_reserve(Doc *d, size_t need)
{
    if (d->cap >= need) {
        return 0;
    }
    size_t cap = d->cap ? d->cap : 256;
    while (cap < need) {
        cap *= 2;
    }
    uint8_t *p = realloc(d->data, cap);
    if (!p) {
        return -1;
    }
    d->data = p;
    d->cap = cap;
    return 0;
}

size_t wp_utf8_decode(const uint8_t *p, size_t n, uint32_t *cp)
{
    if (!p || !n) {
        if (cp) {
            *cp = 0;
        }
        return 0;
    }
    uint8_t b = p[0];
    if (b < 0x80) {
        if (cp) {
            *cp = b;
        }
        return 1;
    }
    if ((b & 0xE0) == 0xC0 && n >= 2 && (p[1] & 0xC0) == 0x80) {
        uint32_t v = ((uint32_t)(b & 0x1F) << 6) | (p[1] & 0x3F);
        if (cp) {
            *cp = v < 0x80 ? 0xFFFD : v;
        }
        return 2;
    }
    if ((b & 0xF0) == 0xE0 && n >= 3 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        uint32_t v = ((uint32_t)(b & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        if (cp) {
            *cp = v < 0x800 ? 0xFFFD : v;
        }
        return 3;
    }
    if ((b & 0xF8) == 0xF0 && n >= 4 && (p[1] & 0xC0) == 0x80 &&
        (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
        uint32_t v = ((uint32_t)(b & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) |
                     ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        if (cp) {
            *cp = (v < 0x10000 || v > 0x10FFFF) ? 0xFFFD : v;
        }
        return 4;
    }
    if (cp) {
        *cp = 0xFFFD;
    }
    return 1;
}

size_t wp_utf8_encode(uint32_t cp, uint8_t *out)
{
    if (!out) {
        return 0;
    }
    if (cp < 0x80) {
        out[0] = (uint8_t)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (uint8_t)(0xC0 | (cp >> 6));
        out[1] = (uint8_t)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (uint8_t)(0xE0 | (cp >> 12));
        out[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (uint8_t)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp <= 0x10FFFF) {
        out[0] = (uint8_t)(0xF0 | (cp >> 18));
        out[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (uint8_t)(0x80 | (cp & 0x3F));
        return 4;
    }
    return wp_utf8_encode(0xFFFD, out);
}

int wp_cp_width(uint32_t cp)
{
    if (cp == 0) {
        return 0;
    }
    return 1;
}

size_t doc_unit_len(const Doc *d, size_t pos)
{
    if (pos >= d->len) {
        return 0;
    }
    const uint8_t *p = d->data + pos;
    size_t n = d->len - pos;
    uint8_t b = p[0];

    if (b < 0x80) {
        return 1;
    }
    if (b == WP_EXT_CHAR && n >= 3) {
        return 3;
    }
    if ((b == WP_ATTR_ON || b == WP_ATTR_OFF) && n >= 2) {
        return 2;
    }
    if (b == WP_FONT && n >= 3) {
        return 3;
    }
    if (b == WP_JUST && n >= 2) {
        return 2;
    }
    if (b == WP_UTF8 && n >= 2) {
        size_t u = p[1];
        if (u >= 1 && u <= 4 && n >= 2 + u) {
            return 2 + u;
        }
    }
    if ((b == WP_TAB || b == WP_INDENT || b == WP_CENTER || b == WP_END_FIELD) && n >= 1) {
        return 1;
    }
    if (b >= 0xD0 && b <= 0xEF && n >= 4) {
        size_t sz = (size_t)p[2] | ((size_t)p[3] << 8);
        if (sz >= 4 && sz <= n) {
            return sz;
        }
    }
    if (b >= 0xF0 && n >= 2) {
        /* Fixed multi-byte groups: skip through matching end gate if present. */
        size_t i;
        for (i = 1; i < n; i++) {
            if (p[i] == b) {
                return i + 1;
            }
        }
    }
    return 1;
}

int doc_is_visible(const Doc *d, size_t pos)
{
    if (pos >= d->len) {
        return 0;
    }
    uint8_t b = d->data[pos];
    if (b == WP_HARD_EOL || b == WP_HARD_PAGE || b == WP_SOFT_EOL || b == '\t' ||
        b == WP_TAB || b == WP_END_FIELD || b == WP_UTF8) {
        return 1;
    }
    if (b >= 32 && b < 127) {
        return 1;
    }
    if (b == WP_EXT_CHAR) {
        return 1;
    }
    return 0;
}

int doc_is_space(const Doc *d, size_t pos)
{
    uint8_t b;
    if (pos >= d->len) {
        return 0;
    }
    b = d->data[pos];
    return b == ' ' || b == '\t' || b == WP_TAB;
}

uint32_t doc_display_cp(const Doc *d, size_t pos)
{
    if (pos >= d->len) {
        return 0;
    }
    uint8_t b = d->data[pos];
    if (b == WP_HARD_EOL) {
        return 0x23CE; /* ⏎ */
    }
    if (b == WP_SOFT_EOL) {
        return 0x21A9; /* ↩ */
    }
    if (b == WP_HARD_PAGE) {
        return 0x21A1; /* ↡ */
    }
    if (b == WP_TAB || b == '\t') {
        return 0x21E5; /* ⇥ */
    }
    if (b == WP_END_FIELD) {
        return '*';
    }
    if (b == WP_UTF8) {
        size_t n = doc_unit_len(d, pos);
        uint32_t cp = 0xFFFD;
        if (n >= 3) {
            wp_utf8_decode(d->data + pos + 2, n - 2, &cp);
        }
        return cp;
    }
    if (b == WP_EXT_CHAR) {
        size_t n = doc_unit_len(d, pos);
        if (n >= 2 && d->data[pos + 1] >= 32) {
            return d->data[pos + 1];
        }
        return '?';
    }
    if (b >= 32 && b < 127) {
        return b;
    }
    return 0;
}

int doc_display_width(const Doc *d, size_t pos)
{
    uint32_t cp = doc_display_cp(d, pos);
    int w = wp_cp_width(cp);
    return w > 0 ? w : 0;
}

unsigned doc_attrs_at(const Doc *d, size_t pos)
{
    unsigned bits = 0;
    size_t i = 0;
    if (pos > d->len) {
        pos = d->len;
    }
    while (i < pos) {
        size_t n = doc_unit_len(d, i);
        if (n == 0) {
            break;
        }
        if (d->data[i] == WP_ATTR_ON && n >= 2) {
            unsigned bit = wp_attr_bit(d->data[i + 1]);
            if (wp_attr_is_size(d->data[i + 1])) {
                bits &= ~ATTR_BITS_SIZE;
            }
            bits |= bit;
        } else if (d->data[i] == WP_ATTR_OFF && n >= 2) {
            bits &= ~wp_attr_bit(d->data[i + 1]);
        }
        i += n;
    }
    return bits;
}

void doc_font_at(const Doc *d, size_t pos, DocFont *out)
{
    size_t i = 0;
    if (!out) {
        return;
    }
    out->family = WP_FONT_COURIER;
    out->size_pt = WP_FONT_DEFAULT_PT;
    out->attrs = 0;
    if (!d) {
        return;
    }
    if (pos > d->len) {
        pos = d->len;
    }
    while (i < pos) {
        size_t n = doc_unit_len(d, i);
        if (n == 0) {
            break;
        }
        if (d->data[i] == WP_FONT && n >= 3) {
            out->family = d->data[i + 1];
            out->size_pt = d->data[i + 2] ? d->data[i + 2] : WP_FONT_DEFAULT_PT;
        } else if (d->data[i] == WP_ATTR_ON && n >= 2) {
            unsigned bit = wp_attr_bit(d->data[i + 1]);
            if (wp_attr_is_size(d->data[i + 1])) {
                out->attrs &= ~ATTR_BITS_SIZE;
            }
            out->attrs |= bit;
        } else if (d->data[i] == WP_ATTR_OFF && n >= 2) {
            out->attrs &= ~wp_attr_bit(d->data[i + 1]);
        }
        i += n;
    }
}

const char *wp_just_name(uint8_t mode)
{
    switch (mode) {
    case WP_JUST_CENTER:
        return "Center";
    case WP_JUST_RIGHT:
        return "Right";
    case WP_JUST_FULL:
        return "Full";
    default:
        return "Left";
    }
}

uint8_t doc_just_at(const Doc *d, size_t pos)
{
    uint8_t just = WP_JUST_LEFT;
    size_t i = 0;
    if (!d) {
        return just;
    }
    if (pos > d->len) {
        pos = d->len;
    }
    while (i <= pos && i < d->len) {
        size_t n = doc_unit_len(d, i);
        if (!n) {
            break;
        }
        if (d->data[i] == WP_JUST && n >= 2) {
            just = d->data[i + 1];
            if (just > WP_JUST_FULL) {
                just = WP_JUST_LEFT;
            }
        }
        if (i == pos) {
            break;
        }
        i += n;
    }
    return just;
}

const char *doc_code_label(const Doc *d, size_t pos, char *tmp, size_t tmpsz)
{
    uint8_t b;
    size_t n;
    if (!d || pos >= d->len || !tmp || tmpsz < 4) {
        return "";
    }
    b = d->data[pos];
    n = doc_unit_len(d, pos);
    if (b == WP_HARD_EOL) {
        return "[HRt]";
    }
    if (b == WP_SOFT_EOL) {
        return "[SRt]";
    }
    if (b == WP_HARD_PAGE) {
        return "[HPg]";
    }
    if (b == WP_TAB || b == '\t') {
        return "[Tab]";
    }
    if (b == WP_INDENT) {
        return "[->Indent]";
    }
    if (b == WP_CENTER) {
        return "[Cntr]";
    }
    if (b == WP_END_FIELD) {
        return "[End Field]";
    }
    if (b == WP_FONT && n >= 3) {
        snprintf(tmp, tmpsz, "[Font:%s %upt]", wp_font_family_name(d->data[pos + 1]),
                 (unsigned)d->data[pos + 2]);
        return tmp;
    }
    if (b == WP_JUST && n >= 2) {
        snprintf(tmp, tmpsz, "[Just:%s]", wp_just_name(d->data[pos + 1]));
        return tmp;
    }
    if (b == WP_ATTR_ON && n >= 2) {
        const char *lab = wp_attr_on_label(d->data[pos + 1]);
        if (lab) {
            snprintf(tmp, tmpsz, "[%s]", lab);
            return tmp;
        }
        snprintf(tmp, tmpsz, "[ON:%u]", d->data[pos + 1]);
        return tmp;
    }
    if (b == WP_ATTR_OFF && n >= 2) {
        const char *lab = wp_attr_off_label(d->data[pos + 1]);
        if (lab) {
            snprintf(tmp, tmpsz, "[%s]", lab);
            return tmp;
        }
        snprintf(tmp, tmpsz, "[off:%u]", d->data[pos + 1]);
        return tmp;
    }
    if (b == WP_UTF8) {
        uint32_t cp = doc_display_cp(d, pos);
        size_t u = wp_utf8_encode(cp, (uint8_t *)tmp);
        if (u + 1 <= tmpsz) {
            tmp[u] = '\0';
            return tmp;
        }
    }
    if (b >= 32 && b < 127) {
        tmp[0] = (char)b;
        tmp[1] = '\0';
        return tmp;
    }
    snprintf(tmp, tmpsz, "[%02X]", b);
    return tmp;
}

int doc_insert(Doc *d, size_t pos, const uint8_t *bytes, size_t n)
{
    if (pos > d->len) {
        pos = d->len;
    }
    if (doc_reserve(d, d->len + n) != 0) {
        return -1;
    }
    memmove(d->data + pos + n, d->data + pos, d->len - pos);
    memcpy(d->data + pos, bytes, n);
    d->len += n;
    if (d->cursor >= pos) {
        d->cursor += n;
    }
    d->dirty = 1;
    return 0;
}

int doc_sel_active(const Doc *d)
{
    return d && d->mark != SIZE_MAX && d->mark != d->cursor;
}

void doc_sel_clear(Doc *d)
{
    if (d) {
        d->mark = SIZE_MAX;
    }
}

void doc_sel_all(Doc *d)
{
    d->mark = 0;
    d->cursor = d->len;
}

void doc_sel_bounds(const Doc *d, size_t *lo, size_t *hi)
{
    size_t a = d->mark;
    size_t b = d->cursor;
    if (a == SIZE_MAX) {
        a = b;
    }
    if (a > d->len) {
        a = d->len;
    }
    if (b > d->len) {
        b = d->len;
    }
    if (a <= b) {
        *lo = a;
        *hi = b;
    } else {
        *lo = b;
        *hi = a;
    }
}

int doc_sel_delete(Doc *d)
{
    size_t lo, hi, n;
    if (!doc_sel_active(d)) {
        return 0;
    }
    doc_sel_bounds(d, &lo, &hi);
    n = hi - lo;
    memmove(d->data + lo, d->data + hi, d->len - hi);
    d->len -= n;
    d->cursor = lo;
    d->mark = SIZE_MAX;
    d->dirty = 1;
    return 0;
}

static int visible_utf8_at(const Doc *d, size_t pos, char *out, size_t outsz, size_t *written);

char *doc_range_utf8(const Doc *d, size_t lo, size_t hi, size_t *out_n)
{
    size_t cap = 64;
    size_t n = 0;
    char *s = malloc(cap);
    size_t i;
    if (!s) {
        return NULL;
    }
    if (hi > d->len) {
        hi = d->len;
    }
    for (i = lo; i < hi; ) {
        size_t u = doc_unit_len(d, i);
        uint8_t b;
        char buf[8];
        size_t w = 0;
        if (!u) {
            break;
        }
        b = d->data[i];
        if (b == WP_HARD_EOL || b == WP_SOFT_EOL) {
            buf[0] = '\n';
            w = 1;
        } else if (b == WP_HARD_PAGE) {
            buf[0] = '\n';
            buf[1] = '\n';
            w = 2;
        } else {
            visible_utf8_at(d, i, buf, sizeof(buf), &w);
        }
        if (w) {
            if (n + w + 1 > cap) {
                char *p;
                while (n + w + 1 > cap) {
                    cap *= 2;
                }
                p = realloc(s, cap);
                if (!p) {
                    free(s);
                    return NULL;
                }
                s = p;
            }
            memcpy(s + n, buf, w);
            n += w;
        }
        i += u;
    }
    s[n] = 0;
    if (out_n) {
        *out_n = n;
    }
    return s;
}

static void doc_sel_consume(Doc *d)
{
    if (doc_sel_active(d)) {
        doc_sel_delete(d);
    }
}

int doc_insert_char(Doc *d, int ch)
{
    uint8_t b;
    doc_sel_consume(d);
    if (ch == '\t') {
        b = WP_TAB;
    } else if (ch >= 32 && ch < 127) {
        b = (uint8_t)ch;
    } else {
        return -1;
    }
    return doc_insert(d, d->cursor, &b, 1);
}

int doc_insert_utf8(Doc *d, const char *s)
{
    const uint8_t *p;
    size_t left;
    if (!s) {
        return -1;
    }
    doc_sel_consume(d);
    p = (const uint8_t *)s;
    left = strlen(s);
    while (left) {
        uint32_t cp;
        size_t n = wp_utf8_decode(p, left, &cp);
        if (n == 0 || n > left) {
            break;
        }
        if (cp == '\r') {
            if (n < left && p[n] == '\n') {
                n++;
            }
            if (doc_insert_hard_return(d) != 0) {
                return -1;
            }
        } else if (cp == '\n') {
            if (doc_insert_hard_return(d) != 0) {
                return -1;
            }
        } else if (cp == '\t') {
            if (doc_insert_char(d, '\t') != 0) {
                return -1;
            }
        } else if (cp >= 32 && cp < 127) {
            if (doc_insert_char(d, (int)cp) != 0) {
                return -1;
            }
        } else if (cp >= 32) {
            uint8_t buf[6];
            size_t enc = wp_utf8_encode(cp, buf + 2);
            buf[0] = WP_UTF8;
            buf[1] = (uint8_t)enc;
            if (doc_insert(d, d->cursor, buf, 2 + enc) != 0) {
                return -1;
            }
        }
        p += n;
        left -= n;
    }
    return 0;
}

int doc_insert_hard_return(Doc *d)
{
    uint8_t b = WP_HARD_EOL;
    doc_sel_consume(d);
    return doc_insert(d, d->cursor, &b, 1);
}

static int insert_attr_pair(Doc *d, size_t pos, uint8_t onoff, uint8_t attr)
{
    uint8_t pair[2];
    pair[0] = onoff;
    pair[1] = attr;
    return doc_insert(d, pos, pair, 2);
}

static int is_size_code_at(const Doc *d, size_t pos)
{
    size_t n;
    if (!d || pos >= d->len) {
        return 0;
    }
    n = doc_unit_len(d, pos);
    return n >= 2 && (d->data[pos] == WP_ATTR_ON || d->data[pos] == WP_ATTR_OFF) &&
           wp_attr_is_size(d->data[pos + 1]);
}

static size_t size_code_run_start(const Doc *d, size_t pos)
{
    size_t i = pos;
    while (i > 0) {
        size_t prev = doc_prev_unit(d, i);
        if (prev >= i || !is_size_code_at(d, prev)) {
            break;
        }
        i = prev;
    }
    return i;
}

static void delete_span(Doc *d, size_t lo, size_t hi)
{
    size_t n;
    if (!d || hi <= lo || lo > d->len) {
        return;
    }
    if (hi > d->len) {
        hi = d->len;
    }
    n = hi - lo;
    memmove(d->data + lo, d->data + hi, d->len - hi);
    d->len -= n;
    if (d->cursor >= hi) {
        d->cursor -= n;
    } else if (d->cursor > lo) {
        d->cursor = lo;
    }
    if (d->mark != SIZE_MAX) {
        if (d->mark >= hi) {
            d->mark -= n;
        } else if (d->mark > lo) {
            d->mark = lo;
        }
    }
    d->dirty = 1;
}

static int delete_unit_at(Doc *d, size_t pos)
{
    size_t n;
    if (pos >= d->len) {
        return -1;
    }
    n = doc_unit_len(d, pos);
    if (!n) {
        return -1;
    }
    memmove(d->data + pos, d->data + pos + n, d->len - pos - n);
    d->len -= n;
    if (d->cursor > pos) {
        d->cursor -= n;
    }
    d->dirty = 1;
    return 0;
}

static int range_all_attr(const Doc *d, size_t lo, size_t hi, unsigned mask)
{
    size_t i = lo;
    int any = 0;
    while (i < hi && i < d->len) {
        size_t n = doc_unit_len(d, i);
        uint8_t b;
        if (!n) {
            break;
        }
        b = d->data[i];
        if (doc_is_visible(d, i) && b != WP_HARD_EOL && b != WP_SOFT_EOL && b != WP_HARD_PAGE) {
            any = 1;
            if (!(doc_attrs_at(d, i) & mask)) {
                return 0;
            }
        }
        i += n;
    }
    return any;
}

static int unwrap_attr(Doc *d, uint8_t attr, size_t lo, size_t hi)
{
    size_t nlo;
    size_t prev;
    int del_on = 0;
    int del_off = 0;
    nlo = (lo < d->len) ? doc_unit_len(d, lo) : 0;
    if (nlo >= 2 && d->data[lo] == WP_ATTR_ON && d->data[lo + 1] == attr) {
        del_on = 1;
    }
    prev = (hi > 0) ? doc_prev_unit(d, hi) : 0;
    if (hi > 0 && prev < hi && prev < d->len) {
        size_t np = doc_unit_len(d, prev);
        if (np >= 2 && d->data[prev] == WP_ATTR_OFF && d->data[prev + 1] == attr) {
            del_off = 1;
        }
    }
    if (del_off) {
        if (delete_unit_at(d, prev) != 0) {
            return -1;
        }
        hi = prev;
    }
    if (del_on) {
        if (delete_unit_at(d, lo) != 0) {
            return -1;
        }
        hi = (hi >= 2) ? hi - 2 : 0;
    } else if (insert_attr_pair(d, lo, WP_ATTR_OFF, attr) != 0) {
        return -1;
    } else {
        hi += 2;
        lo += 2;
    }
    if (!del_off) {
        if (insert_attr_pair(d, hi, WP_ATTR_ON, attr) != 0) {
            return -1;
        }
        hi += 2;
    }
    d->cursor = hi;
    d->mark = SIZE_MAX;
    return 0;
}

static int wrap_attr(Doc *d, uint8_t attr)
{
    size_t lo, hi;
    unsigned mask = wp_attr_bit(attr);
    doc_sel_bounds(d, &lo, &hi);
    if (range_all_attr(d, lo, hi, mask)) {
        return unwrap_attr(d, attr, lo, hi);
    }
    if (insert_attr_pair(d, hi, WP_ATTR_OFF, attr) != 0) {
        return -1;
    }
    if (insert_attr_pair(d, lo, WP_ATTR_ON, attr) != 0) {
        return -1;
    }
    d->cursor = hi + 4;
    d->mark = SIZE_MAX;
    return 0;
}

static int wrap_size(Doc *d, uint8_t attr)
{
    size_t lo, hi;
    size_t i;
    uint8_t inherited;
    uint8_t effective;

    doc_sel_bounds(d, &lo, &hi);
    effective = wp_size_attr_from_bits(doc_attrs_at(d, lo));
    while (lo > 0) {
        size_t prev = doc_prev_unit(d, lo);
        if (prev >= lo || !is_size_code_at(d, prev)) {
            break;
        }
        lo = prev;
    }
    while (hi < d->len && is_size_code_at(d, hi)) {
        size_t n = doc_unit_len(d, hi);
        if (!n) {
            break;
        }
        hi += n;
    }
    i = lo;
    while (i < hi) {
        size_t n = doc_unit_len(d, i);
        if (!n) {
            break;
        }
        if (is_size_code_at(d, i)) {
            delete_unit_at(d, i);
            if (hi >= n) {
                hi -= n;
            }
        } else {
            i += n;
        }
    }
    inherited = wp_size_attr_from_bits(doc_attrs_at(d, lo));
    d->mark = SIZE_MAX;
    if (attr == 0xFF || attr == effective) {
        if (inherited == 0xFF) {
            d->cursor = hi;
            return 0;
        }
        if (insert_attr_pair(d, lo, WP_ATTR_OFF, inherited) != 0) {
            return -1;
        }
        hi += 2;
        if (insert_attr_pair(d, hi, WP_ATTR_ON, inherited) != 0) {
            return -1;
        }
        d->cursor = hi;
        return 0;
    }
    if (inherited != 0xFF) {
        if (insert_attr_pair(d, lo, WP_ATTR_OFF, inherited) != 0) {
            return -1;
        }
        lo += 2;
        hi += 2;
    }
    if (insert_attr_pair(d, lo, WP_ATTR_ON, attr) != 0) {
        return -1;
    }
    hi += 2;
    if (insert_attr_pair(d, hi, WP_ATTR_OFF, attr) != 0) {
        return -1;
    }
    if (inherited != 0xFF) {
        if (insert_attr_pair(d, hi + 2, WP_ATTR_ON, inherited) != 0) {
            return -1;
        }
    }
    d->cursor = hi;
    return 0;
}

static int apply_size_at_cursor(Doc *d, uint8_t attr)
{
    size_t run;
    uint8_t effective;
    uint8_t inherited;

    if (doc_sel_active(d)) {
        return wrap_size(d, attr);
    }
    effective = wp_size_attr_from_bits(doc_attrs_at(d, d->cursor));
    run = size_code_run_start(d, d->cursor);
    if (run < d->cursor) {
        delete_span(d, run, d->cursor);
        d->cursor = run;
    }
    inherited = wp_size_attr_from_bits(doc_attrs_at(d, d->cursor));
    if (attr == 0xFF || attr == effective) {
        if (inherited != 0xFF) {
            return insert_attr_pair(d, d->cursor, WP_ATTR_OFF, inherited);
        }
        return 0;
    }
    if (inherited == attr) {
        return 0;
    }
    if (inherited != 0xFF) {
        if (insert_attr_pair(d, d->cursor, WP_ATTR_OFF, inherited) != 0) {
            return -1;
        }
    }
    return insert_attr_pair(d, d->cursor, WP_ATTR_ON, attr);
}

int doc_toggle_attr(Doc *d, uint8_t attr)
{
    unsigned mask = wp_attr_bit(attr);
    unsigned bits;
    uint8_t pair[2];
    size_t n;
    if (!mask) {
        return -1;
    }
    if (wp_attr_is_size(attr)) {
        return apply_size_at_cursor(d, attr);
    }
    if (doc_sel_active(d)) {
        return wrap_attr(d, attr);
    }
    n = doc_unit_len(d, d->cursor);
    if (n >= 2 && d->cursor < d->len &&
        (d->data[d->cursor] == WP_ATTR_ON || d->data[d->cursor] == WP_ATTR_OFF) &&
        d->data[d->cursor + 1] == attr) {
        return delete_unit_at(d, d->cursor);
    }
    bits = doc_attrs_at(d, d->cursor);
    pair[0] = (bits & mask) ? WP_ATTR_OFF : WP_ATTR_ON;
    pair[1] = attr;
    return doc_insert(d, d->cursor, pair, 2);
}

int doc_insert_font(Doc *d, uint8_t family, uint8_t size_pt)
{
    uint8_t buf[3];
    if (family > WP_FONT_HELVETICA) {
        family = WP_FONT_COURIER;
    }
    if (!size_pt) {
        size_pt = WP_FONT_DEFAULT_PT;
    }
    buf[0] = WP_FONT;
    buf[1] = family;
    buf[2] = size_pt;
    if (doc_sel_active(d)) {
        size_t lo, hi;
        DocFont prev;
        uint8_t restore[3];
        doc_sel_bounds(d, &lo, &hi);
        doc_font_at(d, lo, &prev);
        restore[0] = WP_FONT;
        restore[1] = prev.family;
        restore[2] = prev.size_pt ? prev.size_pt : WP_FONT_DEFAULT_PT;
        if (doc_insert(d, hi, restore, 3) != 0) {
            return -1;
        }
        if (doc_insert(d, lo, buf, 3) != 0) {
            return -1;
        }
        d->mark = SIZE_MAX;
        d->cursor = lo + 3 + (hi - lo);
        return 0;
    }
    return doc_insert(d, d->cursor, buf, 3);
}

int doc_normal_size(Doc *d)
{
    return apply_size_at_cursor(d, 0xFF);
}

int doc_normal_attr(Doc *d)
{
    static const uint8_t attrs[] = {
        WP_ATTR_BOLD, WP_ATTR_UNDERLINE, WP_ATTR_ITALIC
    };
    unsigned bits;
    size_t i;
    if (doc_normal_size(d) != 0) {
        return -1;
    }
    bits = doc_attrs_at(d, d->cursor);
    for (i = 0; i < sizeof(attrs); i++) {
        if (bits & wp_attr_bit(attrs[i])) {
            if (insert_attr_pair(d, d->cursor, WP_ATTR_OFF, attrs[i]) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

int doc_strip_codes(Doc *d)
{
    size_t r = 0;
    size_t w = 0;
    if (!d) {
        return -1;
    }
    while (r < d->len) {
        size_t n = doc_unit_len(d, r);
        uint8_t b;
        if (!n) {
            break;
        }
        b = d->data[r];
        if (b == WP_ATTR_ON || b == WP_ATTR_OFF || b == WP_FONT || b == WP_CENTER ||
            b == WP_INDENT || b == WP_END_FIELD || b == WP_JUST) {
            r += n;
            continue;
        }
        if (w != r) {
            memmove(d->data + w, d->data + r, n);
        }
        w += n;
        r += n;
    }
    if (w != d->len) {
        d->len = w;
        d->dirty = 1;
    }
    if (d->cursor > d->len) {
        d->cursor = d->len;
    }
    d->mark = SIZE_MAX;
    return 0;
}

int doc_insert_just(Doc *d, uint8_t mode)
{
    uint8_t buf[2];
    size_t prev;
    if (mode > WP_JUST_FULL) {
        mode = WP_JUST_FULL;
    }
    if (d->cursor < d->len && d->data[d->cursor] == WP_JUST &&
        doc_unit_len(d, d->cursor) >= 2) {
        d->data[d->cursor + 1] = mode;
        d->dirty = 1;
        return 0;
    }
    if (d->cursor > 0) {
        prev = doc_prev_unit(d, d->cursor);
        if (prev < d->len && d->data[prev] == WP_JUST && doc_unit_len(d, prev) >= 2) {
            d->data[prev + 1] = mode;
            d->dirty = 1;
            return 0;
        }
    }
    buf[0] = WP_JUST;
    buf[1] = mode;
    return doc_insert(d, d->cursor, buf, 2);
}

int doc_insert_code(Doc *d, uint8_t code)
{
    doc_sel_consume(d);
    return doc_insert(d, d->cursor, &code, 1);
}

static int visible_utf8_at(const Doc *d, size_t pos, char *out, size_t outsz, size_t *written)
{
    uint8_t b;
    if (pos >= d->len || !doc_is_visible(d, pos) || !out || outsz == 0) {
        return 0;
    }
    b = d->data[pos];
    if (b == WP_HARD_EOL || b == WP_SOFT_EOL || b == WP_HARD_PAGE) {
        return 0;
    }
    if (b == WP_TAB || b == '\t') {
        out[0] = '\t';
        *written = 1;
        return 1;
    }
    if (b == WP_END_FIELD) {
        out[0] = '*';
        *written = 1;
        return 1;
    }
    if (b == WP_UTF8) {
        size_t n = doc_unit_len(d, pos);
        size_t u = n >= 2 ? n - 2 : 0;
        if (u == 0 || u > outsz) {
            return 0;
        }
        memcpy(out, d->data + pos + 2, u);
        *written = u;
        return 1;
    }
    if (b == WP_EXT_CHAR) {
        size_t n = doc_unit_len(d, pos);
        out[0] = (n >= 2 && d->data[pos + 1] >= 32) ? (char)d->data[pos + 1] : '?';
        *written = 1;
        return 1;
    }
    if (b >= 32 && b < 127) {
        out[0] = (char)b;
        *written = 1;
        return 1;
    }
    return 0;
}

int doc_search(Doc *d, const char *needle, int backward)
{
    size_t nlen;
    size_t nbytes = 0;
    size_t i;
    if (!needle || !(nlen = strlen(needle))) {
        return -1;
    }
    for (i = 0; i < d->len; ) {
        char buf[8];
        size_t w = 0;
        size_t n = doc_unit_len(d, i);
        if (visible_utf8_at(d, i, buf, sizeof(buf), &w)) {
            nbytes += w;
        }
        i += n ? n : 1;
    }
    if (nbytes == 0) {
        return -1;
    }
    char *text = malloc(nbytes + 1);
    size_t *pos = malloc(nbytes * sizeof(size_t));
    if (!text || !pos) {
        free(text);
        free(pos);
        return -1;
    }
    size_t k = 0;
    int cur_k = -1;
    for (i = 0; i < d->len; ) {
        char buf[8];
        size_t w = 0;
        size_t n;
        if (i == d->cursor) {
            cur_k = (int)k;
        }
        n = doc_unit_len(d, i);
        if (visible_utf8_at(d, i, buf, sizeof(buf), &w)) {
            size_t t;
            for (t = 0; t < w; t++) {
                text[k] = buf[t];
                pos[k] = i;
                k++;
            }
        }
        i += n ? n : 1;
    }
    if (d->cursor >= d->len) {
        cur_k = (int)k;
    }
    text[k] = '\0';
    int found = -1;
    if (!backward) {
        int start = cur_k < 0 ? 0 : cur_k;
        /* Skip the unit under the cursor so F2 again finds the next hit. */
        if (start < (int)k) {
            size_t here = pos[start];
            while (start < (int)k && pos[start] == here) {
                start++;
            }
        }
        for (i = (size_t)start; i + nlen <= k; i++) {
            if (memcmp(text + i, needle, nlen) == 0) {
                found = (int)i;
                break;
            }
        }
        if (found < 0) {
            for (i = 0; i + nlen <= k && (int)i < start; i++) {
                if (memcmp(text + i, needle, nlen) == 0) {
                    found = (int)i;
                    break;
                }
            }
        }
    } else {
        int start = cur_k < 0 ? (int)k : cur_k;
        int j;
        for (j = start - (int)nlen; j >= 0; j--) {
            if (memcmp(text + j, needle, nlen) == 0) {
                found = j;
                break;
            }
        }
        if (found < 0) {
            for (j = (int)k - (int)nlen; j >= start; j--) {
                if (memcmp(text + j, needle, nlen) == 0) {
                    found = j;
                    break;
                }
            }
        }
    }
    if (found >= 0) {
        d->cursor = pos[found];
    }
    free(text);
    free(pos);
    return found >= 0 ? 0 : -1;
}

size_t doc_prev_unit(const Doc *d, size_t pos)
{
    size_t i = 0;
    size_t prev = 0;
    if (!d || pos == 0) {
        return 0;
    }
    if (pos > d->len) {
        pos = d->len;
    }
    while (i < pos) {
        size_t n = doc_unit_len(d, i);
        prev = i;
        if (n == 0) {
            break;
        }
        i += n;
    }
    return prev;
}

int doc_backspace(Doc *d, int codes)
{
    size_t prev;
    if (doc_sel_active(d)) {
        return doc_sel_delete(d);
    }
    if (d->cursor == 0) {
        return 0;
    }
    prev = doc_prev_unit(d, d->cursor);
    if (!codes) {
        while (prev > 0 && !doc_is_visible(d, prev)) {
            prev = doc_prev_unit(d, prev);
        }
        if (!doc_is_visible(d, prev)) {
            return 0;
        }
    }
    return delete_unit_at(d, prev);
}

int doc_delete_forward(Doc *d, int codes)
{
    size_t pos;
    if (doc_sel_active(d)) {
        return doc_sel_delete(d);
    }
    if (d->cursor >= d->len) {
        return 0;
    }
    pos = d->cursor;
    if (!codes) {
        while (pos < d->len && !doc_is_visible(d, pos)) {
            pos += doc_unit_len(d, pos);
        }
        if (pos >= d->len) {
            return 0;
        }
    }
    return delete_unit_at(d, pos);
}

void doc_snap_cursor(Doc *d)
{
    if (d->cursor > d->len) {
        d->cursor = d->len;
    }
}

void doc_move_left(Doc *d, int codes)
{
    size_t prev;
    if (d->cursor == 0) {
        return;
    }
    prev = doc_prev_unit(d, d->cursor);
    if (!codes) {
        while (prev > 0 && !doc_is_visible(d, prev)) {
            prev = doc_prev_unit(d, prev);
        }
        d->cursor = prev;
        return;
    }
    d->cursor = prev;
}

void doc_move_right(Doc *d, int codes)
{
    if (d->cursor >= d->len) {
        return;
    }
    d->cursor += doc_unit_len(d, d->cursor);
    if (!codes) {
        while (d->cursor < d->len && !doc_is_visible(d, d->cursor)) {
            d->cursor += doc_unit_len(d, d->cursor);
        }
    }
}

void doc_move_home(Doc *d)
{
    d->cursor = 0;
}

void doc_move_end(Doc *d)
{
    d->cursor = d->len;
}

static void doc_nav_prep(Doc *d, int extend)
{
    if (extend) {
        if (d->mark == SIZE_MAX) {
            d->mark = d->cursor;
        }
    } else if (doc_sel_active(d)) {
        /* caller handles collapse */
    } else {
        d->mark = SIZE_MAX;
    }
}

void doc_nav_left(Doc *d, int extend, int codes)
{
    if (!extend && doc_sel_active(d)) {
        size_t lo, hi;
        doc_sel_bounds(d, &lo, &hi);
        d->cursor = lo;
        d->mark = SIZE_MAX;
        return;
    }
    doc_nav_prep(d, extend);
    if (!extend) {
        d->mark = SIZE_MAX;
    }
    doc_move_left(d, codes);
}

void doc_nav_right(Doc *d, int extend, int codes)
{
    if (!extend && doc_sel_active(d)) {
        size_t lo, hi;
        doc_sel_bounds(d, &lo, &hi);
        d->cursor = hi;
        d->mark = SIZE_MAX;
        return;
    }
    doc_nav_prep(d, extend);
    if (!extend) {
        d->mark = SIZE_MAX;
    }
    doc_move_right(d, codes);
}

void doc_nav_home(Doc *d, int extend)
{
    doc_nav_prep(d, extend);
    if (!extend) {
        d->mark = SIZE_MAX;
    }
    doc_move_home(d);
}

void doc_nav_end(Doc *d, int extend)
{
    doc_nav_prep(d, extend);
    if (!extend) {
        d->mark = SIZE_MAX;
    }
    doc_move_end(d);
}
