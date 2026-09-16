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

#include "font.h"

#include <ctype.h>
#include <string.h>

static int has_caseless(const char *hay, const char *needle)
{
    size_t n, i;
    if (!hay || !needle || !needle[0]) {
        return 0;
    }
    n = strlen(needle);
    for (i = 0; hay[i]; i++) {
        size_t k;
        for (k = 0; k < n; k++) {
            unsigned char a = (unsigned char)hay[i + k];
            unsigned char b = (unsigned char)needle[k];
            if (!a || tolower(a) != tolower(b)) {
                break;
            }
        }
        if (k == n) {
            return 1;
        }
    }
    return 0;
}

unsigned wp_attr_bit(uint8_t attr)
{
    switch (attr) {
    case WP_ATTR_BOLD:
        return ATTR_BIT_BOLD;
    case WP_ATTR_UNDERLINE:
        return ATTR_BIT_UNDERLINE;
    case WP_ATTR_ITALIC:
        return ATTR_BIT_ITALIC;
    case WP_ATTR_FINE:
        return ATTR_BIT_FINE;
    case WP_ATTR_SMALL:
        return ATTR_BIT_SMALL;
    case WP_ATTR_LARGE:
        return ATTR_BIT_LARGE;
    case WP_ATTR_VRY_LARGE:
        return ATTR_BIT_VRY_LARGE;
    case WP_ATTR_EXT_LARGE:
        return ATTR_BIT_EXT_LARGE;
    default:
        return 0;
    }
}

int wp_attr_is_size(uint8_t attr)
{
    return attr == WP_ATTR_FINE || attr == WP_ATTR_SMALL || attr == WP_ATTR_LARGE ||
           attr == WP_ATTR_VRY_LARGE || attr == WP_ATTR_EXT_LARGE;
}

uint8_t wp_size_attr_from_bits(unsigned attrs)
{
    if (attrs & ATTR_BIT_EXT_LARGE) {
        return WP_ATTR_EXT_LARGE;
    }
    if (attrs & ATTR_BIT_VRY_LARGE) {
        return WP_ATTR_VRY_LARGE;
    }
    if (attrs & ATTR_BIT_LARGE) {
        return WP_ATTR_LARGE;
    }
    if (attrs & ATTR_BIT_SMALL) {
        return WP_ATTR_SMALL;
    }
    if (attrs & ATTR_BIT_FINE) {
        return WP_ATTR_FINE;
    }
    return 0xFF;
}

const char *wp_font_family_name(uint8_t family)
{
    switch (family) {
    case WP_FONT_TIMES:
        return "Times";
    case WP_FONT_HELVETICA:
        return "Helvetica";
    default:
        return "Courier";
    }
}

const char *wp_font_pdf_name(uint8_t family, int bold, int italic)
{
    if (family == WP_FONT_TIMES) {
        if (bold && italic) {
            return "Times-BoldItalic";
        }
        if (bold) {
            return "Times-Bold";
        }
        if (italic) {
            return "Times-Italic";
        }
        return "Times-Roman";
    }
    if (family == WP_FONT_HELVETICA) {
        if (bold && italic) {
            return "Helvetica-BoldOblique";
        }
        if (bold) {
            return "Helvetica-Bold";
        }
        if (italic) {
            return "Helvetica-Oblique";
        }
        return "Helvetica";
    }
    if (bold && italic) {
        return "Courier-BoldOblique";
    }
    if (bold) {
        return "Courier-Bold";
    }
    if (italic) {
        return "Courier-Oblique";
    }
    return "Courier";
}

const char *wp_font_docx_name(uint8_t family)
{
    switch (family) {
    case WP_FONT_TIMES:
        return "Times New Roman";
    case WP_FONT_HELVETICA:
        return "Arial";
    default:
        return "Courier New";
    }
}

