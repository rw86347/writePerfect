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

#include "cmd.h"

#include "iofmt.h"
#include "wpd.h"

#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum {
    AFTER_NONE = 0,
    AFTER_SAVE_EXIT,
    AFTER_SAVE
};

void app_init(App *a)
{
    memset(a, 0, sizeof(*a));
    a->mode = MODE_EDIT;
    a->running = 1;
}

void app_free(App *a)
{
    memset(a, 0, sizeof(*a));
}

static void begin_prompt(App *a, const char *prompt, const char *seed)
{
    a->mode = MODE_PROMPT;
    snprintf(a->prompt, sizeof(a->prompt), "%s", prompt);
    a->input[0] = '\0';
    a->input_len = 0;
    if (seed && seed[0]) {
        snprintf(a->input, sizeof(a->input), "%s", seed);
        a->input_len = strlen(a->input);
    }
}

static void begin_confirm(App *a, const char *prompt)
{
    a->mode = MODE_CONFIRM;
    snprintf(a->prompt, sizeof(a->prompt), "%s", prompt);
    a->confirm_yes = 1;
}

static const char *font_menu_text(const App *a)
{
    switch (a->font_menu) {
    case FONT_MENU_SIZE:
        return "1 Fine;  2 Small;  3 Large;  4 Vry Large;  5 Ext Large;  6 Normal: 0";
    case FONT_MENU_APPEAR:
        return "1 Bold;  2 Undln;  3 Italc: 0";
    case FONT_MENU_BASE:
        return "1 Cour 10  2 Cour 12  3 Times 12  4 Times 14  5 Helv 12  6 Helv 14: 0";
    default:
        return "1 Size;  2 Appearance;  3 Normal;  4 Base Font: 0";
    }
}

static void show_font_menu(App *a, View *v)
{
    snprintf(v->message, sizeof(v->message), "%s", font_menu_text(a));
    v->msg_ttl = 1;
}

static void begin_font_menu(App *a, View *v)
{
    a->mode = MODE_FONT;
    a->font_menu = FONT_MENU_MAIN;
    show_font_menu(a, v);
}

static void show_just_menu(View *v)
{
    snprintf(v->message, sizeof(v->message), "%s",
             "Justification:  1 Left;  2 Center;  3 Right;  4 Full: 0");
    v->msg_ttl = 1;
}

static void begin_just_menu(App *a, View *v)
{
    a->mode = MODE_JUST;
    show_just_menu(v);
}

static void apply_just(Doc *d, View *v, uint8_t mode)
{
    doc_insert_just(d, mode);
    view_message(v, "Just %s", wp_just_name(mode));
}

static void show_prompt_status(App *a, View *v)
{
    if (a->mode == MODE_PROMPT) {
        snprintf(v->message, sizeof(v->message), "%s%s", a->prompt, a->input);
        v->msg_ttl = 1;
    } else if (a->mode == MODE_CONFIRM) {
        snprintf(v->message, sizeof(v->message), "%s %s", a->prompt,
                 a->confirm_yes ? "Yes (No)" : "No (Yes)");
        v->msg_ttl = 1;
    } else if (a->mode == MODE_FONT) {
        show_font_menu(a, v);
    } else if (a->mode == MODE_JUST) {
        show_just_menu(v);
    }
}

static int do_save(Doc *d, View *v, const char *path)
{
    char real[1200];
    real[0] = '\0';
    if (io_save(d, path, real, sizeof(real)) != 0) {
        view_message(v, "ERROR: cannot save %s", path);
        return -1;
    }
    snprintf(d->path, sizeof(d->path), "%s", real[0] ? real : path);
    d->dirty = 0;
    view_message(v, "Document saved: %s", d->path);
    return 0;
}

static void finish_save_prompt(App *a, Doc *d, View *v)
{
    if (a->input_len == 0) {
        a->mode = MODE_EDIT;
        view_message(v, "Cancelled");
        return;
    }
    if (do_save(d, v, a->input) == 0 && a->after_confirm == AFTER_SAVE_EXIT) {
        a->running = 0;
        a->mode = MODE_QUIT;
        return;
    }
    a->mode = MODE_EDIT;
    a->after_confirm = AFTER_NONE;
}

