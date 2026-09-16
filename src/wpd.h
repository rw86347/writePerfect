#ifndef WPD_H
#define WPD_H

#include "doc.h"

int wpd_load(Doc *d, const char *path);
int wpd_save(const Doc *d, const char *path);

/* Encode/decode to a memory buffer (tests). Returns malloc'd buffer. */
uint8_t *wpd_encode(const Doc *d, size_t *out_len);
int wpd_decode(Doc *d, const uint8_t *buf, size_t len);

#endif
