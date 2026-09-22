#include "sr_uefi.h"
#include "sr_uefi_hii.h"

typedef sr_efi_status (*hii_list_fn)(
    const void *self,
    sr_u8 package_type,
    const sr_efi_guid *package_guid,
    sr_size *handle_bytes,
    void **handles);

typedef sr_efi_status (*hii_export_fn)(
    const void *self,
    void *handle,
    sr_size *buffer_size,
    void *buffer);

typedef sr_efi_status (*hii_get_string_fn)(
    const void *self,
    const char *language,
    void *handle,
    sr_u16 string_id,
    sr_u16 *string,
    sr_size *string_size,
    void **font_info);

typedef sr_efi_status (*hii_get_languages_fn)(
    const void *self,
    void *handle,
    char *languages,
    sr_size *language_size);

typedef struct {
    void *new_package_list;
    void *remove_package_list;
    void *update_package_list;
    hii_list_fn list_package_lists;
    hii_export_fn export_package_lists;
} sr_hii_database_protocol;

typedef struct {
    void *new_string;
    hii_get_string_fn get_string;
    void *set_string;
    hii_get_languages_fn get_languages;
} sr_hii_string_protocol;

typedef struct {
    sr_hii_string_protocol *strings;
    void *handle;
    sr_uefi_hii_stats *stats;
} sr_string_ctx;

static const sr_efi_guid g_hii_database_guid =
    {0xef9fc172u,0xa1b2u,0x4693u,{0xb3,0x27,0x6d,0x32,0xfc,0x41,0x60,0x42}};
static const sr_efi_guid g_hii_string_guid =
    {0x0fd96974u,0x23aau,0x4cdcu,{0xb9,0xcb,0x98,0xd1,0x77,0x50,0x32,0x2a}};

static sr_u8 g_export_buffer[1024u * 1024u];
static void *g_handles[256];

static sr_u32 h_rd24(const sr_u8 *p) {
    return (sr_u32)p[0] | ((sr_u32)p[1] << 8) | ((sr_u32)p[2] << 16);
}

static sr_u32 h_rd32(const sr_u8 *p) {
    return (sr_u32)p[0] | ((sr_u32)p[1] << 8) |
           ((sr_u32)p[2] << 16) | ((sr_u32)p[3] << 24);
}

static int utf8_put(char *out, sr_u32 cap, sr_u32 *used, sr_u32 cp) {
    if (!out || !used || *used >= cap) return 0;
    if (cp <= 0x7fu) {
        if (*used + 1u >= cap) return 0;
        out[(*used)++] = (char)cp;
    } else if (cp <= 0x7ffu) {
        if (*used + 2u >= cap) return 0;
        out[(*used)++] = (char)(0xc0u | (cp >> 6));
        out[(*used)++] = (char)(0x80u | (cp & 0x3fu));
    } else if (cp <= 0xffffu) {
        if (*used + 3u >= cap) return 0;
        out[(*used)++] = (char)(0xe0u | (cp >> 12));
        out[(*used)++] = (char)(0x80u | ((cp >> 6) & 0x3fu));
        out[(*used)++] = (char)(0x80u | (cp & 0x3fu));
    } else {
        if (*used + 4u >= cap) return 0;
        out[(*used)++] = (char)(0xf0u | (cp >> 18));
        out[(*used)++] = (char)(0x80u | ((cp >> 12) & 0x3fu));
        out[(*used)++] = (char)(0x80u | ((cp >> 6) & 0x3fu));
        out[(*used)++] = (char)(0x80u | (cp & 0x3fu));
    }
    out[*used] = 0;
    return 1;
}

static int utf16_to_utf8(const sr_u16 *in, char *out, sr_u32 cap) {
    sr_u32 i = 0;
    sr_u32 used = 0;
    if (!in || !out || cap < 2u) return 0;
    out[0] = 0;
    while (in[i]) {
        sr_u32 cp = in[i++];
        if (cp >= 0xd800u && cp <= 0xdbffu) {
            sr_u32 low = in[i];
            if (low >= 0xdc00u && low <= 0xdfffu) {
                ++i;
                cp = 0x10000u + (((cp - 0xd800u) << 10) | (low - 0xdc00u));
            } else {
                cp = 0xfffdu;
            }
        } else if (cp >= 0xdc00u && cp <= 0xdfffu) {
            cp = 0xfffdu;
        }
        if (!utf8_put(out, cap, &used, cp)) return 0;
    }
    return used != 0u;
}

static int try_language(sr_string_ctx *ctx,
                        const char *lang,
                        sr_u16 string_id,
                        char *out,
                        sr_u32 out_cap) {
    sr_u16 text[256];
    sr_size bytes = sizeof(text);
    sr_efi_status st;
    if (!ctx || !ctx->strings || !ctx->strings->get_string) return 0;
    st = ctx->strings->get_string(
        ctx->strings, lang, ctx->handle, string_id,
        text, &bytes, 0);
    if (st != 0u) return 0;
    text[(sizeof(text) / sizeof(text[0])) - 1u] = 0;
    return utf16_to_utf8(text, out, out_cap);
}