static void request_save(App *a, Doc *d, int after)
{
    a->after_confirm = after;
    char seed[1200];
    seed[0] = '\0';
    if (d->path[0]) {
        io_path_for_format(d->path, io_default_format(), seed, sizeof(seed));
    }
    begin_prompt(a, "Document to be saved: ", seed);
}

static int is_ex_prompt(const App *a)
{
    return a->prompt[0] == ':' && a->prompt[1] == '\0';
}

static int is_search_prompt(const App *a)
{
    return strstr(a->prompt, "Srch") != NULL;
}

static void begin_search(App *a, int backward)
{
    a->search_back = backward;
    begin_prompt(a, backward ? "<- Srch: " : "-> Srch: ", a->search);
}

static void run_search(App *a, Doc *d, View *v)
{
    if (a->input_len) {
        snprintf(a->search, sizeof(a->search), "%s", a->input);
    }
    a->mode = MODE_EDIT;
    if (!a->search[0]) {
        view_message(v, "Cancelled");
        return;
    }
    if (doc_search(d, a->search, a->search_back) == 0) {
        view_message(v, a->search_back ? "<- Search" : "-> Search");
    } else {
        view_message(v, "* Not found *");
    }
}

static void run_ex(App *a, Doc *d, View *v, const char *cmd)
{
    while (*cmd == ' ') {
        cmd++;
    }
    a->mode = MODE_EDIT;
    a->after_confirm = AFTER_NONE;
    if (!strcmp(cmd, "q!") || !strcmp(cmd, "quit!")) {
        a->running = 0;
        a->mode = MODE_QUIT;
        return;
    }
    if (!strcmp(cmd, "q") || !strcmp(cmd, "quit")) {
        if (d->dirty) {
            view_message(v, "No write since last change (use :q! to discard)");
            return;
        }
        a->running = 0;
        a->mode = MODE_QUIT;
        return;
    }
    if (!strcmp(cmd, "w") || !strncmp(cmd, "w ", 2)) {
        const char *path = d->path;
        if (!strncmp(cmd, "w ", 2)) {
            path = cmd + 2;
            while (*path == ' ') {
                path++;
            }
        }
        if (!path || !path[0]) {
            request_save(a, d, AFTER_SAVE);
            return;
        }
        do_save(d, v, path);
        return;
    }
    if (!strcmp(cmd, "wq") || !strncmp(cmd, "wq ", 3)) {
        const char *path = d->path;
        if (!strncmp(cmd, "wq ", 3)) {
            path = cmd + 3;
            while (*path == ' ') {
                path++;
            }
        }
        if (!path || !path[0]) {
            request_save(a, d, AFTER_SAVE_EXIT);
            return;
        }
        if (do_save(d, v, path) == 0) {
            a->running = 0;
            a->mode = MODE_QUIT;
        }
        return;
    }
    if (!strcmp(cmd, "font")) {
        begin_font_menu(a, v);
        return;
    }
    if (!strcmp(cmd, "just") || !strcmp(cmd, "justify") || !strcmp(cmd, "format")) {
        begin_just_menu(a, v);
        return;
    }
    if (!strcmp(cmd, "strip")) {
        doc_strip_codes(d);
        view_message(v, "Codes stripped");
        return;
    }
    view_message(v, "Not an editor command: %s", cmd);
}

