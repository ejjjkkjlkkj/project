#include <stdio.h>
#include <string.h>
#include "sr_hii_ifr.h"

static int resolve(void *ctx, sr_u16 id, char *out, sr_u32 cap) {
    const char *s = "";
    (void)ctx;
    switch (id) {
        case 1: s = "Demarrage securise"; break;
        case 2: s = "Active ou desactive"; break;
        case 3: s = "Ordre de demarrage"; break;
        case 4: s = "Choisir un peripherique"; break;
        case 5: s = "Nom machine"; break;
        case 6: s = "Saisir le nom"; break;
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
    /*
     * Package header: 24-bit length + type 0x02.
     * Then synthetic IFR opcodes with the real two-byte IFR op header shape.
     */
    static const sr_u8 pkg[] = {
        0x28,0x00,0x00,0x02,

        /* CHECKBOX len 13, prompt=1, help=2, read-only flag at byte 12 */
        0x06,0x0d, 0x01,0x00, 0x02,0x00,
        0x10,0x00, 0x00,0x00, 0x00,0x00, 0x01,

        /* ONE_OF len 13, prompt=3, help=4 */
        0x05,0x0d, 0x03,0x00, 0x04,0x00,
        0x11,0x00, 0x00,0x00, 0x00,0x00, 0x00,

        /* STRING len 10, prompt=5, help=6 */
        0x1c,0x0a, 0x05,0x00, 0x06,0x00, 0,0,0,0
    };
    sr_node nodes[8];
    char arena[1024];
    sr_hii_model model;

    sr_hii_model_init(&model, nodes, 8, arena, sizeof(arena), resolve, 0);
    if (!check(sr_hii_parse_forms_package(&model, pkg, sizeof(pkg)), "parse")) return 1;
    if (!check(model.node_count == 3u, "three semantic controls")) return 1;
    if (!check(nodes[0].role == SR_ROLE_CHECKBOX, "checkbox role")) return 1;
    if (!check(nodes[0].state & SR_STATE_READONLY, "read-only flag")) return 1;
    if (!check(strcmp(nodes[0].label, "Demarrage securise") == 0, "label resolved")) return 1;
    if (!check(nodes[1].role == SR_ROLE_COMBO, "one-of role")) return 1;
    if (!check(nodes[2].role == SR_ROLE_EDIT, "string role")) return 1;
    if (!check(model.malformed_opcodes == 0u, "no malformed opcodes")) return 1;

    printf("UEFI_HII_IFR_MODEL=PASS\n");
    return 0;
}
