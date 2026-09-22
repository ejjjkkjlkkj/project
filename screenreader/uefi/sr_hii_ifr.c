#include "sr_hii_ifr.h"

#define SR_HII_PACKAGE_FORMS 0x02u

static sr_u16 h_rd16(const sr_u8 *p) {
    return (sr_u16)((sr_u16)p[0] | ((sr_u16)p[1] << 8));
}

static sr_u32 h_rd24(const sr_u8 *p) {
    return (sr_u32)p[0] | ((sr_u32)p[1] << 8) | ((sr_u32)p[2] << 16);
}

static sr_role h_role(sr_u8 op, sr_u32 *state_out) {
    sr_u32 state = SR_STATE_ENABLED;
    sr_role role = SR_ROLE_UNKNOWN;

    switch (op) {
        case 0x02u: role = SR_ROLE_TEXT; break;          /* subtitle */
        case 0x03u: role = SR_ROLE_TEXT; break;          /* text */
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
    if (!model || !model->string_arena || !text) return 0;
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
    model->malformed_opcodes = 0;
    model->dropped_nodes = 0;
}

int sr_hii_parse_forms_package(sr_hii_model *model,
                               const sr_u8 *package,
                               sr_u32 package_bytes) {
    sr_u32 declared;
    sr_u8 type;
    const sr_u8 *p;
    const sr_u8 *end;

    if (!model || !package || package_bytes < 4u) return 0;
    declared = h_rd24(package);
    type = package[3];
    if (type != SR_HII_PACKAGE_FORMS || declared < 4u || declared > package_bytes)
        return 0;

    p = package + 4u;
    end = package + declared;

    while (p + 2u <= end) {
        sr_u8 op = p[0];
        sr_u32 op_len = (sr_u32)(p[1] & 0x7fu);
        sr_role role;
        sr_u32 state = 0;
        sr_u16 prompt_id;
        sr_u16 help_id;
        char *label;
        char *help;

        if (op_len < 2u || p + op_len > end) {
            model->malformed_opcodes++;
            return 0;
        }

        role = h_role(op, &state);
        if (role != SR_ROLE_UNKNOWN && op_len >= 4u) {
            if (model->node_count >= model->node_capacity) {
                model->dropped_nodes++;
                p += op_len;
                continue;
            }

            prompt_id = h_rd16(p + 2u);
            help_id = op_len >= 6u ? h_rd16(p + 4u) : 0u;
            label = h_resolve_store(model, prompt_id);
            help = h_resolve_store(model, help_id);

            if (label && label[0]) {
                sr_node *node = &model->nodes[model->node_count];
                node->id = model->node_count + 1u;
                node->role = role;
                node->state = state;
                node->label = label;
                node->value = "";
                node->help = help ? help : "";

                /*
                 * EFI_IFR_QUESTION_HEADER flags follow statement header,
                 * QuestionId, VarStoreId and VarStoreInfo. READ_ONLY is bit 0.
                 */
                if (h_is_question(op) && op_len >= 13u && (p[12] & 0x01u))
                    node->state |= SR_STATE_READONLY;

                model->node_count++;
            }
        }

        p += op_len;
    }

    return model->node_count != 0u;
}