static int handle_prompt(App *a, Doc *d, View *v, int key)
{
    if (key == KEY_F(1) || key == 27) {
        a->mode = MODE_EDIT;
        a->after_confirm = AFTER_NONE;
        view_message(v, "Cancelled");
        return 1;
    }
    if (key == KEY_F(2) && is_search_prompt(a)) {
        run_search(a, d, v);
        return 1;
    }
    if (key == '\n' || key == KEY_ENTER) {
        if (is_ex_prompt(a)) {
            run_ex(a, d, v, a->input);
            return 1;
        }
        if (is_search_prompt(a)) {
            run_search(a, d, v);
            return 1;
        }
        finish_save_prompt(a, d, v);
        return 1;
    }
    if (key == 21) { /* Ctrl-U: wipe the field (for scripted save-as) */
        a->input[0] = '\0';
        a->input_len = 0;
        return 1;
    }
    if (key == KEY_BACKSPACE || key == 127 || key == 8) {
        if (a->input_len > 0) {
            size_t i = a->input_len;
            do {
                i--;
            } while (i > 0 && ((unsigned char)a->input[i] & 0xC0) == 0x80);
            a->input[i] = '\0';
            a->input_len = i;
        }
        return 1;
    }
    if (key >= 32 && key < 127 && a->input_len + 1 < sizeof(a->input)) {
        a->input[a->input_len++] = (char)key;
        a->input[a->input_len] = '\0';
        return 1;
    }
    return 1;
}

static int handle_confirm(App *a, Doc *d, View *v, int key)
{
    if (key == KEY_F(1) || key == 27) {
        a->mode = MODE_EDIT;
        a->after_confirm = AFTER_NONE;
        view_message(v, "Cancelled");
        return 1;
    }
    if (key == 'y' || key == 'Y') {
        a->confirm_yes = 1;
        key = '\n';
    } else if (key == 'n' || key == 'N') {
        a->confirm_yes = 0;
        key = '\n';
    } else if (key == '\t' || key == KEY_LEFT || key == KEY_RIGHT) {
        a->confirm_yes = !a->confirm_yes;
        return 1;
    }
    if (key == '\n' || key == KEY_ENTER) {
        if (a->confirm_yes) {
            request_save(a, d, AFTER_SAVE_EXIT);
        } else {
            a->running = 0;
            a->mode = MODE_QUIT;
        }
        return 1;
    }
    return 1;
}

static void render_list(const App *a, Screen *s)
{
    int i;
    int rows = s->rows;
    int cols = s->cols;
    int body = rows - 3;
    char line[256];
    int top = a->list.top;

    screen_clear(s);
    if (a->list.selected < top) {
        top = a->list.selected;
    }
    if (a->list.selected >= top + body) {
        top = a->list.selected - body + 1;
    }
    if (top < 0) {
        top = 0;
    }

    screen_fill_line(s, 0, SCR_REVERSE);
    snprintf(line, sizeof(line), "List Files  %s", a->list.dir);
    screen_putn(s, 0, 1, line, cols - 2, SCR_REVERSE);

    for (i = 0; i < body; i++) {
        int idx = top + i;
        if (idx >= a->list.count) {
            continue;
        }
        unsigned attr = (idx == a->list.selected) ? SCR_REVERSE : 0;
        screen_putn(s, 1 + i, 2, a->list.names[idx], cols - 4, attr);
    }
    screen_fill_line(s, rows - 2, SCR_REVERSE);
    screen_putn(s, rows - 2, 1, "Enter retrieve   F1 cancel   Arrows move", cols - 2, SCR_REVERSE);
    screen_fill_line(s, rows - 1, SCR_REVERSE);
    snprintf(line, sizeof(line), "%d file(s)", a->list.count);
    screen_putn(s, rows - 1, 1, line, cols - 2, SCR_REVERSE);
    s->show_cursor = 0;
}

void app_prepare_draw(App *a, View *v)
{
    if (a->mode == MODE_PROMPT) {
        snprintf(v->message, sizeof(v->message), "%s%s", a->prompt, a->input);
        v->msg_ttl = 1;
    } else if (a->mode == MODE_CONFIRM) {
        snprintf(v->message, sizeof(v->message), "%s %s", a->prompt,
                 a->confirm_yes ? "Yes (No)" : "No (Yes)");
        v->msg_ttl = 1;
    } else if (a->mode == MODE_FONT) {
        show_font_menu(a, v);
    } else if (a->mode == MODE_JUST) {
        show_just_menu(v);
    }
}

void app_render(const App *a, const View *v, const Doc *d, Screen *s)
{
    if (a->mode == MODE_LIST) {
        render_list(a, s);
    } else {
        view_render(v, d, s);
    }
}

