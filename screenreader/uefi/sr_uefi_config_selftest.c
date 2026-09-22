#include <stdio.h>
#include <string.h>
#include "sr_uefi.h"
#include "sr_uefi_config.h"

typedef sr_efi_status (*export_fn)(void *, sr_u16 **);
typedef sr_efi_status (*route_fn)(void *, const sr_u16 *, sr_u16 **);
typedef sr_efi_status (*block_to_config_fn)(
    void *, const sr_u16 *, const sr_u8 *, sr_size, sr_u16 **, sr_u16 **);
typedef sr_efi_status (*config_to_block_fn)(
    void *, const sr_u16 *, sr_u8 *, sr_size *, sr_u16 **);

typedef struct {
    void *extract;
    export_fn export_config;
    route_fn route_config;
    block_to_config_fn block_to_config;
    config_to_block_fn config_to_block;
    void *alt;
} fake_routing;

static fake_routing g_routing;
static sr_u16 g_results[512];
static sr_u16 g_commit[64];
static sr_u8 g_current_block[16];
static unsigned g_routes;
static unsigned g_blocks;
static unsigned g_frees;
static sr_u16 g_last_request[256];

static void to_u16(const char *ascii, sr_u16 *out, unsigned cap) {
    unsigned i = 0;
    while (ascii[i] && i + 1u < cap) {
        out[i] = (sr_u16)(unsigned char)ascii[i];
        ++i;
    }
    out[i] = 0;
}

static int u16_contains(const sr_u16 *text, const char *needle) {
    unsigned i, j;
    for (i = 0; text[i]; ++i) {
        for (j = 0; needle[j] && text[i + j]; ++j)
            if (text[i + j] != (sr_u16)(unsigned char)needle[j]) break;
        if (!needle[j]) return 1;
    }
    return 0;
}

static sr_efi_status fake_export(void *self, sr_u16 **results) {
    (void)self;
    *results = g_results;
    return 0;
}

static sr_efi_status fake_to_block(
    void *self, const sr_u16 *resp, sr_u8 *block,
    sr_size *block_size, sr_u16 **progress) {
    (void)self; (void)resp;
    if (!block || !block_size || *block_size < sizeof(g_current_block))
        return 1;
    memcpy(block, g_current_block, sizeof(g_current_block));
    *block_size = sizeof(g_current_block);
    if (progress) *progress = 0;
    return 0;
}

static sr_efi_status fake_from_block(
    void *self, const sr_u16 *request, const sr_u8 *block,
    sr_size block_size, sr_u16 **config, sr_u16 **progress) {
    unsigned i = 0;
    (void)self;
    if (!request || !block || block_size != sizeof(g_current_block))
        return 1;
    while (request[i] && i + 1u < 256u) {
        g_last_request[i] = request[i];
        ++i;
    }
    g_last_request[i] = 0;
    memcpy(g_current_block, block, sizeof(g_current_block));
    to_u16("ROUTED=1", g_commit, 64);
    *config = g_commit;
    if (progress) *progress = 0;
    g_blocks++;
    return 0;
}

static sr_efi_status fake_route(
    void *self, const sr_u16 *configuration, sr_u16 **progress) {
    (void)self;
    if (!configuration || configuration[0] != (sr_u16)'R') return 1;
    if (progress) *progress = 0;
    g_routes++;
    return 0;
}

static sr_efi_status fake_free(void *p) {
    (void)p;
    g_frees++;
    return 0;
}

static sr_efi_status fake_locate(
    const sr_efi_guid *guid, void *registration, void **out) {
    (void)registration;
    if (!guid || !out) return 1;
    if (guid->data1 == 0x587e72d7u) {
        *out = &g_routing;
        return 0;
    }
    return 2;
}

static int check(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        return 0;
    }
    return 1;
}

int main(void) {
    unsigned char st[0x100] = {0};
    unsigned char bs[0x200] = {0};
    sr_node nodes[1];
    sr_hii_binding bindings[1];
    sr_hii_option options[2];
    sr_hii_varstore stores[1];
    char arena[256];
    sr_hii_model model;
    sr_uefi_config config;
    unsigned i;

    *(void **)(st + 0x60u) = bs;
    *(void **)(bs + 0x140u) = (void *)fake_locate;
    *(void **)(bs + 0x48u) = (void *)fake_free;

    g_routing.extract = 0;
    g_routing.export_config = fake_export;
    g_routing.route_config = fake_route;
    g_routing.block_to_config = fake_from_block;
    g_routing.config_to_block = fake_to_block;
    g_routing.alt = 0;

    to_u16(
        "GUID=000102030405060708090A0B0C0D0E0F"
        "&NAME=00530065007400750070"
        "&PATH=0000&OFFSET=0000&WIDTH=0010&VALUE=00",
        g_results, 512);

    memset(g_current_block, 0, sizeof(g_current_block));
    g_current_block[9] = 3;

    sr_hii_model_init(&model, nodes, 1, arena, sizeof(arena), 0, 0);
    sr_hii_model_attach_metadata(&model, bindings, 1, options, 2);
    sr_hii_model_attach_varstores(&model, stores, 1);

    model.node_count = 1;
    nodes[0].id = 1;
    nodes[0].role = SR_ROLE_COMBO;
    nodes[0].state = SR_STATE_FOCUSABLE | SR_STATE_ENABLED;
    nodes[0].label = "Ordre de demarrage";
    nodes[0].value = "";
    nodes[0].help = "";

    bindings[0].question_id = 0x20;
    bindings[0].varstore_id = 7;
    bindings[0].varstore_info = 9;
    bindings[0].option_start = 0;
    bindings[0].option_count = 2;
    bindings[0].opcode = 0x05;
    bindings[0].question_flags = 0;
    bindings[0].oneof_flags = 0;

    options[0].value = 2;
    options[0].label = "UEFI";
    options[0].width = 1;
    options[0].flags = 0;
    options[1].value = 3;
    options[1].label = "NVMe";
    options[1].width = 1;
    options[1].flags = 0;
    model.option_count = 2;

    stores[0].id = 7;
    stores[0].size = sizeof(g_current_block);
    stores[0].attributes = 0;
    stores[0].opcode = 0x24;
    stores[0].name = "Setup";
    for (i = 0; i < 16u; ++i) stores[0].guid[i] = (sr_u8)i;
    model.varstore_count = 1;

    if (!check(sr_uefi_config_init(&config, st, &model), "config init"))
        return 1;
    if (!check(sr_uefi_config_refresh_value(&config, 0), "refresh"))
        return 1;
    if (!check(strcmp(nodes[0].value, "NVMe") == 0, "current option"))
        return 1;

    if (!check(sr_uefi_config_adjust(&config, 0, 1), "adjust next"))
        return 1;
    if (!check(g_current_block[9] == 2u, "block updated")) return 1;
    if (!check(strcmp(nodes[0].value, "UEFI") == 0, "semantic value updated"))
        return 1;
    if (!check(g_blocks == 1u && g_routes == 1u, "routing calls"))
        return 1;
    if (!check(u16_contains(g_last_request, "&OFFSET=0009"), "offset request"))
        return 1;
    if (!check(u16_contains(g_last_request, "&WIDTH=0001"), "width request"))
        return 1;
    if (!check(config.stats.commit_successes == 1u, "commit stats"))
        return 1;
    if (!check(g_frees >= 3u, "routing buffers released"))
        return 1;

    nodes[0].state |= SR_STATE_READONLY;
    if (!check(!sr_uefi_config_adjust(&config, 0, 1), "read-only veto"))
        return 1;

    printf("UEFI_CONFIG_ROUTING=PASS\n");
    return 0;
}
