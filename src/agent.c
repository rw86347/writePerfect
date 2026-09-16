#include "agent.h"

#include "font.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <ncurses.h>

#define AGENT_QMAX 256
#define AGENT_REPLY 4096

static int g_fd = -1;
static int g_port;
static App *g_app;
static Doc *g_doc;
static View *g_view;

static int g_keys[AGENT_QMAX];
static int g_kn, g_ki;

static struct sockaddr_in g_peer;
static socklen_t g_peerlen;
static int g_have_peer;

int wp_parse_key_line(const char *line, char *textout, size_t textoutsz)
{
    const char *p = line;
    if (!p) {
        return -1;
    }
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (!strncmp(p, "type ", 5)) {
        if (textout && textoutsz) {
            snprintf(textout, textoutsz, "%s", p + 5);
        }
        return -2;
    }
    if (!strncmp(p, "input ", 6)) {
        if (textout && textoutsz) {
            snprintf(textout, textoutsz, "%s\n", p + 6);
        }
        return -2;
    }
    if (!strcmp(p, "enter")) {
        return '\n';
    }
    if (!strcmp(p, "colon")) {
        return ':';
    }
    if (!strcmp(p, "ctrl-c")) {
        return 3;
    }
    if (!strcmp(p, "ctrl-u") || !strcmp(p, "wipe")) {
        return 21;
    }
    if (!strcmp(p, "esc") || !strcmp(p, "f1")) {
        return KEY_F(1);
    }
    if (!strcmp(p, "f2")) {
        return KEY_F(2);
    }
    if (!strcmp(p, "f3")) {
        return KEY_F(3);
    }
    if (!strcmp(p, "f4")) {
        return KEY_F(4);
    }
    if (!strcmp(p, "f5")) {
        return KEY_F(5);
    }
    if (!strcmp(p, "f6")) {
        return KEY_F(6);
    }
    if (!strcmp(p, "f7")) {
        return KEY_F(7);
    }
    if (!strcmp(p, "f8")) {
        return KEY_F(8);
    }
    if (!strcmp(p, "f9")) {
        return KEY_F(9);
    }
    if (!strcmp(p, "f10")) {
        return KEY_F(10);
    }
    if (!strcmp(p, "f11")) {
        return KEY_F(11);
    }
    if (!strcmp(p, "f12")) {
        return KEY_F(12);
    }
    if (!strcmp(p, "shift-f2") || !strcmp(p, "f14")) {
        return KEY_F(14);
    }
    if (!strcmp(p, "font") || !strcmp(p, "ctrl-f8") || !strcmp(p, "shift-f8") ||
        !strcmp(p, "cmd-t")) {
        return WP_KEY_FONT;
    }
    if (!strcmp(p, "italic") || !strcmp(p, "italc")) {
        return WP_KEY_ITALIC;
    }
    if (!strcmp(p, "fine")) {
        return WP_KEY_FINE;
    }
    if (!strcmp(p, "small")) {
        return WP_KEY_SMALL;
    }
    if (!strcmp(p, "large")) {
        return WP_KEY_LARGE;
    }
    if (!strcmp(p, "vry") || !strcmp(p, "vrylarge") || !strcmp(p, "very-large")) {
        return WP_KEY_VRY;
    }
    if (!strcmp(p, "ext") || !strcmp(p, "extlarge") || !strcmp(p, "extra-large")) {
        return WP_KEY_EXT;
    }
    if (!strcmp(p, "normal")) {
        return WP_KEY_NORMAL;
    }
    if (!strcmp(p, "normsize") || !strcmp(p, "normalsize") || !strcmp(p, "sizenormal")) {
        return WP_KEY_SIZE_NORMAL;
    }
    if (!strcmp(p, "strip")) {
        return WP_KEY_STRIP;
    }
    if (!strcmp(p, "just") || !strcmp(p, "justify") || !strcmp(p, "format")) {
        return WP_KEY_JUST;
    }
    if (!strcmp(p, "just-left") || !strcmp(p, "justleft")) {
        return WP_KEY_JUST_LEFT;
    }
    if (!strcmp(p, "just-center") || !strcmp(p, "justcenter")) {
        return WP_KEY_JUST_CENTER;
    }
    if (!strcmp(p, "just-right") || !strcmp(p, "justright")) {
        return WP_KEY_JUST_RIGHT;
    }
    if (!strcmp(p, "just-full") || !strcmp(p, "justfull") || !strcmp(p, "fulljust")) {
        return WP_KEY_JUST_FULL;
    }
    if (!strcmp(p, "home")) {
        return KEY_HOME;
    }
    if (!strcmp(p, "end")) {
        return KEY_END;
    }
    if (!strcmp(p, "left")) {
        return KEY_LEFT;
    }
    if (!strcmp(p, "right")) {
        return KEY_RIGHT;
    }
    if (!strcmp(p, "up")) {
        return KEY_UP;
    }
    if (!strcmp(p, "down")) {
        return KEY_DOWN;
    }
    if (!strcmp(p, "backspace")) {
        return KEY_BACKSPACE;
    }
    if (!strcmp(p, "delete")) {
        return KEY_DC;
    }
    if (!strcmp(p, "tab")) {
        return '\t';
    }
    if (!strcmp(p, "yes")) {
        return 'y';
    }
    if (!strcmp(p, "no")) {
        return 'n';
    }
    if (p[0] && !p[1] && (unsigned char)p[0] >= 32 && (unsigned char)p[0] < 127) {
        return (int)(unsigned char)p[0];
    }
    return -1;
}