static int handle_list(App *a, Doc *d, View *v, int key)
{
    int rows = LINES > 0 ? LINES : WP51_ROWS;
    int body = rows - 3;
    if (key == KEY_F(1) || key == 27) {
        a->mode = MODE_EDIT;
        view_message(v, "Cancelled");
        return 1;
    }
    if (key == KEY_UP) {
        files_move(&a->list, -1);
    } else if (key == KEY_DOWN) {
        files_move(&a->list, 1);
    } else if (key == KEY_PPAGE) {
        files_move(&a->list, -body);
    } else if (key == KEY_NPAGE) {
        files_move(&a->list, body);
    } else if (key == KEY_HOME) {
        a->list.selected = 0;
    } else if (key == KEY_END && a->list.count) {
        a->list.selected = a->list.count - 1;
    } else if (key == '\n' || key == KEY_ENTER) {
        char path[1024];
        if (files_selected_path(&a->list, path, sizeof(path))) {
            if (io_load(d, path) != 0) {
                view_message(v, "ERROR: cannot open %s", path);
            } else {
                view_message(v, "Retrieved %s", path);
            }
        }
        a->mode = MODE_EDIT;
        return 1;
    }
    if (a->list.selected < a->list.top) {
        a->list.top = a->list.selected;
    }
    if (a->list.selected >= a->list.top + body) {
        a->list.top = a->list.selected - body + 1;
    }
    return 1;
}

void app_insert_text(App *a, Doc *d, View *v, const char *utf8)
{
    size_t n;
    if (!utf8 || !utf8[0]) {
        return;
    }
    n = strlen(utf8);
    if (a->mode == MODE_PROMPT) {
        if (a->input_len + n < sizeof(a->input)) {
            memcpy(a->input + a->input_len, utf8, n);
            a->input_len += n;
            a->input[a->input_len] = '\0';
        }
        show_prompt_status(a, v);
        return;
    }
    if (a->mode != MODE_EDIT) {
        return;
    }
    doc_insert_utf8(d, utf8);
}

static int handle_just(App *a, Doc *d, View *v, int key)
{
    if (key == KEY_F(1) || key == 27 || key == '0') {
        a->mode = MODE_EDIT;
        view_message(v, "Cancelled");
        return 1;
    }
    if (key == '1') {
        apply_just(d, v, WP_JUST_LEFT);
        a->mode = MODE_EDIT;
        return 1;
    }
    if (key == '2') {
        apply_just(d, v, WP_JUST_CENTER);
        a->mode = MODE_EDIT;
        return 1;
    }
    if (key == '3') {
        apply_just(d, v, WP_JUST_RIGHT);
        a->mode = MODE_EDIT;
        return 1;
    }
    if (key == '4' || key == 'f' || key == 'F') {
        apply_just(d, v, WP_JUST_FULL);
        a->mode = MODE_EDIT;
        return 1;
    }
    show_just_menu(v);
    return 1;
}

