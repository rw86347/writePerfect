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
