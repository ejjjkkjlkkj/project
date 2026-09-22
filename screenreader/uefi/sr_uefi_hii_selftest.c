#include <stdio.h>
#include <string.h>
#include "sr_uefi_hii.h"

typedef unsigned long long efi_status;

typedef efi_status (*list_fn)(
    const void *, unsigned char, const void *,
    unsigned long long *, void **);
typedef efi_status (*export_fn)(
    const void *, void *, unsigned long long *, void *);
typedef efi_status (*get_string_fn)(
    const void *, const char *, void *, unsigned short,
    unsigned short *, unsigned long long *, void **);
typedef efi_status (*get_languages_fn)(
    const void *, void *, char *, unsigned long long *);

typedef struct {
    void *a, *b, *c;
    list_fn list;
    export_fn export_lists;
} fake_db;

typedef struct {
    void *a;
    get_string_fn get_string;
    void *c;
    get_languages_fn get_languages;
} fake_strings;

static fake_db g_db;
static fake_strings g_strings;
static unsigned char g_package_list[128];
static unsigned g_package_len;

static void wr16(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}
static void wr24(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
}
static void wr32(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static efi_status fake_list(
    const void *self, unsigned char type, const void *guid,
    unsigned long long *bytes, void **handles) {
    (void)self; (void)guid;
    if (type != 0x02u || !bytes) return 1;
    if (*bytes < sizeof(void *)) return 2;
    handles[0] = (void *)0x1234;
    *bytes = sizeof(void *);
    return 0;
}

static efi_status fake_export(
    const void *self, void *handle, unsigned long long *bytes, void *out) {
    (void)self;
    if (handle != (void *)0x1234 || !bytes || *bytes < g_package_len) return 1;
    memcpy(out, g_package_list, g_package_len);
    *bytes = g_package_len;
    return 0;
}

static const unsigned short *lookup(unsigned short id) {
    static const unsigned short s1[] = {
        'D',0x00e9,'m','a','r','r','a','g','e',0
    };
    static const unsigned short s2[] = {
        'A','c','t','i','v','e','r',0
    };
    static const unsigned short s3[] = {
        'O','r','d','r','e',0
    };
    static const unsigned short s4[] = {
        'C','h','o','i','s','i','r',0
    };
    switch (id) {
        case 1: return s1;
        case 2: return s2;
        case 3: return s3;
        case 4: return s4;
        default: return 0;
    }
}

static efi_status fake_get_string(
    const void *self, const char *lang, void *handle, unsigned short id,
    unsigned short *out, unsigned long long *bytes, void **font) {
    const unsigned short *src;
    unsigned long long need = 0;
    (void)self; (void)font;
    if (!lang || handle != (void *)0x1234 || !out || !bytes) return 1;
    src = lookup(id);
    if (!src) return 2;
    while (src[need / 2u]) need += 2u;
    need += 2u;
    if (*bytes < need) return 3;
    memcpy(out, src, (size_t)need);
    *bytes = need;
    return 0;
}

static efi_status fake_get_languages(
    const void *self, void *handle, char *out, unsigned long long *bytes) {
    static const char langs[] = "fr-FR;en-US";
    (void)self;
    if (handle != (void *)0x1234 || !out || !bytes || *bytes < sizeof(langs))
        return 1;
    memcpy(out, langs, sizeof(langs));
    *bytes = sizeof(langs);
    return 0;
}

static efi_status fake_locate(const void *guid, void *registration, void **out) {
    const unsigned *data1 = (const unsigned *)guid;
    (void)registration;
    if (!guid || !out) return 1;
    if (*data1 == 0xef9fc172u) {
        *out = &g_db;
        return 0;
    }
    if (*data1 == 0x0fd96974u) {
        *out = &g_strings;
        return 0;
    }
    return 2;
}

static void build_package(void) {
    unsigned char *p;
    unsigned forms_len;

    memset(g_package_list, 0, sizeof(g_package_list));
    p = g_package_list + 20u;

    /* CHECKBOX: prompt=1, help=2, question header, read-only bit. */
    p[0] = 0x06u; p[1] = 0x0du;
    wr16(p + 2, 1); wr16(p + 4, 2);
    wr16(p + 6, 0x10); wr16(p + 8, 0); wr16(p + 10, 0);
    p[12] = 1;
    p += 13;

    /* ONE_OF: prompt=3, help=4. */
    p[0] = 0x05u; p[1] = 0x0du;
    wr16(p + 2, 3); wr16(p + 4, 4);
    wr16(p + 6, 0x11); wr16(p + 8, 0); wr16(p + 10, 0);
    p[12] = 0;
    p += 13;

    forms_len = (unsigned)(p - (g_package_list + 20u)) + 4u;

    /* Move IFR opcodes four bytes forward to insert package header. */
    memmove(g_package_list + 24u, g_package_list + 20u, forms_len - 4u);
    wr24(g_package_list + 20u, forms_len);
    g_package_list[23] = 0x02u;

    g_package_len = 20u + forms_len;
    wr32(g_package_list + 16u, g_package_len);
}

static int check(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        return 0;
    }
    return 1;
}

int main(void) {
    unsigned char st[0x100];
    unsigned char bs[0x200];
    sr_node nodes[8];
    char arena[1024];
    sr_hii_model model;
    sr_uefi_hii_stats stats;

    memset(st, 0, sizeof(st));
    memset(bs, 0, sizeof(bs));
    g_db.a = g_db.b = g_db.c = 0;
    g_db.list = fake_list;
    g_db.export_lists = fake_export;
    g_strings.a = g_strings.c = 0;
    g_strings.get_string = fake_get_string;
    g_strings.get_languages = fake_get_languages;
    build_package();

    *(void **)(st + 0x60u) = bs;
    *(void **)(bs + 0x140u) = (void *)fake_locate;

    sr_hii_model_init(&model, nodes, 8, arena, sizeof(arena), 0, 0);
    if (!check(sr_uefi_collect_hii(st, &model, &stats), "collector")) return 1;
    if (!check(model.node_count == 2u, "node count")) return 1;
    if (!check(nodes[0].role == SR_ROLE_CHECKBOX, "checkbox role")) return 1;
    if (!check(nodes[0].state & SR_STATE_READONLY, "read-only state")) return 1;
    if (!check(strcmp(nodes[0].label, "D\xc3\xa9marrage") == 0, "utf16 to utf8")) return 1;
    if (!check(strcmp(nodes[0].help, "Activer") == 0, "help resolved")) return 1;
    if (!check(nodes[1].role == SR_ROLE_COMBO, "one-of role")) return 1;
    if (!check(stats.package_lists_seen == 1u, "package list stats")) return 1;
    if (!check(stats.forms_packages_seen == 1u, "forms stats")) return 1;
    if (!check(stats.strings_resolved == 4u, "string stats")) return 1;

    printf("UEFI_HII_COLLECTOR=PASS\n");
    return 0;
}
