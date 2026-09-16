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

#include "files.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int cmp_name(const void *a, const void *b)
{
    return strcasecmp((const char *)a, (const char *)b);
}

int files_load(FileList *fl, const char *dir)
{
    memset(fl, 0, sizeof(*fl));
    snprintf(fl->dir, sizeof(fl->dir), "%s", dir && dir[0] ? dir : ".");

    DIR *dp = opendir(fl->dir);
    if (!dp) {
        return -1;
    }
    struct dirent *ent;
    while ((ent = readdir(dp)) != NULL && fl->count < FILES_MAX) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        snprintf(fl->names[fl->count], FILES_NAME, "%s", ent->d_name);
        fl->count++;
    }
    closedir(dp);
    if (fl->count > 1) {
        qsort(fl->names, (size_t)fl->count, FILES_NAME, cmp_name);
    }
    fl->selected = 0;
    fl->top = 0;
    return 0;
}

void files_move(FileList *fl, int delta)
{
    if (fl->count == 0) {
        return;
    }
    int s = fl->selected + delta;
    if (s < 0) {
        s = 0;
    }
    if (s >= fl->count) {
        s = fl->count - 1;
    }
    fl->selected = s;
}

const char *files_selected_path(FileList *fl, char *out, size_t out_sz)
{
    if (fl->count == 0 || fl->selected < 0 || fl->selected >= fl->count) {
        return NULL;
    }
    snprintf(out, out_sz, "%s/%s", fl->dir, fl->names[fl->selected]);
    return out;
}
