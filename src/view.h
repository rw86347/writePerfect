#ifndef VIEW_H
#define VIEW_H

#include "doc.h"
#include "screen.h"

typedef struct {
    int wrap;
    int wrap_word; /* 1 = break on whitespace, 0 = strict column wrap */
    int lines_per_page;
    int nlines;
    size_t *line_start; /* stream pos of each visual line */
    size_t line_cap;
    int cursor_line;
    int cursor_col;
    int cursor_page;
    int top_line;
    int reveal;
    int hide_strip; /* GUI puts F-keys in the Mac top bar instead */
    int spell_n;
    size_t spell_lo[400];
    size_t spell_hi[400];
    int msg_ttl;
    char status[WP51_COLS + 1];
    char message[WP51_COLS + 1];
} View;

extern const char *const view_fkey_names[12];
extern const char *const view_fkey_names_ctrl[12];
extern const char *const view_fkey_names_shift[12];

enum {
    FKEY_MOD_NONE = 0,
    FKEY_MOD_CTRL,
    FKEY_MOD_SHIFT
};

/* index 0 = F1. */
const char *view_fkey_name(int index0, int mod);

void view_init(View *v);
void view_free(View *v);
void view_apply_prefs(View *v);
void view_relayout(View *v, const Doc *d);
void view_ensure_cursor_visible(View *v);
void view_status(View *v, const Doc *d);
void view_message(View *v, const char *fmt, ...);
void view_split(const View *v, int rows, int *text_rows, int *reveal_y, int *reveal_rows);
void view_render(const View *v, const Doc *d, Screen *s);
void view_render_text(const View *v, const Doc *d, Screen *s);
int view_line_start_col(const View *v, const Doc *d, int li);
size_t view_pos_at_reveal(const View *v, const Doc *d, int cols, int rel_row, int col);
void view_draw(const View *v, const Doc *d, int rows, int cols);
void view_move_vert(View *v, Doc *d, int delta);
void view_nav_vert(View *v, Doc *d, int delta, int extend);
size_t view_pos_at(const View *v, const Doc *d, int line, int col);
int view_in_spell(const View *v, size_t pos);

#endif
