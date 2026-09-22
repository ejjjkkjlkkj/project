#include <stdio.h>
#include <string.h>
#include "sr_hii_ifr.h"

static int resolve(void *ctx, sr_u16 id, char *out, sr_u32 cap) {
    const char *s = "";
    (void)ctx;
    switch (id) {
        case 1: s = "Demarrage rapide"; break;
        case 2: s = "Active ou desactive"; break;
        case 3: s = "Ordre de demarrage"; break;
        case 4: s = "Choisissez le peripherique"; break;
        case 5: s = "UEFI"; break;
        case 6: s = "NVMe"; break;
        default: return 0;
    }
    if ((sr_u32)strlen(s) + 1u > cap) return 0;
    strcpy(out, s);
    return 1;
}

static int check(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        return 0;
    }
    return 1;
}

int main(void) {
    static const sr_u8 pkg[] = {
        0x2f,0x00,0x00,0x02,

        /* ONE_OF len=14 scoped; QuestionId=0x20, VarStoreId=7, offset=9. */
        0x05,0x8e,
        0x03,0x00, 0x04,0x00,
        0x20,0x00, 0x07,0x00, 0x09,0x00,
        0x00, 0x00,

        /* ONE_OF_OPTION string=5, type UINT8, value=2. */
        0x09,0x07, 0x05,0x00, 0x00,0x00,0x02,

        /* ONE_OF_OPTION string=6, type UINT8, value=3. */
        0x09,0x07, 0x06,0x00, 0x00,0x00,0x03,

        /* END. */
        0x29,0x02,

        /* CHECKBOX len=13. */
        0x06,0x0d,
        0x01,0x00, 0x02,0x00,
        0x21,0x00, 0x07,0x00, 0x0a,0x00,
        0x00
    };
    sr_node nodes[4];
    sr_hii_binding bindings[4];
    sr_hii_option options[8];
    char arena[1024];
    sr_hii_model model;

    sr_hii_model_init(&model, nodes, 4, arena, sizeof(arena), resolve, 0);
    sr_hii_model_attach_metadata(&model, bindings, 4, options, 8);

    if (!check(sr_hii_parse_forms_package(&model, pkg, sizeof(pkg)), "parse"))
        return 1;
    if (!check(model.node_count == 2u, "node count")) return 1;
    if (!check(model.option_count == 2u, "option count")) return 1;
    if (!check(bindings[0].question_id == 0x20u, "question id")) return 1;
    if (!check(bindings[0].varstore_id == 7u, "varstore id")) return 1;
    if (!check(bindings[0].varstore_info == 9u, "varstore offset")) return 1;
    if (!check(bindings[0].option_count == 2u, "binding option count")) return 1;
    if (!check(options[0].value == 2u && options[1].value == 3u,
               "typed option values")) return 1;

    if (!check(sr_hii_apply_raw_value(&model, 0, 3u), "apply one-of value"))
        return 1;
    if (!check(strcmp(nodes[0].value, "NVMe") == 0, "selected label"))
        return 1;

    if (!check(sr_hii_apply_raw_value(&model, 1, 1u), "checkbox on"))
        return 1;
    if (!check(nodes[1].state & SR_STATE_CHECKED, "checked state")) return 1;
    if (!check(strcmp(nodes[1].value, "oui") == 0, "checked value")) return 1;

    if (!check(sr_hii_apply_raw_value(&model, 1, 0u), "checkbox off"))
        return 1;
    if (!check(!(nodes[1].state & SR_STATE_CHECKED), "unchecked state"))
        return 1;
    if (!check(strcmp(nodes[1].value, "non") == 0, "unchecked value"))
        return 1;

    printf("UEFI_HII_SEMANTIC_VALUES=PASS\n");
    return 0;
}
