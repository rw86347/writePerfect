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

#include "iofmt.h"

#include "font.h"
#include "view.h"
#include "wpd.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>

static int g_fmt = IO_FMT_DOCX;
static int g_wrap = IO_WRAP_WORD;

int io_default_format(void)
{
    return g_fmt;
}

void io_set_default_format(int fmt)
{
    if (fmt >= IO_FMT_DOCX && fmt <= IO_FMT_WPD) {
        g_fmt = fmt;
    }
}

int io_wrap_mode(void)
{
    return g_wrap;
}

void io_set_wrap_mode(int mode)
{
    g_wrap = (mode == IO_WRAP_STRICT80) ? IO_WRAP_STRICT80 : IO_WRAP_WORD;
}

int io_wrap_is_word(void)
{
    return g_wrap != IO_WRAP_STRICT80;
}

static const char *ext_of(const char *path)
{
    const char *slash;
    const char *dot;
    if (!path) {
        return "";
    }
    slash = strrchr(path, '/');
    slash = slash ? slash + 1 : path;
    dot = strrchr(slash, '.');
    return dot ? dot + 1 : "";
}

int io_format_from_path(const char *path)
{
    const char *e = ext_of(path);
    if (!strcasecmp(e, "docx")) {
        return IO_FMT_DOCX;
    }
    if (!strcasecmp(e, "md") || !strcasecmp(e, "markdown")) {
        return IO_FMT_MD;
    }
    if (!strcasecmp(e, "pdf")) {
        return IO_FMT_PDF;
    }
    if (!strcasecmp(e, "wpd") || !strcasecmp(e, "wps") || !strcasecmp(e, "wp") ||
        !strcasecmp(e, "wkb")) {
        return IO_FMT_WPD;
    }
    return -1;
}

const char *io_format_ext(int fmt)
{
    switch (fmt) {
    case IO_FMT_MD:
        return "md";
    case IO_FMT_PDF:
        return "pdf";
    case IO_FMT_WPD:
        return "wpd";
    default:
        return "docx";
    }
}

const char *io_default_filename(void)
{
    switch (g_fmt) {
    case IO_FMT_MD:
        return "document.md";
    case IO_FMT_PDF:
        return "document.pdf";
    case IO_FMT_WPD:
        return "document.wpd";
    default:
        return "document.docx";
    }
}

int io_path_has_known_ext(const char *path)
{
    return io_format_from_path(path) >= 0;
}

int io_path_for_format(const char *path, int fmt, char *out, size_t outsz)
{
    const char *slash;
    const char *dot;
    size_t n;
    if (!out || outsz < 8) {
        return -1;
    }
    if (fmt < IO_FMT_DOCX || fmt > IO_FMT_WPD) {
        fmt = g_fmt;
    }
    if (!path || !path[0]) {
        snprintf(out, outsz, "%s", io_default_filename());
        return 0;
    }
    slash = strrchr(path, '/');
    slash = slash ? slash + 1 : path;
    dot = strrchr(slash, '.');
    /* Keep .wpd/.wps/.wp/.wkb when saving WordPerfect — old 5.1 opens all of them. */
    if (fmt == IO_FMT_WPD && io_format_from_path(path) == IO_FMT_WPD) {
        snprintf(out, outsz, "%s", path);
        return 0;
    }
    if (dot && io_format_from_path(path) >= 0) {
        n = (size_t)(dot - path);
        if (n + 8 >= outsz) {
            return -1;
        }
        memcpy(out, path, n);
        out[n] = 0;
        snprintf(out + n, outsz - n, ".%s", io_format_ext(fmt));
        return 0;
    }
    snprintf(out, outsz, "%s.%s", path, io_format_ext(fmt));
    return 0;
}

static int resolve_path(const char *path, char *out, size_t outsz)
{
    if (!path || !path[0] || !out || outsz < 8) {
        return -1;
    }
    if (io_path_has_known_ext(path)) {
        snprintf(out, outsz, "%s", path);
        return 0;
    }
    snprintf(out, outsz, "%s.%s", path, io_format_ext(g_fmt));
    return 0;
}

/* ---- growable buffer ---- */

typedef struct {
    char *p;
    size_t n;
    size_t cap;
} Buf;

static int buf_reserve(Buf *b, size_t need)
{
    size_t cap;
    char *p;
    if (b->cap >= need) {
        return 0;
    }
    cap = b->cap ? b->cap : 256;
    while (cap < need) {
        cap *= 2;
    }
    p = realloc(b->p, cap);
    if (!p) {
        return -1;
    }
    b->p = p;
    b->cap = cap;
    return 0;
}

static int buf_add(Buf *b, const void *data, size_t n)
{
    if (buf_reserve(b, b->n + n + 1) != 0) {
        return -1;
    }
    memcpy(b->p + b->n, data, n);
    b->n += n;
    b->p[b->n] = 0;
    return 0;
}

static int buf_adds(Buf *b, const char *s)
{
    return buf_add(b, s, strlen(s));
}

static void buf_free(Buf *b)
{
    free(b->p);
    memset(b, 0, sizeof(*b));
}

static uint32_t crc32_bytes(const void *data, size_t n)
{
    const uint8_t *p = data;
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    for (i = 0; i < n; i++) {
        int k;
        crc ^= p[i];
        for (k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int)(crc & 1));
        }
    }
    return ~crc;
}