static void q_push_key(int key)
{
    if (g_kn < AGENT_QMAX) {
        g_keys[g_kn++] = key;
    }
}

static void q_push_text(const char *s)
{
    while (s && *s) {
        q_push_key((unsigned char)*s++);
    }
}

static void agent_reply(const char *s)
{
    if (g_fd < 0 || !g_have_peer || !s) {
        return;
    }
    sendto(g_fd, s, strlen(s), 0, (struct sockaddr *)&g_peer, g_peerlen);
}

static void reply_status(void)
{
    char buf[AGENT_REPLY];
    DocFont f;
    size_t n = 0;
    char *text;
    if (!g_doc || !g_app) {
        agent_reply("err no document\n");
        return;
    }
    doc_font_at(g_doc, g_doc->cursor, &f);
    text = doc_range_utf8(g_doc, 0, g_doc->len, &n);
    snprintf(buf, sizeof(buf),
             "ok\nmode=%d\ncursor=%zu\nlen=%zu\ndirty=%d\npath=%s\nmsg=%s\n"
             "font=%s %upt\nattrs=%u\npt=%d\ntext=%s\n",
             g_app->mode, g_doc->cursor, g_doc->len, g_doc->dirty, g_doc->path,
             g_view ? g_view->message : "", wp_font_family_name(f.family),
             (unsigned)f.size_pt, f.attrs, wp_effective_pt(&f), text ? text : "");
    free(text);
    agent_reply(buf);
}

static void reply_reveal(void)
{
    char buf[AGENT_REPLY];
    size_t used = 0;
    size_t i = 0;
    if (!g_doc) {
        agent_reply("err no document\n");
        return;
    }
    used = (size_t)snprintf(buf, sizeof(buf), "ok\nreveal=");
    while (i < g_doc->len && used + 8 < sizeof(buf)) {
        char tmp[40];
        const char *lab = doc_code_label(g_doc, i, tmp, sizeof(tmp));
        size_t ln = strlen(lab);
        if (used + ln >= sizeof(buf) - 2) {
            break;
        }
        memcpy(buf + used, lab, ln);
        used += ln;
        i += doc_unit_len(g_doc, i);
    }
    buf[used++] = '\n';
    buf[used] = 0;
    agent_reply(buf);
}

static void reply_dump(void)
{
    char buf[AGENT_REPLY];
    size_t n = 0;
    char *text;
    if (!g_doc) {
        agent_reply("err no document\n");
        return;
    }
    text = doc_range_utf8(g_doc, 0, g_doc->len, &n);
    snprintf(buf, sizeof(buf), "ok\n%.*s\n", (int)(n > 3500 ? 3500 : n), text ? text : "");
    free(text);
    agent_reply(buf);
}

static int is_query(const char *p)
{
    return !strcmp(p, "ping") || !strcmp(p, "status") || !strcmp(p, "dump") ||
           !strcmp(p, "reveal") || !strcmp(p, "help");
}

