#include "sr_uefi.h"
#include "sr_uefi_input.h"

typedef struct {
    sr_u16 scan_code;
    sr_u16 unicode_char;
} sr_efi_input_key;

typedef struct {
    sr_u32 key_shift_state;
    sr_u8 key_toggle_state;
    sr_u8 pad[3];
} sr_efi_key_state;

typedef struct {
    sr_efi_input_key key;
    sr_efi_key_state key_state;
} sr_efi_key_data;

typedef sr_efi_status (*read_basic_fn)(void *self, sr_efi_input_key *key);
typedef sr_efi_status (*read_ex_fn)(void *self, sr_efi_key_data *key_data);

typedef struct {
    void *reset;
    read_basic_fn read_key;
    void *wait_for_key;
} sr_simple_text_input;

typedef struct {
    void *reset;
    read_ex_fn read_key_ex;
    void *wait_for_key_ex;
    void *set_state;
    void *register_key_notify;
    void *unregister_key_notify;
} sr_simple_text_input_ex;

static const sr_efi_guid g_simple_text_input_ex_guid =
    {0xdd9e7534u,0x7762u,0x4698u,{0x8c,0x14,0xf5,0x85,0x17,0xa6,0x25,0xaa}};

#define EFI_SHIFT_STATE_VALID 0x80000000u
#define EFI_RIGHT_SHIFT_PRESSED 0x00000001u
#define EFI_LEFT_SHIFT_PRESSED  0x00000002u
#define EFI_RIGHT_CONTROL_PRESSED 0x00000004u
#define EFI_LEFT_CONTROL_PRESSED  0x00000008u
#define EFI_RIGHT_ALT_PRESSED   0x00000010u
#define EFI_LEFT_ALT_PRESSED    0x00000020u

static sr_u32 map_modifiers(sr_u32 shift_state) {
    sr_u32 mods = 0;
    if (!(shift_state & EFI_SHIFT_STATE_VALID)) return 0;
    if (shift_state & (EFI_RIGHT_SHIFT_PRESSED | EFI_LEFT_SHIFT_PRESSED))
        mods |= SR_MOD_SHIFT;
    if (shift_state & (EFI_RIGHT_CONTROL_PRESSED | EFI_LEFT_CONTROL_PRESSED))
        mods |= SR_MOD_CTRL;
    if (shift_state & (EFI_RIGHT_ALT_PRESSED | EFI_LEFT_ALT_PRESSED))
        mods |= SR_MOD_ALT;
    return mods;
}

int sr_uefi_keyboard_init(sr_uefi_keyboard *kbd, void *system_table) {
    sr_locate_protocol_fn locate;
    if (!kbd || !system_table) return 0;
    kbd->basic_input = *(void **)((sr_u8 *)system_table + 0x30u);
    kbd->input_ex = 0;
    kbd->has_input_ex = 0;

    locate = sr_uefi_locate_protocol(system_table);
    if (locate) {
        void *input_ex = 0;
        if (locate(&g_simple_text_input_ex_guid, 0, &input_ex) == 0u &&
            input_ex && ((sr_simple_text_input_ex *)input_ex)->read_key_ex) {
            kbd->input_ex = input_ex;
            kbd->has_input_ex = 1;
        }
    }

    if (kbd->has_input_ex) return 1;
    return kbd->basic_input &&
           ((sr_simple_text_input *)kbd->basic_input)->read_key;
}

int sr_uefi_keyboard_poll(sr_uefi_keyboard *kbd, sr_key *key_out) {
    if (!kbd || !key_out) return 0;

    key_out->scan_code = 0;
    key_out->unicode_char = 0;
    key_out->modifiers = 0;

    if (kbd->has_input_ex && kbd->input_ex) {
        sr_efi_key_data data;
        sr_simple_text_input_ex *input =
            (sr_simple_text_input_ex *)kbd->input_ex;
        data.key.scan_code = 0;
        data.key.unicode_char = 0;
        data.key_state.key_shift_state = 0;
        data.key_state.key_toggle_state = 0;
        if (input->read_key_ex(input, &data) != 0u) return 0;
        key_out->scan_code = data.key.scan_code;
        key_out->unicode_char = data.key.unicode_char;
        key_out->modifiers = map_modifiers(data.key_state.key_shift_state);
        return 1;
    }

    if (kbd->basic_input) {
        sr_efi_input_key key;
        sr_simple_text_input *input = (sr_simple_text_input *)kbd->basic_input;
        key.scan_code = 0;
        key.unicode_char = 0;
        if (!input->read_key || input->read_key(input, &key) != 0u) return 0;
        key_out->scan_code = key.scan_code;
        key_out->unicode_char = key.unicode_char;
        return 1;
    }

    return 0;
}
