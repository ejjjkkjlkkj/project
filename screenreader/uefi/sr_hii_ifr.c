#include "sr_hii_ifr.h"

#define SR_HII_PACKAGE_FORMS 0x02u
#define SR_IFR_ONE_OF 0x05u
#define SR_IFR_CHECKBOX 0x06u
#define SR_IFR_ONE_OF_OPTION 0x09u
#define SR_IFR_END 0x29u
#define SR_INVALID_INDEX 0xffffffffu
#define SR_ONEOF_STACK_MAX 8u

static sr_u16 h_rd16(const sr_u8 *p) {
    return (sr_u16)((sr_u16)p[0] | ((sr_u16)p[1] << 8));
}

static sr_u32 h_rd24(const sr_u8 *p) {
    return (sr_u32)p[0] | ((sr_u32)p[1] << 8) | ((sr_u32)p[2] << 16);
}

static sr_u64 h_rd_value(const sr_u8 *p, sr_u32 width) {
    sr_u64 value = 0;
    sr_u32 i;
    if (width > 8u) width = 8u;
    for (i = 0; i < width; ++i)
        value |= ((sr_u64)p[i]) << (i * 8u);
    return value;
}

static sr_role h_role(sr_u8 op, sr_u32 *state_out) {
    sr_u32 state = SR_STATE_ENABLED;
    sr_role role = SR_ROLE_UNKNOWN;

    switch (op) {
        case 0x02u: role = SR_ROLE_TEXT; break;
        case 0x03u: role = SR_ROLE_TEXT; break;
        case 0x05u: role = SR_ROLE_COMBO; state |= SR_STATE_FOCUSABLE; break;
        case 0x06u: role = SR_ROLE_CHECKBOX; state |= SR_STATE_FOCUSABLE; break;
        case 0x07u: role = SR_ROLE_EDIT; state |= SR_STATE_FOCUSABLE; break;
        case 0x08u: role = SR_ROLE_EDIT; state |= SR_STATE_FOCUSABLE; break;
        case 0x0cu: role = SR_ROLE_BUTTON; state |= SR_STATE_FOCUSABLE; break;
        case 0x0du: role = SR_ROLE_BUTTON; state |= SR_STATE_FOCUSABLE; break;
        case 0x0fu: role = SR_ROLE_MENU_ITEM; state |= SR_STATE_FOCUSABLE; break;
        case 0x1au: role = SR_ROLE_EDIT; state |= SR_STATE_FOCUSABLE; break;
        case 0x1bu: role = SR_ROLE_EDIT; state |= SR_STATE_FOCUSABLE; break;
        case 0x1cu: role = SR_ROLE_EDIT; state |= SR_STATE_FOCUSABLE; break;
        case 0x23u: role = SR_ROLE_COMBO; state |= SR_STATE_FOCUSABLE; break;
        default: break;
    }

    if (state_out) *state_out = state;
    return role;
}

static char *h_store(sr_hii_model *model, const char *text) {
    sr_u32 n = 0;
    char *dst;
    if (!model || !model->string_arena || !text ||
        model->string_used > model->string_capacity) return 0;
    while (text[n]) ++n;
    if (n + 1u > model->string_capacity - model->string_used) return 0;
    dst = model->string_arena + model->string_used;
    {
        sr_u32 i;
        for (i = 0; i < n; ++i) dst[i] = text[i];
    }
    dst[n] = 0;
    model->string_used += n + 1u;
    return dst;
}

static char *h_resolve_store(sr_hii_model *model, sr_u16 id) {
    char tmp[256];
    if (!id || !model || !model->resolve_string) return 0;
    tmp[0] = 0;
    if (!model->resolve_string(model->resolve_ctx, id, tmp, (sr_u32)sizeof(tmp)))
        return 0;
    tmp[sizeof(tmp) - 1u] = 0;
    return h_store(model, tmp);
}

static int h_is_question(sr_u8 op) {
    switch (op) {
        case 0x05u: case 0x06u: case 0x07u: case 0x08u:
        case 0x0fu: case 0x1au: case 0x1bu: case 0x1cu:
        case 0x23u:
            return 1;
        default:
            return 0;
    }
}

static void h_clear_binding(sr_hii_binding *binding) {
    if (!binding) return;
    binding->question_id = 0;
    binding->varstore_id = 0;
    binding->varstore_info = 0;
    binding->option_start = 0;
    binding->option_count = 0;
    binding->opcode = 0;
    binding->question_flags = 0;
    binding->oneof_flags = 0;
}