static int handle_font(App *a, Doc *d, View *v, int key)
{
    if (key == KEY_F(1) || key == 27) {
        a->mode = MODE_EDIT;
        view_message(v, "Cancelled");
        return 1;
    }
    if (key == '0') {
        a->mode = MODE_EDIT;
        view_message(v, "Cancelled");
        return 1;
    }
    if (a->font_menu == FONT_MENU_MAIN) {
        if (key == '1') {
            a->font_menu = FONT_MENU_SIZE;
            show_font_menu(a, v);
            return 1;
        }
        if (key == '2') {
            a->font_menu = FONT_MENU_APPEAR;
            show_font_menu(a, v);
            return 1;
        }
        if (key == '3') {
            doc_normal_attr(d);
            a->mode = MODE_EDIT;
            view_message(v, "Normal");
            return 1;
        }
        if (key == '4') {
            a->font_menu = FONT_MENU_BASE;
            show_font_menu(a, v);
            return 1;
        }
        show_font_menu(a, v);
        return 1;
    }
    if (a->font_menu == FONT_MENU_SIZE) {
        uint8_t attr = 0;
        const char *name = NULL;
        if (key == '1') {
            attr = WP_ATTR_FINE;
            name = "Fine";
        } else if (key == '2') {
            attr = WP_ATTR_SMALL;
            name = "Small";
        } else if (key == '3') {
            attr = WP_ATTR_LARGE;
            name = "Large";
        } else if (key == '4') {
            attr = WP_ATTR_VRY_LARGE;
            name = "Very Large";
        } else         if (key == '5') {
            attr = WP_ATTR_EXT_LARGE;
            name = "Extra Large";
        } else if (key == '6' || key == 'n' || key == 'N') {
            doc_normal_size(d);
            a->mode = MODE_EDIT;
            view_message(v, "Normal");
            return 1;
        }
        if (name) {
            doc_toggle_attr(d, attr);
            a->mode = MODE_EDIT;
            view_message(v, "%s", name);
            return 1;
        }
        show_font_menu(a, v);
        return 1;
    }
    if (a->font_menu == FONT_MENU_APPEAR) {
        if (key == '1') {
            doc_toggle_attr(d, WP_ATTR_BOLD);
            a->mode = MODE_EDIT;
            view_message(v, "Bold");
            return 1;
        }
        if (key == '2') {
            doc_toggle_attr(d, WP_ATTR_UNDERLINE);
            a->mode = MODE_EDIT;
            view_message(v, "Underline");
            return 1;
        }
        if (key == '3') {
            doc_toggle_attr(d, WP_ATTR_ITALIC);
            a->mode = MODE_EDIT;
            view_message(v, "Italics");
            return 1;
        }
        show_font_menu(a, v);
        return 1;
    }
    if (a->font_menu == FONT_MENU_BASE) {
        uint8_t fam = WP_FONT_COURIER;
        uint8_t pt = 12;
        int ok = 1;
        if (key == '1') {
            fam = WP_FONT_COURIER;
            pt = 10;
        } else if (key == '2') {
            fam = WP_FONT_COURIER;
            pt = 12;
        } else if (key == '3') {
            fam = WP_FONT_TIMES;
            pt = 12;
        } else if (key == '4') {
            fam = WP_FONT_TIMES;
            pt = 14;
        } else if (key == '5') {
            fam = WP_FONT_HELVETICA;
            pt = 12;
        } else if (key == '6') {
            fam = WP_FONT_HELVETICA;
            pt = 14;
        } else {
            ok = 0;
        }
        if (ok) {
            doc_insert_font(d, fam, pt);
            a->mode = MODE_EDIT;
            view_message(v, "Base Font: %s %upt", wp_font_family_name(fam), (unsigned)pt);
            return 1;
        }
        show_font_menu(a, v);
        return 1;
    }
    return 1;
}

