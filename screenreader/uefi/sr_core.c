#include "sr_core.h"

#define SR_INVALID_INDEX 0xffffffffu

static sr_u32 sr_strlen(const char *s) {
    sr_u32 n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

static void sr_zero(char *dst, sr_u32 cap) {
    sr_u32 i;
    for (i = 0; i < cap; ++i) dst[i] = 0;
}

static void sr_append(char *dst, sr_u32 cap, sr_u32 *used, const char *src) {
    sr_u32 i = 0;
    if (!src || !cap || !used) return;
    while (src[i] && (*used + 1u) < cap) {
        dst[*used] = src[i];
        ++(*used);
        ++i;
    }
    dst[*used] = 0;
}

static void sr_append_sep(char *dst, sr_u32 cap, sr_u32 *used) {
    if (*used) sr_append(dst, cap, used, ", ");
}

static void sr_append_u32(char *dst, sr_u32 cap, sr_u32 *used, sr_u32 value) {
    char tmp[11];
    sr_u32 n = 0;
    if (value == 0) {
        sr_append(dst, cap, used, "0");
        return;
    }
    while (value && n < (sr_u32)sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (n) {
        char one[2];
        one[0] = tmp[--n];
        one[1] = 0;
        sr_append(dst, cap, used, one);
    }
}

static int sr_is_focusable(const sr_node *node) {
    return node &&
           !(node->state & SR_STATE_HIDDEN) &&
           (node->state & SR_STATE_FOCUSABLE);
}

const char *sr_role_name(sr_role role) {
    switch (role) {
        case SR_ROLE_WINDOW: return "fenetre";
        case SR_ROLE_GROUP: return "groupe";
        case SR_ROLE_TEXT: return "texte";
        case SR_ROLE_BUTTON: return "bouton";
        case SR_ROLE_CHECKBOX: return "case a cocher";
        case SR_ROLE_RADIO: return "bouton radio";
        case SR_ROLE_COMBO: return "liste deroulante";
        case SR_ROLE_EDIT: return "champ de saisie";
        case SR_ROLE_SLIDER: return "curseur";
        case SR_ROLE_LIST: return "liste";
        case SR_ROLE_LIST_ITEM: return "element de liste";
        case SR_ROLE_MENU: return "menu";
        case SR_ROLE_MENU_ITEM: return "element de menu";
        case SR_ROLE_TAB: return "onglet";
        case SR_ROLE_STATUS: return "etat";
        case SR_ROLE_DIALOG: return "dialogue";
        default: return "controle";
    }
}

static int sr_emit(sr_runtime *rt, const char *text, sr_speech_priority priority) {
    sr_u32 len;
    int ok = 1;
    if (!rt || !text) return 0;
    len = sr_strlen(text);
    if (!len) return 1;

    if (rt->speech_active && rt->speech.cancel) {
        rt->speech.cancel(rt->speech.ctx);
        rt->speech_interrupts++;
    }

    rt->speech_token++;
    if (rt->speech_token == 0) rt->speech_token = 1;

    if (rt->speech.begin) {
        ok = rt->speech.begin(rt->speech.ctx, rt->speech_token, priority);
        if (!ok) {
            rt->speech_active = 0;
            return 0;
        }
    }
    if (rt->speech.write_utf8) {
        ok = rt->speech.write_utf8(rt->speech.ctx, text, len);
        if (!ok) {
            if (rt->speech.cancel) rt->speech.cancel(rt->speech.ctx);
            rt->speech_active = 0;
            return 0;
        }
    }
    if (rt->speech.commit) {
        ok = rt->speech.commit(rt->speech.ctx);
        if (!ok) {
            if (rt->speech.cancel) rt->speech.cancel(rt->speech.ctx);
            rt->speech_active = 0;
            return 0;
        }
    }

    rt->speech_active = 1;
    rt->emitted_utterances++;
    return 1;
}

static void sr_build_focus(const sr_runtime *rt, char *out, sr_u32 cap) {
    const sr_node *node = sr_current(rt);
    sr_u32 used = 0;
    sr_u32 ordinal = 0;
    sr_u32 total = 0;
    sr_u32 i;

    if (!out || !cap) return;
    out[0] = 0;
    if (!node) return;

    if (node->label && node->label[0]) {
        sr_append(out, cap, &used, node->label);
    }
    if (rt->verbosity != SR_VERBOSITY_BRIEF) {
        sr_append_sep(out, cap, &used);
        sr_append(out, cap, &used, sr_role_name(node->role));
    }
    if (node->value && node->value[0]) {
        sr_append_sep(out, cap, &used);
        sr_append(out, cap, &used, node->value);
    }

    if (!(node->state & SR_STATE_ENABLED)) {
        sr_append_sep(out, cap, &used);
        sr_append(out, cap, &used, "indisponible");
    }
    if (node->state & SR_STATE_CHECKED) {
        sr_append_sep(out, cap, &used);
        sr_append(out, cap, &used, "coche");
    }
    if (node->state & SR_STATE_SELECTED) {
        sr_append_sep(out, cap, &used);
        sr_append(out, cap, &used, "selectionne");
    }
    if (node->state & SR_STATE_EXPANDED) {
        sr_append_sep(out, cap, &used);
        sr_append(out, cap, &used, "developpe");
    } else if (node->state & SR_STATE_COLLAPSED) {
        sr_append_sep(out, cap, &used);
        sr_append(out, cap, &used, "reduit");
    }
    if (node->state & SR_STATE_REQUIRED) {
        sr_append_sep(out, cap, &used);
        sr_append(out, cap, &used, "obligatoire");
    }
    if (node->state & SR_STATE_READONLY) {
        sr_append_sep(out, cap, &used);
        sr_append(out, cap, &used, "lecture seule");
    }

    if (rt->verbosity == SR_VERBOSITY_VERBOSE) {
        for (i = 0; i < rt->node_count; ++i) {
            if (sr_is_focusable(&rt->nodes[i])) {
                ++total;
                if (i <= rt->focus_index) ++ordinal;
            }
        }
        if (total) {
            sr_append_sep(out, cap, &used);
            sr_append_u32(out, cap, &used, ordinal);
            sr_append(out, cap, &used, " sur ");
            sr_append_u32(out, cap, &used, total);
        }
    }

    if (!out[0]) {
        sr_append(out, cap, &used, sr_role_name(node->role));
    }
}

void sr_init(sr_runtime *rt,
             const sr_node *nodes,
             sr_u32 node_count,
             sr_speech_sink sink) {
    if (!rt) return;
    rt->nodes = nodes;
    rt->node_count = node_count;
    rt->focus_index = 0;
    rt->page_step = 5;
    rt->speech_token = 0;
    rt->focus_events = 0;
    rt->speech_interrupts = 0;
    rt->emitted_utterances = 0;
    rt->has_focus = 0;
    rt->speech_active = 0;
    rt->verbosity = SR_VERBOSITY_NORMAL;
    rt->speech = sink;
    sr_zero(rt->last_utterance, (sr_u32)sizeof(rt->last_utterance));
}

const sr_node *sr_current(const sr_runtime *rt) {
    if (!rt || !rt->nodes || !rt->has_focus || rt->focus_index >= rt->node_count)
        return 0;
    return &rt->nodes[rt->focus_index];
}

int sr_set_focus(sr_runtime *rt, sr_u32 index, int speak) {
    if (!rt || !rt->nodes || index >= rt->node_count) return 0;
    if (!sr_is_focusable(&rt->nodes[index])) return 0;
    rt->focus_index = index;
    rt->has_focus = 1;
    rt->focus_events++;
    if (speak) return sr_announce_focus(rt);
    return 1;
}

int sr_focus_first(sr_runtime *rt) {
    sr_u32 i;
    if (!rt) return 0;
    for (i = 0; i < rt->node_count; ++i)
        if (sr_is_focusable(&rt->nodes[i])) return sr_set_focus(rt, i, 1);
    return 0;
}

int sr_focus_last(sr_runtime *rt) {
    sr_u32 i;
    if (!rt || !rt->node_count) return 0;
    i = rt->node_count;
    while (i--) {
        if (sr_is_focusable(&rt->nodes[i])) return sr_set_focus(rt, i, 1);
    }
    return 0;
}

int sr_move(sr_runtime *rt, int delta) {
    sr_u32 i;
    if (!rt || !rt->node_count || delta == 0) return 0;
    if (!rt->has_focus) return delta > 0 ? sr_focus_first(rt) : sr_focus_last(rt);

    i = rt->focus_index;
    if (delta > 0) {
        while (++i < rt->node_count)
            if (sr_is_focusable(&rt->nodes[i])) return sr_set_focus(rt, i, 1);
    } else {
        while (i > 0) {
            --i;
            if (sr_is_focusable(&rt->nodes[i])) return sr_set_focus(rt, i, 1);
        }
    }
    return 0;
}

int sr_move_page(sr_runtime *rt, int direction) {
    sr_u32 moved = 0;
    sr_u32 target;
    if (!rt || !direction) return 0;
    target = rt->page_step ? rt->page_step : 5u;
    while (moved < target) {
        if (!sr_move(rt, direction > 0 ? 1 : -1)) break;
        ++moved;
    }
    return moved != 0;
}

int sr_move_role(sr_runtime *rt, sr_role role, int direction) {
    sr_u32 i;
    if (!rt || !rt->node_count || !direction) return 0;
    if (!rt->has_focus) return sr_focus_first(rt);

    i = rt->focus_index;
    if (direction > 0) {
        while (++i < rt->node_count) {
            if (sr_is_focusable(&rt->nodes[i]) && rt->nodes[i].role == role)
                return sr_set_focus(rt, i, 1);
        }
    } else {
        while (i > 0) {
            --i;
            if (sr_is_focusable(&rt->nodes[i]) && rt->nodes[i].role == role)
                return sr_set_focus(rt, i, 1);
        }
    }
    return 0;
}

int sr_announce_focus(sr_runtime *rt) {
    if (!rt || !sr_current(rt)) return 0;
    sr_zero(rt->last_utterance, (sr_u32)sizeof(rt->last_utterance));
    sr_build_focus(rt, rt->last_utterance, (sr_u32)sizeof(rt->last_utterance));
    return sr_emit(rt, rt->last_utterance, SR_SPEECH_FOCUS);
}

int sr_repeat(sr_runtime *rt) {
    if (!rt || !rt->last_utterance[0]) return 0;
    return sr_emit(rt, rt->last_utterance, SR_SPEECH_INFO);
}

int sr_help(sr_runtime *rt) {
    const sr_node *node = sr_current(rt);
    if (!node) return 0;
    if (node->help && node->help[0])
        return sr_emit(rt, node->help, SR_SPEECH_INFO);
    return sr_emit(rt, "Aucune aide disponible", SR_SPEECH_INFO);
}

int sr_where_am_i(sr_runtime *rt) {
    char msg[768];
    sr_u32 used = 0;
    if (!rt || !sr_current(rt)) return 0;
    msg[0] = 0;
    sr_append(msg, (sr_u32)sizeof(msg), &used, "Vous etes sur ");
    sr_build_focus(rt, msg + used, (sr_u32)sizeof(msg) - used);
    return sr_emit(rt, msg, SR_SPEECH_INFO);
}

void sr_stop_speech(sr_runtime *rt) {
    if (!rt) return;
    if (rt->speech_active && rt->speech.cancel) {
        rt->speech.cancel(rt->speech.ctx);
        rt->speech_interrupts++;
    }
    rt->speech_active = 0;
}

static int sr_activate(sr_runtime *rt) {
    const sr_node *node = sr_current(rt);
    if (!node) return 0;
    if (!(node->state & SR_STATE_ENABLED))
        return sr_emit(rt, "Controle indisponible", SR_SPEECH_CRITICAL);
    return sr_emit(rt, "Active", SR_SPEECH_INFO);
}

int sr_handle(sr_runtime *rt, sr_command command) {
    switch (command) {
        case SR_CMD_PREVIOUS: return sr_move(rt, -1);
        case SR_CMD_NEXT: return sr_move(rt, 1);
        case SR_CMD_FIRST: return sr_focus_first(rt);
        case SR_CMD_LAST: return sr_focus_last(rt);
        case SR_CMD_PAGE_PREVIOUS: return sr_move_page(rt, -1);
        case SR_CMD_PAGE_NEXT: return sr_move_page(rt, 1);
        case SR_CMD_ACTIVATE: return sr_activate(rt);
        case SR_CMD_HELP: return sr_help(rt);
        case SR_CMD_REPEAT: return sr_repeat(rt);
        case SR_CMD_WHERE_AM_I: return sr_where_am_i(rt);
        case SR_CMD_STOP_SPEECH: sr_stop_speech(rt); return 1;
        case SR_CMD_NEXT_CONTROL:
            return sr_move_role(rt, SR_ROLE_BUTTON, 1) ||
                   sr_move_role(rt, SR_ROLE_COMBO, 1) ||
                   sr_move_role(rt, SR_ROLE_CHECKBOX, 1);
        case SR_CMD_PREVIOUS_CONTROL:
            return sr_move_role(rt, SR_ROLE_BUTTON, -1) ||
                   sr_move_role(rt, SR_ROLE_COMBO, -1) ||
                   sr_move_role(rt, SR_ROLE_CHECKBOX, -1);
        case SR_CMD_NEXT_EDIT: return sr_move_role(rt, SR_ROLE_EDIT, 1);
        case SR_CMD_PREVIOUS_EDIT: return sr_move_role(rt, SR_ROLE_EDIT, -1);
        case SR_CMD_NEXT_CHECKBOX: return sr_move_role(rt, SR_ROLE_CHECKBOX, 1);
        case SR_CMD_PREVIOUS_CHECKBOX: return sr_move_role(rt, SR_ROLE_CHECKBOX, -1);
        case SR_CMD_NEXT_CHOICE:
            return sr_move_role(rt, SR_ROLE_RADIO, 1) ||
                   sr_move_role(rt, SR_ROLE_COMBO, 1);
        case SR_CMD_PREVIOUS_CHOICE:
            return sr_move_role(rt, SR_ROLE_RADIO, -1) ||
                   sr_move_role(rt, SR_ROLE_COMBO, -1);
        default: return 0;
    }
}
