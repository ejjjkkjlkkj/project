#include <stdio.h>
#include <string.h>
#include "sr_core.h"
#include "sr_keymap.h"

typedef struct {
    unsigned begins;
    unsigned writes;
    unsigned commits;
    unsigned cancels;
    unsigned last_token;
    sr_speech_priority last_priority;
    char text[1024];
} fake_voice;

static int fake_begin(void *ctx, sr_u32 token, sr_speech_priority priority) {
    fake_voice *v = (fake_voice *)ctx;
    v->begins++;
    v->last_token = token;
    v->last_priority = priority;
    v->text[0] = 0;
    return 1;
}

static int fake_write(void *ctx, const char *text, sr_u32 bytes) {
    fake_voice *v = (fake_voice *)ctx;
    size_t n = bytes;
    if (n >= sizeof(v->text)) n = sizeof(v->text) - 1u;
    memcpy(v->text, text, n);
    v->text[n] = 0;
    v->writes++;
    return 1;
}

static int fake_commit(void *ctx) {
    fake_voice *v = (fake_voice *)ctx;
    v->commits++;
    return 1;
}

static void fake_cancel(void *ctx) {
    fake_voice *v = (fake_voice *)ctx;
    v->cancels++;
}

static int check(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 0;
    }
    return 1;
}

int main(void) {
    static const sr_node nodes[] = {
        {1, SR_ROLE_TEXT, SR_STATE_HIDDEN, "cache", "", ""},
        {2, SR_ROLE_BUTTON, SR_STATE_FOCUSABLE | SR_STATE_ENABLED, "Continuer", "", "Active l'option selectionnee"},
        {3, SR_ROLE_CHECKBOX, SR_STATE_FOCUSABLE | SR_STATE_ENABLED | SR_STATE_CHECKED, "Demarrage rapide", "", "Active ou desactive le demarrage rapide"},
        {4, SR_ROLE_EDIT, SR_STATE_FOCUSABLE | SR_STATE_ENABLED | SR_STATE_REQUIRED, "Nom", "UEFI", "Saisissez un nom"},
        {5, SR_ROLE_COMBO, SR_STATE_FOCUSABLE | SR_STATE_ENABLED | SR_STATE_COLLAPSED, "Ordre de demarrage", "NVMe", "Choisissez le peripherique"},
        {6, SR_ROLE_BUTTON, SR_STATE_FOCUSABLE, "Effacer", "", "Action non disponible"}
    };
    fake_voice voice = {0};
    sr_speech_sink sink = {&voice, fake_begin, fake_write, fake_commit, fake_cancel};
    sr_runtime rt;
    sr_key key;

    sr_init(&rt, nodes, (sr_u32)(sizeof(nodes) / sizeof(nodes[0])), sink);

    if (!check(sr_focus_first(&rt), "first focus")) return 1;
    if (!check(rt.focus_index == 1u, "hidden node skipped")) return 1;
    if (!check(strstr(voice.text, "Continuer") != NULL, "label spoken")) return 1;
    if (!check(strstr(voice.text, "bouton") != NULL, "role spoken")) return 1;

    if (!check(sr_move(&rt, 1), "move next")) return 1;
    if (!check(rt.focus_index == 2u, "checkbox focused")) return 1;
    if (!check(strstr(voice.text, "coche") != NULL, "checked state spoken")) return 1;
    if (!check(voice.cancels >= 1u, "focus interrupts previous speech")) return 1;

    if (!check(sr_help(&rt), "help")) return 1;
    if (!check(strstr(voice.text, "demarrage rapide") != NULL, "context help spoken")) return 1;

    if (!check(sr_where_am_i(&rt), "where am i")) return 1;
    if (!check(strstr(voice.text, "Vous etes sur") != NULL, "where am i prefix")) return 1;

    if (!check(sr_handle(&rt, SR_CMD_NEXT_CONTROL), "next control navigation")) return 1;
    if (!check(rt.focus_index == 3u, "next control preserves document order")) return 1;
    if (!check(sr_handle(&rt, SR_CMD_PREVIOUS_CONTROL), "previous control navigation")) return 1;
    if (!check(rt.focus_index == 2u, "previous control preserves document order")) return 1;
    if (!check(sr_move_role(&rt, SR_ROLE_EDIT, 1), "structural edit navigation")) return 1;
    if (!check(rt.focus_index == 3u, "edit focused")) return 1;
    if (!check(strstr(voice.text, "obligatoire") != NULL, "required state spoken")) return 1;

    rt.verbosity = SR_VERBOSITY_VERBOSE;
    if (!check(sr_announce_focus(&rt), "verbose focus")) return 1;
    if (!check(strstr(voice.text, " sur ") != NULL, "position announced")) return 1;

    if (!check(sr_focus_last(&rt), "last focus")) return 1;
    if (!check(strstr(voice.text, "indisponible") != NULL, "disabled state spoken")) return 1;
    if (!check(sr_handle(&rt, SR_CMD_ACTIVATE), "disabled activation feedback")) return 1;
    if (!check(strcmp(voice.text, "Controle indisponible") == 0, "critical disabled feedback")) return 1;
    if (!check(voice.last_priority == SR_SPEECH_CRITICAL, "critical priority")) return 1;

    key.scan_code = 0;
    key.modifiers = 0;
    key.unicode_char = (sr_u16)'w';
    if (!check(sr_key_to_command(key) == SR_CMD_WHERE_AM_I, "where-am-i key")) return 1;
    key.unicode_char = 0x0009u;
    if (!check(sr_key_to_command(key) == SR_CMD_NEXT, "tab next")) return 1;
    key.modifiers = SR_MOD_SHIFT;
    if (!check(sr_key_to_command(key) == SR_CMD_PREVIOUS, "shift-tab previous")) return 1;

    sr_stop_speech(&rt);
    if (!check(rt.speech_active == 0u, "stop speech")) return 1;

    printf("UEFI_SCREENREADER_CORE=PASS\n");
    printf("FOCUS_EVENTS=%u\n", rt.focus_events);
    printf("SPEECH_INTERRUPTS=%u\n", rt.speech_interrupts);
    printf("UTTERANCES=%u\n", rt.emitted_utterances);
    return 0;
}
