#include "doc.h"
#include "iofmt.h"
#include "view.h"
#include "wpd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int failures;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        failures++;
    }
}

static void test_empty_wpd(void)
{
    Doc d;
    doc_init(&d);
    size_t n = 0;
    uint8_t *buf = wpd_encode(&d, &n);
    expect(buf != NULL, "encode empty");
    expect(n == 16, "empty file is 16-byte header");
    expect(buf && buf[0] == 0xFF && buf[1] == 'W' && buf[2] == 'P' && buf[3] == 'C', "magic");
    expect(buf && buf[8] == 1 && buf[9] == 0x0A, "product/type");
    expect(buf && buf[10] == 0 && buf[11] == 1, "WP 5.1 version 00.01");
    expect(buf && buf[4] == 16 && buf[5] == 0 && buf[6] == 0 && buf[7] == 0, "doc pointer 16");

    Doc e;
    doc_init(&e);
    expect(wpd_decode(&e, buf, n) == 0, "decode empty");
    expect(e.len == 0, "empty doc area");
    free(buf);
    doc_free(&d);
    doc_free(&e);
}

static void test_paragraph(void)
{
    Doc d;
    doc_init(&d);
    const char *s = "Hello";
    size_t i;
    for (i = 0; s[i]; i++) {
        doc_insert_char(&d, s[i]);
    }
    doc_insert_hard_return(&d);
    expect(d.len == 6, "Hello + HRt");
    expect(d.data[5] == WP_HARD_EOL, "hard return");

    size_t n = 0;
    uint8_t *buf = wpd_encode(&d, &n);
    expect(n == 22, "16 + 6");
    expect(memcmp(buf + 16, "Hello", 5) == 0, "text in doc area");
    expect(buf[21] == WP_HARD_EOL, "HRt at end");

    Doc e;
    doc_init(&e);
    expect(wpd_decode(&e, buf, n) == 0, "round-trip paragraph");
    expect(e.len == 6, "round-trip len");
    expect(memcmp(e.data, d.data, 6) == 0, "round-trip bytes");
    free(buf);
    doc_free(&d);
    doc_free(&e);
}

static void test_bold_underline(void)
{
    Doc d;
    doc_init(&d);
    doc_toggle_attr(&d, WP_ATTR_BOLD);
    doc_insert_char(&d, 'B');
    doc_toggle_attr(&d, WP_ATTR_BOLD);
    doc_toggle_attr(&d, WP_ATTR_UNDERLINE);
    doc_insert_char(&d, 'U');
    doc_toggle_attr(&d, WP_ATTR_UNDERLINE);

    /* C3 0C C3 'B' C4 0C C4 C3 0E C3 'U' C4 0E C4 */
    expect(d.len == 14, "attr stream length");
    expect(d.data[0] == WP_ATTR_ON && d.data[1] == WP_ATTR_BOLD && d.data[2] == WP_ATTR_ON,
           "bold on");
    expect(d.data[3] == 'B', "B");
    expect(d.data[4] == WP_ATTR_OFF && d.data[5] == WP_ATTR_BOLD && d.data[6] == WP_ATTR_OFF,
           "bold off");
    expect(d.data[7] == WP_ATTR_ON && d.data[8] == WP_ATTR_UNDERLINE && d.data[9] == WP_ATTR_ON,
           "und on");
    expect(d.data[10] == 'U', "U");
    expect(d.data[11] == WP_ATTR_OFF && d.data[12] == WP_ATTR_UNDERLINE &&
               d.data[13] == WP_ATTR_OFF,
           "und off");
    expect(doc_attrs_at(&d, 3) & ATTR_BIT_BOLD, "bold at B");
    expect(!(doc_attrs_at(&d, 7) & ATTR_BIT_BOLD), "not bold after off");
    expect(doc_attrs_at(&d, 10) & ATTR_BIT_UNDERLINE, "underline at U");

    size_t n = 0;
    uint8_t *buf = wpd_encode(&d, &n);
    Doc e;
    doc_init(&e);
    expect(wpd_decode(&e, buf, n) == 0, "round-trip attrs");
    expect(e.len == d.len && memcmp(e.data, d.data, d.len) == 0, "attr bytes match");
    free(buf);
    doc_free(&d);
    doc_free(&e);
}

static void test_edit(void)
{
    Doc d;
    doc_init(&d);
    doc_insert_char(&d, 'A');
    doc_insert_char(&d, 'B');
    doc_insert_char(&d, 'C');
    expect(d.cursor == 3 && d.len == 3, "typed ABC");
    doc_backspace(&d, 0);
    expect(d.len == 2 && d.data[0] == 'A' && d.data[1] == 'B', "backspace C");
    doc_move_home(&d);
    doc_delete_forward(&d, 0);
    expect(d.len == 1 && d.data[0] == 'B', "delete A");
    doc_free(&d);
}

static void test_file_roundtrip(void)
{
    Doc d;
    doc_init(&d);
    const char *s = "WordPerfect";
    size_t i;
    for (i = 0; s[i]; i++) {
        doc_insert_char(&d, s[i]);
    }
    if (system("mkdir -p tests/out") != 0) {
        expect(0, "mkdir tests/out");
        doc_free(&d);
        return;
    }
    expect(wpd_save(&d, "tests/out/hello.wpd") == 0, "save hello.wpd");
    Doc e;
    doc_init(&e);
    expect(wpd_load(&e, "tests/out/hello.wpd") == 0, "load hello.wpd");
    expect(e.len == d.len && memcmp(e.data, d.data, d.len) == 0, "file bytes");
    FILE *f = fopen("tests/out/hello.wpd", "rb");
    expect(f != NULL, "open saved");
    if (f) {
        unsigned char magic[4];
        expect(fread(magic, 1, 4, f) == 4, "read magic");
        expect(magic[0] == 0xFF && magic[1] == 'W' && magic[2] == 'P' && magic[3] == 'C',
               "file magic FF WPC");
        fclose(f);
    }
    doc_free(&d);
    doc_free(&e);
}