static void h_fill_binding(sr_hii_model *model,
                           sr_u32 node_index,
                           sr_u8 op,
                           const sr_u8 *p,
                           sr_u32 op_len) {
    sr_hii_binding *binding;
    if (!model || !model->bindings ||
        node_index >= model->binding_capacity) return;

    binding = &model->bindings[node_index];
    h_clear_binding(binding);
    binding->opcode = op;
    binding->option_start = (sr_u16)(
        model->option_count > 0xffffu ? 0xffffu : model->option_count);

    if (h_is_question(op) && op_len >= 13u) {
        binding->question_id = h_rd16(p + 6u);
        binding->varstore_id = h_rd16(p + 8u);
        binding->varstore_info = h_rd16(p + 10u);
        binding->question_flags = p[12u];
    }
    if (op == SR_IFR_ONE_OF && op_len >= 14u)
        binding->oneof_flags = p[13u];
}

static sr_u32 h_oneof_child(const sr_u32 *nodes,
                            const sr_u32 *child_depths,
                            sr_u32 count,
                            sr_u32 depth) {
    sr_u32 i = count;
    while (i) {
        --i;
        if (child_depths[i] == depth) return nodes[i];
    }
    return SR_INVALID_INDEX;
}

static void h_add_option(sr_hii_model *model,
                         sr_u32 node_index,
                         const sr_u8 *p,
                         sr_u32 op_len) {
    sr_hii_binding *binding;
    sr_hii_option *option;
    sr_u8 type;
    sr_u32 width;
    sr_u16 string_id;
    char *label;

    if (!model || !model->bindings || !model->options ||
        node_index >= model->binding_capacity ||
        model->option_count >= model->option_capacity ||
        op_len < 7u) return;

    type = p[5u];
    if (type > 3u) return;
    width = 1u << type;
    if (6u + width > op_len) return;

    string_id = h_rd16(p + 2u);
    label = h_resolve_store(model, string_id);
    if (!label || !label[0]) return;

    binding = &model->bindings[node_index];
    option = &model->options[model->option_count];
    option->value = h_rd_value(p + 6u, width);
    option->label = label;
    option->width = (sr_u8)width;
    option->flags = p[4u];
    model->option_count++;

    if (binding->option_count < 0xffu)
        binding->option_count++;
}

