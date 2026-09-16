#include "wpd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int official_append(uint8_t **p, size_t *n, size_t *cap, const uint8_t *src, size_t add)
{
    uint8_t *q;
    if (!add) {
        return 0;
    }
    if (*n + add > *cap) {
        size_t c = *cap ? *cap : 256;
        while (c < *n + add) {
            c *= 2;
        }
        q = realloc(*p, c);
        if (!q) {
            return -1;
        }
        *p = q;
        *cap = c;
    }
    memcpy(*p + *n, src, add);
    *n += add;
    return 0;
}

/* Bytes old WordPerfect 5.1 can read. Private C6/C7/C8 stay in memory only. */
static int official_body(const Doc *d, uint8_t **out, size_t *out_n)
{
    uint8_t *body = NULL;
    size_t n = 0, cap = 0, i = 0;
    *out = NULL;
    *out_n = 0;
    while (i < d->len) {
        size_t u = doc_unit_len(d, i);
        uint8_t b;
        if (!u) {
            break;
        }
        b = d->data[i];
        if ((b == WP_ATTR_ON || b == WP_ATTR_OFF) && u >= 2) {
            uint8_t trip[3];
            trip[0] = b;
            trip[1] = d->data[i + 1];
            trip[2] = b;
            if (official_append(&body, &n, &cap, trip, 3) != 0) {
                free(body);
                return -1;
            }
        } else if (b == WP_UTF8) {
            uint32_t cp = doc_display_cp(d, i);
            uint8_t c;
            if (cp >= 32 && cp < 127) {
                c = (uint8_t)cp;
            } else if (cp == 0x2014 || cp == 0x2013) {
                c = '-';
            } else {
                c = '?';
            }
            if (official_append(&body, &n, &cap, &c, 1) != 0) {
                free(body);
                return -1;
            }
        } else if (b == WP_FONT && u >= 3 && d->data[i + 1] <= WP_FONT_HELVETICA) {
            /* App-only C7 family size; 5.1 would desync. */
        } else if (b == WP_JUST && u >= 2 && d->data[i + 1] <= WP_JUST_FULL) {
            /* App-only C8 mode; 5.1 would desync. */
        } else if (official_append(&body, &n, &cap, d->data + i, u) != 0) {
            free(body);
            return -1;
        }
        i += u;
    }
    *out = body ? body : calloc(1, 1);
    *out_n = n;
    return *out ? 0 : -1;
}

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint8_t *wpd_pack(const uint8_t *body, size_t body_n, size_t *out_len)
{
    size_t len = 16 + body_n;
    uint8_t *buf = malloc(len);
    if (!buf) {
        return NULL;
    }
    buf[0] = 0xFF;
    buf[1] = 'W';
    buf[2] = 'P';
    buf[3] = 'C';
    put_u32(buf + 4, 16);
    buf[8] = WPD_PRODUCT;
    buf[9] = WPD_TYPE_DOC;
    buf[10] = WPD_MAJOR;
    buf[11] = WPD_MINOR;
    put_u16(buf + 12, 0);
    put_u16(buf + 14, 0);
    if (body_n) {
        memcpy(buf + 16, body, body_n);
    }
    *out_len = len;
    return buf;
}

uint8_t *wpd_encode_native(const Doc *d, size_t *out_len)
{
    if (!d || !out_len) {
        return NULL;
    }
    return wpd_pack(d->data, d->len, out_len);
}

uint8_t *wpd_encode(const Doc *d, size_t *out_len)
{
    uint8_t *body = NULL;
    size_t body_n = 0;
    uint8_t *buf;
    if (!d || official_body(d, &body, &body_n) != 0) {
        return NULL;
    }
    buf = wpd_pack(body, body_n, out_len);
    free(body);
    return buf;
}

int wpd_decode(Doc *d, const uint8_t *buf, size_t len)
{
    if (len < 16 || buf[0] != 0xFF || buf[1] != 'W' || buf[2] != 'P' || buf[3] != 'C') {
        return -1;
    }
    uint32_t docptr = get_u32(buf + 4);
    uint16_t enc;
    if (docptr < 16 || docptr > len) {
        return -1;
    }
    /* DOS/Windows 5.x document, or Macintosh WP 2–4 document. */
    if (buf[8] != WPD_PRODUCT ||
        (buf[9] != WPD_TYPE_DOC && buf[9] != WPD_TYPE_MAC_DOC)) {
        return -1;
    }
    enc = (uint16_t)buf[12] | ((uint16_t)buf[13] << 8);
    if (enc) {
        return -1;
    }

    doc_clear(d);
    const uint8_t *src = buf + docptr;
    size_t n = len - docptr;
    if (doc_reserve(d, n) != 0) {
        return -1;
    }
    memcpy(d->data, src, n);
    d->len = n;
    d->cursor = 0;
    d->dirty = 0;
    return 0;
}

int wpd_load(Doc *d, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long sz = ftell(f);
    if (sz < 16) {
        fclose(f);
        return -1;
    }
    rewind(f);
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return -1;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    int rc = wpd_decode(d, buf, (size_t)sz);
    free(buf);
    if (rc == 0) {
        snprintf(d->path, sizeof(d->path), "%s", path);
        d->dirty = 0;
    }
    return rc;
}

int wpd_save(const Doc *d, const char *path)
{
    size_t n = 0;
    uint8_t *buf = wpd_encode(d, &n);
    if (!buf) {
        return -1;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        free(buf);
        return -1;
    }
    size_t w = fwrite(buf, 1, n, f);
    fclose(f);
    free(buf);
    return w == n ? 0 : -1;
}
