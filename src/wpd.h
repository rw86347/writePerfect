#ifndef WPD_H
#define WPD_H

#include "doc.h"

int wpd_load(Doc *d, const char *path);
int wpd_save(const Doc *d, const char *path);

/* Official WP 5.1 bytes (old 5.1 can open). Returns malloc'd buffer. */
uint8_t *wpd_encode(const Doc *d, size_t *out_len);
/* In-memory stream with our C6/C7/C8 extras (PDF sidecar). */
uint8_t *wpd_encode_native(const Doc *d, size_t *out_len);
int wpd_decode(Doc *d, const uint8_t *buf, size_t len);

#endif