static void put_u16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static uint16_t get_u16le(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

typedef struct {
    char name[128];
    uint32_t crc;
    uint32_t size;
    uint32_t offset;
} ZipEnt;

static int zip_add(Buf *zip, ZipEnt *ents, int *nent, const char *name, const void *data, size_t n)
{
    uint8_t lh[30];
    size_t namelen = strlen(name);
    ZipEnt *e;
    if (*nent >= 16 || namelen >= sizeof(ents[0].name)) {
        return -1;
    }
    e = &ents[*nent];
    memset(e, 0, sizeof(*e));
    snprintf(e->name, sizeof(e->name), "%s", name);
    e->crc = crc32_bytes(data, n);
    e->size = (uint32_t)n;
    e->offset = (uint32_t)zip->n;
    memset(lh, 0, sizeof(lh));
    lh[0] = 'P';
    lh[1] = 'K';
    lh[2] = 3;
    lh[3] = 4;
    put_u16le(lh + 4, 20);
    put_u32le(lh + 14, e->crc);
    put_u32le(lh + 18, e->size);
    put_u32le(lh + 22, e->size);
    put_u16le(lh + 26, (uint16_t)namelen);
    if (buf_add(zip, lh, 30) != 0 || buf_add(zip, name, namelen) != 0 ||
        buf_add(zip, data, n) != 0) {
        return -1;
    }
    (*nent)++;
    return 0;
}

static int zip_finish(Buf *zip, ZipEnt *ents, int nent)
{
    uint32_t cd_off = (uint32_t)zip->n;
    uint32_t cd_sz;
    uint8_t eocd[22];
    int i;
    for (i = 0; i < nent; i++) {
        uint8_t ch[46];
        size_t namelen = strlen(ents[i].name);
        memset(ch, 0, sizeof(ch));
        ch[0] = 'P';
        ch[1] = 'K';
        ch[2] = 1;
        ch[3] = 2;
        put_u16le(ch + 4, 20);
        put_u16le(ch + 6, 20);
        put_u32le(ch + 16, ents[i].crc);
        put_u32le(ch + 20, ents[i].size);
        put_u32le(ch + 24, ents[i].size);
        put_u16le(ch + 28, (uint16_t)namelen);
        put_u32le(ch + 42, ents[i].offset);
        if (buf_add(zip, ch, 46) != 0 || buf_add(zip, ents[i].name, namelen) != 0) {
            return -1;
        }
    }
    cd_sz = (uint32_t)zip->n - cd_off;
    memset(eocd, 0, sizeof(eocd));
    eocd[0] = 'P';
    eocd[1] = 'K';
    eocd[2] = 5;
    eocd[3] = 6;
    put_u16le(eocd + 8, (uint16_t)nent);
    put_u16le(eocd + 10, (uint16_t)nent);
    put_u32le(eocd + 12, cd_sz);
    put_u32le(eocd + 16, cd_off);
    return buf_add(zip, eocd, 22);
}

static int write_all(const char *path, const void *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    size_t w;
    if (!f) {
        return -1;
    }
    w = fwrite(data, 1, n, f);
    fclose(f);
    return w == n ? 0 : -1;
}

static uint8_t *read_all(const char *path, size_t *out_n)
{
    FILE *f = fopen(path, "rb");
    long sz;
    uint8_t *buf;
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (sz && fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    buf[sz] = 0;
    *out_n = (size_t)sz;
    return buf;
}

static int zip_extract(const uint8_t *zip, size_t len, const char *want, Buf *out)
{
    size_t i = 0;
    while (i + 30 <= len) {
        uint16_t namelen, extra, method;
        uint32_t size, csize;
        const uint8_t *name;
        if (zip[i] != 'P' || zip[i + 1] != 'K' || zip[i + 2] != 3 || zip[i + 3] != 4) {
            i++;
            continue;
        }
        method = get_u16le(zip + i + 8);
        csize = get_u32le(zip + i + 18);
        size = get_u32le(zip + i + 22);
        namelen = get_u16le(zip + i + 26);
        extra = get_u16le(zip + i + 28);
        if (i + 30 + namelen + extra + csize > len) {
            break;
        }
        name = zip + i + 30;
        if (namelen == strlen(want) && memcmp(name, want, namelen) == 0) {
            if (method != 0 || csize != size) {
                return -1;
            }
            return buf_add(out, zip + i + 30 + namelen + extra, size);
        }
        i += 30 + namelen + extra + csize;
    }
    return -1;
}

/* ---- walk document to text / markup ---- */

static void xml_escape_into(Buf *b, const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (c == '&') {
            buf_adds(b, "&amp;");
        } else if (c == '<') {
            buf_adds(b, "&lt;");
        } else if (c == '>') {
            buf_adds(b, "&gt;");
        } else if (c == '"') {
            buf_adds(b, "&quot;");
        } else {
            buf_add(b, &c, 1);
        }
    }
}

static int emit_visible_utf8(Buf *b, const Doc *d, size_t pos)
{
    uint8_t byte = d->data[pos];
    char tmp[8];
    size_t w = 0;
    if (byte == WP_HARD_EOL || byte == WP_SOFT_EOL || byte == WP_HARD_PAGE) {
        return 0;
    }
    if (byte == WP_TAB || byte == '\t') {
        return buf_adds(b, "\t");
    }
    if (byte == WP_END_FIELD) {
        return buf_adds(b, "*");
    }
    if (byte == WP_UTF8) {
        size_t n = doc_unit_len(d, pos);
        if (n >= 3) {
            return buf_add(b, d->data + pos + 2, n - 2);
        }
        return 0;
    }
    if (byte == WP_EXT_CHAR) {
        tmp[0] = (char)d->data[pos + 1];
        return buf_add(b, tmp, 1);
    }
    if (byte >= 32 && byte < 127) {
        tmp[0] = (char)byte;
        return buf_add(b, tmp, 1);
    }
    (void)w;
    return 0;
}

static int range_has_code(const Doc *d, size_t lo, size_t hi, uint8_t code);

static size_t para_end_pos(const Doc *d, size_t i)
{
    while (i < d->len) {
        uint8_t b = d->data[i];
        if (b == WP_HARD_EOL || b == WP_SOFT_EOL || b == WP_HARD_PAGE) {
            return i;
        }
        i += doc_unit_len(d, i);
    }
    return d->len;
}

static int md_font_open(const DocFont *f)
{
    return f->family != WP_FONT_COURIER || f->size_pt != WP_FONT_DEFAULT_PT;
}

static void md_close_marks(Buf *b, int *und, int *bold, int *italic, int *size_on, int *font_on)
{
    if (*und) {
        buf_adds(b, "_");
        *und = 0;
    }
    if (*bold) {
        buf_adds(b, "**");
        *bold = 0;
    }
    if (*italic) {
        buf_adds(b, "</i>");
        *italic = 0;
    }
    if (*size_on) {
        buf_adds(b, "</span>");
        *size_on = 0;
    }
    if (*font_on) {
        buf_adds(b, "</span>");
        *font_on = 0;
    }
}

static void md_sync_font(Buf *b, const DocFont *f, int *font_on, uint8_t *cur_fam, uint8_t *cur_pt)
{
    int want = md_font_open(f);
    if (*font_on && (!want || *cur_fam != f->family || *cur_pt != f->size_pt)) {
        buf_adds(b, "</span>");
        *font_on = 0;
    }
    if (want && !*font_on) {
        char tag[160];
        snprintf(tag, sizeof(tag),
                 "<span data-wp-font=\"%s\" data-wp-pt=\"%u\" "
                 "style=\"font-family:%s, '%s', %s;font-size:%upt\">",
                 wp_font_family_name(f->family), (unsigned)f->size_pt,
                 wp_font_family_name(f->family), wp_font_docx_name(f->family),
                 f->family == WP_FONT_COURIER ? "monospace" :
                 (f->family == WP_FONT_TIMES ? "serif" : "sans-serif"),
                 (unsigned)f->size_pt);
        buf_adds(b, tag);
        *font_on = 1;
        *cur_fam = f->family;
        *cur_pt = f->size_pt;
    }
}

static const char *md_size_name(unsigned bits)
{
    uint8_t a = wp_size_attr_from_bits(bits);
    return a == 0xFF ? NULL : wp_attr_on_label(a);
}

static int save_md(const Doc *d, const char *path)
{
    Buf b = {0};
    size_t i = 0;
    int bold = 0, und = 0, italic = 0;
    int size_on = 0, font_on = 0;
    uint8_t cur_fam = WP_FONT_COURIER, cur_pt = WP_FONT_DEFAULT_PT;
    const char *cur_size = NULL;
    int center = 0;
    int just_p = 0;
    int para_start = 1;
    int rc;
    while (i < d->len) {
        size_t n = doc_unit_len(d, i);
        uint8_t byte;
        DocFont f;
        if (!n) {
            break;
        }
        if (para_start) {
            uint8_t just = doc_just_at(d, i);
            center = range_has_code(d, i, para_end_pos(d, i), WP_CENTER);
            just_p = 0;
            if (center) {
                buf_adds(&b, "<center>");
            } else if (just == WP_JUST_FULL) {
                buf_adds(&b, "<p data-wp-just=\"FULL\" style=\"text-align:justify\">");
                just_p = 1;
            } else if (just == WP_JUST_RIGHT) {
                buf_adds(&b, "<p data-wp-just=\"RIGHT\" style=\"text-align:right\">");
                just_p = 1;
            } else if (just == WP_JUST_CENTER) {
                buf_adds(&b, "<p data-wp-just=\"CENTER\" style=\"text-align:center\">");
                just_p = 1;
            }
            para_start = 0;
        }
        byte = d->data[i];
        doc_font_at(d, i, &f);
        if (doc_is_visible(d, i) && byte != WP_HARD_EOL && byte != WP_SOFT_EOL &&
            byte != WP_HARD_PAGE) {
            int want_b = (f.attrs & ATTR_BIT_BOLD) != 0;
            int want_u = (f.attrs & ATTR_BIT_UNDERLINE) != 0;
            int want_i = (f.attrs & ATTR_BIT_ITALIC) != 0;
            const char *want_sz = md_size_name(f.attrs);
            if (und && !want_u) {
                buf_adds(&b, "_");
                und = 0;
            }
            if (bold && !want_b) {
                buf_adds(&b, "**");
                bold = 0;
            }
            if (italic && !want_i) {
                buf_adds(&b, "</i>");
                italic = 0;
            }
            if (size_on && want_sz != cur_size) {
                buf_adds(&b, "</span>");
                size_on = 0;
                cur_size = NULL;
            }
            md_sync_font(&b, &f, &font_on, &cur_fam, &cur_pt);
            if (want_sz && !size_on) {
                char tag[64];
                snprintf(tag, sizeof(tag), "<span data-wp-size=\"%s\">", want_sz);
                buf_adds(&b, tag);
                size_on = 1;
                cur_size = want_sz;
            }
            if (want_i && !italic) {
                buf_adds(&b, "<i>");
                italic = 1;
            }
            if (want_b && !bold) {
                buf_adds(&b, "**");
                bold = 1;
            }
            if (want_u && !und) {
                buf_adds(&b, "_");
                und = 1;
            }
            emit_visible_utf8(&b, d, i);
        } else if (byte == WP_HARD_PAGE || byte == WP_HARD_EOL || byte == WP_SOFT_EOL) {
            md_close_marks(&b, &und, &bold, &italic, &size_on, &font_on);
            cur_size = NULL;
            if (center) {
                buf_adds(&b, "</center>");
                center = 0;
            }
            if (just_p) {
                buf_adds(&b, "</p>");
                just_p = 0;
            }
            if (byte == WP_HARD_PAGE) {
                buf_adds(&b, "\n\n---\n\n");
            } else {
                buf_adds(&b, "\n");
            }
            para_start = 1;
        }
        i += n;
    }
    md_close_marks(&b, &und, &bold, &italic, &size_on, &font_on);
    if (center) {
        buf_adds(&b, "</center>");
    }
    if (just_p) {
        buf_adds(&b, "</p>");
    }
    rc = write_all(path, b.p ? b.p : "", b.n);
    buf_free(&b);
    return rc;
}

static uint8_t md_size_from_name(const char *s)
{
    if (!s) {
        return 0xFF;
    }
    if (!strcasecmp(s, "FINE")) {
        return WP_ATTR_FINE;
    }
    if (!strcasecmp(s, "SMALL")) {
        return WP_ATTR_SMALL;
    }
    if (!strcasecmp(s, "LARGE")) {
        return WP_ATTR_LARGE;
    }
    if (!strcasecmp(s, "VRY LARGE") || !strcasecmp(s, "VRYLARGE")) {
        return WP_ATTR_VRY_LARGE;
    }
    if (!strcasecmp(s, "EXT LARGE") || !strcasecmp(s, "EXTLARGE")) {
        return WP_ATTR_EXT_LARGE;
    }
    return 0xFF;
}

static int load_md(Doc *d, const char *s, size_t n)
{
    size_t i = 0;
    int bold = 0, und = 0, italic = 0;
    uint8_t size_on = 0xFF;
    uint8_t font_stack_fam[8];
    uint8_t font_stack_pt[8];
    int font_sp = 0;
    doc_clear(d);
    font_stack_fam[0] = WP_FONT_COURIER;
    font_stack_pt[0] = WP_FONT_DEFAULT_PT;
    while (i < n) {
        if (i + 5 <= n && !memcmp(s + i, "\n---\n", 5)) {
            if (bold) {
                doc_toggle_attr(d, WP_ATTR_BOLD);
                bold = 0;
            }
            if (und) {
                doc_toggle_attr(d, WP_ATTR_UNDERLINE);
                und = 0;
            }
            doc_insert_code(d, WP_HARD_PAGE);
            i += 5;
            continue;
        }
        if (i + 8 <= n && !memcmp(s + i, "<center>", 8)) {
            doc_insert_code(d, WP_CENTER);
            i += 8;
            continue;
        }
        if (i + 9 <= n && !memcmp(s + i, "</center>", 9)) {
            i += 9;
            continue;
        }
        if (i + 2 <= n && !memcmp(s + i, "<p", 2) && (s[i + 2] == ' ' || s[i + 2] == '>')) {
            size_t j = i;
            uint8_t mode = 0xFF;
            while (j < n && s[j] != '>') {
                if (j + 16 <= n && !memcmp(s + j, "data-wp-just=\"", 14)) {
                    j += 14;
                    if (j + 4 <= n && !memcmp(s + j, "FULL", 4)) {
                        mode = WP_JUST_FULL;
                    } else if (j + 5 <= n && !memcmp(s + j, "RIGHT", 5)) {
                        mode = WP_JUST_RIGHT;
                    } else if (j + 6 <= n && !memcmp(s + j, "CENTER", 6)) {
                        mode = WP_JUST_CENTER;
                    } else if (j + 4 <= n && !memcmp(s + j, "LEFT", 4)) {
                        mode = WP_JUST_LEFT;
                    }
                } else if (j + 21 <= n && !memcmp(s + j, "text-align:justify", 18)) {
                    mode = WP_JUST_FULL;
                }
                j++;
            }
            if (j < n && s[j] == '>') {
                j++;
            }
            if (mode != 0xFF && doc_just_at(d, d->cursor) != mode) {
                doc_insert_just(d, mode);
            }
            i = j;
            continue;
        }
        if (i + 4 <= n && !memcmp(s + i, "</p>", 4)) {
            i += 4;
            continue;
        }
        if (i + 3 <= n && !memcmp(s + i, "<i>", 3)) {
            if (!italic) {
                doc_toggle_attr(d, WP_ATTR_ITALIC);
                italic = 1;
            }
            i += 3;
            continue;
        }
        if (i + 4 <= n && !memcmp(s + i, "</i>", 4)) {
            if (italic) {
                doc_toggle_attr(d, WP_ATTR_ITALIC);
                italic = 0;
            }
            i += 4;
            continue;
        }
        if (s[i] == '<' && i + 1 < n) {
            size_t j = i + 1;
            int closing = 0;
            if (s[j] == '/') {
                closing = 1;
                j++;
            }
            if (j + 4 <= n && !memcmp(s + j, "span", 4)) {
                char name[32];
                char fam[32];
                unsigned pt = 0;
                uint8_t sz;
                name[0] = fam[0] = 0;
                while (j < n && s[j] != '>') {
                    if (j + 13 <= n && !memcmp(s + j, "data-wp-size=\"", 14)) {
                        size_t k = 0;
                        j += 14;
                        while (j < n && s[j] != '"' && k + 1 < sizeof(name)) {
                            name[k++] = s[j++];
                        }
                        name[k] = 0;
                    } else if (j + 13 <= n && !memcmp(s + j, "data-wp-font=\"", 14)) {
                        size_t k = 0;
                        j += 14;
                        while (j < n && s[j] != '"' && k + 1 < sizeof(fam)) {
                            fam[k++] = s[j++];
                        }
                        fam[k] = 0;
                    } else if (j + 11 <= n && !memcmp(s + j, "data-wp-pt=\"", 12)) {
                        j += 12;
                        while (j < n && s[j] >= '0' && s[j] <= '9') {
                            pt = pt * 10 + (unsigned)(s[j++] - '0');
                        }
                    } else {
                        j++;
                    }
                }
                if (j < n && s[j] == '>') {
                    j++;
                }
                if (closing) {
                    if (size_on != 0xFF) {
                        doc_toggle_attr(d, size_on);
                        size_on = 0xFF;
                    } else if (font_sp > 0) {
                        font_sp--;
                        doc_insert_font(d, font_stack_fam[font_sp], font_stack_pt[font_sp]);
                    }
                } else if (name[0]) {
                    sz = md_size_from_name(name);
                    if (sz != 0xFF) {
                        if (size_on != 0xFF) {
                            doc_toggle_attr(d, size_on);
                        }
                        doc_toggle_attr(d, sz);
                        size_on = sz;
                    }
                } else if (fam[0] || pt) {
                    DocFont cur;
                    if (font_sp + 1 < (int)(sizeof(font_stack_fam))) {
                        doc_font_at(d, d->cursor, &cur);
                        font_stack_fam[font_sp] = cur.family;
                        font_stack_pt[font_sp] = cur.size_pt;
                        font_sp++;
                    }
                    doc_insert_font(d, wp_font_family_from_name(fam),
                                    pt ? (uint8_t)pt : WP_FONT_DEFAULT_PT);
                }
                i = j;
                continue;
            }
        }
        if (i + 2 <= n && s[i] == '*' && s[i + 1] == '*') {
            doc_toggle_attr(d, WP_ATTR_BOLD);
            bold = !bold;
            i += 2;
            continue;
        }
        if (s[i] == '_') {
            doc_toggle_attr(d, WP_ATTR_UNDERLINE);
            und = !und;
            i++;
            continue;
        }
        if (s[i] == '\n') {
            if (bold) {
                doc_toggle_attr(d, WP_ATTR_BOLD);
                bold = 0;
            }
            if (und) {
                doc_toggle_attr(d, WP_ATTR_UNDERLINE);
                und = 0;
            }
            doc_insert_hard_return(d);
            i++;
            continue;
        }
        {
            uint32_t cp;
            size_t u = wp_utf8_decode((const uint8_t *)s + i, n - i, &cp);
            char one[8];
            if (!u) {
                break;
            }
            memcpy(one, s + i, u);
            one[u] = 0;
            doc_insert_utf8(d, one);
            i += u;
        }
    }
    d->cursor = 0;
    d->dirty = 0;
    return 0;
}

static int save_docx(const Doc *d, const char *path)
{
    Buf body = {0};
    Buf zip = {0};
    ZipEnt ents[8];
    int nent = 0;
    size_t i = 0;
    int para_open = 0;
    int rc = -1;
    const char *ct =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/word/document.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "</Types>";
    const char *rels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
        "Target=\"word/document.xml\"/>"
        "</Relationships>";
    const char *docrels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"/>";

    buf_adds(&body,
             "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
             "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
             "<w:body>");

    while (i <= d->len) {
        size_t n;
        uint8_t byte;
        unsigned bits;
        if (!para_open) {
            buf_adds(&body, "<w:p>");
            if (range_has_code(d, i, para_end_pos(d, i), WP_CENTER)) {
                buf_adds(&body, "<w:pPr><w:jc w:val=\"center\"/></w:pPr>");
            } else {
                uint8_t just = doc_just_at(d, i);
                if (just == WP_JUST_FULL) {
                    buf_adds(&body, "<w:pPr><w:jc w:val=\"both\"/></w:pPr>");
                } else if (just == WP_JUST_RIGHT) {
                    buf_adds(&body, "<w:pPr><w:jc w:val=\"right\"/></w:pPr>");
                } else if (just == WP_JUST_CENTER) {
                    buf_adds(&body, "<w:pPr><w:jc w:val=\"center\"/></w:pPr>");
                }
            }
            para_open = 1;
        }
        if (i >= d->len) {
            break;
        }
        n = doc_unit_len(d, i);
        if (!n) {
            break;
        }
        byte = d->data[i];
        bits = doc_attrs_at(d, i);
        if (byte == WP_HARD_PAGE) {
            buf_adds(&body, "<w:r><w:br w:type=\"page\"/></w:r></w:p>");
            para_open = 0;
        } else if (byte == WP_HARD_EOL || byte == WP_SOFT_EOL) {
            buf_adds(&body, "</w:p>");
            para_open = 0;
        } else if (doc_is_visible(d, i)) {
            Buf text = {0};
            DocFont f;
            emit_visible_utf8(&text, d, i);
            doc_font_at(d, i, &f);
            if (text.n) {
                int pt = wp_effective_pt(&f);
                buf_adds(&body, "<w:r><w:rPr>");
                if (f.family != WP_FONT_COURIER || f.size_pt != WP_FONT_DEFAULT_PT ||
                    (f.attrs & ATTR_BITS_SIZE)) {
                    char tag[192];
                    snprintf(tag, sizeof(tag),
                             "<w:rFonts w:ascii=\"%s\" w:hAnsi=\"%s\"/>"
                             "<w:sz w:val=\"%d\"/><w:szCs w:val=\"%d\"/>",
                             wp_font_docx_name(f.family), wp_font_docx_name(f.family),
                             pt * 2, pt * 2);
                    buf_adds(&body, tag);
                }
                if (bits & ATTR_BIT_BOLD) {
                    buf_adds(&body, "<w:b/>");
                }
                if (bits & ATTR_BIT_ITALIC) {
                    buf_adds(&body, "<w:i/>");
                }
                if (bits & ATTR_BIT_UNDERLINE) {
                    buf_adds(&body, "<w:u w:val=\"single\"/>");
                }
                buf_adds(&body, "</w:rPr><w:t xml:space=\"preserve\">");
                xml_escape_into(&body, text.p, text.n);
                buf_adds(&body, "</w:t></w:r>");
            }
            buf_free(&text);
        }
        i += n;
    }
    if (para_open) {
        buf_adds(&body, "</w:p>");
    }
    buf_adds(&body, "</w:body></w:document>");

    if (zip_add(&zip, ents, &nent, "[Content_Types].xml", ct, strlen(ct)) == 0 &&
        zip_add(&zip, ents, &nent, "_rels/.rels", rels, strlen(rels)) == 0 &&
        zip_add(&zip, ents, &nent, "word/_rels/document.xml.rels", docrels, strlen(docrels)) == 0 &&
        zip_add(&zip, ents, &nent, "word/document.xml", body.p, body.n) == 0 &&
        zip_finish(&zip, ents, nent) == 0) {
        rc = write_all(path, zip.p, zip.n);
    }
    buf_free(&body);
    buf_free(&zip);
    return rc;
}

static void xml_unescape_insert(Doc *d, const char *s, size_t n)
{
    Buf t = {0};
    size_t i = 0;
    while (i < n) {
        if (s[i] == '&') {
            if (i + 5 <= n && !memcmp(s + i, "&amp;", 5)) {
                buf_adds(&t, "&");
                i += 5;
            } else if (i + 4 <= n && !memcmp(s + i, "&lt;", 4)) {
                buf_adds(&t, "<");
                i += 4;
            } else if (i + 4 <= n && !memcmp(s + i, "&gt;", 4)) {
                buf_adds(&t, ">");
                i += 4;
            } else if (i + 6 <= n && !memcmp(s + i, "&quot;", 6)) {
                buf_adds(&t, "\"");
                i += 6;
            } else if (i + 6 <= n && !memcmp(s + i, "&apos;", 6)) {
                buf_adds(&t, "'");
                i += 6;
            } else {
                buf_add(&t, s + i, 1);
                i++;
            }
        } else {
            buf_add(&t, s + i, 1);
            i++;
        }
    }
    if (t.n) {
        doc_insert_utf8(d, t.p);
    }
    buf_free(&t);
}

static int load_docx_xml(Doc *d, const char *xml, size_t n)
{
    size_t i = 0;
    int first_p = 1;
    int bold = 0, und = 0, italic = 0;
    int run_b = 0, run_u = 0, run_i = 0;
    uint8_t run_fam = WP_FONT_COURIER;
    uint8_t run_pt = WP_FONT_DEFAULT_PT;
    uint8_t cur_fam = WP_FONT_COURIER;
    uint8_t cur_pt = WP_FONT_DEFAULT_PT;
    int run_has_font = 0;
    doc_clear(d);
    while (i < n) {
        if (i + 4 <= n && !memcmp(xml + i, "<w:p", 4) &&
            (xml[i + 4] == '>' || xml[i + 4] == ' ' || xml[i + 4] == '/')) {
            if (!first_p) {
                if (bold) {
                    doc_toggle_attr(d, WP_ATTR_BOLD);
                    bold = 0;
                }
                if (und) {
                    doc_toggle_attr(d, WP_ATTR_UNDERLINE);
                    und = 0;
                }
                if (italic) {
                    doc_toggle_attr(d, WP_ATTR_ITALIC);
                    italic = 0;
                }
                doc_insert_hard_return(d);
            }
            first_p = 0;
            {
                size_t j = i;
                int centered = 0;
                int fullj = 0;
                int rightj = 0;
                while (j + 5 < n && memcmp(xml + j, "<w:r", 4) && memcmp(xml + j, "</w:p", 5)) {
                    if (j + 5 <= n && !memcmp(xml + j, "<w:jc", 5)) {
                        size_t k = j;
                        while (k < n && xml[k] != '>') {
                            if (k + 6 <= n && !memcmp(xml + k, "center", 6)) {
                                centered = 1;
                                break;
                            }
                            if (k + 4 <= n && !memcmp(xml + k, "both", 4)) {
                                fullj = 1;
                                break;
                            }
                            if (k + 5 <= n && !memcmp(xml + k, "right", 5)) {
                                rightj = 1;
                                break;
                            }
                            k++;
                        }
                    }
                    j++;
                }
                if (centered) {
                    doc_insert_code(d, WP_CENTER);
                } else if (fullj && doc_just_at(d, d->cursor) != WP_JUST_FULL) {
                    doc_insert_just(d, WP_JUST_FULL);
                } else if (rightj && doc_just_at(d, d->cursor) != WP_JUST_RIGHT) {
                    doc_insert_just(d, WP_JUST_RIGHT);
                }
            }
            i += 4;
            continue;
        }
        if (i + 9 <= n && !memcmp(xml + i, "<w:rFonts", 9)) {
            size_t k = i;
            while (k < n && xml[k] != '>') {
                if (k + 8 <= n && !memcmp(xml + k, "ascii=\"", 7)) {
                    char name[64];
                    size_t t = 0;
                    k += 7;
                    while (k < n && xml[k] != '"' && t + 1 < sizeof(name)) {
                        name[t++] = xml[k++];
                    }
                    name[t] = 0;
                    run_fam = wp_font_family_from_name(name);
                    run_has_font = 1;
                } else {
                    k++;
                }
            }
            i = k;
            continue;
        }
        if (i + 4 <= n && !memcmp(xml + i, "<w:r", 4) && xml[i + 4] != 'P' && xml[i + 4] != 's' &&
            xml[i + 4] != 'F') {
            run_b = run_u = run_i = 0;
            run_fam = WP_FONT_COURIER;
            run_pt = WP_FONT_DEFAULT_PT;
            run_has_font = 0;
            i += 4;
            continue;
        }
        if (i + 5 <= n && !memcmp(xml + i, "<w:sz", 5) && (xml[i + 5] == ' ' || xml[i + 5] == '/')) {
            size_t k = i;
            while (k + 6 <= n && memcmp(xml + k, "val=\"", 5)) {
                if (xml[k] == '>') {
                    break;
                }
                k++;
            }
            if (k + 6 <= n && !memcmp(xml + k, "val=\"", 5)) {
                unsigned half = 0;
                k += 5;
                while (k < n && xml[k] >= '0' && xml[k] <= '9') {
                    half = half * 10 + (unsigned)(xml[k++] - '0');
                }
                if (half >= 2) {
                    run_pt = (uint8_t)(half / 2);
                    if (!run_pt) {
                        run_pt = WP_FONT_DEFAULT_PT;
                    }
                    run_has_font = 1;
                }
            }
            i += 5;
            continue;
        }
        if (i + 4 <= n && !memcmp(xml + i, "<w:b", 4) && xml[i + 4] != 'C' && xml[i + 4] != 'i') {
            run_b = 1;
            i += 4;
            continue;
        }
        if (i + 4 <= n && !memcmp(xml + i, "<w:i", 4) && xml[i + 4] != 'n') {
            run_i = 1;
            i += 4;
            continue;
        }
        if (i + 4 <= n && !memcmp(xml + i, "<w:u", 4)) {
            run_u = 1;
            i += 4;
            continue;
        }
        if (i + 4 <= n && !memcmp(xml + i, "<w:t", 4)) {
            size_t start;
            while (i < n && xml[i] != '>') {
                i++;
            }
            if (i < n) {
                i++;
            }
            start = i;
            while (i + 6 <= n && memcmp(xml + i, "</w:t>", 6)) {
                i++;
            }
            if (run_b && !bold) {
                doc_toggle_attr(d, WP_ATTR_BOLD);
                bold = 1;
            }
            if (!run_b && bold) {
                doc_toggle_attr(d, WP_ATTR_BOLD);
                bold = 0;
            }
            if (run_u && !und) {
                doc_toggle_attr(d, WP_ATTR_UNDERLINE);
                und = 1;
            }
            if (!run_u && und) {
                doc_toggle_attr(d, WP_ATTR_UNDERLINE);
                und = 0;
            }
            if (run_i && !italic) {
                doc_toggle_attr(d, WP_ATTR_ITALIC);
                italic = 1;
            }
            if (!run_i && italic) {
                doc_toggle_attr(d, WP_ATTR_ITALIC);
                italic = 0;
            }
            if (run_has_font && (run_fam != cur_fam || run_pt != cur_pt)) {
                doc_insert_font(d, run_fam, run_pt);
                cur_fam = run_fam;
                cur_pt = run_pt;
            }
            xml_unescape_insert(d, xml + start, i - start);
            if (i + 6 <= n) {
                i += 6;
            }
            continue;
        }
        i++;
    }
    d->cursor = 0;
    d->dirty = 0;
    return 0;
}

static int pdf_esc(Buf *b, const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\\' || c == '(' || c == ')') {
            char e[2] = {'\\', (char)c};
            if (buf_add(b, e, 2) != 0) {
                return -1;
            }
        } else if (c >= 32 && c < 127) {
            if (buf_add(b, &s[i], 1) != 0) {
                return -1;
            }
        } else if (c == '\t') {
            if (buf_adds(b, "    ") != 0) {
                return -1;
            }
        } else {
            char oct[8];
            snprintf(oct, sizeof(oct), "\\%03o", c);
            if (buf_adds(b, oct) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static int range_has_code(const Doc *d, size_t lo, size_t hi, uint8_t code)
{
    size_t i = lo;
    while (i < hi && i < d->len) {
        if (d->data[i] == code) {
            return 1;
        }
        i += doc_unit_len(d, i);
    }
    return 0;
}

static int pdf_begin_page(Buf **pages, Buf **gfx, int *npages, int *cap, Buf **cur, Buf **gcur)
{
    if (*npages >= *cap) {
        int ncap = *cap ? *cap * 2 : 4;
        Buf *p = realloc(*pages, (size_t)ncap * sizeof(Buf));
        Buf *g = realloc(*gfx, (size_t)ncap * sizeof(Buf));
        if (!p || !g) {
            return -1;
        }
        *pages = p;
        *gfx = g;
        *cap = ncap;
    }
    memset(&(*pages)[*npages], 0, sizeof(Buf));
    memset(&(*gfx)[*npages], 0, sizeof(Buf));
    if (buf_adds(&(*pages)[*npages], "q\n70 64 476 672 re W n\nBT\n") != 0 ||
        buf_adds(&(*gfx)[*npages], "0.6 w\n") != 0) {
        return -1;
    }
    *cur = &(*pages)[*npages];
    *gcur = &(*gfx)[*npages];
    (*npages)++;
    return 0;
}

static int pdf_flush_run(Buf *cur, Buf *run, int font_id, int pt, double x, int y)
{
    char tm[80];
    if (!run->n) {
        return 0;
    }
    if (pt < 4) {
        pt = 4;
    }
    snprintf(tm, sizeof(tm), "1 0 0 1 %.2f %d Tm\n/F%d %d Tf (", x, y, font_id, pt);
    if (buf_adds(cur, tm) != 0 ||
        pdf_esc(cur, run->p, run->n) != 0 ||
        buf_adds(cur, ") Tj\n") != 0) {
        return -1;
    }
    run->n = 0;
    if (run->p) {
        run->p[0] = 0;
    }
    return 0;
}

static int pdf_und_seg(Buf *gfx, double x0, double x1, int y)
{
    char tmp[80];
    if (x1 <= x0) {
        return 0;
    }
    snprintf(tmp, sizeof(tmp), "%.2f %d m %.2f %d l S\n", x0, y - 1, x1, y - 1);
    return buf_adds(gfx, tmp);
}

#define PDF_MARGIN 72.0
#define PDF_TEXT_W 468.0
#define PDF_RIGHT (PDF_MARGIN + PDF_TEXT_W)
#define PDF_Y_TOP 720
#define PDF_Y_MIN 86
#define PDF_LINE_STEP 14

static double pdf_char_w(const DocFont *f, unsigned char ch, int pt)
{
    int thou = wp_char_width_thou(f->family, ch);
    if (ch == 0x97 && f->family != WP_FONT_COURIER) {
        thou = 1000;
    } else if (ch == 0x96 && f->family != WP_FONT_COURIER) {
        thou = 500;
    }
    return (double)thou * (double)pt / 1000.0;
}

static unsigned char pdf_winansi(uint32_t cp)
{
    if (cp >= 32 && cp <= 126) {
        return (unsigned char)cp;
    }
    if (cp == 0x2014) {
        return 0x97;
    }
    if (cp == 0x2013) {
        return 0x96;
    }
    if (cp == 0x2018 || cp == 0x2019 || cp == 0x00B4) {
        return '\'';
    }
    if (cp == 0x201C || cp == 0x201D) {
        return '"';
    }
    if (cp == 0x00A0) {
        return ' ';
    }
    if (cp <= 0xFF) {
        return (unsigned char)cp;
    }
    return '?';
}

static int pdf_map_unit(const Doc *d, size_t i, unsigned char *ch, int *repeat)
{
    uint8_t b;
    *repeat = 1;
    if (i >= d->len || !doc_is_visible(d, i)) {
        return 0;
    }
    b = d->data[i];
    if (b == WP_HARD_EOL || b == WP_SOFT_EOL || b == WP_HARD_PAGE) {
        return 0;
    }
    if (b == WP_TAB || b == '\t') {
        *ch = ' ';
        *repeat = 4;
        return 1;
    }
    if (b == WP_END_FIELD) {
        *ch = '*';
        return 1;
    }
    if (b == WP_UTF8) {
        size_t n = doc_unit_len(d, i);
        uint32_t cp = '?';
        if (n >= 3) {
            wp_utf8_decode(d->data + i + 2, n - 2, &cp);
        }
        *ch = pdf_winansi(cp);
        return 1;
    }
    if (b == WP_EXT_CHAR) {
        size_t n = doc_unit_len(d, i);
        *ch = (n >= 2 && d->data[i + 1] >= 32) ? d->data[i + 1] : '?';
        return 1;
    }
    if (b >= 32 && b < 127) {
        *ch = b;
        return 1;
    }
    return 0;
}

static double pdf_unit_w(const Doc *d, size_t i)
{
    DocFont f;
    int ept;
    unsigned char ch;
    int repeat = 1;
    if (!pdf_map_unit(d, i, &ch, &repeat)) {
        return 0;
    }
    doc_font_at(d, i, &f);
    ept = wp_effective_pt(&f);
    if (ept < 4) {
        ept = 4;
    }
    return pdf_char_w(&f, ch, ept) * (double)repeat;
}

static size_t pdf_trim_end(const Doc *d, size_t lo, size_t hi)
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
            last = i + (n ? n : 1);
            seen = 1;
        }
        i += n ? n : 1;
    }
    return seen ? last : lo;
}

