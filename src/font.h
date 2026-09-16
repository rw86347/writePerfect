#ifndef FONT_H
#define FONT_H

#include "wp51.h"

#define WP_FONT_DEFAULT_PT 12

typedef struct {
    uint8_t family;
    uint8_t size_pt;
    unsigned attrs;
} DocFont;

unsigned wp_attr_bit(uint8_t attr);
int wp_attr_is_size(uint8_t attr);
uint8_t wp_size_attr_from_bits(unsigned attrs);

const char *wp_font_family_name(uint8_t family);
const char *wp_font_pdf_name(uint8_t family, int bold, int italic);
const char *wp_font_docx_name(uint8_t family);
uint8_t wp_font_family_from_name(const char *name);

const char *wp_attr_on_label(uint8_t attr);
const char *wp_attr_off_label(uint8_t attr);

int wp_effective_pt(const DocFont *f);
int wp_pdf_font_id(uint8_t family, int bold, int italic); /* 1..12 */
int wp_char_width_thou(uint8_t family, unsigned char ch); /* /1000 em */

#endif
