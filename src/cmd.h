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
