#include <stdio.h>
#include <string.h>
#include "sr_chooser.h"

typedef struct {
    char text[256];
    unsigned cancels;
} voice;

static int begin(void *ctx, sr_u32 token, sr_speech_priority priority) {
    voice *v = (voice *)ctx;
    (void)token; (void)priority;
    v->text[0] = 0;
    return 1;
}
static int write_text(void *ctx, const char *text, sr_u32 bytes) {
    voice *v = (voice *)ctx;
    if (bytes >= sizeof(v->text)) return 0;
    memcpy(v->text, text, bytes);
    v->text[bytes] = 0;
    return 1;
}
static int commit(void *ctx) { (void)ctx; return 1; }
static void cancel(void *ctx) { ((voice *)ctx)->cancels++; }

static int ok(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        return 0;
    }
    return 1;
}

int main(void) {
    static const sr_node nodes[] = {
        {1, SR_ROLE_BUTTON, SR_STATE_FOCUSABLE | SR_STATE_ENABLED, "Continuer", "", ""},
        {2, SR_ROLE_CHECKBOX, SR_STATE_FOCUSABLE | SR_STATE_ENABLED, "Demarrage rapide", "", ""},
        {3, SR_ROLE_COMBO, SR_STATE_FOCUSABLE | SR_STATE_ENABLED, "Ordre de demarrage", "", ""},
        {4, SR_ROLE_EDIT, SR_STATE_FOCUSABLE | SR_STATE_ENABLED, "Nom machine", "", ""}
    };
    voice v = {{0},0};
    sr_speech_sink sink = {&v, begin, write_text, commit, cancel};
    sr_runtime rt;
    sr_chooser chooser;
    sr_key key = {0,0,0};

    sr_init(&rt, nodes, 4, sink);
    sr_chooser_init(&chooser);
    if (!ok(sr_set_focus(&rt, 0, 1), "initial focus")) return 1;
    if (!ok(sr_chooser_open(&chooser, &rt), "open")) return 1;
    if (!ok(chooser.match_count == 4u, "all items")) return 1;

    if (!ok(sr_chooser_type(&chooser, &rt, 'd'), "filter d")) return 1;
    if (!ok(chooser.match_count == 2u, "two d matches")) return 1;
    if (!ok(rt.focus_index == 1u, "first filtered item")) return 1;

    key.scan_code = 0x0002u;
    if (!ok(sr_chooser_handle_key(&chooser, &rt, key), "next result")) return 1;
    if (!ok(rt.focus_index == 2u, "second filtered item")) return 1;

    key.scan_code = 0;
    key.unicode_char = 0x001bu;
    if (!ok(sr_chooser_handle_key(&chooser, &rt, key), "cancel")) return 1;
    if (!ok(rt.focus_index == 0u, "focus restored")) return 1;

    if (!ok(sr_chooser_open(&chooser, &rt), "reopen")) return 1;
    if (!ok(sr_chooser_type(&chooser, &rt, 'n'), "filter n")) return 1;
    if (!ok(sr_chooser_type(&chooser, &rt, 'o'), "filter no")) return 1;
    if (!ok(rt.focus_index == 3u, "name item selected")) return 1;
    key.unicode_char = 0x000du;
    if (!ok(sr_chooser_handle_key(&chooser, &rt, key), "accept")) return 1;
    if (!ok(!chooser.active && rt.focus_index == 3u, "accepted focus kept")) return 1;

    printf("UEFI_ITEM_CHOOSER=PASS\n");
    return 0;
}
