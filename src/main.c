#include "agent.h"
#include "cmd.h"
#include "doc.h"
#include "gui.h"
#include "iofmt.h"
#include "view.h"
#include "wpd.h"

#include <locale.h>
#include <ncurses.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int read_key(void)
{
    return getch();
}

static void die_restore(int sig)
{
    (void)sig;
    endwin();
    _Exit(128 + (sig > 0 ? sig : 1));
}

static void usage(const char *argv0)
{
    fprintf(stderr, "Usage: %s [--gui|--tui] [--agent [port]] [--script commands.txt] [document.wpd]\n", argv0);
    fprintf(stderr, "  --gui   Mac window, menu bar, titlebar F-keys, Touch Bar.\n");
    fprintf(stderr, "  --tui   Terminal (default when run from a shell).\n");
    fprintf(stderr, "  --agent [port]  UDP keyboard on 127.0.0.1 (default %d). Headless if no --gui/--tui.\n",
            WP_AGENT_DEFAULT_PORT);
    fprintf(stderr, "  LCD/Touch Bar F-keys match the on-screen template (F1-F12).\n");
    fprintf(stderr, "  F2 Search  F4 Indent  F9 End Field  F12 Center\n");
    fprintf(stderr, "  F1 Cancel  F5 List  F6 Bold  F7 Exit  F8 Undln  F10 Save  F11 Reveal\n");
    fprintf(stderr, "  Font: Cmd-T (Mac), Shift-F8, titlebar Ctrl then F8, or :font.  F3 Help.\n");
    fprintf(stderr, "  Size test keys: Ctrl-Shift-1 Fine, 2 Small, 3 Large, 4 Vry, 5 Ext, 0 Normal size, N Normal.\n");
    fprintf(stderr, "  Justify: :just  or Ctrl-Shift-J Full, Ctrl-Shift-L menu.  Agent: just-full just-left just-right.\n");
    fprintf(stderr, "  Agent/script: fine small large vry ext normal normsize just-full.\n");
    fprintf(stderr, "  Titlebar/Touch Bar: tap Ctrl or Shift to latch, then an F-key (labels change).\n");
    fprintf(stderr, "  macOS steals Ctrl-F8 (status menus). On a Mac laptop, F-keys need Fn\n");
    fprintf(stderr, "  unless System Settings → Keyboard → Use F1, F2, etc. as standard function keys.\n");
}

static FILE *script_fp;
static char type_q[2048];
static size_t type_i, type_n;

static void enqueue_text(const char *s)
{
    while (*s && type_n + 1 < sizeof(type_q)) {
        type_q[type_n++] = *s++;
    }
}

static int dequeue_key(void)
{
    if (type_i >= type_n) {
        return -1;
    }
    unsigned char a = (unsigned char)type_q[type_i++];
    if (a != 0) {
        return a;
    }
    if (type_i + 1 > type_n) {
        return -1;
    }
    unsigned char hi = (unsigned char)type_q[type_i++];
    unsigned char lo = (unsigned char)type_q[type_i++];
    return (hi << 8) | lo;
}

static int script_read_key(void)
{
    int k = dequeue_key();
    if (k >= 0) {
        return k;
    }
    type_i = type_n = 0;
    char line[1024];
    while (fgets(line, sizeof(line), script_fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '#' || *p == '\n' || *p == '\0') {
            continue;
        }
        char *nl = strpbrk(p, "\r\n");
        if (nl) {
            *nl = '\0';
        }
        {
            char text[1024];
            int key = wp_parse_key_line(p, text, sizeof(text));
            if (key == -2) {
                enqueue_text(text);
                return dequeue_key();
            }
            if (key >= 0) {
                return key;
            }
        }
        fprintf(stderr, "wp: unknown script command: %s\n", p);
        exit(2);
    }
    return KEY_F(7);
}