static double pdf_range_w(const Doc *d, size_t lo, size_t hi)
{
    double w = 0;
    size_t i = lo;
    while (i < hi && i < d->len) {
        w += pdf_unit_w(d, i);
        i += doc_unit_len(d, i);
    }
    return w;
}

static size_t pdf_wrap_end(const Doc *d, size_t lo, size_t lim, int wordwrap)
{
    size_t i = lo;
    size_t last_sp_end = lo;
    int have_sp = 0;
    double x = 0;
    while (i < lim && i < d->len) {
        uint8_t b = d->data[i];
        size_t n = doc_unit_len(d, i);
        double w;
        if (!n) {
            break;
        }
        if (b == WP_HARD_EOL || b == WP_SOFT_EOL || b == WP_HARD_PAGE) {
            return i;
        }
        if (!doc_is_visible(d, i)) {
            i += n;
            continue;
        }
        w = pdf_unit_w(d, i);
        if (x + w > PDF_TEXT_W && x > 0) {
            if (wordwrap && have_sp && last_sp_end > lo) {
                return last_sp_end;
            }
            return i;
        }
        if (doc_is_space(d, i) || b == WP_TAB || b == '\t') {
            last_sp_end = i + n;
            have_sp = 1;
        }
        x += w;
        i += n;
    }
    return i;
}