uint8_t wp_font_family_from_name(const char *name)
{
    if (!name || !name[0]) {
        return WP_FONT_COURIER;
    }
    if (has_caseless(name, "Times") || has_caseless(name, "Dutch") || has_caseless(name, "Georgia")) {
        return WP_FONT_TIMES;
    }
    if (has_caseless(name, "Helv") || has_caseless(name, "Arial") || has_caseless(name, "Swiss") ||
        has_caseless(name, "Univers") || has_caseless(name, "Liberation Sans")) {
        return WP_FONT_HELVETICA;
    }
    return WP_FONT_COURIER;
}

const char *wp_attr_on_label(uint8_t attr)
{
    switch (attr) {
    case WP_ATTR_BOLD:
        return "BOLD";
    case WP_ATTR_UNDERLINE:
        return "UND";
    case WP_ATTR_ITALIC:
        return "ITALC";
    case WP_ATTR_FINE:
        return "FINE";
    case WP_ATTR_SMALL:
        return "SMALL";
    case WP_ATTR_LARGE:
        return "LARGE";
    case WP_ATTR_VRY_LARGE:
        return "VRY LARGE";
    case WP_ATTR_EXT_LARGE:
        return "EXT LARGE";
    default:
        return NULL;
    }
}

const char *wp_attr_off_label(uint8_t attr)
{
    switch (attr) {
    case WP_ATTR_BOLD:
        return "bold";
    case WP_ATTR_UNDERLINE:
        return "und";
    case WP_ATTR_ITALIC:
        return "italc";
    case WP_ATTR_FINE:
        return "fine";
    case WP_ATTR_SMALL:
        return "small";
    case WP_ATTR_LARGE:
        return "large";
    case WP_ATTR_VRY_LARGE:
        return "vry large";
    case WP_ATTR_EXT_LARGE:
        return "ext large";
    default:
        return NULL;
    }
}

int wp_effective_pt(const DocFont *f)
{
    int pt = f && f->size_pt ? f->size_pt : WP_FONT_DEFAULT_PT;
    unsigned a = f ? f->attrs : 0;
    if (a & ATTR_BIT_EXT_LARGE) {
        return pt * 2;
    }
    if (a & ATTR_BIT_VRY_LARGE) {
        return (pt * 3 + 1) / 2;
    }
    if (a & ATTR_BIT_LARGE) {
        return (pt * 6 + 2) / 5;
    }
    if (a & ATTR_BIT_SMALL) {
        return (pt * 4 + 2) / 5;
    }
    if (a & ATTR_BIT_FINE) {
        return (pt * 3 + 2) / 5;
    }
    return pt;
}

int wp_pdf_font_id(uint8_t family, int bold, int italic)
{
    if (family > WP_FONT_HELVETICA) {
        family = WP_FONT_COURIER;
    }
    return 1 + (int)family * 4 + (bold ? 1 : 0) + (italic ? 2 : 0);
}

/* Adobe Times-Roman and Helvetica widths, ASCII 32-126, thousandths of an em. */
static const uint16_t times_wx[95] = {
    250, 333, 408, 500, 500, 833, 778, 180, 333, 333, 500, 564, 250, 333, 250, 278,
    500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 278, 278, 564, 564, 564, 444,
    921, 722, 667, 667, 722, 611, 556, 722, 722, 333, 389, 722, 611, 889, 722, 722,
    556, 722, 667, 556, 611, 722, 722, 944, 722, 722, 611, 333, 278, 333, 469, 500,
    333, 444, 500, 444, 500, 444, 333, 500, 500, 278, 278, 500, 278, 778, 500, 500,
    500, 500, 333, 389, 278, 500, 500, 722, 500, 500, 444, 480, 200, 480, 541
};

static const uint16_t helv_wx[95] = {
    278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278,
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556,
    1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778,
    667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556,
    333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556,
    556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584
};

int wp_char_width_thou(uint8_t family, unsigned char ch)
{
    if (ch < 32 || ch > 126) {
        return family == WP_FONT_COURIER ? 600 : 500;
    }
    if (family == WP_FONT_TIMES) {
        return times_wx[ch - 32];
    }
    if (family == WP_FONT_HELVETICA) {
        return helv_wx[ch - 32];
    }
    return 600;
}
