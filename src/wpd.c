#include "wpd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

uint8_t *wpd_encode(const Doc *d, size_t *out_len)
{
    size_t len = 16 + d->len;
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
    if (d->len) {
        memcpy(buf + 16, d->data, d->len);
    }
    *out_len = len;
    return buf;
}

int wpd_decode(Doc *d, const uint8_t *buf, size_t len)
{
    if (len < 16 || buf[0] != 0xFF || buf[1] != 'W' || buf[2] != 'P' || buf[3] != 'C') {
        return -1;
    }
    uint32_t docptr = get_u32(buf + 4);
    if (docptr < 16 || docptr > len) {
        return -1;
    }
    /* Only accept WordPerfect documents (product 1, type 0x0A). */
    if (buf[8] != WPD_PRODUCT || buf[9] != WPD_TYPE_DOC) {
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