static int pdf_space_count(const Doc *d, size_t lo, size_t hi)
{
    size_t i = lo;
    int n = 0;
    while (i < hi && i < d->len) {
        uint8_t b = d->data[i];
        if (b == WP_HARD_EOL || b == WP_SOFT_EOL || b == WP_HARD_PAGE) {
            break;
        }
        if (doc_is_space(d, i)) {
            n++;
        }
        i += doc_unit_len(d, i);
    }
    return n;
}

static int pdf_emit_mapped(Buf *run, const Doc *d, size_t i)
{
    unsigned char ch;
    int repeat = 1;
    int k;
    if (!pdf_map_unit(d, i, &ch, &repeat)) {
        return 0;
    }
    for (k = 0; k < repeat; k++) {
        if (buf_add(run, (const char *)&ch, 1) != 0) {
            return -1;
        }
    }
    return 0;
}

static int pdf_emit_styled_range(Buf *cur, Buf *gfx, const Doc *d, size_t lo, size_t hi, int y,
                                 int center, uint8_t just, int last_of_para)
{
    size_t i;
    Buf run = {0};
    DocFont style;
    int font_id = 1;
    int pt = 10;
    int bold = 0, italic = 0;
    double width;
    double x = PDF_MARGIN;
    double run_x;
    double und_x0 = -1;
    int nspaces = 0;
    int space_i = 0;
    int extra_m = 0;
    int do_full = 0;

    hi = pdf_trim_end(d, lo, hi);
    width = pdf_range_w(d, lo, hi);
    nspaces = pdf_space_count(d, lo, hi);

    if ((center || just == WP_JUST_CENTER) && width > 0 && width < PDF_TEXT_W) {
        x = PDF_MARGIN + (PDF_TEXT_W - width) / 2.0;
    } else if (!center && just == WP_JUST_RIGHT && width > 0 && width < PDF_TEXT_W) {
        x = PDF_MARGIN + (PDF_TEXT_W - width);
    } else if (!center && just == WP_JUST_FULL && !last_of_para && nspaces > 0 &&
               width > 0 && width < PDF_TEXT_W) {
        extra_m = (int)((PDF_TEXT_W - width) * 1000.0 + 0.5);
        do_full = extra_m > 0;
    }
    run_x = x;
    memset(&style, 0, sizeof(style));
    style.family = WP_FONT_COURIER;
    style.size_pt = WP_FONT_DEFAULT_PT;
    i = lo;
    while (i < hi && i < d->len) {
        size_t n = doc_unit_len(d, i);
        uint8_t b;
        DocFont f;
        int want_b, want_i, want_u, fid, ept;
        double w;
        if (!n) {
            break;
        }
        b = d->data[i];
        if (b == WP_HARD_EOL || b == WP_SOFT_EOL || b == WP_HARD_PAGE || !doc_is_visible(d, i)) {
            i += n;
            continue;
        }
        doc_font_at(d, i, &f);
        want_b = (f.attrs & ATTR_BIT_BOLD) != 0;
        want_i = (f.attrs & ATTR_BIT_ITALIC) != 0;
        want_u = (f.attrs & ATTR_BIT_UNDERLINE) != 0;
        ept = wp_effective_pt(&f);
        if (ept < 4) {
            ept = 4;
        }
        w = pdf_unit_w(d, i);
        if (doc_is_space(d, i)) {
            double pad = 0;
            if (pdf_flush_run(cur, &run, font_id, pt, run_x, y) != 0) {
                buf_free(&run);
                return -1;
            }
            if (do_full && space_i < nspaces) {
                pad = (double)(extra_m / nspaces + (space_i < extra_m % nspaces ? 1 : 0)) /
                      1000.0;
                space_i++;
            }
            if (want_u) {
                if (und_x0 < 0) {
                    und_x0 = x;
                }
            } else if (und_x0 >= 0) {
                if (pdf_und_seg(gfx, und_x0, x, y) != 0) {
                    buf_free(&run);
                    return -1;
                }
                und_x0 = -1;
            }
            x += w + pad;
            run_x = x;
            i += n;
            continue;
        }
        fid = wp_pdf_font_id(f.family, want_b, want_i);
        if (fid != font_id || ept != pt || want_b != bold || want_i != italic ||
            f.family != style.family || !run.n) {
            if (pdf_flush_run(cur, &run, font_id, pt, run_x, y) != 0) {
                buf_free(&run);
                return -1;
            }
            run_x = x;
            font_id = fid;
            pt = ept;
            bold = want_b;
            italic = want_i;
            style = f;
        }
        if (want_u) {
            if (und_x0 < 0) {
                und_x0 = x;
            }
        } else if (und_x0 >= 0) {
            if (pdf_und_seg(gfx, und_x0, x, y) != 0) {
                buf_free(&run);
                return -1;
            }
            und_x0 = -1;
        }
        if (pdf_emit_mapped(&run, d, i) != 0) {
            buf_free(&run);
            return -1;
        }
        x += w;
        i += n;
    }
    if (pdf_flush_run(cur, &run, font_id, pt, run_x, y) != 0) {
        buf_free(&run);
        return -1;
    }
    if (und_x0 >= 0 && pdf_und_seg(gfx, und_x0, x, y) != 0) {
        buf_free(&run);
        return -1;
    }
    buf_free(&run);
    return 0;
}

