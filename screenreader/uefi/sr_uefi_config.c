#include "sr_uefi.h"
#include "sr_uefi_config.h"

typedef sr_efi_status (*cfg_export_fn)(void *self, sr_u16 **results);
typedef sr_efi_status (*cfg_route_fn)(
    void *self, const sr_u16 *configuration, sr_u16 **progress);
typedef sr_efi_status (*cfg_block_to_config_fn)(
    void *self,
    const sr_u16 *config_request,
    const sr_u8 *block,
    sr_size block_size,
    sr_u16 **config,
    sr_u16 **progress);
typedef sr_efi_status (*cfg_config_to_block_fn)(
    void *self,
    const sr_u16 *config_resp,
    sr_u8 *block,
    sr_size *block_size,
    sr_u16 **progress);

typedef struct {
    void *extract_config;
    cfg_export_fn export_config;
    cfg_route_fn route_config;
    cfg_block_to_config_fn block_to_config;
    cfg_config_to_block_fn config_to_block;
    void *get_alt_cfg;
} sr_hii_config_routing_protocol;

typedef sr_efi_status (*free_pool_fn)(void *buffer);

static const sr_efi_guid g_hii_config_routing_guid =
    {0x587e72d7u,0xcc50u,0x4f79u,{0x82,0x09,0xca,0x29,0x1f,0xc1,0xa1,0x0f}};

static sr_u8 g_config_block[65536];
static sr_u16 g_commit_request[2048];

static free_pool_fn get_free_pool(void *system_table) {
    void *bs = sr_uefi_boot_services(system_table);
    if (!bs) return 0;
    return *(free_pool_fn *)((sr_u8 *)bs + 0x48u);
}

static int ascii_eq(sr_u16 ch, char ascii) {
    sr_u16 a = (sr_u16)(sr_u8)ascii;
    if (ch >= (sr_u16)'a' && ch <= (sr_u16)'z')
        ch = (sr_u16)(ch - ('a' - 'A'));
    if (a >= (sr_u16)'a' && a <= (sr_u16)'z')
        a = (sr_u16)(a - ('a' - 'A'));
    return ch == a;
}

static int match_ascii(const sr_u16 *text, const char *ascii) {
    sr_u32 i = 0;
    if (!text || !ascii) return 0;
    while (ascii[i]) {
        if (!text[i] || !ascii_eq(text[i], ascii[i])) return 0;
        ++i;
    }
    return 1;
}

static int hex_nibble(sr_u16 ch) {
    if (ch >= (sr_u16)'0' && ch <= (sr_u16)'9')
        return (int)(ch - (sr_u16)'0');
    if (ch >= (sr_u16)'a' && ch <= (sr_u16)'f')
        return 10 + (int)(ch - (sr_u16)'a');
    if (ch >= (sr_u16)'A' && ch <= (sr_u16)'F')
        return 10 + (int)(ch - (sr_u16)'A');
    return -1;
}

static int read_hex_byte(const sr_u16 *p, sr_u8 *value) {
    int hi, lo;
    if (!p || !value) return 0;
    hi = hex_nibble(p[0]);
    lo = hex_nibble(p[1]);
    if (hi < 0 || lo < 0) return 0;
    *value = (sr_u8)((hi << 4) | lo);
    return 1;
}

static int read_hex_u16_4(const sr_u16 *p, sr_u16 *value) {
    sr_u32 i;
    sr_u16 out = 0;
    if (!p || !value) return 0;
    for (i = 0; i < 4u; ++i) {
        int n = hex_nibble(p[i]);
        if (n < 0) return 0;
        out = (sr_u16)((out << 4) | (sr_u16)n);
    }
    *value = out;
    return 1;
}

static int guid_matches(const sr_u16 *p, const sr_u8 guid[16]) {
    sr_u32 i;
    if (!p || !guid) return 0;
    for (i = 0; i < 16u; ++i) {
        sr_u8 byte = 0;
        if (!read_hex_byte(p + i * 2u, &byte) || byte != guid[i])
            return 0;
    }
    return 1;
}

