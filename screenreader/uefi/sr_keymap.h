#ifndef SR_KEYMAP_H
#define SR_KEYMAP_H
#include "sr_core.h"

typedef struct {
    sr_u16 scan_code;
    sr_u16 unicode_char;
    sr_u32 modifiers;
} sr_key;

enum {
    SR_MOD_SHIFT = 1u << 0,
    SR_MOD_CTRL  = 1u << 1,
    SR_MOD_ALT   = 1u << 2
};

sr_command sr_key_to_command(sr_key key);

#endif