static int save_pdf_text(const Doc *d, const char *path)
{
    Buf *pages = NULL;
    Buf *gfx = NULL;
    Buf *cur = NULL;
    Buf *gcur = NULL;
    Buf pdf = {0};
    Buf kids = {0};
    size_t *xref = NULL;
    int npages = 0;
    int page_cap = 0;
    int y = PDF_Y_TOP;
    int last_break = 1;
    int wordwrap = io_wrap_is_word();
    int nobj;
    int i;
    size_t pos = 0;
    int rc = -1;
    char tmp[256];

    if (pdf_begin_page(&pages, &gfx, &npages, &page_cap, &cur, &gcur) != 0) {
        goto done;
    }
    while (pos < d->len) {
        uint8_t b = d->data[pos];
        size_t n = doc_unit_len(d, pos);
        size_t lim;
        int center = 0;
        uint8_t just = doc_just_at(d, pos);
        if (!n) {
            break;
        }
        if (b == WP_HARD_PAGE) {
            if (pdf_begin_page(&pages, &gfx, &npages, &page_cap, &cur, &gcur) != 0) {
                goto done;
            }
            y = PDF_Y_TOP;
            last_break = 1;
            pos += n;
            continue;
        }
        if (b == WP_HARD_EOL || b == WP_SOFT_EOL) {
            if (last_break) {
                y -= PDF_LINE_STEP;
                if (y < PDF_Y_MIN) {
                    if (pdf_begin_page(&pages, &gfx, &npages, &page_cap, &cur, &gcur) != 0) {
                        goto done;
                    }
                    y = PDF_Y_TOP;
                }
            }
            last_break = 1;
            pos += n;
            continue;
        }
        lim = pos;
        while (lim < d->len) {
            uint8_t c = d->data[lim];
            if (c == WP_CENTER) {
                center = 1;
            }
            if (c == WP_HARD_EOL || c == WP_SOFT_EOL || c == WP_HARD_PAGE) {
                break;
            }
            lim += doc_unit_len(d, lim);
        }
        while (pos < lim) {
            size_t end = pdf_wrap_end(d, pos, lim, wordwrap);
            int line_h = PDF_LINE_STEP;
            size_t k;
            if (end <= pos) {
                size_t step = doc_unit_len(d, pos);
                pos += step ? step : 1;
                continue;
            }
            for (k = pos; k < end && k < d->len; k += doc_unit_len(d, k)) {
                if (doc_is_visible(d, k)) {
                    DocFont f;
                    int ept;
                    doc_font_at(d, k, &f);
                    ept = wp_effective_pt(&f);
                    if (ept + 2 > line_h) {
                        line_h = ept + 2;
                    }
                }
            }
            if (y - line_h < PDF_Y_MIN) {
                if (pdf_begin_page(&pages, &gfx, &npages, &page_cap, &cur, &gcur) != 0) {
                    goto done;
                }
                y = PDF_Y_TOP;
            }
            if (pdf_emit_styled_range(cur, gcur, d, pos, end, y, center, just, end >= lim) != 0) {
                goto done;
            }
            y -= line_h;
            last_break = 0;
            pos = end;
        }
    }
    for (i = 0; i < npages; i++) {
        buf_adds(&pages[i], "ET\n");
        buf_add(&pages[i], gfx[i].p, gfx[i].n);
        buf_adds(&pages[i], "Q\n");
    }

    nobj = 14 + npages * 2;
    xref = calloc((size_t)nobj + 1, sizeof(size_t));
    if (!xref) {
        goto done;
    }
    buf_adds(&kids, "[");
    for (i = 0; i < npages; i++) {
        snprintf(tmp, sizeof(tmp), "%s%d 0 R", i ? " " : "", 15 + i * 2);
        buf_adds(&kids, tmp);
    }
    buf_adds(&kids, "]");

    buf_adds(&pdf, "%PDF-1.4\n");
    xref[1] = pdf.n;
    buf_adds(&pdf, "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n");
    xref[2] = pdf.n;
    buf_adds(&pdf, "2 0 obj << /Type /Pages /Kids ");
    buf_add(&pdf, kids.p ? kids.p : "[]", kids.p ? kids.n : 2);
    snprintf(tmp, sizeof(tmp), " /Count %d >> endobj\n", npages);
    buf_adds(&pdf, tmp);
    {
        static const char *const pdf_fonts[12] = {
            "Courier", "Courier-Bold", "Courier-Oblique", "Courier-BoldOblique",
            "Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic",
            "Helvetica", "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique"
        };
        int fi;
        for (fi = 0; fi < 12; fi++) {
            xref[3 + fi] = pdf.n;
            snprintf(tmp, sizeof(tmp),
                     "%d 0 obj << /Type /Font /Subtype /Type1 /BaseFont /%s "
                     "/Encoding /WinAnsiEncoding >> endobj\n",
                     3 + fi, pdf_fonts[fi]);
            buf_adds(&pdf, tmp);
        }
    }
    for (i = 0; i < npages; i++) {
        int page_obj = 15 + i * 2;
        int cont_obj = page_obj + 1;
        xref[page_obj] = pdf.n;
        snprintf(tmp, sizeof(tmp),
                 "%d 0 obj << /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
                 "/Contents %d 0 R ",
                 page_obj, cont_obj);
        buf_adds(&pdf, tmp);
        buf_adds(&pdf,
                 "/Resources << /Font << "
                 "/F1 3 0 R /F2 4 0 R /F3 5 0 R /F4 6 0 R "
                 "/F5 7 0 R /F6 8 0 R /F7 9 0 R /F8 10 0 R "
                 "/F9 11 0 R /F10 12 0 R /F11 13 0 R /F12 14 0 R "
                 ">> >> >> endobj\n");
        xref[cont_obj] = pdf.n;
        snprintf(tmp, sizeof(tmp), "%d 0 obj << /Length %zu >> stream\n",
                 cont_obj, pages[i].n);
        buf_adds(&pdf, tmp);
        buf_add(&pdf, pages[i].p, pages[i].n);
        buf_adds(&pdf, "endstream endobj\n");
    }
    {
        size_t startxref = pdf.n;
        int o;
        snprintf(tmp, sizeof(tmp), "xref\n0 %d\n0000000000 65535 f \n", nobj + 1);
        buf_adds(&pdf, tmp);
        for (o = 1; o <= nobj; o++) {
            snprintf(tmp, sizeof(tmp), "%010zu 00000 n \n", xref[o]);
            buf_adds(&pdf, tmp);
        }
        snprintf(tmp, sizeof(tmp),
                 "trailer << /Size %d /Root 1 0 R >>\nstartxref\n%zu\n%%%%EOF\n",
                 nobj + 1, startxref);
        buf_adds(&pdf, tmp);
    }
    rc = write_all(path, pdf.p, pdf.n);
    if (rc == 0) {
        rc = io_pdf_append_wpd(path, d);
    }

done:
    for (i = 0; i < npages; i++) {
        if (pages) {
            buf_free(&pages[i]);
        }
        if (gfx) {
            buf_free(&gfx[i]);
        }
    }
    free(pages);
    free(gfx);
    free(xref);
    buf_free(&pdf);
    buf_free(&kids);
    return rc;
}