static void test_reject_bad(void)
{
    Doc d;
    doc_init(&d);
    uint8_t bad[] = { 'N', 'O', 'P', 'E' };
    expect(wpd_decode(&d, bad, sizeof(bad)) != 0, "reject non-WPD");
    doc_free(&d);
}

static void test_mac_wpd(void)
{
    Doc d;
    const char *path = "tests/corpus/algeria3.wpd";
    doc_init(&d);
    expect(wpd_load(&d, path) == 0, "load Macintosh WP algeria3.wpd");
    expect(d.len > 8, "mac doc has body");
    {
        int found = 0;
        size_t i;
        for (i = 0; i + 7 <= d.len; i++) {
            if (memcmp(d.data + i, "Jaghbub", 7) == 0) {
                found = 1;
                break;
            }
        }
        expect(found, "algeria3 text Jaghbub");
    }
    doc_free(&d);
}

static int doc_has_visible(const Doc *d, const char *s)
{
    char buf[4096];
    size_t o = 0, i = 0;
    size_t slen = strlen(s);
    buf[0] = 0;
    while (i < d->len && o + 1 < sizeof(buf)) {
        size_t n = doc_unit_len(d, i);
        if (!n) {
            break;
        }
        if (doc_is_visible(d, i)) {
            uint8_t b = d->data[i];
            if (b >= 32 && b < 127) {
                buf[o++] = (char)b;
            }
        }
        i += n;
    }
    buf[o] = 0;
    return slen && strstr(buf, s) != NULL;
}

static void test_wp51_attr_pair(void)
{
    Doc d;
    /* Official WP 5.1: C3 0C C3 From: C4 0C C4 */
    static const uint8_t official[] = {
        WP_ATTR_ON, WP_ATTR_BOLD, WP_ATTR_ON,
        'F', 'r', 'o', 'm', ':',
        WP_ATTR_OFF, WP_ATTR_BOLD, WP_ATTR_OFF
    };
    doc_init(&d);
    expect(doc_insert(&d, 0, official, sizeof(official)) == 0, "insert official attrs");
    expect(doc_unit_len(&d, 0) == 3, "official ON is 3 bytes");
    expect(doc_unit_len(&d, 8) == 3, "official OFF is 3 bytes");
    expect(d.data[3] == 'F', "F not eaten");
    expect(doc_has_visible(&d, "From:"), "official From: visible");
    expect(doc_attrs_at(&d, 3) & ATTR_BIT_BOLD, "official bold on F");
    expect((doc_attrs_at(&d, d.len) & ATTR_BIT_BOLD) == 0, "official bold off after");
    doc_free(&d);

    /* We write official 3-byte ON. Older 2-byte files still apply. */
    doc_init(&d);
    doc_toggle_attr(&d, WP_ATTR_BOLD);
    expect(d.len == 3 && d.data[0] == WP_ATTR_ON && d.data[1] == WP_ATTR_BOLD &&
               d.data[2] == WP_ATTR_ON,
           "we write official 3-byte ON");
    expect(doc_unit_len(&d, 0) == 3, "our ON is 3 bytes");
    doc_insert_char(&d, 'X');
    expect(doc_attrs_at(&d, 3) & ATTR_BIT_BOLD, "3-byte bold applies");
    doc_free(&d);

    doc_init(&d);
    {
        static const uint8_t old2[] = { WP_ATTR_ON, WP_ATTR_BOLD };
        size_t n = 0;
        uint8_t *buf;
        expect(doc_insert(&d, 0, old2, sizeof(old2)) == 0, "insert old 2-byte ON");
        expect(doc_unit_len(&d, 0) == 2, "old ON is 2 bytes");
        doc_insert_char(&d, 'X');
        expect(doc_attrs_at(&d, 2) & ATTR_BIT_BOLD, "2-byte bold still applies");
        buf = wpd_encode(&d, &n);
        expect(buf && n >= 20, "encode old 2-byte");
        expect(buf[8] == 1 && buf[9] == 0x0A && buf[10] == 0 && buf[11] == 1,
               "saved header is WP 5.1 document");
        expect(n >= 20 && buf[16] == WP_ATTR_ON && buf[17] == WP_ATTR_BOLD &&
                   buf[18] == WP_ATTR_ON && buf[19] == 'X',
               "save upgrades 2-byte ON to official 3-byte");
        free(buf);
    }
    doc_free(&d);

    doc_init(&d);
    expect(doc_insert_just(&d, WP_JUST_FULL) == 0, "just for official save");
    expect(doc_insert_font(&d, WP_FONT_TIMES, 12) == 0, "font for official save");
    doc_insert_char(&d, 'Q');
    {
        size_t n = 0, i;
        uint8_t *buf = wpd_encode(&d, &n);
        int private = 0;
        expect(buf && n > 16 && buf[0] == 0xFF && buf[1] == 'W' && buf[2] == 'P' && buf[3] == 'C',
               "official magic");
        expect(buf[8] == 1 && buf[9] == 0x0A, "product 1 type document");
        for (i = 16; i < n; i++) {
            if (buf[i] == WP_UTF8 || buf[i] == WP_FONT || buf[i] == WP_JUST) {
                private = 1;
            }
        }
        expect(!private, "save omits C6/C7/C8 so WP 5.1 can open it");
        expect(n > 16 && buf[n - 1] == 'Q', "text survives official save");
        free(buf);
    }
    doc_free(&d);

    doc_init(&d);
    expect(wpd_load(&d, "tests/corpus/memo.wpd") == 0, "load LEARN memo.wpd");
    expect(doc_has_visible(&d, "From:"), "memo From:");
    expect(doc_has_visible(&d, "Date:"), "memo Date:");
    expect(doc_has_visible(&d, "Subject:"), "memo Subject:");
    doc_free(&d);
}

