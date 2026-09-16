#include "doc.h"
#include "iofmt.h"
#include "view.h"
#include "wpd.h"
#include "wp51.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void visible_text(const Doc *d, char *out, size_t outsz, size_t *nvis)
{
    size_t i = 0, o = 0, vis = 0;
    if (outsz) {
        out[0] = 0;
    }
    while (i < d->len) {
        size_t n = doc_unit_len(d, i);
        if (!n) {
            break;
        }
        if (doc_is_visible(d, i)) {
            uint8_t b = d->data[i];
            if (b == WP_HARD_EOL || b == WP_SOFT_EOL || b == WP_HARD_PAGE) {
                if (o + 1 < outsz) {
                    out[o++] = ' ';
                }
                vis++;
            } else if (b >= 32 && b < 127) {
                if (o + 1 < outsz) {
                    out[o++] = (char)b;
                }
                vis++;
            } else {
                vis++;
            }
        }
        i += n;
    }
    if (outsz) {
        out[o] = 0;
    }
    if (nvis) {
        *nvis = vis;
    }
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static const char *why_header(const uint8_t *buf, size_t n)
{
    uint16_t enc;
    if (n < 16 || buf[0] != 0xFF || buf[1] != 'W' || buf[2] != 'P' || buf[3] != 'C') {
        return "not-wpc";
    }
    if (buf[8] != WPD_PRODUCT) {
        return "not-wordperfect-product";
    }
    enc = (uint16_t)buf[12] | ((uint16_t)buf[13] << 8);
    if (enc) {
        return "encrypted";
    }
    if (buf[9] == WPD_TYPE_DOC) {
        return "wp-doc";
    }
    if (buf[9] == WPD_TYPE_MAC_DOC) {
        return "mac-doc";
    }
    return "unsupported-type";
}

int main(void)
{
    const char *dir = "tests/corpus";
    DIR *dp;
    struct dirent *ent;
    char **names = NULL;
    size_t nnames = 0, cap = 0, i;
    int pass = 0, fail = 0;

    dp = opendir(dir);
    if (!dp) {
        fprintf(stderr, "FAIL: cannot open %s\n", dir);
        return 1;
    }
    while ((ent = readdir(dp))) {
        size_t len = strlen(ent->d_name);
        if (len < 5 || strcasecmp(ent->d_name + len - 4, ".wpd") != 0) {
            continue;
        }
        if (nnames == cap) {
            cap = cap ? cap * 2 : 64;
            names = realloc(names, cap * sizeof(*names));
        }
        names[nnames++] = strdup(ent->d_name);
    }
    closedir(dp);
    if (!nnames) {
        fprintf(stderr, "FAIL: no .wpd in %s\n", dir);
        return 1;
    }
    qsort(names, nnames, sizeof(*names), cmp_str);

    printf("%-24s %8s %6s %6s %s\n", "file", "bytes", "load", "chars", "preview");
    for (i = 0; i < nnames; i++) {
        char path[512];
        uint8_t *raw = NULL;
        size_t rawn = 0;
        FILE *f;
        Doc d;
        View v;
        char preview[96];
        size_t nvis = 0;
        int rc;
        const char *reason = "";

        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        f = fopen(path, "rb");
        if (f) {
            if (fseek(f, 0, SEEK_END) == 0) {
                long sz = ftell(f);
                if (sz > 0) {
                    rewind(f);
                    raw = malloc((size_t)sz);
                    if (raw && fread(raw, 1, (size_t)sz, f) == (size_t)sz) {
                        rawn = (size_t)sz;
                    }
                }
            }
            fclose(f);
        }
        reason = why_header(raw, rawn);
        doc_init(&d);
        view_init(&v);
        rc = io_load(&d, path);
        if (rc == 0) {
            view_relayout(&v, &d);
            visible_text(&d, preview, sizeof(preview), &nvis);
            printf("%-24s %8zu   ok   %6zu %s\n", names[i], rawn, nvis, preview);
            pass++;
        } else if (!strcmp(reason, "encrypted") || !strcmp(reason, "unsupported-type")) {
            printf("%-24s %8zu skip          %s\n", names[i], rawn, reason);
        } else {
            printf("%-24s %8zu FAIL          %s\n", names[i], rawn, reason);
            fail++;
        }
        view_free(&v);
        doc_free(&d);
        free(raw);
        free(names[i]);
    }
    free(names);
    printf("%d opened, %d failed, %zu files\n", pass, fail, nnames);
    return fail ? 1 : 0;
}
