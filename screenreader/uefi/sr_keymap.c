#include "sr_keymap.h"

static int sr_is(sr_u16 ch, char lower) {
    return ch == (sr_u16)lower || ch == (sr_u16)(lower - ('a' - 'A'));
}

sr_command sr_key_to_command(sr_key key) {
    switch (key.scan_code) {
        case 0x0001u: return SR_CMD_PREVIOUS;      /* Up */
        case 0x0002u: return SR_CMD_NEXT;          /* Down */
        case 0x0003u: return SR_CMD_VALUE_NEXT;   /* Right */
        case 0x0004u: return SR_CMD_VALUE_PREVIOUS;/* Left */
        case 0x0005u: return SR_CMD_FIRST;         /* Home */
        case 0x0006u: return SR_CMD_LAST;          /* End */
        case 0x0009u: return SR_CMD_PAGE_PREVIOUS; /* Page Up */
        case 0x000au: return SR_CMD_PAGE_NEXT;     /* Page Down */
        case 0x0012u: return SR_CMD_ITEM_CHOOSER;  /* F8 */
        case 0x0017u: return SR_CMD_STOP_SPEECH;   /* Escape */
        default: break;
    }

    if (key.unicode_char == 0x0009u) {
        return (key.modifiers & SR_MOD_SHIFT) ? SR_CMD_PREVIOUS : SR_CMD_NEXT;
    }
    if (key.unicode_char == 0x001bu) return SR_CMD_STOP_SPEECH;
    if (key.unicode_char == (sr_u16)' ') return SR_CMD_ACTIVATE;
    if (key.unicode_char == 0x000du) return SR_CMD_ACTIVATE;

    if (sr_is(key.unicode_char, 'r')) return SR_CMD_REPEAT;
    if (sr_is(key.unicode_char, 'h')) return SR_CMD_HELP;
    if (sr_is(key.unicode_char, 'w')) return SR_CMD_WHERE_AM_I;
    if (sr_is(key.unicode_char, 's')) return SR_CMD_STOP_SPEECH;

    if (sr_is(key.unicode_char, 'b'))
        return (key.modifiers & SR_MOD_SHIFT) ? SR_CMD_PREVIOUS_CONTROL : SR_CMD_NEXT_CONTROL;
    if (sr_is(key.unicode_char, 'e'))
        return (key.modifiers & SR_MOD_SHIFT) ? SR_CMD_PREVIOUS_EDIT : SR_CMD_NEXT_EDIT;
    if (sr_is(key.unicode_char, 'c'))
        return (key.modifiers & SR_MOD_SHIFT) ? SR_CMD_PREVIOUS_CHECKBOX : SR_CMD_NEXT_CHECKBOX;
    if (sr_is(key.unicode_char, 'x'))
        return (key.modifiers & SR_MOD_SHIFT) ? SR_CMD_PREVIOUS_CHOICE : SR_CMD_NEXT_CHOICE;

    return SR_CMD_NONE;
}