static int name_matches(const sr_u16 *p, const char *name) {
    sr_u32 i = 0;
    if (!p || !name) return 0;
    while (name[i]) {
        sr_u16 code = 0;
        if (!read_hex_u16_4(p, &code) ||
            code != (sr_u16)(sr_u8)name[i]) return 0;
        p += 4u;
        ++i;
    }
    return match_ascii(p, "&PATH=");
}

static sr_u16 *find_config_resp(sr_u16 *results,
                                const sr_hii_varstore *store) {
    sr_u16 *p;
    if (!results || !store || store->opcode != 0x24u || !store->name)
        return 0;

    for (p = results; *p; ++p) {
        sr_u16 *q;
        if (!match_ascii(p, "GUID=")) continue;
        q = p + 5u;
        if (!guid_matches(q, store->guid)) continue;
        q += 32u;
        if (!match_ascii(q, "&NAME=")) continue;
        q += 6u;
        if (!name_matches(q, store->name)) continue;
        return p;
    }
    return 0;
}

static sr_u16 *find_next_config(sr_u16 *p) {
    if (!p) return 0;
    ++p;
    while (*p) {
        if (p[0] == (sr_u16)'&' && match_ascii(p + 1u, "GUID="))
            return p;
        ++p;
    }
    return 0;
}

static sr_u32 binding_width(const sr_hii_binding *binding) {
    if (!binding) return 0;
    if (binding->opcode == 0x05u)
        return 1u << (binding->oneof_flags & 0x03u);
    if (binding->opcode == 0x06u)
        return 1u;
    return 0;
}

static sr_u64 block_read(const sr_u8 *p, sr_u32 width) {
    sr_u64 value = 0;
    sr_u32 i;
    if (!p || !width || width > 8u) return 0;
    for (i = 0; i < width; ++i)
        value |= ((sr_u64)p[i]) << (i * 8u);
    return value;
}

static void block_write(sr_u8 *p, sr_u32 width, sr_u64 value) {
    sr_u32 i;
    if (!p || !width || width > 8u) return;
    for (i = 0; i < width; ++i)
        p[i] = (sr_u8)(value >> (i * 8u));
}

static void free_pool_safe(sr_uefi_config *config, void *ptr) {
    free_pool_fn free_pool;
    if (!config || !ptr) return;
    free_pool = get_free_pool(config->system_table);
    if (free_pool) (void)free_pool(ptr);
}

static int export_block(sr_uefi_config *config,
                        sr_u32 node_index,
                        sr_u16 **results_out,
                        sr_u16 **segment_out,
                        sr_u16 **boundary_out,
                        const sr_hii_varstore **store_out,
                        sr_u32 *width_out,
                        sr_u64 *raw_out) {
    sr_hii_config_routing_protocol *routing;
    sr_hii_binding *binding;
    const sr_hii_varstore *store;
    sr_u16 *results = 0;
    sr_u16 *segment;
    sr_u16 *boundary;
    sr_u16 saved = 0;
    sr_u16 *progress = 0;
    sr_size block_size;
    sr_u32 width;
    sr_efi_status status;

    if (!config || !config->model || !config->routing ||
        !config->model->bindings ||
        node_index >= config->model->node_count ||
        node_index >= config->model->binding_capacity) return 0;

    binding = &config->model->bindings[node_index];
    store = sr_hii_find_varstore(config->model, binding->varstore_id);
    width = binding_width(binding);
    if (!store || store->opcode != 0x24u || !width ||
        (sr_u32)binding->varstore_info + width > store->size) return 0;

    routing = (sr_hii_config_routing_protocol *)config->routing;
    if (!routing->export_config || !routing->config_to_block) return 0;
    status = routing->export_config(routing, &results);
    if (status != 0u || !results) return 0;

    segment = find_config_resp(results, store);
    if (!segment) {
        free_pool_safe(config, results);
        return 0;
    }

    boundary = find_next_config(segment);
    if (boundary) {
        saved = *boundary;
        *boundary = 0;
    }

    block_size = store->size;
    progress = 0;
    status = routing->config_to_block(
        routing, segment, g_config_block, &block_size, &progress);

    if (boundary) *boundary = saved;

    if (status != 0u ||
        block_size < (sr_size)((sr_u32)binding->varstore_info + width)) {
        free_pool_safe(config, results);
        return 0;
    }

    if (raw_out)
        *raw_out = block_read(
            g_config_block + binding->varstore_info, width);
    if (results_out) *results_out = results;
    else free_pool_safe(config, results);
    if (segment_out) *segment_out = segment;
    if (boundary_out) *boundary_out = boundary;
    if (store_out) *store_out = store;
    if (width_out) *width_out = width;
    return 1;
}