static void test_search_indent_merge_center(void)
{
    Doc d;
    doc_init(&d);
    const char *s = "one two two three";
    size_t i;
    for (i = 0; s[i]; i++) {
        doc_insert_char(&d, s[i]);
    }
    doc_move_home(&d);
    expect(doc_search(&d, "two", 0) == 0, "find first two");
    expect(d.cursor == 4, "cursor on first two");
    expect(doc_search(&d, "two", 0) == 0, "find second two");
    expect(d.cursor == 8, "cursor on second two");
    expect(doc_search(&d, "xyz", 0) != 0, "missing string");

    doc_move_home(&d);
    doc_insert_code(&d, WP_INDENT);
    expect(d.data[0] == WP_INDENT, "indent code");
    doc_insert_code(&d, WP_CENTER);
    expect(d.data[1] == WP_CENTER, "center code");
    doc_move_end(&d);
    doc_insert_code(&d, WP_END_FIELD);
    expect(d.data[d.len - 1] == WP_END_FIELD, "end field");
    expect(doc_is_visible(&d, d.len - 1), "end field visible");
    doc_free(&d);
}

static void test_utf8_and_specials(void)
{
    Doc d;
    uint8_t u8[8];
    size_t n;
    doc_init(&d);
    expect(doc_insert_utf8(&d, "中") == 0, "insert CJK");
    expect(d.len == 5, "C6 + len + 3 bytes");
    expect(d.data[0] == WP_UTF8, "utf8 wrapper");
    expect(d.data[1] == 3, "utf8 nbytes");
    expect(d.data[2] == 0xE4 && d.data[3] == 0xB8 && d.data[4] == 0xAD, "中 bytes");
    expect(doc_unit_len(&d, 0) == 5, "unit 5");
    expect(doc_display_cp(&d, 0) == 0x4E2D, "U+4E2D");
    expect(doc_display_width(&d, 0) == 1, "CJK width 1");
    doc_move_home(&d);
    expect(doc_search(&d, "中", 0) == 0, "search 中");

    doc_clear(&d);
    doc_insert_char(&d, 'A');
    doc_insert_hard_return(&d);
    expect(doc_display_cp(&d, 1) == 0x23CE, "HRt is ⏎");
    expect(doc_display_width(&d, 1) == 1, "HRt width 1");
    n = wp_utf8_encode(0x23CE, u8);
    expect(n == 3, "⏎ utf8 len");

    doc_clear(&d);
    expect(doc_insert_utf8(&d, "Hello\n世界") == 0, "insert Hello/world");
    expect(doc_search(&d, "世界", 0) == 0, "search 世界");
    expect(d.data[d.cursor] == WP_UTF8, "cursor on 世");

    doc_clear(&d);
    doc_insert_char(&d, '\t');
    expect(doc_display_cp(&d, 0) == 0x21E5, "Tab is ⇥");
    doc_free(&d);
}

static void mkdir_out(void)
{
    mkdir("tests/out", 0755);
}