int main(int argc, char **argv)
{
    const char *script_path = NULL;
    const char *doc_path = NULL;
    int use_gui = strstr(argv[0], ".app/Contents/MacOS/") != NULL;
    int use_tui = 0;
    int tui_forced = 0;
    int agent_port = -1;
    const char *env_port;
    int i;
    env_port = getenv("WP_AGENT_PORT");
    if (env_port && env_port[0]) {
        agent_port = atoi(env_port);
        if (agent_port <= 0) {
            agent_port = WP_AGENT_DEFAULT_PORT;
        }
    }
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        }
        if (!strcmp(argv[i], "--gui")) {
            use_gui = 1;
            continue;
        }
        if (!strcmp(argv[i], "--tui")) {
            use_gui = 0;
            tui_forced = 1;
            continue;
        }
        if (!strcmp(argv[i], "--agent")) {
            agent_port = WP_AGENT_DEFAULT_PORT;
            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') {
                agent_port = atoi(argv[++i]);
            }
            continue;
        }
        if (!strcmp(argv[i], "--script") && i + 1 < argc) {
            script_path = argv[++i];
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "wp: unknown option %s\n", argv[i]);
            return 1;
        }
        doc_path = argv[i];
    }

    setlocale(LC_ALL, "");

    Doc doc;
    View view;
    App app;
    doc_init(&doc);
    view_init(&view);
    app_init(&app);

    if (doc_path) {
        if (io_load(&doc, doc_path) != 0) {
            fprintf(stderr, "wp: cannot open document: %s\n", doc_path);
            doc_free(&doc);
            view_free(&view);
            return 1;
        }
    }

    if (script_path) {
        script_fp = fopen(script_path, "r");
        if (!script_fp) {
            fprintf(stderr, "wp: cannot open script %s\n", script_path);
            doc_free(&doc);
            view_free(&view);
            return 1;
        }
    }

    if (agent_port > 0) {
        if (agent_start(agent_port) != 0) {
            fprintf(stderr, "wp: cannot bind agent on 127.0.0.1:%d\n", agent_port);
            doc_free(&doc);
            view_free(&view);
            return 1;
        }
        agent_set_context(&app, &doc, &view);
    }

    if (use_gui) {
        if (script_fp) {
            fclose(script_fp);
            script_fp = NULL;
        }
        int rc = gui_run(&app, &doc, &view);
        agent_stop();
        doc_free(&doc);
        view_free(&view);
        app_free(&app);
        return rc;
    }

    use_tui = tui_forced || (!script_fp && !agent_active());
    if (use_tui) {
        initscr();
        cbreak();
        noecho();
        keypad(stdscr, TRUE);
        /* cbreak, not raw: Ctrl-C must generate SIGINT so you can always leave. */
        signal(SIGINT, die_restore);
        signal(SIGTERM, die_restore);
        if (has_colors()) {
            start_color();
            use_default_colors();
        }
        curs_set(1);
        if (agent_active()) {
            timeout(50);
        }
    }

    while (app.running) {
        int rows = use_tui && LINES > 0 ? LINES : WP51_ROWS;
        int cols = use_tui && COLS > 0 ? COLS : WP51_COLS;
        view_apply_prefs(&view);
        view_relayout(&view, &doc);
        view_ensure_cursor_visible(&view);
        view_status(&view, &doc);

        if (use_tui) {
            app_prepare_draw(&app, &view);
            if (app.mode == MODE_LIST) {
                app_draw_list(&app);
            } else {
                view_draw(&view, &doc, rows, cols);
            }
            refresh();
        }

        if (agent_active()) {
            agent_dispatch(&app, &doc, &view);
            if (!app.running) {
                break;
            }
        }

        int key;
        if (script_fp) {
            key = script_read_key();
        } else if (use_tui) {
            key = read_key();
            if (key == ERR || key == KEY_RESIZE) {
                continue;
            }
        } else if (agent_active()) {
            agent_wait();
            continue;
        } else {
            break;
        }
        if (key == KEY_RESIZE) {
            continue;
        }
        app_handle_key(&app, &doc, &view, key);
    }

    if (use_tui) {
        keypad(stdscr, FALSE);
        nocbreak();
        echo();
        endwin();
    }
    if (script_fp) {
        fclose(script_fp);
    }
    agent_stop();
    doc_free(&doc);
    view_free(&view);
    app_free(&app);
    return 0;
}
