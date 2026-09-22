#ifndef SR_UEFI_INPUT_H
#define SR_UEFI_INPUT_H

#include "sr_keymap.h"

typedef struct {
    void *basic_input;
    void *input_ex;
    sr_u8 has_input_ex;
} sr_uefi_keyboard;

int sr_uefi_keyboard_init(sr_uefi_keyboard *kbd, void *system_table);
int sr_uefi_keyboard_poll(sr_uefi_keyboard *kbd, sr_key *key_out);

#endif