static void test_export_and_wrap(void)
{
    Doc d, e;
    View v;
    char mdpath[] = "tests/out/round.md";
    char docxpath[] = "tests/out/round.docx";
    char pdfpath[] = "tests/out/round.pdf";
    char resolved[256];

    mkdir_out();
    doc_init(&d);
    doc_insert_utf8(&d, "Hello world");
    doc_insert_hard_return(&d);
    doc_insert_utf8(&d, "中文");
    expect(io_save(&d, mdpath, resolved, sizeof(resolved)) == 0, "save md");
    doc_init(&e);
    expect(io_load(&e, mdpath) == 0, "load md");
    expect(doc_search(&e, "Hello", 0) == 0, "md has Hello");
    expect(doc_search(&e, "中文", 0) == 0, "md has 中文");
    doc_free(&e);

    expect(io_save(&d, docxpath, resolved, sizeof(resolved)) == 0, "save docx");
    doc_init(&e);
    expect(io_load(&e, docxpath) == 0, "load docx");
    expect(doc_search(&e, "Hello", 0) == 0, "docx has Hello");
    expect(doc_search(&e, "中文", 0) == 0, "docx has 中文");
    doc_free(&e);

    doc_init(&e);
    expect(doc_insert_code(&e, WP_CENTER) == 0, "docx center code");
    expect(doc_insert_utf8(&e, "Title") == 0, "docx center text");
    expect(io_save(&e, docxpath, resolved, sizeof(resolved)) == 0, "save centered docx");
    doc_free(&e);
    doc_init(&e);
    expect(io_load(&e, docxpath) == 0, "load centered docx");
    expect(e.len > 0 && e.data[0] == WP_CENTER, "docx restores center");
    expect(doc_search(&e, "Title", 0) == 0, "docx centered title");
    doc_free(&e);

    expect(io_save(&d, pdfpath, resolved, sizeof(resolved)) == 0, "save pdf");
    doc_init(&e);
    expect(io_load(&e, pdfpath) == 0, "load pdf editable stream");
    expect(doc_search(&e, "Hello", 0) == 0, "pdf has Hello");
    expect(doc_search(&e, "中文", 0) == 0, "pdf has 中文");
    doc_free(&e);
    doc_free(&d);

    doc_init(&d);
    {
        int k;
        int tj = 0;
        FILE *pf;
        for (k = 0; k < 20; k++) {
            expect(doc_insert_utf8(&d, "hello ") == 0, "pdf wrap pad");
        }
        io_set_wrap_mode(IO_WRAP_WORD);
        expect(io_save(&d, pdfpath, resolved, sizeof(resolved)) == 0, "save wrapped pdf");
        pf = fopen(pdfpath, "rb");
        expect(pf != NULL, "open wrapped pdf");
        if (pf) {
            int c, prev = 0;
            while ((c = fgetc(pf)) != EOF) {
                if (prev == 'T' && c == 'j') {
                    tj++;
                }
                prev = c;
            }
            fclose(pf);
        }
        expect(tj >= 2, "pdf wraps long paragraph");
        {
            FILE *cf = fopen(pdfpath, "rb");
            char *all = NULL;
            long sz = 0;
            expect(cf != NULL, "reopen wrapped pdf");
            if (cf) {
                if (fseek(cf, 0, SEEK_END) == 0) {
                    sz = ftell(cf);
                }
                if (sz > 0 && fseek(cf, 0, SEEK_SET) == 0) {
                    all = malloc((size_t)sz + 1);
                    if (all && fread(all, 1, (size_t)sz, cf) == (size_t)sz) {
                        all[sz] = 0;
                        expect(strstr(all, "/F1 3 0 R /F2 4 0 R") != NULL,
                               "pdf page dictionary is complete");
                        {
                            const char *p = all;
                            while ((p = strstr(p, "1 0 0 1 ")) != NULL) {
                                double tm_x = 0;
                                p += 8;
                                if (sscanf(p, "%lf", &tm_x) == 1) {
                                    expect(tm_x >= 72.0 && tm_x < 540.0,
                                           "wrapped pdf stays inside right margin");
                                }
                            }
                        }
                    }
                }
                free(all);
                fclose(cf);
            }
        }
    }
    doc_free(&d);

    doc_init(&d);
    expect(doc_insert_code(&d, WP_CENTER) == 0, "insert center");
    expect(doc_insert_utf8(&d, "Title") == 0, "centered title");
    expect(doc_insert_hard_return(&d) == 0, "title return");
    expect(io_save(&d, pdfpath, resolved, sizeof(resolved)) == 0, "save centered pdf");
    {
        FILE *cf = fopen(pdfpath, "rb");
        char *all = NULL;
        long sz = 0;
        expect(cf != NULL, "open centered pdf");
        if (cf && fseek(cf, 0, SEEK_END) == 0) {
            sz = ftell(cf);
            if (sz > 0 && fseek(cf, 0, SEEK_SET) == 0) {
                all = malloc((size_t)sz + 1);
                if (all && fread(all, 1, (size_t)sz, cf) == (size_t)sz) {
                    char *hit;
                    all[sz] = 0;
                    hit = strstr(all, "Title");
                    expect(hit != NULL, "pdf has Title");
                    expect(strstr(all, "1 0 0 1 288.00") != NULL ||
                           strstr(all, "1 0 0 1 288") != NULL,
                           "pdf centers title inside 1-inch margins");
                    {
                        const char *p = all;
                        while ((p = strstr(p, "1 0 0 1 ")) != NULL) {
                            double tm_x = 0;
                            p += 8;
                            if (sscanf(p, "%lf", &tm_x) == 1) {
                                expect(tm_x >= 72.0 && tm_x < 540.0,
                                       "pdf text stays inside printable margins");
                            }
                        }
                    }
                }
            }
        }
        free(all);
        if (cf) {
            fclose(cf);
        }
    }
    doc_free(&d);

    doc_init(&d);
    view_init(&v);
    doc_insert_utf8(&d, "hello world");
    v.wrap = 8;
    v.wrap_word = 1;
    view_relayout(&v, &d);
    expect(v.nlines >= 2, "word wrap splits on space");
    expect(v.line_start[1] == 6, "second line starts at 'w'");

    v.wrap_word = 0;
    view_relayout(&v, &d);
    expect(v.nlines >= 2, "strict wrap still splits");
    expect(v.line_start[1] == 8, "strict wrap at column 8");
    view_free(&v);
    doc_free(&d);

    expect(io_default_format() == IO_FMT_DOCX, "default format is docx");
    {
        char out[256];
        expect(io_path_for_format("declaration.pdf", IO_FMT_MD, out, sizeof(out)) == 0,
               "rewrite ext");
        expect(strcmp(out, "declaration.md") == 0, "pdf stem becomes md");
        expect(io_path_for_format("/tmp/doc.wpd", IO_FMT_DOCX, out, sizeof(out)) == 0,
               "rewrite wpd");
        expect(strcmp(out, "/tmp/doc.docx") == 0, "wpd stem becomes docx");
        expect(io_format_from_path("/tmp/letter.wps") == IO_FMT_WPD, "wps is wp document");
        expect(io_format_from_path("/tmp/LETTER.WPS") == IO_FMT_WPD, "WPS case");
        expect(io_format_from_path("/tmp/memo.wp") == IO_FMT_WPD, "wp is wp document");
        expect(io_path_for_format("/tmp/letter.wps", IO_FMT_WPD, out, sizeof(out)) == 0,
               "keep wps");
        expect(strcmp(out, "/tmp/letter.wps") == 0, "wps stays wps for WP save");
    }
}

