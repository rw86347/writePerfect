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
