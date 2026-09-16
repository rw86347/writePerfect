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

#ifndef CMD_H
#define CMD_H

#include "doc.h"
#include "view.h"
#include "files.h"

typedef struct {
    int mode;
    int running;
    char prompt[WP51_COLS + 1];
    char input[1024];
    size_t input_len;
    int confirm_yes;
    int after_confirm; /* 0 none, 1 save-then-exit, 2 save-as */
    int font_menu;
    char search[256];
    int search_back;
    FileList list;
} App;

void app_init(App *a);
void app_free(App *a);
void app_insert_text(App *a, Doc *d, View *v, const char *utf8);
int app_handle_key(App *a, Doc *d, View *v, int key);
void app_prepare_draw(App *a, View *v);
void app_render(const App *a, const View *v, const Doc *d, Screen *s);
void app_draw_list(const App *a);

#endif