static int doc_has_code(const Doc *d, uint8_t code)
{
    size_t i = 0;
    while (i < d->len) {
        if (d->data[i] == code) {
            return 1;
        }
        i += doc_unit_len(d, i);
    }
    return 0;
}

static void fill_styled(Doc *d)
{
    doc_init(d);
    expect(doc_insert_code(d, WP_CENTER) == 0, "style center");
    expect(doc_toggle_attr(d, WP_ATTR_BOLD) == 0, "style bold on");
    expect(doc_insert_utf8(d, "Bold") == 0, "style bold text");
    expect(doc_toggle_attr(d, WP_ATTR_BOLD) == 0, "style bold off");
    expect(doc_toggle_attr(d, WP_ATTR_UNDERLINE) == 0, "style und on");
    expect(doc_insert_utf8(d, "Under") == 0, "style und text");
    expect(doc_toggle_attr(d, WP_ATTR_UNDERLINE) == 0, "style und off");
}

static void expect_styled(const Doc *d, const char *label)
{
    char msg[80];
    snprintf(msg, sizeof(msg), "%s keeps center", label);
    expect(doc_has_code(d, WP_CENTER), msg);
    ((Doc *)d)->cursor = ((Doc *)d)->len;
    snprintf(msg, sizeof(msg), "%s has Bold", label);
    expect(doc_search((Doc *)d, "Bold", 0) == 0, msg);
    snprintf(msg, sizeof(msg), "%s bold attr", label);
    expect((doc_attrs_at(d, ((Doc *)d)->cursor) & ATTR_BIT_BOLD) != 0, msg);
    ((Doc *)d)->cursor = ((Doc *)d)->len;
    snprintf(msg, sizeof(msg), "%s has Under", label);
    expect(doc_search((Doc *)d, "Under", 0) == 0, msg);
    snprintf(msg, sizeof(msg), "%s underline attr", label);
    expect((doc_attrs_at(d, ((Doc *)d)->cursor) & ATTR_BIT_UNDERLINE) != 0, msg);
}

static void test_styles_all_formats(void)
{
    Doc d, e;
    char resolved[256];
    const char *paths[] = {
        "tests/out/style.docx",
        "tests/out/style.md",
        "tests/out/style.pdf",
        "tests/out/style.wpd"
    };
    int i;
    mkdir_out();
    fill_styled(&d);
    for (i = 0; i < 4; i++) {
        expect(io_save(&d, paths[i], resolved, sizeof(resolved)) == 0, paths[i]);
        doc_init(&e);
        expect(io_load(&e, paths[i]) == 0, paths[i]);
        expect_styled(&e, paths[i]);
        doc_free(&e);
    }
    {
        FILE *mf = fopen("tests/out/style.md", "rb");
        char buf[256];
        size_t n = 0;
        expect(mf != NULL, "open style md");
        if (mf) {
            n = fread(buf, 1, sizeof(buf) - 1, mf);
            buf[n] = 0;
            fclose(mf);
        }
        expect(strstr(buf, "<center>") != NULL, "md writes center");
        expect(strstr(buf, "**Bold**") != NULL, "md writes bold");
        expect(strstr(buf, "_Under_") != NULL, "md writes underline");
    }
    {
        FILE *pf = fopen("tests/out/style.pdf", "rb");
        char *all = NULL;
        long sz = 0;
        expect(pf != NULL, "open style pdf");
        if (pf && fseek(pf, 0, SEEK_END) == 0) {
            sz = ftell(pf);
            if (sz > 0 && fseek(pf, 0, SEEK_SET) == 0) {
                all = malloc((size_t)sz + 1);
                if (all && fread(all, 1, (size_t)sz, pf) == (size_t)sz) {
                    all[sz] = 0;
                    expect(strstr(all, "/Courier-Bold") != NULL, "pdf has bold font");
                    expect(strstr(all, " l S\n") != NULL, "pdf underlines");
                    expect(strstr(all, "Bold") != NULL, "pdf has Bold");
                    expect(strstr(all, "Under") != NULL, "pdf has Under");
                }
            }
        }
        free(all);
        if (pf) {
            fclose(pf);
        }
    }
    doc_free(&d);
}

static void test_selection(void)
{
    Doc d;
    char *s;
    size_t n = 0;
    doc_init(&d);
    doc_insert_utf8(&d, "Hello");
    d.mark = 0;
    expect(doc_sel_active(&d), "sel active");
    s = doc_range_utf8(&d, 0, d.len, &n);
    expect(s && !strcmp(s, "Hello"), "range Hello");
    free(s);
    d.cursor = 2;
    d.mark = 5;
    expect(doc_sel_delete(&d) == 0, "cut tail");
    expect(d.len == 2, "He left");
    expect(!doc_sel_active(&d), "sel cleared");
    doc_sel_all(&d);
    doc_insert_utf8(&d, "中");
    expect(d.data[0] == WP_UTF8, "replace with CJK");
    doc_free(&d);
}

