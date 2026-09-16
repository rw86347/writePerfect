#ifndef GUI_H
#define GUI_H

#include "cmd.h"

/* macOS Cocoa window: menu bar, titlebar F-key strip, Touch Bar. */
int gui_run(App *a, Doc *d, View *v);

/* White-paper PDF via Core Text (1" laser margins). */
int gui_write_print_pdf(Doc *d, const char *path);

#endif