static int resolve_string(void *opaque,
                          sr_u16 string_id,
                          char *out,
                          sr_u32 out_cap) {
    sr_string_ctx *ctx = (sr_string_ctx *)opaque;
    char langs[256];
    sr_size lang_bytes = sizeof(langs);
    sr_u32 n = 0;

    if (!ctx || !string_id || !out || !out_cap) return 0;

    if (try_language(ctx, "fr-FR", string_id, out, out_cap) ||
        try_language(ctx, "fr", string_id, out, out_cap) ||
        try_language(ctx, "en-US", string_id, out, out_cap)) {
        if (ctx->stats) ctx->stats->strings_resolved++;
        return 1;
    }

    if (ctx->strings->get_languages &&
        ctx->strings->get_languages(
            ctx->strings, ctx->handle, langs, &lang_bytes) == 0u &&
        lang_bytes) {
        while (n + 1u < (sr_u32)sizeof(langs) &&
               n < (sr_u32)lang_bytes &&
               langs[n] && langs[n] != ';') {
            ++n;
        }
        if (n && n < (sr_u32)sizeof(langs)) {
            langs[n] = 0;
            if (try_language(ctx, langs, string_id, out, out_cap)) {
                if (ctx->stats) ctx->stats->strings_resolved++;
                return 1;
            }
        }
    }

    if (ctx->stats) ctx->stats->strings_failed++;
    return 0;
}

static int parse_exported_list(sr_hii_model *model,
                               sr_hii_string_protocol *strings,
                               void *handle,
                               sr_u32 bytes,
                               sr_uefi_hii_stats *stats) {
    const sr_u8 *p = g_export_buffer;
    const sr_u8 *list_end;
    sr_u32 list_len;
    sr_string_ctx ctx;
    int any = 0;

    if (bytes < 20u) return 0;
    list_len = h_rd32(p + 16u);
    if (list_len < 20u || list_len > bytes) return 0;
    list_end = p + list_len;
    p += 20u;

    ctx.strings = strings;
    ctx.handle = handle;
    ctx.stats = stats;

    while (p + 4u <= list_end) {
        sr_u32 len = h_rd24(p);
        sr_u8 type = p[3];
        if (len < 4u || p + len > list_end) return any;
        if (type == 0xdfu) break;

        if (type == 0x02u) {
            sr_hii_string_resolver old_resolver = model->resolve_string;
            void *old_ctx = model->resolve_ctx;
            model->resolve_string = resolve_string;
            model->resolve_ctx = &ctx;
            if (stats) stats->forms_packages_seen++;
            if (sr_hii_parse_forms_package(model, p, len)) any = 1;
            model->resolve_string = old_resolver;
            model->resolve_ctx = old_ctx;
        }
        p += len;
    }
    return any;
}

int sr_uefi_collect_hii(void *system_table,
                        sr_hii_model *model,
                        sr_uefi_hii_stats *stats) {
    sr_locate_protocol_fn locate;
    sr_hii_database_protocol *db = 0;
    sr_hii_string_protocol *strings = 0;
    sr_size handle_bytes = sizeof(g_handles);
    sr_u32 handle_count;
    sr_u32 i;
    int any = 0;

    if (!system_table || !model) return 0;
    if (stats) {
        stats->package_lists_seen = 0;
        stats->forms_packages_seen = 0;
        stats->strings_resolved = 0;
        stats->strings_failed = 0;
    }

    locate = sr_uefi_locate_protocol(system_table);
    if (!locate) return 0;
    if (locate(&g_hii_database_guid, 0, (void **)&db) != 0u || !db ||
        !db->list_package_lists || !db->export_package_lists) return 0;
    if (locate(&g_hii_string_guid, 0, (void **)&strings) != 0u || !strings ||
        !strings->get_string) return 0;

    if (db->list_package_lists(
            db, 0x02u, 0, &handle_bytes, g_handles) != 0u ||
        !handle_bytes || handle_bytes > sizeof(g_handles)) return 0;

    handle_count = (sr_u32)(handle_bytes / sizeof(void *));
    for (i = 0; i < handle_count; ++i) {
        sr_size bytes = sizeof(g_export_buffer);
        if (!g_handles[i]) continue;
        if (db->export_package_lists(
                db, g_handles[i], &bytes, g_export_buffer) != 0u ||
            bytes < 20u || bytes > sizeof(g_export_buffer)) {
            continue;
        }
        if (stats) stats->package_lists_seen++;
        if (parse_exported_list(
                model, strings, g_handles[i], (sr_u32)bytes, stats)) {
            any = 1;
        }
    }
    return any;
}
