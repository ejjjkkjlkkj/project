#include "sr_chooser.h"

static char fold_ascii(char ch) {
    if (ch >= 'A' && ch <= 'Z') return (char)(ch + ('a' - 'A'));
    return ch;
}

static int contains_ci(const char *text, const char *query) {
    sr_u32 i, j;
    if (!query || !query[0]) return 1;
    if (!text) return 0;

    for (i = 0; text[i]; ++i) {
        for (j = 0; query[j] && text[i + j]; ++j) {
            if (fold_ascii(text[i + j]) != fold_ascii(query[j])) break;
        }
        if (!query[j]) return 1;
    }
    return 0;
}

static int focusable(const sr_node *node) {
    return node && !(node->state & SR_STATE_HIDDEN) &&
           (node->state & SR_STATE_FOCUSABLE);
}

static int rebuild(sr_chooser *chooser, sr_runtime *rt) {
    sr_u32 i;
    if (!chooser || !rt || !rt->nodes) return 0;

    chooser->match_count = 0;
    chooser->selected_match = 0;

    for (i = 0; i < rt->node_count; ++i) {
        const sr_node *node = &rt->nodes[i];
        if (!focusable(node)) continue;
        if (!contains_ci(node->label, chooser->query)) continue;
        if (chooser->match_count >= SR_CHOOSER_MAX_MATCHES) break;
        chooser->matches[chooser->match_count++] = i;
    }

    if (!chooser->match_count) {
        (void)sr_say(rt, "Aucun element", SR_SPEECH_INFO);
        return 0;
    }

    for (i = 0; i < chooser->match_count; ++i) {
        if (rt->has_focus && chooser->matches[i] == rt->focus_index) {
            chooser->selected_match = i;
            break;
        }
    }

    return sr_set_focus(
        rt, chooser->matches[chooser->selected_match], 1);
}

void sr_chooser_init(sr_chooser *chooser) {
    sr_u32 i;
    if (!chooser) return;
    for (i = 0; i < SR_CHOOSER_MAX_MATCHES; ++i) chooser->matches[i] = 0;
    chooser->match_count = 0;
    chooser->selected_match = 0;
    chooser->original_focus = 0;
    chooser->query[0] = 0;
    chooser->query_len = 0;
    chooser->active = 0;
    chooser->had_original_focus = 0;
}

int sr_chooser_open(sr_chooser *chooser, sr_runtime *rt) {
    if (!chooser || !rt) return 0;
    chooser->had_original_focus = rt->has_focus;
    chooser->original_focus = rt->focus_index;
    chooser->query[0] = 0;
    chooser->query_len = 0;
    chooser->active = 1;
    if (!rebuild(chooser, rt)) {
        chooser->active = 0;
        return 0;
    }
    return 1;
}

int sr_chooser_cancel(sr_chooser *chooser, sr_runtime *rt) {
    sr_u32 restore;
    if (!chooser || !rt || !chooser->active) return 0;
    restore = chooser->original_focus;
    chooser->active = 0;
    chooser->query[0] = 0;
    chooser->query_len = 0;
    if (chooser->had_original_focus && restore < rt->node_count)
        return sr_set_focus(rt, restore, 1);
    return 1;
}

int sr_chooser_accept(sr_chooser *chooser, sr_runtime *rt) {
    (void)rt;
    if (!chooser || !chooser->active || !chooser->match_count) return 0;
    chooser->active = 0;
    chooser->query[0] = 0;
    chooser->query_len = 0;
    return 1;
}

int sr_chooser_next(sr_chooser *chooser, sr_runtime *rt, int direction) {
    if (!chooser || !rt || !chooser->active || !chooser->match_count ||
        !direction) return 0;

    if (direction > 0) {
        chooser->selected_match++;
        if (chooser->selected_match >= chooser->match_count)
            chooser->selected_match = 0;
    } else {
        if (!chooser->selected_match)
            chooser->selected_match = chooser->match_count - 1u;
        else
            chooser->selected_match--;
    }

    return sr_set_focus(
        rt, chooser->matches[chooser->selected_match], 1);
}

int sr_chooser_type(sr_chooser *chooser, sr_runtime *rt, sr_u16 unicode_char) {
    char ch;
    sr_u32 old_len;
    if (!chooser || !rt || !chooser->active) return 0;
    if (unicode_char < 0x20u || unicode_char > 0x7eu) return 0;
    if (chooser->query_len + 1u >= SR_CHOOSER_QUERY_CAP) return 0;

    ch = (char)unicode_char;
    old_len = chooser->query_len;
    chooser->query[chooser->query_len++] = ch;
    chooser->query[chooser->query_len] = 0;

    if (!rebuild(chooser, rt)) {
        chooser->query_len = old_len;
        chooser->query[old_len] = 0;
        (void)rebuild(chooser, rt);
        return 0;
    }
    return 1;
}

int sr_chooser_backspace(sr_chooser *chooser, sr_runtime *rt) {
    if (!chooser || !rt || !chooser->active || !chooser->query_len) return 0;
    chooser->query_len--;
    chooser->query[chooser->query_len] = 0;
    return rebuild(chooser, rt);
}

int sr_chooser_handle_key(sr_chooser *chooser, sr_runtime *rt, sr_key key) {
    if (!chooser || !rt || !chooser->active) return 0;

    if (key.scan_code == 0x0001u) return sr_chooser_next(chooser, rt, -1);
    if (key.scan_code == 0x0002u) return sr_chooser_next(chooser, rt, 1);
    if (key.scan_code == 0x0012u) return sr_chooser_cancel(chooser, rt);

    if (key.unicode_char == 0x000du) return sr_chooser_accept(chooser, rt);
    if (key.unicode_char == 0x001bu || key.scan_code == 0x0017u)
        return sr_chooser_cancel(chooser, rt);
    if (key.unicode_char == 0x0008u)
        return sr_chooser_backspace(chooser, rt);
    if (key.unicode_char >= 0x20u && key.unicode_char <= 0x7eu)
        return sr_chooser_type(chooser, rt, key.unicode_char);

    return 0;
}
