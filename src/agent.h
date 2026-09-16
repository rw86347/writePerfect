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

#ifndef AGENT_H
#define AGENT_H

#include "cmd.h"

#define WP_AGENT_DEFAULT_PORT 19510

int agent_start(int port);
void agent_stop(void);
void agent_set_context(App *a, Doc *d, View *v);
int agent_active(void);
int agent_port(void);

/* Non-blocking. 1 = key ready, 0 = idle. Queries are answered here. */
int agent_poll(int *key);
void agent_wait(void);
void agent_dispatch(App *a, Doc *d, View *v);

/* Shared with --script. Returns a key, or -1 unknown, or -2 for type/input
   (text copied to textout). */
int wp_parse_key_line(const char *line, char *textout, size_t textoutsz);

#endif
