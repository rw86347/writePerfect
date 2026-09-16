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

#ifndef GUI_H
#define GUI_H

#include "cmd.h"

/* macOS Cocoa window: menu bar, titlebar F-key strip, Touch Bar. */
int gui_run(App *a, Doc *d, View *v);

/* White-paper PDF via Core Text (1" laser margins). */
int gui_write_print_pdf(Doc *d, const char *path);

#endif
