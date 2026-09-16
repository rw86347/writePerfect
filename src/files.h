#ifndef FILES_H
#define FILES_H

#include "wp51.h"

#define FILES_MAX 512
#define FILES_NAME 256

typedef struct {
    char dir[1024];
    char names[FILES_MAX][FILES_NAME];
    int count;
    int selected;
    int top;
} FileList;

int files_load(FileList *fl, const char *dir);
void files_move(FileList *fl, int delta);
const char *files_selected_path(FileList *fl, char *out, size_t out_sz);

#endif