int io_pdf_append_wpd(const char *path, const Doc *d)
{
    size_t n = 0;
    uint8_t *buf = wpd_encode_native(d, &n);
    FILE *f;
    int rc = -1;
    if (!buf) {
        return -1;
    }
    f = fopen(path, "ab");
    if (f) {
        if (fprintf(f, "\n%%%%WP51-WPD %zu\n", n) > 0 && fwrite(buf, 1, n, f) == n) {
            rc = 0;
        }
        fclose(f);
    }
    free(buf);
    return rc;
}

static int load_pdf(Doc *d, const uint8_t *buf, size_t len)
{
    const char *mark = "%%WP51-WPD ";
    size_t ml = strlen(mark);
    size_t i;
    ssize_t last = -1;
    for (i = 0; i + ml < len; i++) {
        if (memcmp(buf + i, mark, ml) == 0) {
            last = (ssize_t)i;
        }
    }
    if (last < 0) {
        return -1;
    }
    {
        size_t sz = 0;
        const uint8_t *p = buf + (size_t)last + ml;
        const uint8_t *end = buf + len;
        while (p < end && *p >= '0' && *p <= '9') {
            sz = sz * 10 + (size_t)(*p - '0');
            p++;
        }
        if (p < end && *p == '\n') {
            p++;
        }
        if (sz == 0 || p + sz > end) {
            return -1;
        }
        return wpd_decode(d, p, sz);
    }
}