static sr_u16 hex_char(sr_u32 nibble) {
    nibble &= 0x0fu;
    return (sr_u16)(nibble < 10u ? ('0' + nibble) : ('A' + nibble - 10u));
}

static sr_u16 *append_ascii(sr_u16 *out, sr_u16 *end, const char *text) {
    if (!out || !end || !text) return 0;
    while (*text) {
        if (out + 1u >= end) return 0;
        *out++ = (sr_u16)(sr_u8)*text++;
    }
    return out;
}

static sr_u16 *append_hex4(sr_u16 *out, sr_u16 *end, sr_u16 value) {
    int shift;
    if (!out || !end || out + 4u >= end) return 0;
    for (shift = 12; shift >= 0; shift -= 4)
        *out++ = hex_char((sr_u32)(value >> shift));
    return out;
}

static int build_commit_request(const sr_u16 *segment,
                                sr_u16 offset,
                                sr_u16 width) {
    sr_u16 *out = g_commit_request;
    sr_u16 *end = g_commit_request +
        (sizeof(g_commit_request) / sizeof(g_commit_request[0]));
    const sr_u16 *p = segment;

    if (!segment) return 0;
    while (*p) {
        if (p[0] == (sr_u16)'&' && match_ascii(p, "&OFFSET="))
            break;
        if (out + 1u >= end) return 0;
        *out++ = *p++;
    }
    if (!*p) return 0;

    out = append_ascii(out, end, "&OFFSET=");
    if (!out) return 0;
    out = append_hex4(out, end, offset);
    if (!out) return 0;
    out = append_ascii(out, end, "&WIDTH=");
    if (!out) return 0;
    out = append_hex4(out, end, width);
    if (!out || out >= end) return 0;
    *out = 0;
    return 1;
}

int sr_uefi_config_init(sr_uefi_config *config,
                        void *system_table,
                        sr_hii_model *model) {
    sr_locate_protocol_fn locate;
    void *routing = 0;
    if (!config || !system_table || !model) return 0;

    config->system_table = system_table;
    config->model = model;
    config->routing = 0;
    config->stats.refresh_attempts = 0;
    config->stats.refresh_successes = 0;
    config->stats.commit_attempts = 0;
    config->stats.commit_successes = 0;
    config->stats.routing_failures = 0;

    locate = sr_uefi_locate_protocol(system_table);
    if (!locate ||
        locate(&g_hii_config_routing_guid, 0, &routing) != 0u ||
        !routing) return 0;

    config->routing = routing;
    return 1;
}

int sr_uefi_config_refresh_value(sr_uefi_config *config,
                                 sr_u32 node_index) {
    sr_u64 raw = 0;
    if (!config) return 0;
    config->stats.refresh_attempts++;
    if (!export_block(config, node_index, 0, 0, 0, 0, 0, &raw)) {
        config->stats.routing_failures++;
        return 0;
    }
    if (!sr_hii_apply_raw_value(config->model, node_index, raw))
        return 0;
    config->stats.refresh_successes++;
    return 1;
}

sr_u32 sr_uefi_config_refresh_all(sr_uefi_config *config) {
    sr_u32 i;
    sr_u32 ok = 0;
    if (!config || !config->model) return 0;
    for (i = 0; i < config->model->node_count; ++i)
        if (sr_uefi_config_refresh_value(config, i)) ++ok;
    return ok;
}

