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
