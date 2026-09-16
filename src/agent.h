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