int sr_uefi_config_set_raw_value(sr_uefi_config *config,
                                 sr_u32 node_index,
                                 sr_u64 raw_value) {
    sr_hii_config_routing_protocol *routing;
    sr_hii_binding *binding;
    const sr_hii_varstore *store = 0;
    sr_u16 *results = 0;
    sr_u16 *segment = 0;
    sr_u16 *boundary = 0;
    sr_u16 *commit_config = 0;
    sr_u16 *progress = 0;
    sr_u32 width = 0;
    sr_u64 current = 0;
    sr_efi_status status;

    if (!config || !config->model ||
        node_index >= config->model->node_count ||
        !config->model->bindings ||
        node_index >= config->model->binding_capacity) return 0;

    if (config->model->nodes[node_index].state & SR_STATE_READONLY)
        return 0;

    config->stats.commit_attempts++;
    binding = &config->model->bindings[node_index];

    if (!export_block(
            config, node_index, &results, &segment, &boundary,
            &store, &width, &current)) {
        config->stats.routing_failures++;
        return 0;
    }

    (void)boundary;
    (void)current;
    block_write(
        g_config_block + binding->varstore_info, width, raw_value);

    if (!build_commit_request(
            segment, binding->varstore_info, (sr_u16)width)) {
        free_pool_safe(config, results);
        return 0;
    }

    routing = (sr_hii_config_routing_protocol *)config->routing;
    if (!routing->block_to_config || !routing->route_config) {
        free_pool_safe(config, results);
        return 0;
    }

    progress = 0;
    status = routing->block_to_config(
        routing,
        g_commit_request,
        g_config_block,
        store->size,
        &commit_config,
        &progress);
    if (status != 0u || !commit_config) {
        free_pool_safe(config, results);
        config->stats.routing_failures++;
        return 0;
    }

    progress = 0;
    status = routing->route_config(
        routing, commit_config, &progress);

    free_pool_safe(config, commit_config);
    free_pool_safe(config, results);

    if (status != 0u) {
        config->stats.routing_failures++;
        return 0;
    }

    /*
     * Do not ExportConfig again in this boot immediately after RouteConfig.
     * Some firmware drivers rebuild HII backing state. Update the local
     * semantic model from the value that was successfully routed.
     */
    (void)sr_hii_apply_raw_value(config->model, node_index, raw_value);
    config->stats.commit_successes++;
    return 1;
}

int sr_uefi_config_adjust(sr_uefi_config *config,
                          sr_u32 node_index,
                          int direction) {
    sr_hii_binding *binding;
    sr_u64 raw = 0;
    sr_u16 *results = 0;
    sr_u32 i;
    if (!config || !config->model || !direction ||
        !config->model->bindings ||
        node_index >= config->model->node_count ||
        node_index >= config->model->binding_capacity) return 0;

    binding = &config->model->bindings[node_index];
    if (config->model->nodes[node_index].state & SR_STATE_READONLY)
        return 0;

    if (!export_block(
            config, node_index, &results, 0, 0, 0, 0, &raw))
        return 0;
    free_pool_safe(config, results);

    if (binding->opcode == 0x06u)
        return sr_uefi_config_set_raw_value(config, node_index, raw ? 0u : 1u);

    if (binding->opcode == 0x05u && config->model->options &&
        binding->option_count) {
        sr_u32 start = binding->option_start;
        sr_u32 end = start + binding->option_count;
        sr_u32 current = start;
        if (end > config->model->option_count) end = config->model->option_count;

        for (i = start; i < end; ++i) {
            sr_hii_option *option = &config->model->options[i];
            sr_u64 mask = option->width >= 8u
                ? ~(sr_u64)0
                : (((sr_u64)1u << (option->width * 8u)) - 1u);
            if ((raw & mask) == (option->value & mask)) {
                current = i;
                break;
            }
        }

        if (direction > 0) {
            current++;
            if (current >= end) current = start;
        } else {
            if (current <= start) current = end - 1u;
            else current--;
        }
        return sr_uefi_config_set_raw_value(
            config, node_index, config->model->options[current].value);
    }

    return 0;
}