static void test_fonts(void)
{
    Doc d, e;
    DocFont f;
    char resolved[256];
    char tmp[40];
    const char *lab;
    mkdir_out();

    doc_init(&d);
    doc_insert_utf8(&d, "hello ");
    expect(doc_insert_font(&d, WP_FONT_TIMES, 12) == 0, "insert Times 12");
    expect(doc_toggle_attr(&d, WP_ATTR_LARGE) == 0, "large on");
    expect(doc_insert_utf8(&d, "World") == 0, "world");
    expect(doc_toggle_attr(&d, WP_ATTR_LARGE) == 0, "large off");
    expect(d.data[6] == WP_FONT && d.data[7] == WP_FONT_TIMES && d.data[8] == 12, "font code");
    expect(d.data[9] == WP_ATTR_ON && d.data[10] == WP_ATTR_LARGE, "LARGE on");
    lab = doc_code_label(&d, 6, tmp, sizeof(tmp));
    expect(strstr(lab, "Times") != NULL && strstr(lab, "12") != NULL, "reveal Font:Times 12pt");
    lab = doc_code_label(&d, 9, tmp, sizeof(tmp));
    expect(!strcmp(lab, "[LARGE]"), "reveal [LARGE]");
    doc_font_at(&d, 12, &f);
    expect(f.family == WP_FONT_TIMES && f.size_pt == 12, "font at World");
    expect((f.attrs & ATTR_BIT_LARGE) != 0, "large bit");
    expect(wp_effective_pt(&f) == 14, "12pt * 1.2 = 14");

    expect(io_save(&d, "tests/out/font.pdf", resolved, sizeof(resolved)) == 0, "save font pdf");
    expect(io_save(&d, "tests/out/font.md", resolved, sizeof(resolved)) == 0, "save font md");
    expect(io_save(&d, "tests/out/font.docx", resolved, sizeof(resolved)) == 0, "save font docx");
    expect(io_save(&d, "tests/out/font-core.wpd", resolved, sizeof(resolved)) == 0, "save font wpd");

    {
        FILE *pf = fopen("tests/out/font.pdf", "rb");
        char *all = NULL;
        long sz = 0;
        expect(pf != NULL, "open font pdf");
        if (pf && fseek(pf, 0, SEEK_END) == 0) {
            sz = ftell(pf);
            if (sz > 0 && fseek(pf, 0, SEEK_SET) == 0) {
                all = malloc((size_t)sz + 1);
                if (all && fread(all, 1, (size_t)sz, pf) == (size_t)sz) {
                    all[sz] = 0;
                    expect(strstr(all, "/Times-Roman") != NULL, "pdf has Times-Roman");
                    expect(strstr(all, "/Helvetica") != NULL, "pdf declares Helvetica");
                    expect(strstr(all, "World") != NULL, "pdf has World");
                }
            }
        }
        free(all);
        if (pf) {
            fclose(pf);
        }
    }
    {
        FILE *mf = fopen("tests/out/font.md", "rb");
        char buf[512];
        size_t n = 0;
        expect(mf != NULL, "open font md");
        if (mf) {
            n = fread(buf, 1, sizeof(buf) - 1, mf);
            buf[n] = 0;
            fclose(mf);
        }
        expect(strstr(buf, "data-wp-font=\"Times\"") != NULL, "md times span");
        expect(strstr(buf, "data-wp-size=\"LARGE\"") != NULL, "md large span");
        expect(strstr(buf, "hello") != NULL && strstr(buf, "World") != NULL, "md text");
    }

    doc_init(&e);
    expect(io_load(&e, "tests/out/font.md") == 0, "load font md");
    expect(doc_search(&e, "World", 0) == 0, "md has World");
    doc_font_at(&e, e.cursor, &f);
    expect(f.family == WP_FONT_TIMES, "md roundtrip Times");
    expect((doc_attrs_at(&e, e.cursor) & ATTR_BIT_LARGE) != 0, "md roundtrip LARGE");
    doc_free(&e);

    doc_init(&e);
    expect(io_load(&e, "tests/out/font.docx") == 0, "load font docx");
    expect(doc_search(&e, "World", 0) == 0, "docx has World");
    doc_font_at(&e, e.cursor, &f);
    expect(f.family == WP_FONT_TIMES, "docx roundtrip Times");
    doc_free(&e);

    doc_init(&e);
    expect(io_load(&e, "tests/out/font.pdf") == 0, "load font pdf sidecar");
    expect(e.len == d.len && memcmp(e.data, d.data, d.len) == 0, "pdf sidecar lossless");
    doc_free(&e);

    doc_sel_all(&d);
    expect(doc_insert_font(&d, WP_FONT_HELVETICA, 14) == 0, "wrap sel Helv 14");
    expect(d.data[0] == WP_FONT && d.data[1] == WP_FONT_HELVETICA && d.data[2] == 14,
           "sel font at start");
    doc_free(&d);

    doc_init(&d);
    doc_toggle_attr(&d, WP_ATTR_LARGE);
    doc_toggle_attr(&d, WP_ATTR_FINE);
    expect(d.len == 3, "wrong size then wanted size replaces, does not stack");
    expect(d.data[0] == WP_ATTR_ON && d.data[1] == WP_ATTR_FINE && d.data[2] == WP_ATTR_ON,
           "pending size is Fine");
    expect(!(doc_attrs_at(&d, d.cursor) & ATTR_BIT_LARGE), "Large did not remain");
    expect((doc_attrs_at(&d, d.cursor) & ATTR_BIT_FINE) != 0, "Fine is on");
    doc_normal_size(&d);
    expect(d.len == 0, "Normal removes unused pending size");
    expect(doc_attrs_at(&d, d.cursor) == 0, "normal size clears");
    doc_toggle_attr(&d, WP_ATTR_LARGE);
    doc_insert_utf8(&d, "Hi");
    doc_toggle_attr(&d, WP_ATTR_FINE);
    expect(d.data[0] == WP_ATTR_ON && d.data[1] == WP_ATTR_LARGE && d.data[2] == WP_ATTR_ON,
           "Large stays around typed text");
    expect(d.data[3] == 'H' && d.data[4] == 'i', "Hi after Large");
    expect(d.data[5] == WP_ATTR_OFF && d.data[6] == WP_ATTR_LARGE && d.data[7] == WP_ATTR_OFF,
           "Large off after Hi");
    expect(d.data[8] == WP_ATTR_ON && d.data[9] == WP_ATTR_FINE && d.data[10] == WP_ATTR_ON,
           "Fine on after replace");
    doc_insert_utf8(&d, "Z");
    doc_normal_size(&d);
    expect(d.data[d.len - 3] == WP_ATTR_OFF && d.data[d.len - 2] == WP_ATTR_FINE &&
               d.data[d.len - 1] == WP_ATTR_OFF,
           "Normal size closes Fine");
    expect(doc_attrs_at(&d, d.cursor) == 0, "cursor normal after size Normal");
    doc_toggle_attr(&d, WP_ATTR_ITALIC);
    expect((doc_attrs_at(&d, d.cursor) & ATTR_BIT_ITALIC) != 0, "italic on");
    doc_normal_attr(&d);
    expect(doc_attrs_at(&d, d.cursor) == 0, "Normal clears italic");
    doc_free(&d);

    doc_init(&d);
    doc_insert_utf8(&d, "ab");
    d.mark = 0;
    d.cursor = 2;
    doc_toggle_attr(&d, WP_ATTR_LARGE);
    d.mark = 3;
    d.cursor = 5;
    doc_toggle_attr(&d, WP_ATTR_FINE);
    expect(d.data[0] == WP_ATTR_ON && d.data[1] == WP_ATTR_FINE && d.data[2] == WP_ATTR_ON,
           "sel size replace starts Fine");
    expect(d.data[3] == 'a' && d.data[4] == 'b', "sel text kept");
    expect(d.data[5] == WP_ATTR_OFF && d.data[6] == WP_ATTR_FINE && d.data[7] == WP_ATTR_OFF,
           "sel size replace ends Fine");
    expect(d.len == 8, "sel size replace is one pair");
    doc_free(&d);
}