static void handle_query(const char *p)
{
    if (!strcmp(p, "ping")) {
        agent_reply("pong\n");
        return;
    }
    if (!strcmp(p, "status")) {
        reply_status();
        return;
    }
    if (!strcmp(p, "dump")) {
        reply_dump();
        return;
    }
    if (!strcmp(p, "reveal")) {
        reply_reveal();
        return;
    }
    if (!strcmp(p, "help")) {
        agent_reply("ok\nkeys: type, input, f1-f12, font, italic, fine, small, large, "
                    "vry, ext, normal, normsize, just, just-full, just-left, just-right, "
                    "enter, left, right, home, end, backspace\nqueries: ping status dump reveal\n");
        return;
    }
}

static void ingest_line(char *line)
{
    char text[1024];
    int key;
    char *p = line;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p == '#' || *p == '\0') {
        return;
    }
    if (is_query(p)) {
        handle_query(p);
        return;
    }
    key = wp_parse_key_line(p, text, sizeof(text));
    if (key == -2) {
        q_push_text(text);
        return;
    }
    if (key >= 0) {
        q_push_key(key);
        return;
    }
    agent_reply("err unknown command\n");
}

static void ingest_packet(char *msg, size_t n)
{
    char *p = msg;
    char *end = msg + n;
    int queued0 = g_kn;
    if (n && msg[n - 1] == '\0') {
        end = msg + n - 1;
    }
    while (p < end) {
        char *nl = p;
        char save;
        while (nl < end && *nl != '\n' && *nl != '\r') {
            nl++;
        }
        save = *nl;
        *nl = '\0';
        ingest_line(p);
        *nl = save;
        p = nl;
        while (p < end && (*p == '\n' || *p == '\r')) {
            p++;
        }
    }
    if (g_kn > queued0) {
        agent_reply("ok\n");
    }
}

int agent_start(int port)
{
    struct sockaddr_in addr;
    int one = 1;
    int flags;
    if (g_fd >= 0) {
        return 0;
    }
    if (port <= 0) {
        port = WP_AGENT_DEFAULT_PORT;
    }
    g_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_fd < 0) {
        return -1;
    }
    setsockopt(g_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if (bind(g_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(g_fd);
        g_fd = -1;
        return -1;
    }
    flags = fcntl(g_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(g_fd, F_SETFL, flags | O_NONBLOCK);
    }
    g_port = port;
    fprintf(stderr, "wp: agent 127.0.0.1:%d\n", g_port);
    return 0;
}

void agent_stop(void)
{
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }
    g_kn = g_ki = 0;
    g_have_peer = 0;
}

void agent_set_context(App *a, Doc *d, View *v)
{
    g_app = a;
    g_doc = d;
    g_view = v;
}

int agent_active(void)
{
    return g_fd >= 0;
}

int agent_port(void)
{
    return g_port;
}

static void recv_pending(void)
{
    char buf[2048];
    for (;;) {
        g_peerlen = sizeof(g_peer);
        ssize_t n = recvfrom(g_fd, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&g_peer, &g_peerlen);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            break;
        }
        if (n == 0) {
            break;
        }
        buf[n] = '\0';
        g_have_peer = 1;
        ingest_packet(buf, (size_t)n);
    }
}

int agent_poll(int *key)
{
    if (g_fd < 0) {
        return 0;
    }
    recv_pending();
    if (g_ki >= g_kn) {
        g_ki = g_kn = 0;
        return 0;
    }
    if (key) {
        *key = g_keys[g_ki++];
    }
    if (g_ki >= g_kn) {
        g_ki = g_kn = 0;
    }
    return 1;
}

void agent_wait(void)
{
    fd_set rfds;
    if (g_fd < 0) {
        return;
    }
    FD_ZERO(&rfds);
    FD_SET(g_fd, &rfds);
    select(g_fd + 1, &rfds, NULL, NULL, NULL);
}

void agent_dispatch(App *a, Doc *d, View *v)
{
    int key;
    agent_set_context(a, d, v);
    while (agent_poll(&key) == 1) {
        app_handle_key(a, d, v, key);
    }
}