int app_handle_key(App *a, Doc *d, View *v, int key)
{
    if (key == 3 || key == KEY_BREAK) { /* Ctrl-C: abandon, always */
        a->running = 0;
        a->mode = MODE_QUIT;
        return 0;
    }
    if ((a->mode == MODE_EDIT || a->mode == MODE_FONT || a->mode == MODE_JUST) &&
        (key == WP_KEY_JUST_LEFT || key == WP_KEY_JUST_CENTER || key == WP_KEY_JUST_RIGHT ||
         key == WP_KEY_JUST_FULL)) {
        if (key == WP_KEY_JUST_LEFT) {
            apply_just(d, v, WP_JUST_LEFT);
        } else if (key == WP_KEY_JUST_CENTER) {
            apply_just(d, v, WP_JUST_CENTER);
        } else if (key == WP_KEY_JUST_RIGHT) {
            apply_just(d, v, WP_JUST_RIGHT);
        } else {
            apply_just(d, v, WP_JUST_FULL);
        }
        a->mode = MODE_EDIT;
        return 0;
    }
    if ((a->mode == MODE_EDIT || a->mode == MODE_FONT) &&
        (key == WP_KEY_FINE || key == WP_KEY_SMALL || key == WP_KEY_LARGE || key == WP_KEY_VRY ||
         key == WP_KEY_EXT || key == WP_KEY_NORMAL || key == WP_KEY_SIZE_NORMAL)) {
        switch (key) {
        case WP_KEY_FINE:
            doc_toggle_attr(d, WP_ATTR_FINE);
            view_message(v, "Fine");
            break;
        case WP_KEY_SMALL:
            doc_toggle_attr(d, WP_ATTR_SMALL);
            view_message(v, "Small");
            break;
        case WP_KEY_LARGE:
            doc_toggle_attr(d, WP_ATTR_LARGE);
            view_message(v, "Large");
            break;
        case WP_KEY_VRY:
            doc_toggle_attr(d, WP_ATTR_VRY_LARGE);
            view_message(v, "Very Large");
            break;
        case WP_KEY_EXT:
            doc_toggle_attr(d, WP_ATTR_EXT_LARGE);
            view_message(v, "Extra Large");
            break;
        case WP_KEY_SIZE_NORMAL:
            doc_normal_size(d);
            view_message(v, "Normal");
            break;
        default:
            doc_normal_attr(d);
            view_message(v, "Normal");
            break;
        }
        a->mode = MODE_EDIT;
        return 0;
    }
    if (a->mode == MODE_JUST) {
        handle_just(a, d, v, key);
        if (a->mode == MODE_JUST) {
            show_just_menu(v);
        }
        return 0;
    }
    if (a->mode == MODE_FONT) {
        handle_font(a, d, v, key);
        if (a->mode == MODE_FONT) {
            show_font_menu(a, v);
        }
        return 0;
    }
    if (a->mode == MODE_PROMPT) {
        show_prompt_status(a, v);
        handle_prompt(a, d, v, key);
        if (a->mode == MODE_PROMPT) {
            show_prompt_status(a, v);
        }
        return 0;
    }
    if (a->mode == MODE_CONFIRM) {
        handle_confirm(a, d, v, key);
        if (a->mode == MODE_CONFIRM) {
            show_prompt_status(a, v);
        }
        return 0;
    }
    if (a->mode == MODE_LIST) {
        handle_list(a, d, v, key);
        return 0;
    }

    if (v->msg_ttl > 0) {
        v->msg_ttl--;
        if (v->msg_ttl == 0) {
            v->message[0] = '\0';
        }
    }

    switch (key) {
    case KEY_F(1):
    case 27:
        view_message(v, "Cancelled");
        break;
    case KEY_F(2):
        begin_search(a, 0);
        show_prompt_status(a, v);
        break;
    case KEY_F(14): /* Shift-F2: search backward */
        begin_search(a, 1);
        show_prompt_status(a, v);
        break;
    case KEY_F(3):
        view_message(v, "F6 Bold  :font  :just Full  F11 Reveal  F12 Cntr");
        break;
    case KEY_F(20): /* Shift-F8 in many terminals */
    case WP_KEY_FONT:
        begin_font_menu(a, v);
        break;
    case WP_KEY_ITALIC:
        doc_toggle_attr(d, WP_ATTR_ITALIC);
        break;
    case WP_KEY_FINE:
        doc_toggle_attr(d, WP_ATTR_FINE);
        view_message(v, "Fine");
        break;
    case WP_KEY_SMALL:
        doc_toggle_attr(d, WP_ATTR_SMALL);
        view_message(v, "Small");
        break;
    case WP_KEY_LARGE:
        doc_toggle_attr(d, WP_ATTR_LARGE);
        view_message(v, "Large");
        break;
    case WP_KEY_VRY:
        doc_toggle_attr(d, WP_ATTR_VRY_LARGE);
        view_message(v, "Very Large");
        break;
    case WP_KEY_EXT:
        doc_toggle_attr(d, WP_ATTR_EXT_LARGE);
        view_message(v, "Extra Large");
        break;
    case WP_KEY_NORMAL:
        doc_normal_attr(d);
        view_message(v, "Normal");
        break;
    case WP_KEY_SIZE_NORMAL:
        doc_normal_size(d);
        view_message(v, "Normal");
        break;
    case WP_KEY_STRIP:
        doc_strip_codes(d);
        view_message(v, "Codes stripped");
        break;
    case WP_KEY_JUST:
        begin_just_menu(a, v);
        break;
    case WP_KEY_JUST_LEFT:
        apply_just(d, v, WP_JUST_LEFT);
        break;
    case WP_KEY_JUST_CENTER:
        apply_just(d, v, WP_JUST_CENTER);
        break;
    case WP_KEY_JUST_RIGHT:
        apply_just(d, v, WP_JUST_RIGHT);
        break;
    case WP_KEY_JUST_FULL:
        apply_just(d, v, WP_JUST_FULL);
        break;
    case KEY_F(4):
        doc_insert_code(d, WP_INDENT);
        view_message(v, "->Indent");
        break;
    case KEY_F(11):
        v->reveal = !v->reveal;
        view_message(v, v->reveal ? "Reveal Codes  (Del/Bksp remove [BOLD] [Font])"
                                 : "Reveal Codes off");
        break;
    case KEY_F(9):
        doc_insert_code(d, WP_END_FIELD);
        view_message(v, "End Field");
        break;
    case KEY_F(12):
        doc_insert_code(d, WP_CENTER);
        view_message(v, "Center");
        break;
    case KEY_F(5): {
        char cwd[1024];
        if (!getcwd(cwd, sizeof(cwd))) {
            snprintf(cwd, sizeof(cwd), ".");
        }
        if (files_load(&a->list, cwd) != 0) {
            view_message(v, "ERROR: cannot list %s", cwd);
        } else {
            a->mode = MODE_LIST;
        }
        break;
    }
    case KEY_F(6):
        doc_toggle_attr(d, WP_ATTR_BOLD);
        break;
    case KEY_F(7):
        if (d->dirty) {
            begin_confirm(a, "Save document?");
            show_prompt_status(a, v);
        } else {
            a->running = 0;
        }
        break;
    case KEY_F(8):
        doc_toggle_attr(d, WP_ATTR_UNDERLINE);
        break;
    case KEY_F(10):
        request_save(a, d, AFTER_SAVE);
        show_prompt_status(a, v);
        break;
    case KEY_LEFT:
        doc_nav_left(d, 0, v->reveal);
        break;
    case KEY_RIGHT:
        doc_nav_right(d, 0, v->reveal);
        break;
    case KEY_UP:
        view_nav_vert(v, d, -1, 0);
        break;
    case KEY_DOWN:
        view_nav_vert(v, d, 1, 0);
        break;
    case KEY_HOME:
        doc_nav_home(d, 0);
        break;
    case KEY_END:
        doc_nav_end(d, 0);
        break;
    case KEY_BACKSPACE:
    case 127:
    case 8:
        doc_backspace(d, v->reveal);
        break;
    case KEY_DC:
        doc_delete_forward(d, v->reveal);
        break;
    case '\n':
    case KEY_ENTER:
        doc_insert_hard_return(d);
        break;
    case '\t':
        doc_insert_char(d, '\t');
        break;
    case ':':
        begin_prompt(a, ":", "");
        show_prompt_status(a, v);
        break;
    default:
        if (key >= 32 && key < 127) {
            doc_insert_char(d, key);
        }
        break;
    }
    return 0;
}

void app_draw_list(const App *a)
{
    Screen s;
    int rows = LINES > 0 ? LINES : WP51_ROWS;
    int cols = COLS > 0 ? COLS : WP51_COLS;
    int y, x;

    screen_init(&s);
    if (screen_resize(&s, rows, cols) != 0) {
        return;
    }
    render_list(a, &s);
    erase();
    for (y = 0; y < s.rows; y++) {
        for (x = 0; x < s.cols; x++) {
            Cell c = s.cells[y * s.cols + x];
            int attr = 0;
            char u8[8];
            if (c.cp == 0) {
                continue;
            }
            if (c.attr & SCR_REVERSE) {
                attr |= A_REVERSE;
            }
            if (attr) {
                attron(attr);
            }
            if (screen_cell_utf8(&c, u8, sizeof(u8)) && (unsigned char)u8[0] >= 128) {
                mvaddstr(y, x, u8);
            } else {
                mvaddch(y, x, (chtype)(c.cp < 128 ? c.cp : ' '));
            }
            if (attr) {
                attroff(attr);
            }
        }
    }
    screen_free(&s);
}