static void test_reveal_unbold(void)
{
    Doc d;
    size_t i;
    unsigned before;

    doc_init(&d);
    doc_toggle_attr(&d, WP_ATTR_BOLD);
    doc_toggle_attr(&d, WP_ATTR_UNDERLINE);
    doc_insert_utf8(&d, "Hi");
    expect(d.data[0] == WP_ATTR_ON && d.data[1] == WP_ATTR_BOLD && d.data[2] == WP_ATTR_ON,
           "starts with [BOLD]");
    expect(d.data[3] == WP_ATTR_ON && d.data[4] == WP_ATTR_UNDERLINE && d.data[5] == WP_ATTR_ON,
           "then [UND]");
    d.cursor = 6; /* on 'H' */
    doc_move_left(&d, 0);
    expect(d.cursor == 0, "reveal-off left skips hidden codes");
    d.cursor = 6;
    doc_move_left(&d, 1);
    expect(d.cursor == 3 && d.data[3] == WP_ATTR_ON && d.data[4] == WP_ATTR_UNDERLINE,
           "reveal-on left lands on [UND]");
    doc_move_left(&d, 1);
    expect(d.cursor == 0 && d.data[0] == WP_ATTR_ON, "next left is [BOLD]");
    expect(doc_delete_forward(&d, 1) == 0, "delete [BOLD]");
    expect(d.data[0] == WP_ATTR_ON && d.data[1] == WP_ATTR_UNDERLINE, "UND remains");
    expect(!(doc_attrs_at(&d, 3) & ATTR_BIT_BOLD), "H not bold after delete ON");
    expect((doc_attrs_at(&d, 3) & ATTR_BIT_UNDERLINE) != 0, "H still underlined");
    doc_free(&d);

    doc_init(&d);
    doc_toggle_attr(&d, WP_ATTR_BOLD);
    doc_insert_utf8(&d, "Hi");
    doc_toggle_attr(&d, WP_ATTR_BOLD);
    d.cursor = 3;
    expect(doc_backspace(&d, 1) == 0, "reveal backspace deletes [BOLD]");
    expect(d.data[0] == 'H', "backspace removed ON");
    doc_free(&d);

    doc_init(&d);
    doc_toggle_attr(&d, WP_ATTR_BOLD);
    doc_insert_utf8(&d, "Hi");
    doc_toggle_attr(&d, WP_ATTR_BOLD);
    doc_sel_all(&d);
    before = 0;
    for (i = 0; i < d.len; i += doc_unit_len(&d, i)) {
        if (d.data[i] == 'H' || d.data[i] == 'i') {
            before |= doc_attrs_at(&d, i);
        }
    }
    expect((before & ATTR_BIT_BOLD) != 0, "selection is bold");
    expect(doc_toggle_attr(&d, WP_ATTR_BOLD) == 0, "F6 unwraps selection");
    expect(!(doc_attrs_at(&d, 0) & ATTR_BIT_BOLD), "unbolded after select+F6");
    expect(d.data[0] == 'H' || d.data[0] == WP_ATTR_OFF, "text or leftover off");
    {
        int has_h = 0;
        for (i = 0; i < d.len; i += doc_unit_len(&d, i) ? doc_unit_len(&d, i) : 1) {
            if (d.data[i] == 'H') {
                has_h = 1;
                expect(!(doc_attrs_at(&d, i) & ATTR_BIT_BOLD), "H not bold");
            }
        }
        expect(has_h, "H survived unwrap");
    }
    doc_free(&d);
}

