#ifndef WP51_H
#define WP51_H

#include <stddef.h>
#include <stdint.h>

#define WP51_COLS 80
#define WP51_ROWS 25
#define WP51_WRAP 65
#define WP51_STRIP_ROWS 2 /* F-key template above the status line */
#define WP51_LINES_PER_PAGE 54
#define WP51_LEFT_MARGIN_TENTHS 10

#define WPD_MAGIC "\xFFWPC"
#define WPD_PRODUCT 1
#define WPD_TYPE_DOC 0x0A
#define WPD_MAJOR 0
#define WPD_MINOR 1

#define WP_HARD_EOL 0x0A
#define WP_HARD_PAGE 0x0C
#define WP_SOFT_EOL 0x0D
#define WP_EXT_CHAR 0xC0
#define WP_TAB 0xC1
#define WP_INDENT 0xC2
#define WP_ATTR_ON 0xC3
#define WP_ATTR_OFF 0xC4
#define WP_CENTER 0xC5
#define WP_UTF8 0xC6 /* C6, nbytes, utf8[nbytes] */
#define WP_FONT 0xC7 /* C7, family, size_pt */
#define WP_JUST 0xC8 /* C8, mode */
#define WP_END_FIELD 0xB6
#define WP_INDENT_COLS 5

#define WP_JUST_LEFT 0
#define WP_JUST_CENTER 1
#define WP_JUST_RIGHT 2
#define WP_JUST_FULL 3

/* Official WP 5.1 attribute numbers. */
#define WP_ATTR_EXT_LARGE 0
#define WP_ATTR_VRY_LARGE 1
#define WP_ATTR_LARGE 2
#define WP_ATTR_SMALL 3
#define WP_ATTR_FINE 4
#define WP_ATTR_ITALIC 8
#define WP_ATTR_BOLD 12
#define WP_ATTR_UNDERLINE 14

#define WP_FONT_COURIER 0
#define WP_FONT_TIMES 1
#define WP_FONT_HELVETICA 2

#define ATTR_BIT_BOLD (1u << 0)
#define ATTR_BIT_UNDERLINE (1u << 1)
#define ATTR_BIT_ITALIC (1u << 2)
#define ATTR_BIT_FINE (1u << 3)
#define ATTR_BIT_SMALL (1u << 4)
#define ATTR_BIT_LARGE (1u << 5)
#define ATTR_BIT_VRY_LARGE (1u << 6)
#define ATTR_BIT_EXT_LARGE (1u << 7)
#define ATTR_BITS_SIZE \
    (ATTR_BIT_FINE | ATTR_BIT_SMALL | ATTR_BIT_LARGE | ATTR_BIT_VRY_LARGE | ATTR_BIT_EXT_LARGE)

/* Synthetic keys (not ncurses KEY_F). */
#define WP_KEY_FONT 0x0F20
#define WP_KEY_ITALIC 0x0F21
#define WP_KEY_FINE 0x0F22
#define WP_KEY_SMALL 0x0F23
#define WP_KEY_LARGE 0x0F24
#define WP_KEY_VRY 0x0F25
#define WP_KEY_EXT 0x0F26
#define WP_KEY_NORMAL 0x0F27
#define WP_KEY_SIZE_NORMAL 0x0F28
#define WP_KEY_STRIP 0x0F29
#define WP_KEY_JUST 0x0F2A
#define WP_KEY_JUST_LEFT 0x0F2B
#define WP_KEY_JUST_CENTER 0x0F2C
#define WP_KEY_JUST_RIGHT 0x0F2D
#define WP_KEY_JUST_FULL 0x0F2E

enum {
    MODE_EDIT = 0,
    MODE_REVEAL,
    MODE_LIST,
    MODE_PROMPT,
    MODE_CONFIRM,
    MODE_FONT,
    MODE_JUST,
    MODE_QUIT
};

enum {
    FONT_MENU_MAIN = 0,
    FONT_MENU_SIZE,
    FONT_MENU_APPEAR,
    FONT_MENU_BASE
};

#endif