int io_save(const Doc *d, const char *path, char *resolved, size_t resolved_sz)
{
    char use[1200];
    int fmt;
    int rc;
    if (resolve_path(path, use, sizeof(use)) != 0) {
        return -1;
    }
    fmt = io_format_from_path(use);
    switch (fmt) {
    case IO_FMT_MD:
        rc = save_md(d, use);
        break;
    case IO_FMT_DOCX:
        rc = save_docx(d, use);
        break;
    case IO_FMT_PDF:
        rc = save_pdf_text(d, use);
        break;
    default:
        rc = wpd_save(d, use);
        break;
    }
    if (rc == 0 && resolved && resolved_sz) {
        snprintf(resolved, resolved_sz, "%s", use);
    }
    return rc;
}

int io_load(Doc *d, const char *path)
{
    size_t n = 0;
    uint8_t *buf;
    int fmt;
    int rc = -1;
    if (!d || !path) {
        return -1;
    }
    buf = read_all(path, &n);
    if (!buf) {
        return -1;
    }
    fmt = io_format_from_path(path);
    if (fmt == IO_FMT_PDF || (n >= 4 && !memcmp(buf, "%PDF", 4))) {
        rc = load_pdf(d, buf, n);
    } else if (fmt == IO_FMT_DOCX || (n >= 2 && buf[0] == 'P' && buf[1] == 'K')) {
        Buf xml = {0};
        if (zip_extract(buf, n, "word/document.xml", &xml) == 0) {
            rc = load_docx_xml(d, xml.p, xml.n);
        }
        buf_free(&xml);
    } else if (fmt == IO_FMT_MD) {
        rc = load_md(d, (char *)buf, n);
    } else {
        rc = wpd_decode(d, buf, n);
        if (rc != 0 && fmt < 0) {
            if (n >= 4 && !memcmp(buf, "%PDF", 4)) {
                rc = load_pdf(d, buf, n);
            } else if (n >= 2 && buf[0] == 'P' && buf[1] == 'K') {
                Buf xml = {0};
                if (zip_extract(buf, n, "word/document.xml", &xml) == 0) {
                    rc = load_docx_xml(d, xml.p, xml.n);
                }
                buf_free(&xml);
            }
        }
    }
    free(buf);
    if (rc == 0) {
        snprintf(d->path, sizeof(d->path), "%s", path);
        d->dirty = 0;
        d->cursor = 0;
    }
    return rc;
}