static char *h_store_u64(sr_hii_model *model, sr_u64 value) {
    char tmp[32];
    char rev[32];
    sr_u32 n = 0;
    sr_u32 i = 0;
    if (!value) return h_store(model, "0");
    while (value && n < (sr_u32)sizeof(rev)) {
        rev[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (n) tmp[i++] = rev[--n];
    tmp[i] = 0;
    return h_store(model, tmp);
}

void sr_hii_model_init(sr_hii_model *model,
                       sr_node *nodes,
                       sr_u32 node_capacity,
                       char *string_arena,
                       sr_u32 string_capacity,
                       sr_hii_string_resolver resolver,
                       void *resolver_ctx) {
    if (!model) return;
    model->nodes = nodes;
    model->node_capacity = node_capacity;
    model->node_count = 0;
    model->string_arena = string_arena;
    model->string_capacity = string_capacity;
    model->string_used = 0;
    model->resolve_string = resolver;
    model->resolve_ctx = resolver_ctx;
    model->bindings = 0;
    model->binding_capacity = 0;
    model->options = 0;
    model->option_capacity = 0;
    model->option_count = 0;
    model->malformed_opcodes = 0;
    model->dropped_nodes = 0;
}

void sr_hii_model_attach_metadata(sr_hii_model *model,
                                  sr_hii_binding *bindings,
                                  sr_u32 binding_capacity,
                                  sr_hii_option *options,
                                  sr_u32 option_capacity) {
    sr_u32 i;
    if (!model) return;
    model->bindings = bindings;
    model->binding_capacity = binding_capacity;
    model->options = options;
    model->option_capacity = option_capacity;
    model->option_count = 0;

    if (bindings) {
        for (i = 0; i < binding_capacity; ++i)
            h_clear_binding(&bindings[i]);
    }
}

int sr_hii_parse_forms_package(sr_hii_model *model,
                               const sr_u8 *package,
                               sr_u32 package_bytes) {
    sr_u32 declared;
    sr_u8 type;
    const sr_u8 *p;
    const sr_u8 *end;
    sr_u32 depth = 0;
    sr_u32 oneof_nodes[SR_ONEOF_STACK_MAX];
    sr_u32 oneof_depths[SR_ONEOF_STACK_MAX];
    sr_u32 oneof_count = 0;

    if (!model || !package || package_bytes < 4u) return 0;
    declared = h_rd24(package);
    type = package[3];
    if (type != SR_HII_PACKAGE_FORMS || declared < 4u || declared > package_bytes)
        return 0;

    p = package + 4u;
    end = package + declared;

    while (p + 2u <= end) {
        sr_u8 op = p[0];
        sr_u8 scoped = (sr_u8)(p[1] & 0x80u);
        sr_u32 op_len = (sr_u32)(p[1] & 0x7fu);
        sr_role role;
        sr_u32 state = 0;
        sr_u16 prompt_id;
        sr_u16 help_id;
        char *label;
        char *help;
        sr_u32 added_index = SR_INVALID_INDEX;

        if (op_len < 2u || p + op_len > end) {
            model->malformed_opcodes++;
            return 0;
        }

        if (op == SR_IFR_END) {
            while (oneof_count && oneof_depths[oneof_count - 1u] == depth)
                --oneof_count;
            if (depth) --depth;
            p += op_len;
            continue;
        }

        if (op == SR_IFR_ONE_OF_OPTION && oneof_count) {
            sr_u32 owner = h_oneof_child(
                oneof_nodes, oneof_depths, oneof_count, depth);
            if (owner != SR_INVALID_INDEX)
                h_add_option(model, owner, p, op_len);
        }

        role = h_role(op, &state);
        if (role != SR_ROLE_UNKNOWN && op_len >= 4u) {
            if (model->node_count >= model->node_capacity) {
                model->dropped_nodes++;
            } else {
                prompt_id = h_rd16(p + 2u);
                help_id = op_len >= 6u ? h_rd16(p + 4u) : 0u;
                label = h_resolve_store(model, prompt_id);
                help = h_resolve_store(model, help_id);

                if (label && label[0]) {
                    sr_node *node = &model->nodes[model->node_count];
                    added_index = model->node_count;
                    node->id = model->node_count + 1u;
                    node->role = role;
                    node->state = state;
                    node->label = label;
                    node->value = "";
                    node->help = help ? help : "";

                    if (h_is_question(op) && op_len >= 13u &&
                        (p[12u] & 0x01u))
                        node->state |= SR_STATE_READONLY;

                    h_fill_binding(model, added_index, op, p, op_len);
                    model->node_count++;
                }
            }
        }

        if (op == SR_IFR_ONE_OF && scoped &&
            added_index != SR_INVALID_INDEX &&
            oneof_count < SR_ONEOF_STACK_MAX) {
            oneof_nodes[oneof_count] = added_index;
            oneof_depths[oneof_count] = depth + 1u;
            oneof_count++;
        }

        if (scoped) ++depth;
        p += op_len;
    }

    return model->node_count != 0u;
}

int sr_hii_apply_raw_value(sr_hii_model *model,
                           sr_u32 node_index,
                           sr_u64 raw_value) {
    sr_hii_binding *binding;
    sr_node *node;
    sr_u32 i;

    if (!model || !model->nodes || node_index >= model->node_count)
        return 0;
    node = &model->nodes[node_index];

    if (!model->bindings || node_index >= model->binding_capacity) {
        char *number = h_store_u64(model, raw_value);
        if (!number) return 0;
        node->value = number;
        return 1;
    }

    binding = &model->bindings[node_index];

    if (binding->opcode == SR_IFR_CHECKBOX) {
        if (raw_value) {
            node->state |= SR_STATE_CHECKED;
            node->value = "oui";
        } else {
            node->state &= ~SR_STATE_CHECKED;
            node->value = "non";
        }
        return 1;
    }

    if (binding->opcode == SR_IFR_ONE_OF && model->options) {
        sr_u32 start = binding->option_start;
        sr_u32 end = start + binding->option_count;
        if (end > model->option_count) end = model->option_count;
        for (i = start; i < end; ++i) {
            sr_hii_option *option = &model->options[i];
            sr_u64 mask = option->width >= 8u
                ? ~(sr_u64)0
                : (((sr_u64)1u << (option->width * 8u)) - 1u);
            if ((raw_value & mask) == (option->value & mask)) {
                node->value = option->label;
                return 1;
            }
        }
    }

    {
        char *number = h_store_u64(model, raw_value);
        if (!number) return 0;
        node->value = number;
    }
    return 1;
}
