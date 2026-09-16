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

#ifndef IOFMT_H
#define IOFMT_H

#include "doc.h"

enum {
    IO_FMT_DOCX = 0,
    IO_FMT_MD,
    IO_FMT_PDF,
    IO_FMT_WPD
};

enum {
    IO_WRAP_WORD = 0,
    IO_WRAP_STRICT80
};

int io_default_format(void);
void io_set_default_format(int fmt);
int io_wrap_mode(void);
void io_set_wrap_mode(int mode);
int io_wrap_is_word(void);

int io_format_from_path(const char *path);
const char *io_format_ext(int fmt);
const char *io_default_filename(void);
int io_path_has_known_ext(const char *path);
int io_path_for_format(const char *path, int fmt, char *out, size_t outsz);

/* Save. If resolved is non-NULL, it receives the path actually written. */
int io_save(const Doc *d, const char *path, char *resolved, size_t resolved_sz);
int io_load(Doc *d, const char *path);

/* After a visual PDF is written, append the editable WP stream. */
int io_pdf_append_wpd(const char *path, const Doc *d);

#endif