static void test_strip_codes(void)
{
    Doc d;
    doc_init(&d);
    doc_insert_code(&d, WP_CENTER);
    doc_toggle_attr(&d, WP_ATTR_BOLD);
    doc_insert_utf8(&d, "Hi");
    doc_toggle_attr(&d, WP_ATTR_BOLD);
    doc_insert_font(&d, WP_FONT_TIMES, 12);
    doc_insert_just(&d, WP_JUST_FULL);
    expect(doc_strip_codes(&d) == 0, "strip");
    expect(d.len == 2 && d.data[0] == 'H' && d.data[1] == 'i', "only text remains");
    expect(doc_attrs_at(&d, 0) == 0, "no attrs after strip");
    doc_free(&d);
}

static void test_justification(void)
{
    Doc d;
    char tmp[40];
    const char *lab;
    char resolved[256];
    char *md;
    FILE *f;
    long sz;

    doc_init(&d);
    expect(doc_insert_just(&d, WP_JUST_FULL) == 0, "insert full");
    expect(d.data[0] == WP_JUST && d.data[1] == WP_JUST_FULL, "Just Full bytes");
    expect(doc_just_at(&d, 0) == WP_JUST_FULL, "just at code");
    lab = doc_code_label(&d, 0, tmp, sizeof(tmp));
    expect(!strcmp(lab, "[Just:Full]"), "reveal [Just:Full]");
    doc_insert_utf8(&d, "When in the Course of human events it becomes necessary");
    expect(doc_just_at(&d, d.cursor) == WP_JUST_FULL, "just persists");
    d.cursor = 2;
    doc_insert_just(&d, WP_JUST_LEFT);
    expect(d.data[0] == WP_JUST && d.data[1] == WP_JUST_LEFT, "replace adjacent just");
    d.cursor = 0;
    doc_insert_just(&d, WP_JUST_FULL);
    expect(d.data[0] == WP_JUST && d.data[1] == WP_JUST_FULL, "replace at cursor");
    mkdir_out();
    expect(io_save(&d, "tests/out/just.md", resolved, sizeof(resolved)) == 0, "save just md");
    f = fopen("tests/out/just.md", "rb");
    expect(f != NULL, "open just md");
    md = NULL;
    sz = 0;
    if (f && fseek(f, 0, SEEK_END) == 0) {
        sz = ftell(f);
        if (sz > 0 && fseek(f, 0, SEEK_SET) == 0) {
            md = malloc((size_t)sz + 1);
            if (md && fread(md, 1, (size_t)sz, f) == (size_t)sz) {
                md[sz] = 0;
                expect(strstr(md, "text-align:justify") != NULL, "md justify style");
                expect(strstr(md, "data-wp-just=\"FULL\"") != NULL, "md just attr");
            }
        }
    }
    if (f) {
        fclose(f);
    }
    free(md);
    {
        char pdfpath[256];
        char *pdf = NULL;
        expect(io_path_for_format("tests/out/just-full", IO_FMT_PDF, pdfpath, sizeof(pdfpath)) == 0,
               "just pdf path");
        d.cursor = d.len;
        expect(doc_insert_utf8(&d, "\xE2\x80\x94That whenever any Form of Government becomes "
                                   "destructive of these ends, it is the Right of the People") == 0,
               "emdash sentence");
        expect(io_save(&d, pdfpath, resolved, sizeof(resolved)) == 0, "save just pdf");
        f = fopen(resolved[0] ? resolved : pdfpath, "rb");
        expect(f != NULL, "open just pdf");
        if (f && fseek(f, 0, SEEK_END) == 0) {
            sz = ftell(f);
            if (sz > 0 && fseek(f, 0, SEEK_SET) == 0) {
                pdf = malloc((size_t)sz + 1);
                if (pdf && fread(pdf, 1, (size_t)sz, f) == (size_t)sz) {
                    pdf[sz] = 0;
                    expect(strstr(pdf, "???That") == NULL, "emdash is not three question marks");
                    expect(strstr(pdf, "(Form)") != NULL || strstr(pdf, "Form of") != NULL,
                           "Form stays intact");
                    expect(strstr(pdf, "(of)") != NULL || strstr(pdf, "Form of") != NULL,
                           "of stays intact");
                    expect(strstr(pdf, "Tw") == NULL, "full just does not use overflowing Tw");
                }
            }
        }
        if (f) {
            fclose(f);
        }
        free(pdf);
    }
    doc_free(&d);
}

int main(void)
{
    test_empty_wpd();
    test_paragraph();
    test_bold_underline();
    test_edit();
    test_file_roundtrip();
    test_reject_bad();
    test_mac_wpd();
    test_wp51_attr_pair();
    test_search_indent_merge_center();
    test_utf8_and_specials();
    test_export_and_wrap();
    test_styles_all_formats();
    test_selection();
    test_fonts();
    test_reveal_unbold();
    test_strip_codes();
    test_justification();
    if (failures) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("test_core: all passed\n");
    return 0;
}
